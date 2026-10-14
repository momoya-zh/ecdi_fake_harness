# ECDI_fake_harness · P1 实施规格：GUI 骨架（对回放）

> **定位**：P1 = 让 GUI 能对着夹具跑起来，并产出第一批 ECDI 评估读数。
> 轻量实施规格，不启用 ECDI 那种「需求 → 初设 → 详设」三件套。
> 状态：**v1.1 已实施**（2026-10-05 收口；v1.1 增补 = Phase 27 探针套件 + 真实转录夹具，见 §1.1 条 9）。实施时发现规格与事实不符，先改规格再改代码。

---

## §1 范围

### 1.1 做

| # | 内容 |
|---|---|
| 1 | 独立 CMake 工程 `gui/`，以 `find_package(ECDI CONFIG REQUIRED)` **外部消费** ECDI |
| 2 | `MessageSource` 接口 + `ReplaySource`（读 JSONL，`instant` / `timed` 两种模式） |
| 3 | 块模型 + 转录渲染（`ScrollView` + 行布局 + `CollapsiblePanel` 卡片） |
| 4 | 流式合并（~30Hz 定时器刷）与**重绘测量**（应用自有的 `CountingPanel`） |
| 5 | 三种启动模式：`--replay <file> --instant` / `--timed` / `--help` |
| 6 | 输入框（`TextBox`）+ 发送 / 停止按钮（v1 仅 UI，不回话） |
| 7 | 权限 `ask` 的显示与回答（回放模式：点击后继续） |
| 8 | 退出时输出机器可读评估摘要（`--out <json>`） |
| 9 | **Phase 27 探针套件**（v1.1 增补）：`PaintProbe`/`MeasureProbe` 装饰器 + `--cull-probe`（命令分类 / 执行端帧时 / 测量成本）+ `--scroll-sweep <px>`（滚动扫描，扫完自动退出）；真实转录夹具 `protocol/fixtures/realistic/` |

### 1.2 不做（显式排除）

| 内容 | 理由 |
|---|---|
| `ChildSource` / 子进程 / `PostToUi` 真路径 | 属于 P1.5 / P3；本规格**只留接口**，不实现 |
| Go core、真实模型、任何工具 | P3 以后。★ **但 P3 不是可选收尾——它是本项目成立的理由之一**（见 [`positioning.md`](positioning.md) §2 / §6）。本表说的是 **P1 的范围**，不是说 P3 可以不做 |
| Markdown / 富文本 / 多会话 / 文件树 / 托盘 | v1 范围冻结 |
| **任何对 ECDI 框架的修改** | 铁律：只登记需求，不实现 |

---

## §2 基线事实（2026-10-03 实测）

| # | 事实 | 影响 |
|---|---|---|
| B1 | `Window::GetRootWidget()` 返回 `Widget&`，root 由 `Window` 内部 `make_unique<Widget>()` 构造（`Window.cpp:63`） | ★ **应用无法替换 root 子类** ⇒ 重绘计数不能用"子类化 root"，改用应用自有的 `CountingPanel`（见 §6） |
| B2 | 仓库中**不存在任何 ECDI install 前缀**（`ECDI-install` / `ECDI/install` / `third_party` 均缺失） | `find_package(ECDI CONFIG REQUIRED)` 前必须先执行一次 `cmake --install`（§7.0） |
| B3 | 本机工具链已确认：CLion Ninja `C:/Program Files/JetBrains/CLion 2026.2.1/bin/ninja/win/x64/ninja.exe`；MSVC cl `C:/Program Files/Microsoft Visual Studio/18/Community/VC/Tools/MSVC/14.51.36231/bin/Hostx64/x64/cl.exe` | 构建命令用 `-G Ninja` + `vcvars64`（§7.1） |
| B4 | 关键公共头可消费：`Widget/CollapsiblePanel.h`、`Widget/ScrollView.h`、`Layout/ListLayout.h`、`Widget/TextBox.h`、`Widget/Panel.h`、`Layout/VerticalLayout.h` 均存在 | §5 渲染映射基于这些 |
| B5 | `protocol/validate.py` 4 份夹具全绿；`scale-transcript-2000.jsonl` = 2000 块 / 250 工具对 / 4590 delta | 本规格直接引用的测试输入 |
| B6 | ECDI 采用 `ExactVersion` 兼容性（`0.x` 不容忍自动升级） | `find_package` 不带版本号，用 `HINTS` 指定前缀 |

