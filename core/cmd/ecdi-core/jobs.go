// jobs.go —— 后台长任务（协议 v6 `t:"job"`，docs/background-job-plan.md）。
//
// 设计要点：
//   - jobHub 持有本会话的作业表；作业随会话存活，单轮取消不杀作业；
//   - 每个作业复用 L1 沙盒的启动原语（平台文件里的 startBackgroundProcess）；
//   - 输出进 256KB 环形尾窗；emit/读游标都是绝对偏移，增量可对账；
//   - ask 未答期间（I3）**不发任何 job 消息**：事件出口统一查 hub.paused，
//     答完由 session 调 resume() 补发；end 落在暂停期也会挂起、resume 补发；
//   - 会话退出 shutdown()：关作业句柄（Windows KILL_ON_JOB_CLOSE 兜底）。
package main

import (
	"fmt"
	"io"
	"strings"
	"sync"
	"unicode/utf8"
	"time"
)

const (
	jobOutWindow  = 256 << 10 // 每作业尾部窗口
	jobMaxRunning = 4         // 并发上限
	jobMaxAge     = 30 * time.Minute
	jobFlushTick  = 200 * time.Millisecond
	jobEmitChunk  = 8 << 10 // 一条 output 最多带 8KB
	jobReadLimit  = 16 << 10
	jobKillWait   = 5 * time.Second
)

// bgProcess：已在 L1 沙盒中启动的后台进程（平台原语返回）。
//   wait     = 等进程退出（cmd.Wait，含 WaitDelay 兜底）；
//   kill     = 终止整棵树（Windows 作业句柄 / 非 Windows 进程组）；
//   close    = 释放句柄（wait 返回后调用一次）；
//   exitCode = 退出码（wait 返回后可用）。
type bgProcess struct {
	wait     func() error
	kill     func()
	close    func()
	exitCode func() int
}

type job struct {
	id      string
	command string
	cwd     string
	call    string
	started time.Time
	hub     *jobHub

	mu       sync.Mutex
	dec      *streamDecoder // 原始字节 → UTF-8（GBK 归一，见 decode.go）
	buf      []byte // 尾部窗口：覆盖绝对偏移 [startOff, total)；**始终是 UTF-8**
	startOff int64
	total    int64
	dropped  int64
	emitted  int64 // 已发 output 的绝对偏移
	readPos  int64 // 模型读游标
	done     bool
	killed   bool
	timedOut bool
	status   string // running / exited / failed / killed / timeout
	exitCode int

	// end 事件：最多发一次；暂停期落在 pendingEnd 上，resume 补发。
	pendingEnd      bool
	endSent         bool
	startedReported bool

	kill  func()
	close func()

	stopFlush chan struct{}
}

type jobHub struct {
	mu      sync.Mutex
	jobs    map[string]*job
	order   []string
	nextID  int
	running int
	paused   bool
	closed   bool
	draining bool // shutdown 中：允许 end 事件落地，但不再接新作业
	sink     func(map[string]any)

	// startProc：平台启动原语；测试注入假进程用（生产恒为 startBackgroundProcess）。
	startProc func(command, cwd string, cfg SandboxConfig, sink io.Writer) (*bgProcess, error)
}

func newJobHub(sink func(map[string]any)) *jobHub {
	return &jobHub{jobs: map[string]*job{}, sink: sink, startProc: startBackgroundProcess}
}

func (h *jobHub) isControlTool(name string) bool {
	switch name {
	case "job_output", "job_kill", "job_list":
		return true
	}
	return false
}

// setPaused：ask 挂起前置 true，答完后 resume()。
func (h *jobHub) setPaused(v bool) {
	h.mu.Lock()
	h.paused = v
	h.mu.Unlock()
}

// resume：解除门闩，把暂停期攒下的输出 / 终态补发（在 state=running 之后调用）。
func (h *jobHub) resume() {
	h.setPaused(false)
	h.mu.Lock()
	ids := append([]string(nil), h.order...)
	h.mu.Unlock()
	for _, id := range ids {
		j := h.get(id)
		if j == nil {
			continue
		}
		j.flush()
		j.flushEndIfPending()
	}
}

func (h *jobHub) get(id string) *job {
	h.mu.Lock()
	defer h.mu.Unlock()
	return h.jobs[id]
}

