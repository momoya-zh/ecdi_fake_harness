# ECDI_fake_harness

> **一个能真用的 agent GUI，外加对 ECDI 的评估数据。** 本项目是 [ECDI](https://github.com/momoya-zh/ECDI)
> 的**外部消费者**：GUI 是**协议的渲染器**，对着手写/录制的 JSONL 就能跑完整个界面
> （**不需要**真模型）。
>
> ★ **两个目的都是第一等的**：① 对 ECDI 的**评估数据 + 能力需求账本**；
> ② **一个真正能用的 agent GUI**。
> ② 不是 ① 的副产品——**只做展示的话，本项目就该待在 ECDI 的 `examples/` 里，不该单独成仓**。
> 详见 [`docs/positioning.md`](docs/positioning.md)（定位与信任模型）。
>
> ★★ **现状（2026-10-24 核对）**：协议、假 core、GUI 三侧**都已到 v3 并收口**，界面可以拿回放夹具
> 跑完整流程、也能真起子进程。但 **`core/` 里只有假 core**——**接真 agent（P3）尚未开始**，
> 所以「能拿它干活」目前还只是设计方案而非既成事实。P3 是本项目的主线（`positioning.md` §2/§6）。

---

## 这是什么 / 不是什么

| 是 | 不是 |
|---|---|
| ECDI 的**外部消费者**：`find_package(ECDI CONFIG REQUIRED)` + `ECDI::ECDI` | 不是 ECDI 的一部分，不随框架一起构建 |
| **协议先于两侧实现**：`protocol/` 是唯一契约，GUI 只看它就能跑 | 不是「先写实现、事后补协议」 |
| **假 core**（Go，零第三方）：真子进程 + 双向管道，但不调模型 | 不是真 agent；不接任何 LLM（真 core = **P3**，会用另一个程序替换它，协议不变） |
| **评估数据**的产线：`evals/` 下机器可读读数 | 不是「为展示而存在」——**展示品不该单独成仓** |
| **一个能真用的 agent GUI**：接上真 agent 后能拿它干活 | 不是**商业产品**（无 roadmap / 无支持承诺 / 无工期） |

★ **铁律**：**任何对 ECDI 框架的修改都不在本仓库做**——这里只登记需求、只给证据、只判归属。
判「ECDI 缺能力」之前必须先查**基类公共头 + ECDI 源码仓**，且归属结论须有**只改一个自变量的
受控对照**支撑。（这条纪律是被代价换来的：`REQ-01`/`REQ-03`/`REQ-04` 三条都曾误判为 ECDI 缺陷，
复核后全部反转为 harness 侧。）

## 仓库结构

| 目录 | 职责 |
|---|---|
| `protocol/` | **唯一契约**。`PROTOCOL.md`（**v3**）+ `fixtures/`（**9 份夹具**）+ `validate.py`（**可执行规格**） |
| `gui/` | 独立 CMake 工程 `harness_gui`——外部消费 ECDI。**25 个源文件 / 14,178 行**（`App.cpp` 一直是大头，~0.5 MB） |
| `core/` | **假 core**（Go，零第三方）：`main.go` / `script.go` / `session.go`，3 文件 / **839 行** |
| `evals/` | 机器可读评估读数（JSON）——评估产出的落点（口径见 §当前状态） |
| `docs/` | **定位与信任模型**（`positioning.md`）· P1 / P1.5 规格 · **能力需求账本** · **variant-c 落地对照**（`variant-c-ecdi-mapping.md`）· GUI 原型（单文件 HTML） |
| `tools/` | `precommit-check.py`（BOM / 行尾 / 夹具校验，已接入 git hook）· `regress.py`（全量回归门） |

## 协议要点

- **分帧**：一行一个 JSON，`\n` 结尾；EOF 截断的最后一行**必须报错**，不得静默丢弃。
- **信封**：`{t, seq, ts}`；`seq` 每方向独立、从 0 起、严格递增。
- **不变量 I1–I10**。★ **I4（`seq` 缺口必须显式提示「消息丢失」）是本项目的核心评估钩子**——
  它对应 `Application::PostToUi` 的乱序/丢失语义，是「靠它才看得见」的那一类失败。
  ★ **I8**（v2 新增）：未知 `kind` 必须**中性渲染 + 显式提示**，不得伪装成有语义的形态。
  ★ **I9/I10**（v3 新增，只约束声明 `protocol >= 3` 的流）：`kind:"user"` 块必须带**唯一**
  `client_id`，`workspace` 响应必须带 `request_id` / 布尔 `ok` / `path`（失败还须 `message`）。
- **`kind` 七个值**（v2 起）：`user` / `assistant` / **`reasoning`** / `tool_call` / `tool_result` / `system` / `error`。
- **转录的唯一作者是 core**：GUI 不产生转录块，用户输入由 core 回显为 `kind:"user"`。
  ★ **v3 的边界**：GUI 可以先画一个**待确认**的本地气泡（`· 发送中`），但它不是转录块——
  core 用同一 `client_id` 回显后该气泡**原位确认**，绝不插入第二块；被拒或断连则标 `· 未确认`
  并**不进入**规范转录（因此也不进入回放）。
- **工作区切换是租约，不是命令**（v3）：`workspace` 只在 `idle` 被受理，一次只允许一个未决请求；
  未决期间 GUI **必须禁用发送**，core 也**必须拒绝**交错的 `prompt` / 第二个 `workspace`。
  成功后 GUI 只认 core 回的**实际路径**（规范化：绝对 / `/` 分隔 / 无尾随 `/` / 无 `.`·`..`）。
- **回放两模式**：`instant`（一次性灌入，测稳态渲染）/ `timed`（按 `ts` 真实间隔，测流式刷新）。
- **版本门的三个不对称**（`App.cpp:150` `kProtocolMaxSupported = 3`）：① core 声明的主版本**高于**上限才拒连，
  且拒连 = **停止消费**（`--instant` 下不 break 就会「边抱怨边照做」）；② **低于**上限从不拒连；
  ③ 缺 `caps["workspace"]` **不是**拒连条件，只隐藏工作区交互（v3 §3.1）——早前版本把 ① 和 ③ 混成一个条件，
  导致两份 hostile 夹具的 `hello` 一行都没被解析，I1/I4/I5 的负对照全部空转。
- ★ **`ask` 权限门是合作性契约，不是安全边界**（详见 `docs/positioning.md` §4.2）。

## 怎么跑

**① 校验协议夹具**（不需要构建任何东西）

```bash
python protocol/validate.py            # 退出码 = 未通过预期的夹具数（0 = 全绿）
```

`fixtures/hostile/**` **必须**触发至少一条不变量；其余**必须**满足全部不变量。
★ hostile 夹具「什么都没触发」= **失败**——一个恒成立的负向用例，与没有这个用例无法区分。

**② 构建 GUI**（需要已安装的 ECDI 前缀）

```bash
cmake -S gui -B gui/build -DECDI_PREFIX=<ECDI 安装前缀>
cmake --build gui/build --config Release
```

`gui/build/Release/harness_gui.exe` 是产物。

★ **`third_party/ecdi` 不入库**（在 `.gitignore` 里），clone 下来是**没有**的。
本仓库**不随 ECDI 一起构建**，所以必须先自己在 ECDI 仓库里产出安装前缀：

```bash
# 在 ECDI 仓库内（多配置生成器的库落在 Release/ 子目录，注意 --config）
cmake -S . -B build
cmake --build build --config Release
cmake --install build --config Release --prefix <此仓库>/third_party/ecdi
```

`ECDI_PREFIX` 的默认值就是 `third_party/ecdi`；装到那里则不必显式传参。
★ 消费口是**安装前缀**（`lib/cmake/ECDI/`），不是构建树 ⇒ 只编不 install 是接不上的。

**③ 回放一份夹具**

```bash
harness_gui.exe --replay protocol/fixtures/demo-session.jsonl --instant --out eval.json
```

**④ 拉起真子进程**（P1.5 通道）

先构建假 core（纯 Go、**零第三方**，标准库即可）：

```bash
cd core && go build -o bin/fake-core.exe . && cd ..
```

再跑：

```bash
harness_gui.exe --core core/bin/fake-core.exe --core-args --speed=0.2 --auto-prompt "hello"
```

★ `core/bin/` 不入库（`.gitignore`），`fake-core.exe` 需要自己编。
★ 两个易踩的点：`--core-args` 的值**按空格切分** ⇒ 用等号形式（`--speed=0.2`，不要 `--speed 0.2`）；
`--out` 摘要**只在正常退出后写** ⇒ 跑批用 `--auto-close <ms>` 让 app 自己退，别 kill。
★ **live 用例必须带 `--auto-prompt`**：假 core 是被动的，只回应用户输入——不给 prompt 时双方
握手后即静默，读数是 `blocks=0`（看起来像「core 挂了」，实为没喂输入）。
★ **负载旋钮 `--burst`**（`--core-args --burst`）：关掉节奏等待 + 把一轮输出攒成**一次写出**
⇒ 真造出队列积压。REQ-03 的「单回调预算」判据靠它才**可证伪**——不加时队列最多 8 行、那条
判据恒真；加上后 `maxLinesPerCallback` 恰好顶到 **256**（去掉预算则 430）。

**⑤ 回归门**（改完必跑；全绿才算收口）

```bash
python tools/regress.py     # 6 夹具结构基线 + 13 探针 + 2b live A/B（REQ-03）+ validate.py
```

读数落 `.scratch/`（gitignored）。★ 判据口径：夹具基线只钉 `blocks/deltas/messages`，
不钉 `paints/frames`（后者随字体与实现变化，钉死它会把一次合法收敛判成回归）。
★ 2b 节要真子进程（`core/bin/fake-core.exe`）：没有就 **SKIP**，不是 PASS——跳过必须说出来。

### 探针（**18 个 `--*-probe`，默认全关** —— 关闭时既有路径逐位不变）

| 探针 | 读什么 |
|---|---|
| `--cull-probe` | 命令分类 / 帧时 / 测量成本 |
| `--state-probe` | 回读按钮使能位（REQ-04） |
| `--hit-probe` | 命中门控受控对照（REQ-04 **边界**：禁用 ⇒ 点击不达） |
| `--order-probe` | 块**到达序**可从模型恢复（step 0a） |
| `--rebuild-probe` | 运行期整树重建（换主题的唯一诚实做法） |
| `--session-probe` | 多会话：创建 / 切换 / 冻结时钟 |
| `--sidebar-probe` | 侧栏会话列表与折叠 |
| `--answer-probe` | `ask` 的决定**入模型**（否则重建后答案丢失） |
| `--theme-probe` | 双主题令牌 |
| `--history-probe` | 磁盘文件作会话（replay 到末尾） |
| `--workspace-probe` | 工作区租约 + 目录列表（含 `bubbleShapeOk` 负对照） |
| `--menu-probe` | `···` 内联菜单（ECDI 无浮层，挂进 `VerticalLayout` 排位） |
| `--resize-probe` | 尺寸链：root / scroll / 中栏 / caption 逐相同步 |
| `--enter-probe` | Enter 发送 / Shift+Enter 换行 |
| `--hud-probe` | HUD 字段**读写配对**（抓「只读不写」的死分支） |
| `--grow-probe` | Composer 自动长高 7 相（含 `shrink` 与 `max-clamp`） |
| `--reasoning-probe` | 思维链折叠行为取证 |
| `--async-probe` | **REQ-03**：投递 vs 轮询的 A/B（**live**）——`blockedUs` / 唤醒承载占比 / 单回调预算 / 送达延迟分位 |

另有模式开关：`--legacy-poll`（**REQ-03 对照世界**：逐字复原每 tick 等 8ms 的旧路径）·
`--probe-contrast`（探针专用白底黑字）· `--row-mode autosize|listlayout`（布局 A/B）·
`--scroll-sweep`。★ **`--reasoning-probe` / `--order-probe` / `--workspace-probe` 尚未接入
`tools/regress.py` 的探针循环**——它们跑过，但还不是门（`--async-probe` 走的是 §2b 的 live 段，不在循环里）。

## 交付物在哪

| 想要什么 | 去哪看 |
|---|---|
| ★ **定位与信任模型** | `docs/positioning.md`（**两个目的** · 协议不是安全边界 · MCP 决策） |
| **ECDI 能力需求 / 缺陷登记** | `docs/ecdi-capability-requests.md`（**唯一账本**） |
| **评估读数**（机器可读 JSON） | `evals/`（口径见 §当前状态）· 工作读数在 `.scratch/`（gitignored） |
| **协议契约** | `protocol/PROTOCOL.md`（v3） |
| 阶段规格 | `docs/P1-gui-implementation-spec.md` · `docs/P1.5-child-process-spec.md` |
| GUI 设计原型 | `docs/prototype/`（单文件 HTML） |

## 能力账本现状

| 条目 | 内容 | 状态 |
|---|---|---|
| `REQ-01` | 含 `\n` 的短文本丢换行 | ✅ 闭合（**归属反转**：harness 侧 `needWrap` 只看宽度） |
| `REQ-02` | `tool_call` 的 `summary` 被吞 | ✅ 闭合（harness 侧） |
| `REQ-03` | `PostToUi` 能力已具备，harness 从未走异步投递路径（每 tick **8ms 预算**拉队列） | ✅ **已闭合（2026-10-24）**：改成「读线程投递唤醒 + tick 非阻塞兜底」，`PostToUi` 第一次有**框架外真实消费者**。实测 legacy `avgBlockedUs` **11599** → **0**、wake 承载 **99.8%** 的行、`--speed=1` 下送达 p50 **49µs vs 14774µs**。★ 顺带发现「8ms 预算」实测是 **8.8–11.8ms / max 23.9ms**（`cv.wait_for` 受时钟粒度约束） |
| `REQ-04` | 按钮使能缺 API | ✅ 闭合（**归属反转**）；**边界**（禁用 ⇒ 点击不达）亦已取证 |
| `REQ-05` | GDI / GL 的 `LineHeight` 不一致（14px vs 16px） | ✅ 闭合（**真 ECDI 缺陷**，Phase 31 已修）——账本**首条走完「登记 → 修复 → 跨仓库验证 → 回写」闭环**的需求 |
| `REQ-06` | 跨后端基线偏移（原登记「拉丁 +1 · CJK +2」） | ✅ **已裁决结案（2026-10-17，③ 记为已知现状）**；★ 读数订正：112 格实测 Δ∈[−13,+3] 且符号翻转，**+1/+2 只是 SimSun@14 单点** |
| `REQ-07` | （候选）折叠面板仍测量隐藏内容 | ❌ **复核否决**（ECDI 的 Layout 已跳过不可见子节点）——归属 harness 侧 |
| `REQ-08` | 环形进度条需**圆弧原语**（`DrawArc` 不存在） | ✅ **闭合（2026-10-16）**：ECDI **Phase 33** 落地 `DrawArc` + `RingProgressBar`，本项目作为**首个真实消费者**兑现（右栏上下文环，`commandMix.arc > 0` 机读证据） |
| `REQ-09` | `Font::family` **双链语义矛盾**（GDI=族名 / GL=文件名） | ✅ **已闭合（2026-10-17）**：ECDI `Win32FontSource` 补族名反查（禁子串匹配、文件名直查保留且优先）；验收 8 族名 **8/8 两链同 face**，`ecdi_tests` **465/465**。⇒ 我方 `kMonoFamily="Consolas"` **无需改动即恢复等宽** |
| `REQ-10` | `Font` **表达力**不足——字重 / face index / 复合族名第二项**三者皆不可达** | 🔄 **开放（等 ECDI 立项排期）**：`Font` 只有 `size`+`family`，两处硬编码 `FW_NORMAL`，`FT_New_Face(..., 0, ...)` 恒取 face 0。实测 `lfWeight=FW_BOLD` 在 GDI 侧对 5 个真实族全部 ≥1.29× 墨迹，但 **GL 无字段**；CJK 加粗**两链无公共解** ⇒ 当前唯一同时可用的是「不加粗」。★ **2026-10-24 ECDI 对码核验三条全部属实**，登记 `roadmap-deferred` **#70**（登记不立项） |
| `REQ-11` | `Color.a` **双链覆盖面不对等**（GDI 的 `DrawText` / `DrawFocusRect` 恒丢 alpha）+ 无虚线 | 📋 **已登记推后**：**当前零消费者**（`Theme.h` 全表 alpha=255）；实测 GDI 侧四档 alpha（255/128/64/16）输出**逐位相同**。★ **2026-10-24 ECDI 核验属实**，登记 `roadmap-deferred` **#71**，并改写 `Color.h` 的 alpha 注释声明双链发散 |
| `REQ-12` | 多行 `TextBox` 的 `GetPreferredSize` 返回**当前尺寸**（与 `TextWidget` 的 wrap 分支自相矛盾） | ✅ **已闭合（2026-10-21）**：ECDI 采纳「订正语义」而非「加查询」⇒ `TextBox::MultilinePreferred` 落地；本仓组合实现**同日收敛回框架**，`--grow-probe` 七相读数逐位不变。★ 负对照 4 条变异全 RED |
| `REQ-13` | 窗口**最小尺寸**约束（无 API，只能事后钳制 ⇒ 可见回弹） | ✅ **已闭合（2026-10-23）**：ECDI 落地 `Window::SetMinimumSize`（`WM_GETMINMAXINFO` + `WM_WINDOWPOSCHANGING` 两路拦截，客户区 DIP 口径）；本仓删除 `ClampToMinimum` 绕行。★ ECDI 测试 487 → **490** |

★ 三条路径值得对照着读：`REQ-12` 是「**同一框架两条规则矛盾**」（修答案即可），`REQ-13` 是
「**框架根本没有这条能力**」（只能加 API），`REQ-11` 是「**两后端行为发散**」（零消费者，先记账）。
判别方法随之不同：分别是**分支对读** · **公开 API 全表核对** · **逐原语 alpha 覆盖扫描**。
★ **harness 侧待办**（`F7` 夹具真缺口 / `F8` 数据处理卫生）不在本表，见账本 §4。

### 思维链：协议 v2 已闭合（原「协议空白」）

面向**开源推理模型**的 agent 会流式传**思维链**。v1 的 `kind` 只有六个值、**没有它的位置**，
而 I5 只管信封层的 `t` ⇒ 未知 `kind` **不被任何不变量覆盖**。

后果（已实测）：未知 `kind` 曾**静默掉进工具卡分支**，标题写死 `等待结果`——那正是
`tool_result` 在 `block_end` 到达**前**的占位标题 ⇒ **思维链与「正在等的工具结果」视觉上无法区分**，
且 `validate.py` 对它**返回 0**（一份满屏思维链的夹具会全绿通过）。

★ **处置（v2，2026-10-14）**：`kind:"reasoning"` 成为**一等公民** + 新不变量 **I8**
（未知 `kind` 必须中性渲染）+ §7 渲染形态（**默认收起**，标题 `﹀ 思考中` / `﹀ 已完成`，
点击展开箭头翻 `︿`）。另增 `hello.context`（用量百分比的分母）与 `block_end.tokens`。
详见 `protocol/PROTOCOL.md` §3.3 / §5 / §7 与账本 §2。

## 夹具

| 文件 | 用途 |
|---|---|
| `fixtures/demo-session.jsonl` | 正常路径：完整一场会话（43 条消息） |
| `fixtures/hostile/seq-gap.jsonl` | **I4 负向**：`seq` 缺口 + 未知 `t` + 悬空 `block_end` + 迟到 `delta` |
| `fixtures/hostile/truncated-final-line.jsonl` | **§1.2 负向**：最后一行被截断 |
| `fixtures/hostile/missing-client-id.jsonl` | **I9 负向（v3）**：`kind:"user"` 块没有 `client_id`——GUI 无处确认自己的临时气泡 |
| `fixtures/hostile/duplicate-client-id.jsonl` | **I9 负向（v3）**：两个用户块共用同一 `client_id`——确认会错位到其中任意一个 |
| `fixtures/hostile/workspace-bad-ack.jsonl` | **I10 负向（v3）**：`ok:false` 无 `message` + 重复 `request_id` + `ok:"yes"`（真值字符串会被当成成功） |
| `fixtures/realistic/realistic-session.jsonl` | 真实编码会话形态（Markdown / 长日志 / 1213 条消息） |
| `fixtures/scale/scale-transcript-2000.jsonl` | 规模基线（2000 块 / 8906 条消息 / **921 KB**） |
| `fixtures/cot/cot-session.jsonl` | **思维链**：长 + 流式 + **默认折叠**（1044 条消息 / 44 块）。★ `hello` 带 `context`、思维链 `block_end` 带 `tokens` |

★ **九份夹具全部声明 `protocol:3`**：四份正向（demo / realistic / cot / scale，`client_id` 由生成器或手写保证）
与五份 hostile。hostile 也必须声明 3——I9/I10 只约束 v3 流，若 hostile 停在 2，「负向用例」会被版本门豁免成空转。

三个生成器都是**确定性**的（同参 ⇒ 逐字节相同），所以 2000 / 5000 / 20000 是**旋钮**，不是砖：
`gen_scale.py` · `gen_realistic.py` · `gen_cot.py`（`--turns --thinking-chars --kind --context --tokens`）。

## 编码约定

★ **先分清两条互不相干的轴**。把它们混成「一套文件该怎么存」正是上一版表格的错误：
它说「文本一律 BOM + CRLF」，于是有人照着它把 `PROTOCOL.md` 与 `validate.py` 改成了 CRLF ——
一次「照文档办事」制造了两份坏文件。

### 轴一：行尾 —— 全仓一条规则，且已强制

`.gitattributes` 声明 `* text=auto eol=lf`；`git ls-files --eol` 对每个文本文件都回 `i/lf`。
**仓库里存的就是 LF，没有例外。** 工作区偶尔出现 CRLF 副本是本地工具留下的漂移，提交时被归一化，
`git checkout` 之后也会消失 —— 所以**不要拿工作区字节当约定**。

唯一的硬后果在 `*.jsonl`：CRLF 会被 `validate.py` 的 `read_fixture` 判 `ENC`（分帧靠 `\n`）。

### 轴二：BOM —— 按文件，且必须与该文件在 HEAD 中的字节一致

实测 **146 个**受版本控制的文件里 **37 个带 BOM、109 个不带**（2026-10-24 按 `HEAD` 重数），与扩展名无关：

| 带 BOM（UTF-8 `EF BB BF`） | 不带 BOM |
|---|---|
| `README.md` · `status.txt` · `core/*.go` | `.gitattributes` · `.gitignore` · `LICENSE` · `core/go.mod` |
| `docs/positioning.md` · `docs/P1-gui-implementation-spec.md` · `docs/P1.5-child-process-spec.md` · `docs/prototype/*.html`（**6 个中 5 个**） | `docs/ecdi-capability-requests.md` · `docs/variant-c-ecdi-mapping.md` · `docs/prototype/harness-hifi-variant-a.html` |
| `gui/src/` 的 19 个文件：`ChildSource.*` · `CountingPanel.*` · `Json.*` · `MeasureProbe.*` · `MessageSource.h` · `PaintProbe.*` · `PromptBar.h` · `ProtocolWriter.*` · `ReplaySource.*` · `Transcript.*` · `UiKit.h` | `gui/src/` 的 6 个文件：`App.cpp` · `App.h` · `PromptBar.cpp` · `Theme.h` · `ThemedCaptionBar.h` · `main.cpp` |
| `protocol/PROTOCOL.md` · `protocol/validate.py` | 三个 `gen_*.py` · **全部 `*.jsonl`**（九份夹具）· 全部 `evals/*.json`（82 份） |

★ **2026-10-24 复核订正两处**（原表面描述与 `HEAD` 不符）：① 三个 `gen_*.py` **无** BOM（原表把它们列在 BOM 侧）；
② `docs/prototype/` 现有 **6** 份 HTML（原表写「5 个中 4 个」）。其余逐文件重验一致。

★ **BOM 在本仓库不是编译正确性问题，别把它说成问题。** `gui/CMakeLists.txt:20` 给
`harness_gui` 传了 `/utf-8`（自初始提交 `f243579` 起），MSVC 不看 BOM 也知道源文件是 UTF-8——
证据是 `gui/src/App.cpp` 带 **31692 个非 ASCII 字符、无 BOM**，编译干净。
（`C4819` 那类教训属于**没有** `/utf-8` 的构建，比如 ECDI 自己的工程。）

那为什么还要按文件对齐 BOM？**因为逐字节回归基线**：本项目的判据大量依赖
「探针关闭时摘要逐字节不变」和「重建前后快照相等」。一次没有理由的加 BOM 会让 `git diff`、
sha256 基线与历史 A/B 全部失去可比性，而且这种改动看起来毫无意义，最难被复核。
⇒ **规则是「别动它」，不是「统一它」。**

复现这张表（**读 HEAD，不读工作区**）：

```bash
python - <<'PY'
import subprocess
files = [f for f in subprocess.run(['git', 'ls-files'], capture_output=True, text=True
                                  ).stdout.split('\n') if f]
bom = [f for f in files
       if subprocess.run(['git', 'cat-file', 'blob', 'HEAD:' + f],
                         capture_output=True).stdout.startswith(b'\xef\xbb\xbf')]
print('%d/%d files carry a BOM' % (len(bom), len(files)))
for f in bom:
    print('  ', f)
PY
```

★ 真正**可强制**的只有一处，它已经写进验证器：`*.jsonl` **无 BOM + LF**。
其余按现状描述 —— **不要发明统一规则**，那是上一版表格的原罪。

## 当前状态

> 时点：**2026-10-24**。逐轮进度摘要（机器向）在 `status.txt`；本 README 是人向入口。

### 已收口

| 项 | 读数 / 依据 |
|---|---|
| P1（GUI 骨架）· P1.5（子进程真路径，含反向通道） | 已收口 |
| **协议 v3 三侧落地** | 契约层（`PROTOCOL.md` + `validate.py` + **9 份夹具** + 3 个生成器）· core（`hello protocol:3`，`caps=[tools,ask,workspace]`）· GUI（`kProtocolMaxSupported = 3`）。九份夹具全部声明 `protocol:3`，`validate.py` 退出码 **0** |
| ECDI 消费前缀 | **`0.1.0` MSVC Release 静态**，安装于 2026-10-23 21:09；对应 ECDI 仓 **`096991b`**（含 `REQ-13` 的 `Window::SetMinimumSize`）；`ECDI.lib` **11,063,564 B** / sha256 `68247cd0…` |
| ECDI 自带套件 | `490 passed / 0 failed`（`REQ-13` 闭合时点；早期 `445/445` 见 `status.txt`） |
| 回归门 | `python tools/regress.py` = 6 夹具结构基线 + 13 探针 + **2b live A/B（REQ-03）** + `validate.py`；`tools/precommit-check.py` 已接 git hook（BOM 漂移 / `*.jsonl` 行尾 / 残留 lock） |
| **取数通路（REQ-03）** | 读线程经 `Application::PostToUi` 投递**唤醒**，UI 线程在消息循环里 drain；tick 只做**非阻塞**兜底。实测 UI 线程阻塞 `11599µs → 0`、唤醒承载 **99.8%** 的行、节奏化流下送达 p50 **49µs**（同口径轮询 14774µs）。单回调预算在 `--burst` 下恰好顶到 **256**（去掉预算 430）。`--legacy-poll` 逐字复原旧路径（对照 + 回滚） |

### 现在能做什么（都有探针与负对照）

回放 `instant` / `timed` 两种节奏 · 真子进程（`ChildSource`，双向管道）· 双主题运行期切换
（**整树重建**，不是换色）· 多会话（创建 / 切换 / 冻结时钟 / 删除）· 工作区**租约** + 真实目录列表 +
临时会话 · 会话归档 · 转录导出 · `···` 内联菜单 · 窗口缩放 + **最小尺寸 880×460**（框架拦，不回弹）·
Enter 发送 / Shift+Enter 换行 · Composer 随内容**自动长高**（封顶后内部滚动）·
思维链默认折叠 · 上下文用量环 · I4（`seq` 缺口）/ I8（未知 `kind`）**显式提示**

### **还**不能做什么（★ 这是下一步的输入）

| 缺什么 | 状态 |
|---|---|
| ★ **真 agent core** | **未开始**（`core/` 只有假 core）。协议与 GUI 两侧已就绪，缺的只在 core 那一侧 ⇒ **P3 是主线**，见 `docs/positioning.md` §2 / §6 |
| MCP host | 决策 **A**（harness 管配置）已于 2026-10-14 拍板，**零实现**；排在真 core 之后——没有 core，host 没有可授予的对象 |
| 字重 / 视觉层级 | 受 `REQ-10` 阻塞（ECDI 已登记 `roadmap-deferred` #70，**登记不立项**）；当前靠字号 + 颜色 + 留白代偿，**不做伪加粗** |

### `evals/` 的口径（★ 待定，别把它当「仍在更新」）

`evals/` 现有 82 份读数，**最新一份是 2026-10-16**（`p33-b4-cullprobe.json`）。
2026-10-17 起的读数（DPI 矩阵、`REQ-12`/`REQ-13` 的探针）都落在 `.scratch/`（**gitignored**）。
⇒ 两种口径必居其一，目前**两边都没写**，所以它看起来像停摆：① `evals/` 是**钉住的发布基线**，
`.scratch/` 是工作读数；② `evals/` 就是全部读数，那就该把新读数补进去。**待拍板。**

### 下一步（按依赖顺序，不按诱人程度）

**P3 真 core → MCP host**。`REQ-03`（取数通路）已于 2026-10-24 闭合，它原本排第一个的理由不是「账本还剩一条」，而是**真 agent 突发下第一个会屈服的环节**——现在那一步已经换成「推」，且回归门里有 live A/B 守着。

真 core 与 MCP host 的关系是单向的：**没有 core，host 没有可授予的对象**。

★ **P3（真 agent core）是主线，不是可选收尾**——见 `docs/positioning.md` §2 / §6。

## 许可证

[MIT](LICENSE) © 2026 momoya-zh