---

## §3 文件清单

| 文件 | 职责 |
|---|---|
| `gui/CMakeLists.txt` | 独立工程；`find_package(ECDI CONFIG REQUIRED HINTS ...)`；`add_executable(harness_gui ...)`；链接 `ECDI::ECDI` |
| `gui/src/main.cpp` | `wWinMain`：解析 CLI（`--replay/--timed/--instant/--out/--ecdi-commit`）；创建 `Application/Window`；注入 `ReplaySource`；进入消息循环 |
| `gui/src/App.cpp`（探针） | ★ **`--probe-contrast`**（2026-10-08 增补）：探针专用高对比配色（纯白底 / 纯黑字 / 零描边）——默认关闭，既有路径逐位等价。动机 = 暗色主题下像素探针与圆角/描边/滚动条抢阈值，产生仪器噪声 |
| `gui/src/MessageSource.h` | 接口：`virtual bool Next(std::string& line) = 0;`（阻塞读一条 core→gui 行） |
| `gui/src/ReplaySource.h` + `.cpp` | 读文件；`instant` 同步一次灌入所有行；`timed` 由 App 定时器按 `ts` 到期拉取 |
| `gui/src/Json.h` + `.cpp` | **极简 JSON 解析**（对象/数组/字符串/数字/bool/null，~200 行，零第三方）。**JSON 解析归应用，不进 ECDI** |
| `gui/src/Transcript.h` + `.cpp` | 块模型（`Block{id,kind,name,args,text,ok,for}`）；把一条 JSONL 行解析成事件；维护块表与 `seq` 连续性（I4）；统计 `seqGaps` |
| `gui/src/CountingPanel.h` + `.cpp` | `Panel` 子类，覆盖 `OnPaint`：**每次重绘计数 + 记录帧时**（`GetPaintCount()` / `GetFrameDurationsUs()`） |
| `gui/src/PaintProbe.h` + `.cpp` | `RenderingBackend` 装饰器：逐帧命令计数/分类（rect/text/clip/…）+ 执行端墙钟（`BeginFrame..EndFrame`）——`--cull-probe` 时包 `services.renderer` |
| `gui/src/MeasureProbe.h` + `.cpp` | `TextMeasurer` 装饰器：`MeasureText`/`LineHeight` 调用数、累计墙钟、字符数——同 flag 包 `services.measurer`（autosize 模式测量成本的量具） |
| `gui/src/App.h` + `.cpp` | 组装：`CountingPanel` 作为 root 的唯一子节点；内部 `VerticalLayout`：转录 `ScrollView`（stretch=1）+ `PromptBar`（固定高）；定时器；汇总输出 |
| `gui/src/PromptBar.h` + `.cpp` | 底部输入行：**单行** `TextBox` + 发送/停止 `Button`（v1 点击仅记录并 `Invalidate`，不回话）。★ ECDI 的 `TextBox` 多行/滚动**已随 Phase 8.5.2 落地**（原记账 #16 闭合）——v1 Composer 形态仍取单行（回车发送）；升级多行是 harness 自身的待办，不再是 ECDI 能力请求 |

**新增文件只需改一处**：CMake 用 `file(GLOB_RECURSE ... CONFIGURE_DEPENDS)` 自动入库（沿用 ECDI 条 66 的结论）。

---

## §4 接口定义

### 4.1 MessageSource

```cpp
// MessageSource.h
class MessageSource {
public:
    virtual ~MessageSource() = default;
    // 阻塞读取下一条 core->gui 行；EOF 或致命错误返回 false。
    // P1 只有 ReplaySource；ChildSource（子进程 stdout）在 P3 实现，接口保持不变。
    virtual bool Next(std::string& line) = 0;
};
```

### 4.2 CountingPanel（替代原 "RootCounter"）