// start：启动一个后台作业并立即返回。call = 发起它的 tool_call 块 id。
func (h *jobHub) start(command, cwd, call string, cfg SandboxConfig) (*job, error) {
	h.mu.Lock()
	if h.closed {
		h.mu.Unlock()
		return nil, fmt.Errorf("会话正在退出，不能再启动后台任务")
	}
	if h.running >= jobMaxRunning {
		h.mu.Unlock()
		return nil, fmt.Errorf("并发后台任务已达上限（%d）；先 job_kill 一个，或用 job_list 查看", jobMaxRunning)
	}
	// 先占并发名额再起进程：启动期间别的调用不能挤进第 5 个。
	// id 在启动成功后才分配——失败的启动不消耗 j 编号。
	h.running++
	h.mu.Unlock()

	j := &job{
		command:   command,
		cwd:       cwd,
		call:      call,
		started:   time.Now(),
		hub:       h,
		status:    "running",
		dec:       &streamDecoder{},
		stopFlush: make(chan struct{}),
	}
	proc, err := h.startProc(command, cwd, cfg, &jobSink{j: j})
	if err != nil {
		h.mu.Lock()
		h.running--
		h.mu.Unlock()
		return nil, err
	}
	j.kill = proc.kill
	j.close = proc.close

	h.mu.Lock()
	if h.closed {
		h.mu.Unlock()
		if proc.kill != nil {
			proc.kill()
		}
		if proc.close != nil {
			proc.close()
		}
		return nil, fmt.Errorf("会话正在退出，不能再启动后台任务")
	}
	h.nextID++
	j.id = fmt.Sprintf("j%d", h.nextID)
	h.jobs[j.id] = j
	h.order = append(h.order, j.id)
	h.mu.Unlock()

	// start 必须先于该 id 的任何 output/end（I14）。
	j.mu.Lock()
	j.startedReported = true
	j.mu.Unlock()
	h.emitControl(map[string]any{
		"t": "job", "id": j.id, "phase": "start",
		"command": command, "cwd": cwd, "call": call,
	})

	go j.flushLoop()
	go func() {
		err := proc.wait()
		j.finish(err, proc)
	}()
	time.AfterFunc(jobMaxAge, func() {
		j.mu.Lock()
		finished := j.done
		if !finished {
			j.timedOut = true
		}
		j.mu.Unlock()
		if !finished {
			j.terminate()
		}
	})
	return j, nil
}

// emitControl：控制面事件（start/end）。暂停期只挂起 end；start 不会在暂停期发生
// （它由 ask 之后的工具执行产生）。
func (h *jobHub) emitControl(payload map[string]any) {
	h.mu.Lock()
	defer h.mu.Unlock()
	if h.closed || h.sink == nil {
		return
	}
	h.sink(payload)
}

type jobSink struct{ j *job }

func (s *jobSink) Write(p []byte) (int, error) {
	j := s.j
	j.mu.Lock()
	defer j.mu.Unlock()
	// ★ 解码在**入口**做：环窗、游标、output 事件、job_output 读出来的就都是
	//   UTF-8（见 decode.go）。跨 write 的半个多字节字符由 streamDecoder 扣住。
	if utf8 := j.dec.Write(p); len(utf8) > 0 {
		j.appendLocked(utf8)
	}
	return len(p), nil
}

// appendLocked：把已解码的 UTF-8 字节追加进尾窗并维护溢出计数。
// 调用方必须持有 j.mu。
func (j *job) appendLocked(b []byte) {
	if len(b) == 0 {
		return
	}
	j.buf = append(j.buf, b...)
	j.total += int64(len(b))
	if over := len(j.buf) - jobOutWindow; over > 0 {
		// ★ 环形窗口的**起点必须落在字符边界**：否则窗口裁剪会把一个汉字切成
		//   两半，此后 startOff 处的 output/job_output 都以半个字符开头（U+FFFD）。
		//   宁可多丢 ≤3 字节，也不把字符撕开；多丢的字节如实计入 dropped。
		for over < len(j.buf) && !utf8.RuneStart(j.buf[over]) {
			over++
		}
		copy(j.buf, j.buf[over:])
		j.buf = j.buf[:len(j.buf)-over]
		j.startOff += int64(over)
		j.dropped += int64(over)
	}
}

