package main

import (
	"fmt"
	"io"
	"strings"
	"sync"
	"testing"
	"time"
)

// ── 假进程：jobs_test 专用（不碰真平台原语）──────────────────────────

type fakeExit struct{ code int }

func (e *fakeExit) Error() string { return fmt.Sprintf("exit %d", e.code) }
func (e *fakeExit) ExitCode() int { return e.code }

type fakeProc struct {
	sink   io.Writer
	waitCh chan struct{}
	killCh chan struct{}
	code   int
	once   sync.Once
}

func (p *fakeProc) Write(b []byte) (int, error) { return p.sink.Write(b) }
func (p *fakeProc) exit(code int) {
	p.once.Do(func() {
		p.code = code
		close(p.waitCh)
	})
}

type fakeStart struct {
	mu    sync.Mutex
	procs []*fakeProc
	fail  error
}

func (f *fakeStart) start(command, cwd string, cfg SandboxConfig, sink io.Writer) (*bgProcess, error) {
	if f.fail != nil {
		return nil, f.fail
	}
	p := &fakeProc{sink: sink, waitCh: make(chan struct{}), killCh: make(chan struct{})}
	f.mu.Lock()
	f.procs = append(f.procs, p)
	f.mu.Unlock()
	return &bgProcess{
		wait: func() error {
			<-p.waitCh
			if p.code != 0 {
				return &fakeExit{code: p.code}
			}
			return nil
		},
		kill: func() {
			select {
			case <-p.killCh:
			default:
				close(p.killCh)
			}
			p.exit(-1)
		},
		close:    func() {},
		exitCode: func() int { return p.code },
	}, nil
}

func (f *fakeStart) last(t *testing.T) *fakeProc {
	t.Helper()
	f.mu.Lock()
	defer f.mu.Unlock()
	if len(f.procs) == 0 {
		t.Fatal("没有启动过假进程")
	}
	return f.procs[len(f.procs)-1]
}

type msgLog struct {
	mu   sync.Mutex
	msgs []map[string]any
}

func (l *msgLog) add(m map[string]any) {
	l.mu.Lock()
	l.msgs = append(l.msgs, m)
	l.mu.Unlock()
}

func (l *msgLog) all() []map[string]any {
	l.mu.Lock()
	defer l.mu.Unlock()
	return append([]map[string]any(nil), l.msgs...)
}

func (l *msgLog) wait(t *testing.T, what string, pred func(map[string]any) bool) map[string]any {
	t.Helper()
	deadline := time.Now().Add(3 * time.Second)
	for time.Now().Before(deadline) {
		for _, m := range l.all() {
			if pred(m) {
				return m
			}
		}
		time.Sleep(2 * time.Millisecond)
	}
	t.Fatalf("等不到 %s；已有消息：%v", what, l.all())
	return nil
}

func newFakeHub(t *testing.T) (*jobHub, *fakeStart, *msgLog) {
	t.Helper()
	f := &fakeStart{}
	l := &msgLog{}
	h := newJobHub(l.add)
	h.startProc = f.start
	return h, f, l
}

func isPhase(phase string) func(map[string]any) bool {
	return func(m map[string]any) bool { return m["t"] == "job" && m["phase"] == phase }
}

// ── ① 生命周期：start → output → end，且 start 挂对 tool_call ─────────

func TestJobHub_StartOutputEnd(t *testing.T) {
	h, f, l := newFakeHub(t)
	j, err := h.start("echo hi", ".", "b3", SandboxConfig{})
	if err != nil {
		t.Fatal(err)
	}
	start := l.wait(t, "job start", isPhase("start"))
	if start["id"] != j.id || start["call"] != "b3" || start["command"] != "echo hi" {
		t.Fatalf("start 字段不对：%v", start)
	}
	if j.id != "j1" {
		t.Fatalf("首个作业应为 j1，got %s", j.id)
	}

	p := f.last(t)
	if _, err := p.Write([]byte("hello\n")); err != nil {
		t.Fatal(err)
	}
	l.wait(t, "job output", isPhase("output"))
	j.flush() // 幂等：没有新内容就不该再发

	p.exit(0)
	end := l.wait(t, "job end", isPhase("end"))
	if end["status"] != "exited" || end["exitCode"].(int) != 0 {
		t.Fatalf("end 字段不对：%v", end)
	}
	// 有且仅有一条 end
	n := 0
	for _, m := range l.all() {
		if m["t"] == "job" && m["phase"] == "end" {
			n++
		}
	}
	if n != 1 {
		t.Fatalf("end 应恰好 1 条，got %d", n)
	}
	if h.running != 0 {
		t.Fatalf("running 计数未归零：%d", h.running)
	}
}