```cpp
// CountingPanel.h
class CountingPanel : public Panel {
public:
    size_t PaintCount() const;                       // 实际重绘次数
    const std::vector<uint64_t>& FrameDurationsUs() const;
private:
    void OnPaint(PaintContext& ctx, int x, int y) override;   // 计数 + chrono 计时
    size_t m_paintCount = 0;
    std::vector<uint64_t> m_frameUs;
};
```

★ **为什么是 CountingPanel 而不是 RootCounter**：`Window::GetRootWidget()` 返回 `Widget&`，应用无法替换 root 类型（B1）。`CountingPanel` 是应用自有的 `Panel` 子类，作为 root 的**唯一子节点**铺满客户区，其 `OnPaint` 在每次窗口重绘时都会被调用一次。

### 4.3 Transcript

```cpp
// Transcript.h
class Transcript {
public:
    // 解析一行 JSONL。返回 false 表示违反协议不变量（I1/I2/I4 等），并记录原因。
    bool OnLine(const std::string& jsonLine, std::string& parseError);
    size_t SeqGaps() const;                          // I4 命中数
    size_t Blocks() const;                           // 已启动的块数
};
```

---

## §5 渲染映射与布局

### 5.1 根布局

```
Window 客户区
└─ CountingPanel（stretch=1，铺满）          ← 重绘计数点
   └─ VerticalLayout
      ├─ ScrollView（stretch=1）             ← 转录视口
      │   └─ ScrollContent → 行布局
      └─ PromptBar（固定高度 ~48 DIP）        ← 输入行
```

### 5.2 块 → 行

| 块 kind | 渲染 | 备注 |
|---|---|---|
| `user` | 右对齐 `Label`（行 Panel 内） | 初始文本来自 `block_start.text` |
| `assistant` | 左对齐 `Label`，流式追加 | delta 累加到行内 Label |
| `tool_call` | `CollapsiblePanel`，默认折叠；标题=`name`；展开体=args 美化（JSON 缩进文本） | 行本身是卡片 |
| `tool_result` | `CollapsiblePanel`，默认折叠；标题=`ok ? 成功 : 失败` + `summary` | 需展示 `for` 引用关系（I7） |
| `system` / `error` | 弱化色、居中 `Label` | — |

### 5.3 行布局：A/B 两个模式（★ 这是 P1 的核心评估对照组）

| 模式 | 布局 | 预期行为 | 评估价值 |
|---|---|---|---|
| `autosize`（默认） | `VerticalLayout` + 每行 `AutoSize()`（`GetPreferredSize` → 文本测量） | 行高随内容变化；每帧对所有可见行做文本测量 | 暴露 ECDI 的**文本测量成本**（评估指标 ④） |
| `listlayout` | `ListLayout(固定行高, 0)` | 行高固定；不测量 | 压测 `ListLayout`（刚交付的新路径）本身，并做 A/B 对照 |

`--row-mode autosize|listlayout` 命令行开关，默认 `autosize`。两个模式跑同一份夹具，得到两组读数。

---

## §6 流式合并与重绘测量

### 6.1 单定时器驱动（~30Hz）

`App` 启动一个 ~33ms 的定时器（API 以 ECDI `TimerEvent`/`Window::StartTimer` 为准，实施时核对签名）。每个 tick：

1. **拉取**：调用 `ReplaySource::Next` 拉取 `ts` 已到期的行（`timed` 模式按夹具首行 ts 相对计时；`instant` 模式第一轮全量拉完）。
2. **合并**：新到的 delta 只**追加到块内 Label 的文本缓冲区**，不立刻逐字符刷新。
3. **刷新**：tick 结束调用一次 `CountingPanel->Invalidate()`（实际重绘由 ECDI 合并）。

### 6.2 测量

| 量 | 采集点 | 说明 |
|---|---|---|
| 重绘请求数 | `App` 封装 `requestRepaint()` 计数 | 应用侧 `Invalidate()` 调用次数 |
| 实际重绘数 | `CountingPanel::OnPaint` 计数 | 每次窗口重绘触发一次 |
| 帧时 | `CountingPanel::OnPaint` 内 `std::chrono` 计时 | 存 `m_frameUs` 向量 |

### 6.3 退出摘要（`--out <file>`）