// finish：wait 返回后落终态、补发尾部输出、发 end。只执行一次。
func (j *job) finish(err error, proc *bgProcess) {
	j.mu.Lock()
	if j.done {
		j.mu.Unlock()
		return
	}
	j.done = true
	switch {
	case j.killed:
		j.status = "killed"
		j.exitCode = -1
	case j.timedOut:
		j.status = "timeout"
		j.exitCode = -1
	case err != nil:
		j.status = "failed"
		j.exitCode = -1
		if proc.exitCode != nil {
			j.exitCode = proc.exitCode()
		}
	default:
		j.status = "exited"
	}
	j.mu.Unlock()

	// ★ 解码器里可能还扣着 1-3 个字节（流在半个字符处结束/被杀）。
	//   不 Flush 就是**静默丢字**——这里补进尾窗，后面的 flush 会完整发出去。
	j.mu.Lock()
	if tail := j.dec.Flush(); len(tail) > 0 {
		j.appendLocked(tail)
	}
	j.mu.Unlock()

	close(j.stopFlush)
	j.flush() // 尾窗里剩下的先发（否则 end 之后读不到）
	j.pendingEnd = true
	j.flushEndIfPending()
	if proc.close != nil {
		proc.close()
	}
}

func (j *job) flushEndIfPending() {
	j.mu.Lock()
	if !j.pendingEnd || j.endSent {
		j.mu.Unlock()
		return
	}
	status, code := j.status, j.exitCode
	started := j.started
	j.mu.Unlock()

	h := j.hub
	h.mu.Lock()
	if h.closed {
		h.mu.Unlock()
		return
	}
	if h.paused {
		h.mu.Unlock()
		return // 留在 pendingEnd，resume 再补
	}
	j.mu.Lock()
	j.pendingEnd = false
	j.endSent = true
	j.mu.Unlock()
	h.running--
	if h.sink != nil {
		h.sink(map[string]any{
			"t": "job", "id": j.id, "phase": "end",
			"status": status, "exitCode": code,
			"durationMs": time.Since(started).Milliseconds(),
		})
	}
	h.mu.Unlock()
}

func (j *job) flushLoop() {
	t := time.NewTicker(jobFlushTick)
	defer t.Stop()
	for {
		select {
		case <-j.stopFlush:
			return
		case <-t.C:
			j.flush()
		}
	}
}

// flush：把 [emitted, total) 的增量合并成一条 output 发出（每次 ≤8KB）。
// 暂停/关闭时**不推进 emitted**——恢复后还能补发，不丢内容。
func (j *job) flush() {
	h := j.hub
	if h == nil {
		return
	}
	h.mu.Lock()
	if h.closed || h.paused || h.sink == nil {
		h.mu.Unlock()
		return
	}
	j.mu.Lock()
	if j.emitted >= j.total {
		j.mu.Unlock()
		h.mu.Unlock()
		return
	}
	start := j.emitted
	if start < j.startOff {
		start = j.startOff
	}
	end := j.total
	if end-start > jobEmitChunk {
		end = start + jobEmitChunk
	}
	// ★ 不从 UTF-8 字符中间切开（8KB 处的续字节会让这一条 output 以半个
	//   字符结尾，GUI 侧就是 U+FFFD）。最多回退 3 字节，不可能把整块吞掉。
	for end > start && end < j.total && !utf8.RuneStart(j.buf[end-j.startOff]) {
		end--
	}
	text := string(j.buf[start-j.startOff : end-j.startOff])
	total, dropped := j.total, j.dropped
	j.emitted = end
	j.mu.Unlock()

	h.sink(map[string]any{
		"t": "job", "id": j.id, "phase": "output",
		"text": text, "bytes": total, "dropped": dropped,
	})
	h.mu.Unlock()
}

func (j *job) terminate() {
	j.mu.Lock()
	if j.done {
		j.mu.Unlock()
		return
	}
	j.killed = true
	k := j.kill
	j.mu.Unlock()
	if k != nil {
		k()
	}
}

// ── 工具层 ──────────────────────────────────────────────────────