// ── ② I3：ask 挂起期不得发 job 消息；resume 后按 output → end 补发 ─────

func TestJobHub_PauseDefersEverything(t *testing.T) {
	h, f, l := newFakeHub(t)
	j, err := h.start("sleep", ".", "b1", SandboxConfig{})
	if err != nil {
		t.Fatal(err)
	}
	l.wait(t, "start", isPhase("start"))

	p := f.last(t)
	_, _ = p.Write([]byte("during-ask\n"))
	h.setPaused(true)
	j.flush()
	p.exit(0)
	deadline := time.Now().Add(2 * time.Second)
	for {
		j.mu.Lock()
		done := j.done
		j.mu.Unlock()
		if done || time.Now().After(deadline) {
			break
		}
		time.Sleep(2 * time.Millisecond)
	}
	time.Sleep(50 * time.Millisecond)
	for _, m := range l.all() {
		if m["t"] == "job" && m["phase"] != "start" {
			t.Fatalf("暂停期泄漏了 job 消息：%v", m)
		}
	}

	h.resume()
	out := l.wait(t, "补发的 output", isPhase("output"))
	if !strings.Contains(out["text"].(string), "during-ask") {
		t.Fatalf("补发内容不对：%v", out)
	}
	l.wait(t, "补发的 end", isPhase("end"))
	// 顺序：output 必须在 end 之前
	idx := func(pred func(map[string]any) bool) int {
		for i, m := range l.all() {
			if pred(m) {
				return i
			}
		}
		return -1
	}
	if idx(isPhase("output")) > idx(isPhase("end")) {
		t.Fatalf("output 应在 end 之前")
	}
}

// ── ③ 并发上限 ─────────────────────────────────────────────────────

func TestJobHub_ConcurrencyCap(t *testing.T) {
	h, _, _ := newFakeHub(t)
	for i := 0; i < jobMaxRunning; i++ {
		if _, err := h.start("block", ".", "b", SandboxConfig{}); err != nil {
			t.Fatalf("第 %d 个不该失败：%v", i+1, err)
		}
	}
	if _, err := h.start("block", ".", "b", SandboxConfig{}); err == nil ||
		!strings.Contains(err.Error(), "上限") {
		t.Fatalf("第 %d 个应被上限拒绝，got %v", jobMaxRunning+1, err)
	}
	h.shutdown()
}

// ── ④ 尾窗丢弃 + 总字节对账 ────────────────────────────────────────

func TestJobHub_RingWindowDropped(t *testing.T) {
	h, f, l := newFakeHub(t)
	j, err := h.start("chatty", ".", "b1", SandboxConfig{})
	if err != nil {
		t.Fatal(err)
	}
	p := f.last(t)
	chunk := strings.Repeat("a", 1024)
	for i := 0; i < 300; i++ { // 300KB > 256KB 窗口
		_, _ = p.Write([]byte(chunk))
	}
	j.flush()
	out := l.wait(t, "output", isPhase("output"))
	if out["bytes"].(int64) != int64(300*1024) {
		t.Fatalf("bytes 应报总产出 300KB，got %v", out["bytes"])
	}
	if out["dropped"].(int64) <= 0 {
		t.Fatalf("300KB 应触发丢弃，dropped=%v", out["dropped"])
	}
	if len(j.buf) != jobOutWindow {
		t.Fatalf("窗口应稳定在 %d，got %d", jobOutWindow, len(j.buf))
	}
	h.shutdown()
}