```json
{
  "fixture": "scale-transcript-2000.jsonl",
  "rowMode": "autosize",
  "ecdiCommit": "9ad0697...",
  "blocks": 2000,
  "deltas": 4590,
  "invalidations": 123,
  "paints": 456,
  "frameCount": 10,
  "maxPaintUs": 12345,
  "avgPaintUs": 1234,
  "seqGaps": 0
}
```

`ecdiCommit` 来自命令行 `--ecdi-commit <hash>`（GUI 无法自己知道 ECDI 的 commit），由启动脚本用 `git -C <ECDI 仓库> rev-parse HEAD` 拼进命令。

---

## §7 构建与运行

### §7.0 前置：安装 ECDI 到本仓库 `third_party/ecdi`（一次性）

```powershell
cd D:\Project\ECDI
cmake --build --preset vs2026-debug          # 或：cmake --build cmake-build-vs --config Debug
cmake --install . --config Debug --prefix D:\Project\ECDI_fake_harness\third_party\ecdi
```

`third_party/` 已由 `.gitignore` 排除（待补一条 `third_party/`）。安装产物不入库。

### §7.1 构建 GUI

```powershell
cd D:\Project\ECDI_fake_harness
# 方式 A：先加载 vcvars64，让 cl/ninja 在 PATH 上
cmd /c ""C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" && cmake -S gui -B gui/build -G Ninja -DECDI_PREFIX=D:\Project\ECDI_fake_harness\third_party\ecdi"
cmake --build gui/build
```

### §7.2 运行

```powershell
cd D:\Project\ECDI_fake_harness
$commit = git -C D:\Project\ECDI rev-parse HEAD

gui\build\harness_gui.exe --replay protocol\fixtures\demo-session.jsonl --instant --out evals\demo.json --ecdi-commit $commit
gui\build\harness_gui.exe --replay protocol\fixtures\scale\scale-transcript-2000.jsonl --instant --row-mode autosize --out evals\scale-autosize.json --ecdi-commit $commit
gui\build\harness_gui.exe --replay protocol\fixtures\scale\scale-transcript-2000.jsonl --timed --row-mode listlayout --out evals\scale-list.json --ecdi-commit $commit
```

---

## §8 验收 / 出口判据

| # | 判据 | 验证方式 |
|---|---|---|
| A1 | `gui` 构建成功（§7.1） | `cmake --build gui/build` 退出码 0 |
| A2 | `demo-session.jsonl --instant`：窗口出现、转录渲染、卡片可折叠、输入可打字、退出码 0 | 运行 + 目视 |
| A3 | `scale-transcript-2000.jsonl --instant`：不崩溃、可滚动 | 运行 + 目视 |
| A4 | `--out` 摘要文件生成，字段齐全 | 检查 JSON |
| A5 | `protocol/validate.py` 仍全绿 | 运行，exit 0 |
| A6 | 记录第一轮三个读数：`paints` / `avgPaintUs` / 滚动延迟（人工） | 写进 `evals/` |

**四链**（沿用 ECDI 条 43/44）：至少 MSVC + MinGW 两链跑通再报"绿"；报绿必须写清工具链与显示器缩放。

---

## §9 已知局限（明确推后）

- `ChildSource` / 子进程 / `PostToUi` 真路径 —— P1.5 / P3，本规格只留接口。
- 权限交互：`ask` 显示 + 回答即可；不做 `deny` 后的回滚。
- Markdown 渲染、只读文本选择复制、虚拟滚动 —— 均属 ECDI 能力请求，登记进 **`docs/ecdi-capability-requests.md`**（能力需求账本，v1.0 于 2026-10-08 建立），不在本仓库实现。
  ★ 该账本**此前只被引用、并不存在**（原文写「登记进账本」而全库无此文件）——2026-10-08 补齐，并把 P1 期的 F1–F8 一并收编。
- **多行输入**：ECDI `TextBox` 多行/滚动已于 Phase 8.5.2 落地（原记账 #16 闭合）——v1 Composer 仍用单行 + 回车发送（聊天惯例）；升级多行是 harness 自身的待办。
- 行高策略的两种模式中，`autosize` 对超长文本行不做截断（视口裁剪）。若 `Label` 不裁剪，先以 `ScrollContent` 的裁剪为准，不做额外工作。