func (h *jobHub) toolStart(args map[string]any, root, callID string, cfg SandboxConfig) toolResult {
	command, _ := args["command"].(string)
	j, err := h.start(command, root, callID, cfg)
	if err != nil {
		return toolResult{OK: false, Output: err.Error()}
	}
	var sb strings.Builder
	fmt.Fprintf(&sb, "后台任务 %s 已启动（运行中）。\n命令：%s\n目录：%s\n", j.id, command, root)
	fmt.Fprintf(&sb, "用 job_output 取增量输出（{\"job_id\":%q}）；job_kill 终止。", j.id)
	return toolResult{OK: true, Output: sb.String()}
}

func (h *jobHub) toolOutput(args map[string]any) toolResult {
	id, _ := args["job_id"].(string)
	id = strings.TrimSpace(id)
	if id == "" {
		return toolResult{OK: false, Output: "job_output 缺少 job_id 参数"}
	}
	wait := 0
	if v, ok := args["wait_seconds"].(float64); ok {
		wait = int(v)
	}
	if wait < 0 {
		wait = 0
	}
	if wait > 30 {
		wait = 30
	}
	j := h.get(id)
	if j == nil {
		return toolResult{OK: false, Output: fmt.Sprintf("没有这个后台任务：%s（用 job_list 查看）", id)}
	}
	deadline := time.Now().Add(time.Duration(wait) * time.Second)
	for {
		j.mu.Lock()
		if j.done || j.total > j.readPos || wait == 0 || time.Now().After(deadline) {
			res := j.renderLocked()
			j.mu.Unlock()
			return res
		}
		j.mu.Unlock()
		time.Sleep(50 * time.Millisecond)
	}
}

// renderLocked：调用方必须持有 j.mu。
func (j *job) renderLocked() toolResult {
	head := fmt.Sprintf("[%s] %s · 已运行 %s · 输出 %s",
		statusLabel(j.status), j.id, shortDur(time.Since(j.started)), shortBytes(j.total))
	if j.dropped > 0 {
		head += fmt.Sprintf(" · 已丢弃 %s", shortBytes(j.dropped))
	}
	start := j.readPos
	var lost int64
	if start < j.startOff {
		lost = j.startOff - start
		start = j.startOff
	}
	end := j.total
	if end > start && end-start > jobReadLimit {
		start = end - jobReadLimit
		// 起点**往前**退到字符边界（最多 2 字节）：后退是"多给"，前进是"丢字"。
		for start > j.startOff && !utf8.RuneStart(j.buf[start-j.startOff]) {
			start--
		}
		lost += 0
	}
	data := ""
	if end > start {
		data = string(j.buf[start-j.startOff : end-j.startOff])
	}
	n := end - start
	j.readPos = end

	var body strings.Builder
	if lost > 0 {
		fmt.Fprintf(&body, "[已丢弃最早 %s]\n", shortBytes(lost))
	}
	if data == "" {
		if j.done {
			data = "（无更多输出）"
		} else {
			data = "（暂无新输出，仍在运行）"
		}
	}
	body.WriteString(data)
	res := head + fmt.Sprintf("（本轮新增 %s）\n", shortBytes(n)) + body.String()
	if j.done && j.status != "exited" {
		res += fmt.Sprintf("\n[退出码 %d]", j.exitCode)
	}
	return toolResult{OK: true, Output: res}
}

func (h *jobHub) toolKill(args map[string]any) toolResult {
	id, _ := args["job_id"].(string)
	id = strings.TrimSpace(id)
	if id == "" {
		return toolResult{OK: false, Output: "job_kill 缺少 job_id 参数"}
	}
	j := h.get(id)
	if j == nil {
		return toolResult{OK: false, Output: fmt.Sprintf("没有这个后台任务：%s（用 job_list 查看）", id)}
	}
	j.mu.Lock()
	done := j.done
	j.mu.Unlock()
	if !done {
		j.terminate()
		deadline := time.Now().Add(jobKillWait)
		for time.Now().Before(deadline) {
			j.mu.Lock()
			done = j.done
			j.mu.Unlock()
			if done {
				break
			}
			time.Sleep(50 * time.Millisecond)
		}
	}
	j.mu.Lock()
	defer j.mu.Unlock()
	if !done {
		return toolResult{OK: false, Output: fmt.Sprintf(
			"已请求终止 %s，但 %s 内进程未退出（会话结束时句柄关闭会兜底整树清退）", id, jobKillWait)}
	}
	tail := ""
	if j.total > 0 {
		start := j.total - 4096
		if start < j.startOff {
			start = j.startOff
		}
		tail = string(j.buf[start-j.startOff:])
	}
	res := fmt.Sprintf("[%s] %s · 运行 %s · 退出码 %d", statusLabel(j.status), j.id,
		shortDur(time.Since(j.started)), j.exitCode)
	if tail != "" {
		res += "\n" + tail
	}
	return toolResult{OK: true, Output: res}
}