// ── ⑤ 读游标：同一段输出只读一次 ────────────────────────────────────

func TestJobHub_ReadCursor(t *testing.T) {
	h, f, _ := newFakeHub(t)
	j, err := h.start("x", ".", "b1", SandboxConfig{})
	if err != nil {
		t.Fatal(err)
	}
	p := f.last(t)
	_, _ = p.Write([]byte("hello"))
	r1 := j.renderLocked()
	if !strings.Contains(r1.Output, "hello") {
		t.Fatalf("首次读应含 hello：%s", r1.Output)
	}
	r2 := j.renderLocked()
	if strings.Contains(r2.Output, "hello") {
		t.Fatalf("第二次读不该重复 hello：%s", r2.Output)
	}
	if !strings.Contains(r2.Output, "暂无新输出") {
		t.Fatalf("无增量时应提示仍在运行：%s", r2.Output)
	}
	h.shutdown()
}

// ── ⑥ kill：终态 killed + 退出码 -1 ────────────────────────────────

func TestJobHub_Kill(t *testing.T) {
	h, f, l := newFakeHub(t)
	_, err := h.start("sleep", ".", "b1", SandboxConfig{})
	if err != nil {
		t.Fatal(err)
	}
	p := f.last(t)
	res := h.toolKill(map[string]any{"job_id": "j1"})
	if !res.OK || !strings.Contains(res.Output, "已终止") {
		t.Fatalf("kill 结果不对：%+v", res)
	}
	select {
	case <-p.killCh:
	default:
		t.Fatal("kill 没有调用平台原语")
	}
	end := l.wait(t, "end", isPhase("end"))
	if end["status"] != "killed" || end["exitCode"].(int) != -1 {
		t.Fatalf("kill 终态不对：%v", end)
	}
}

// ── ⑦ 启动失败：不占 id、不泄漏 running ─────────────────────────────

func TestJobHub_StartFailure(t *testing.T) {
	h, f, _ := newFakeHub(t)
	f.fail = fmt.Errorf("沙盒初始化失败")
	if _, err := h.start("x", ".", "b1", SandboxConfig{}); err == nil {
		t.Fatal("应返回启动失败")
	}
	if h.running != 0 || len(h.jobs) != 0 {
		t.Fatalf("启动失败后不该留下运行计数/作业：running=%d jobs=%d", h.running, len(h.jobs))
	}
	f2 := &fakeStart{}
	h.startProc = f2.start
	j, err := h.start("y", ".", "b2", SandboxConfig{})
	if err != nil {
		t.Fatal(err)
	}
	if j.id != "j1" {
		t.Fatalf("失败的启动不应消耗 id，got %s", j.id)
	}
	h.shutdown()
}

// ── ⑧ shutdown：在跑的作业全部强杀 ─────────────────────────────────

func TestJobHub_ShutdownKillsAll(t *testing.T) {
	h, f, l := newFakeHub(t)
	for i := 0; i < 3; i++ {
		if _, err := h.start("x", ".", "b", SandboxConfig{}); err != nil {
			t.Fatal(err)
		}
	}
	h.shutdown()
	f.mu.Lock()
	procs := append([]*fakeProc(nil), f.procs...)
	f.mu.Unlock()
	for i, p := range procs {
		select {
		case <-p.killCh:
		default:
			t.Fatalf("第 %d 个作业没有被 kill", i+1)
		}
	}
	if !h.closed {
		t.Fatal("shutdown 后应标记 closed")
	}
	// ★ I14：shutdown 必须把 end 发完（否则 journal 里 start 永远没有 end）。
	ends := 0
	for _, m := range l.all() {
		if m["t"] == "job" && m["phase"] == "end" {
			ends++
			if m["status"] != "killed" {
				t.Fatalf("shutdown 的 end 应为 killed：%v", m)
			}
		}
	}
	if ends != 3 {
		t.Fatalf("3 个在跑作业应各有一条 end，got %d", ends)
	}
}
