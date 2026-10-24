// llm.go —— OpenAI 兼容端点的流式客户端（P3 规格 §1.1 条 2）。
//
// 职责边界：本文件**不知道协议的存在**——它只把 HTTP/SSE 流变成结构化的
// delta 回调与聚合结果，协议映射（block_start/delta/ask/...）全部在 session.go。
// 这样 llm 层可以单独用脚本化 SSE 服务器做离线 E2E（真 API key 到位前验收）。
//
// 零第三方依赖（P3 规格 D1）：SSE 是纯文本协议，标准库足够。
//
// 兼容面：OpenAI 兼容端点普遍认 `chat/completions` + `stream:true`；
// 思维链字段兼容 `reasoning_content`（GLM / DeepSeek）与 `reasoning` 两种拼写。
// usage 只有在请求带 `stream_options.include_usage` 时才保证出现在末尾块。

package main

import (
	"bufio"
	"bytes"
	"context"
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"net/http"
	"strings"
	"time"
)

// llmConfig = 端点参数（P3 规格 §4.1；不绑定厂商）。
// ★ v4（P3.6）：ProviderID/Context/MaxOutput 支持热切换——`model` 消息
//   （协议 §4）在轮间整体替换这份配置；Context 是 usage 分母（§3.7 按轮覆盖）。
type llmConfig struct {
	BaseURL    string // 如 https://open.bigmodel.cn/api/paas/v4（不含 /chat/completions）
	Model      string
	Key        string
	ProviderID string // 提供商 id（model 消息按它寻址；与 GUI 配置文件同一命名）
	Context    int    // 上下文容量分母；0 = usage 不带 context（GUI 显示 —）
	MaxOutput  int    // 请求体 max_tokens；0 = 不发（端点默认）
}

type llmClient struct {
	cfg llmConfig
	hc  *http.Client
	log func(string, ...any)
}

func newLLMClient(cfg llmConfig, log func(string, ...any)) *llmClient {
	return &llmClient{
		cfg: cfg,
		// 整体不设 Timeout（流式生成可持续数分钟）；传输层给连接/首字节的
		// 合理上界。请求级 deadline 由 Stream 的 ctx 负责（含用户 cancel）。
		hc: &http.Client{
			Transport: &http.Transport{
				ResponseHeaderTimeout: 60 * time.Second,
				TLSHandshakeTimeout:   10 * time.Second,
			},
		},
		log: log,
	}
}

// ── 请求侧类型 ──────────────────────────────────────────────────

// chatMessage = 对话历史的一条。一个结构体覆盖三种角色（OpenAI 兼容形态）：
//   user / system：只 Content；
//   assistant：Content + ToolCalls（模型上一轮发起的调用，必须原样回传）；
//   tool：ToolCallID + Content（工具结果回给模型）。
type chatMessage struct {
	Role       string        `json:"role"`
	Content    string        `json:"content"`
	ToolCalls  []apiToolCall `json:"tool_calls,omitempty"`
	ToolCallID string        `json:"tool_call_id,omitempty"`
}

// apiToolCall = assistant 消息里的工具调用（回传给 API 的形态）。
type apiToolCall struct {
	ID       string   `json:"id"`
	Type     string   `json:"type"` // 恒 "function"
	Index    int      `json:"index,omitempty"`
	Function funcSpec `json:"function"`
}

type funcSpec struct {
	Name      string `json:"name"`
	Arguments string `json:"arguments"` // JSON 字符串（OpenAI 兼容约定，不是对象）
}

// toolDef = 工具定义（JSON Schema 形式上交 API）。
type toolDef struct {
	Name        string         `json:"name"`
	Description string         `json:"description"`
	Parameters  map[string]any `json:"parameters"`
}

// toolEnvelope = OpenAI 兼容协议的 tools 包装层：
// `{"type":"function","function":{name,description,parameters}}`。
// ★ 实测（2026-10-24 云知声）：裸的 toolDef 会被
// `'tools[0].type' must be 'function'` 拒绝——离线脚本服务器只记录不校验，
// 这类 schema 错误正是真 API 验收要抓的（规格 §8.3 的分工）。
type toolEnvelope struct {
	Type     string  `json:"type"`
	Function toolDef `json:"function"`
}

type chatUsage struct {
	PromptTokens     int `json:"prompt_tokens"`
	CompletionTokens int `json:"completion_tokens"`
}

// ── 流式事件 ────────────────────────────────────────────────────

// toolDelta = 一片 tool_calls 增量。id/name 只在首片出现；Arguments 是**分片**，
// 必须按 index 顺序拼接——这正是「聚合」存在的原因（模型把 JSON 参数撕成
// 几十片发过来，直接用最后一片会拿到半截 JSON）。解析目标见 streamChunk.toolDelta0。

// aggregatedCall = 聚合完成的一次工具调用（Stream 的返回值之一）。
type aggregatedCall struct {
	ID        string
	Name      string
	Arguments string
}