func (h *jobHub) toolList() toolResult {
	h.mu.Lock()
	ids := append([]string(nil), h.order...)
	h.mu.Unlock()
	if len(ids) == 0 {
		return toolResult{OK: true, Output: "（本会话还没有后台任务）"}
	}
	var sb strings.Builder
	for _, id := range ids {
		j := h.get(id)
		if j == nil {
			continue
		}
		j.mu.Lock()
		fmt.Fprintf(&sb, "%s  %s  %s  %s  %s\n", j.id, statusLabel(j.status),
			shortDur(time.Since(j.started)), shortBytes(j.total), jobFirstLine(j.command, 80))
		j.mu.Unlock()
	}
	return toolResult{OK: true, Output: strings.TrimRight(sb.String(), "\n")}
}

// shutdown：会话退出时全杀，并把每个已 start 的作业的 end 事件发出去。
//
// ★ 为什么不能「先关 hub 再杀」：journal 里会留下一条没有 end 的 start（I14 违规），
//   下次打开历史会话时那张卡永远停在"运行中"。所以 shutdown 分两段：
//   先把 draining 打开（end 允许落地）、暂停解除、杀进程，最多等 2s 让 finish
//   把 end 发完，然后才 closed=true。调用点在 bye **之前**（processLine）。
func (h *jobHub) shutdown() {
	h.mu.Lock()
	if h.closed || h.draining {
		h.mu.Unlock()
		return
	}
	h.draining = true
	h.paused = false
	var kills []func()
	for _, j := range h.jobs {
		j.mu.Lock()
		if !j.done {
			j.killed = true
			if j.kill != nil {
				kills = append(kills, j.kill)
			}
		}
		j.mu.Unlock()
	}
	h.mu.Unlock()
	for _, k := range kills {
		k()
	}

	deadline := time.Now().Add(2 * time.Second)
	for time.Now().Before(deadline) {
		h.mu.Lock()
		allEnded := true
		for _, j := range h.jobs {
			j.mu.Lock()
			ended := j.endSent || !j.startedReported
			j.mu.Unlock()
			if !ended {
				allEnded = false
				break
			}
		}
		h.mu.Unlock()
		if allEnded {
			break
		}
		time.Sleep(20 * time.Millisecond)
	}
	h.mu.Lock()
	h.closed = true
	h.mu.Unlock()
}

func statusLabel(s string) string {
	switch s {
	case "running":
		return "运行中"
	case "exited":
		return "已完成"
	case "failed":
		return "失败"
	case "killed":
		return "已终止"
	case "timeout":
		return "超时"
	}
	return s
}

func shortDur(d time.Duration) string {
	if d < time.Second {
		return "0s"
	}
	if d < time.Minute {
		return fmt.Sprintf("%ds", int(d.Seconds()))
	}
	return fmt.Sprintf("%dm%02ds", int(d.Minutes()), int(d.Seconds())%60)
}

func shortBytes(n int64) string {
	switch {
	case n < 1024:
		return fmt.Sprintf("%dB", n)
	case n < 1024*1024:
		return fmt.Sprintf("%.1fKB", float64(n)/1024)
	default:
		return fmt.Sprintf("%.1fMB", float64(n)/(1024*1024))
	}
}

func jobFirstLine(s string, max int) string {
	if i := strings.IndexAny(s, "\r\n"); i >= 0 {
		s = s[:i]
	}
	r := []rune(s)
	if len(r) > max {
		return string(r[:max]) + "…"
	}
	return s
}

var _ io.Writer = (*jobSink)(nil)
