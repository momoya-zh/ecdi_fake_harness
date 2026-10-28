package memory

import (
	"fmt"
	"os"
	"path/filepath"
	"sort"
	"strings"
)

const (
	// MemoryLimitRatio：记忆总字节（折算 token）上限 = contextLimit 的 10%
	MemoryLimitRatio = 0.10
	// BytesPerToken：通用折算率（1 token ≈ 4 字节）
	BytesPerToken = 4
)

// FileItem 表示一个参与加载的记忆文件元数据与内容。
type FileItem struct {
	Name       string `json:"name"`                 // 文件名，如 "rules.md"
	Path       string `json:"path,omitempty"`        // 磁盘绝对路径
	Source     string `json:"source"`               // "global" 或 "workspace"
	Bytes      int64  `json:"bytes"`                // 文件字节数
	Tokens     int    `json:"tokens"`               // 折算 token 数
	Content    string `json:"-"`                    // 文件内容（仅给模型吃，不进 hello/JSON）
	Skipped    bool   `json:"skipped"`              // 是否被跳过
	SkipReason string `json:"skip_reason,omitempty"`// 跳过原因（如超出 10% 上限）
}

// MemoryResult 记忆加载与格式化结算结果。
type MemoryResult struct {
	Files        []FileItem // 全部文件（按全局在前、工作区在后的最终顺序，含被跳过项）
	LoadedFiles  []FileItem // 实际加载并注入 prompt 的文件列表
	SkippedFiles []FileItem // 因超限等原因被整份跳过的文件列表
	TotalBytes   int64      // 已加载文件的总字节数
	TotalTokens  int        // 已加载文件的总折算 token 数
	PromptText   string     // 拼装好的 Markdown 提示词区段（无文件时为空）
}

// EstimateTokens 将字节数折算为 token 数。
func EstimateTokens(bytes int64) int {
	if bytes <= 0 {
		return 0
	}
	return int((bytes + BytesPerToken - 1) / BytesPerToken)
}

// DefaultGlobalDir 返回默认全局记忆目录：可执行文件同级目录下的 data/memory。
func DefaultGlobalDir() string {
	if exe, err := os.Executable(); err == nil {
		return filepath.Join(filepath.Dir(exe), "data", "memory")
	}
	return filepath.Join("data", "memory")
}

// scanDir 扫描目录下的直接 *.md 文件（不递归），按文件名正序排序。
func scanDir(dir, source string) ([]FileItem, error) {
	if dir == "" {
		return nil, nil
	}
	entries, err := os.ReadDir(dir)
	if err != nil {
		if os.IsNotExist(err) {
			return nil, nil
		}
		return nil, err
	}

	var items []FileItem
	for _, entry := range entries {
		if entry.IsDir() {
			continue
		}
		name := entry.Name()
		if !strings.EqualFold(filepath.Ext(name), ".md") {
			continue
		}
		fullPath := filepath.Join(dir, name)
		info, err := entry.Info()
		if err != nil {
			continue
		}
		items = append(items, FileItem{
			Name:   name,
			Path:   fullPath,
			Source: source,
			Bytes:  info.Size(),
			Tokens: EstimateTokens(info.Size()),
		})
	}

	sort.Slice(items, func(i, j int) bool {
		return items[i].Name < items[j].Name
	})

	return items, nil
}

// Load 按照 §8.1 / §8.2 / §8.3 规则执行发现、去重覆盖、上限判定与内容读取：
// 1. 全局目录在前，工作区目录在后；
// 2. 仅读直接 .md，按文件名排序；
// 3. 工作区同名文件覆盖全局文件；
// 4. 若 contextLimit > 0，累加折算 token ≤ contextLimit * 10%；
//    超限的文件整份跳过并记录原因；若 contextLimit <= 0 则不设上限。
func Load(globalDir, workspaceDir string, contextLimit int) (*MemoryResult, error) {
	globalItems, err := scanDir(globalDir, "global")
	if err != nil {
		return nil, fmt.Errorf("scan global memory dir: %w", err)
	}

	wsItems, err := scanDir(workspaceDir, "workspace")
	if err != nil {
		return nil, fmt.Errorf("scan workspace memory dir: %w", err)
	}

	// 建立工作区文件集合（用于同名覆盖全局文件）
	wsNames := make(map[string]bool, len(wsItems))
	for _, item := range wsItems {
		wsNames[item.Name] = true
	}

	var candidates []FileItem
	// 全局在前，但被工作区同名覆盖的直接剔除
	for _, g := range globalItems {
		if wsNames[g.Name] {
			continue
		}
		candidates = append(candidates, g)
	}
	// 工作区在后
	candidates = append(candidates, wsItems...)

	res := &MemoryResult{}
	var tokenBudget int
	hasBudget := contextLimit > 0
	if hasBudget {
		tokenBudget = int(float64(contextLimit) * MemoryLimitRatio)
	}

	currTokens := 0
	for _, cand := range candidates {
		item := cand
		if hasBudget && (currTokens+item.Tokens > tokenBudget) {
			item.Skipped = true
			item.SkipReason = fmt.Sprintf("超出容量上限 10%%（需要 %d tokens，剩余预算 %d tokens）",
				item.Tokens, tokenBudget-currTokens)
			res.SkippedFiles = append(res.SkippedFiles, item)
			res.Files = append(res.Files, item)
			continue
		}

		// 读取文件内容
		data, err := os.ReadFile(item.Path)
		if err != nil {
			item.Skipped = true
			item.SkipReason = fmt.Sprintf("读取失败: %v", err)
			res.SkippedFiles = append(res.SkippedFiles, item)
			res.Files = append(res.Files, item)
			continue
		}

		item.Content = string(data)
		currTokens += item.Tokens
		res.TotalBytes += item.Bytes
		res.TotalTokens += item.Tokens
		res.LoadedFiles = append(res.LoadedFiles, item)
		res.Files = append(res.Files, item)
	}

	if len(res.LoadedFiles) > 0 {
		res.PromptText = FormatSystemPrompt(res.LoadedFiles)
	}

	return res, nil
}

// FormatSystemPrompt 将成功加载的记忆文件拼装为 System Prompt 的独立区段。
func FormatSystemPrompt(loaded []FileItem) string {
	if len(loaded) == 0 {
		return ""
	}

	var sb strings.Builder
	sb.WriteString("\n\n## 记忆 (Memory)\n\n")
	sb.WriteString("以下为项目与用户的长期记忆，作为指导上下文。请在回复时遵从记忆中的设定与约定：\n")

	for _, f := range loaded {
		sourceLabel := "工作区"
		if f.Source == "global" {
			sourceLabel = "全局"
		}
		sb.WriteString(fmt.Sprintf("\n### [%s] %s\n\n", sourceLabel, f.Name))
		sb.WriteString(strings.TrimSpace(f.Content))
		sb.WriteString("\n")
	}

	return sb.String()
}