// Stream：一轮 LLM 请求（可能含工具调用）。onDelta 在收到正文/思维链增量时被调
// （两参可为空串；空串不调）。返回聚合后的工具调用列表与 usage。
//
// ctx 取消 = 用户 cancel / 超时：立刻中断 SSE 读并返回 ctx.Err()
// （调用方据此把块收尾、状态回 idle——协议 §4.6）。
func (c *llmClient) Stream(ctx context.Context, msgs []chatMessage, tools []toolDef,
	onDelta func(content, reasoning string)) (calls []aggregatedCall, usage *chatUsage, err error) {

	// tools 上交前套 OpenAI 兼容协议的包装层（toolEnvelope）。
	enveloped := make([]toolEnvelope, len(tools))
	for i, td := range tools {
		enveloped[i] = toolEnvelope{Type: "function", Function: td}
	}
	body := map[string]any{
		"model":    c.cfg.Model,
		"messages": msgs,
	}
	if c.cfg.MaxOutput > 0 {
		body["max_tokens"] = c.cfg.MaxOutput
	}
	body["tools"] = enveloped
	// 模型自己决定是否用工具（P3 纪律：core 不做策略）
	body["tool_choice"] = "auto"
	body["stream"] = true
	body["stream_options"] = map[string]any{
		"include_usage": true, // 末尾块带 usage（真值纪律：不用就不用，不估）
	}
	payload, err := json.Marshal(body)
	if err != nil {
		return nil, nil, fmt.Errorf("编码请求失败: %w", err)
	}

	// 请求级 deadline：流挂着不动（服务端断流不发 FIN）时兜底。
	// 10 分钟足够覆盖长思维链；用户 cancel 随时能提前打断。
	reqCtx, cancel := context.WithTimeout(ctx, 600*time.Second)
	defer cancel()

	url := strings.TrimRight(c.cfg.BaseURL, "/") + "/chat/completions"
	req, err := http.NewRequestWithContext(reqCtx, http.MethodPost, url, bytes.NewReader(payload))
	if err != nil {
		return nil, nil, fmt.Errorf("构造请求失败: %w", err)
	}
	req.Header.Set("Content-Type", "application/json")
	req.Header.Set("Authorization", "Bearer "+c.cfg.Key)

	resp, err := c.hc.Do(req)
	if err != nil {
		// cancel 与超时在这里都表现为 Do 出错；区分开，GUI 的提示才有意义。
		if ctx.Err() != nil {
			return nil, nil, ctx.Err() // 用户主动取消
		}
		if reqCtx.Err() == context.DeadlineExceeded {
			return nil, nil, errors.New("请求超时（600s 无进展）")
		}
		return nil, nil, fmt.Errorf("请求失败: %w", err)
	}
	defer resp.Body.Close()

	if resp.StatusCode != http.StatusOK {
		snippet, _ := io.ReadAll(io.LimitReader(resp.Body, 2048))
		return nil, nil, fmt.Errorf("API 返回 %s: %s", resp.Status, strings.TrimSpace(string(snippet)))
	}

	// ── SSE 解析 ────────────────────────────────────────────────
	// 每个事件一行 `data: {json}`；`data: [DONE]` 结束。空行是事件分隔，
	// 跳过即可——我们按行解析，不维护多行事件缓冲（OpenAI 兼容流不折行）。
	agg := map[int]*aggregatedCall{} // tool_calls 按 index 聚合
	order := []int{}                 // index 首现顺序（保证返回列表稳定）
	sc := bufio.NewScanner(resp.Body)
	sc.Buffer(make([]byte, 0, 64*1024), 4*1024*1024)
	for sc.Scan() {
		if ctx.Err() != nil {
			return nil, usage, ctx.Err()
		}
		line := strings.TrimSpace(sc.Text())
		if !strings.HasPrefix(line, "data:") {
			continue // 空行 / 注释 / 非 data 字段
		}
		data := strings.TrimSpace(strings.TrimPrefix(line, "data:"))
		if data == "[DONE]" {
			break
		}
		var chunk streamChunk
		if err := json.Unmarshal([]byte(data), &chunk); err != nil {
			c.log("跳过无法解析的 SSE 块: %v", err)
			continue
		}
		if chunk.Usage != nil {
			usage = chunk.Usage // 取最后一次非空（= 累计值）
		}
		if len(chunk.Choices) == 0 {
			continue
		}
		d := chunk.Choices[0].Delta
		reasoning := d.Reasoning
		if reasoning == "" {
			reasoning = d.ReasoningAlt
		}
		if d.Content != "" || reasoning != "" {
			onDelta(d.Content, reasoning)
		}
		for _, tc := range d.ToolCalls {
			a, ok := agg[tc.Index]
			if !ok {
				a = &aggregatedCall{}
				agg[tc.Index] = a
				order = append(order, tc.Index)
			}
			if tc.ID != "" {
				a.ID = tc.ID
			}
			if tc.Function.Name != "" {
				a.Name += tc.Function.Name // name 也可能分片
			}
			a.Arguments += tc.Function.Arguments
		}
		if chunk.Choices[0].Finish != "" {
			// finish_reason 出现不代表流结束（后面还有 usage 块），
			// 记录但继续读到 [DONE] / EOF。
			continue
		}
	}
	if err := sc.Err(); err != nil {
		if ctx.Err() != nil {
			return nil, usage, ctx.Err()
		}
		return nil, usage, fmt.Errorf("读取流中断: %w", err)
	}
	for _, i := range order {
		calls = append(calls, *agg[i])
	}
	return calls, usage, nil
}

// streamChunk = SSE data 行的解析目标（只取需要的字段）。
type streamChunk struct {
	Choices []struct {
		Delta struct {
			Content   string        `json:"content"`
			Reasoning string        `json:"reasoning_content"`
			// 有些端点用 `reasoning` 而非 `reasoning_content`——两个字段都试。
			ReasoningAlt string       `json:"reasoning"`
			ToolCalls    []toolDelta0 `json:"tool_calls"`
		} `json:"delta"`
		Finish string `json:"finish_reason"`
	} `json:"choices"`
	Usage *chatUsage `json:"usage"`
}

type toolDelta0 struct {
	Index    int    `json:"index"`
	ID       string `json:"id"`
	Function struct {
		Name      string `json:"name"`
		Arguments string `json:"arguments"`
	} `json:"function"`
}
