# ECDI_fake_harness

> **一个能真用的 agent GUI，外加对 ECDI 的评估数据。** 本项目是 [ECDI](https://github.com/momoya-zh/ECDI)
> 的**外部消费者**：GUI 是**协议的渲染器**，对着手写/录制的 JSONL 就能跑完整个界面
> （**不需要**真模型），也能接上**真 agent** 干活。
>
> ★ **两个目的都是第一等的**：① 对 ECDI 的**评估数据 + 能力需求账本**；
> ② **一个真正能用的 agent GUI**。
> ② 不是 ① 的副产品——**只做展示的话，本项目就该待在 ECDI 的 `examples/` 里，不该单独成仓**。
> 详见 [`docs/positioning.md`](docs/positioning.md)（定位与信任模型）。

---

## 这是什么 / 不是什么

| 是 | 不是 |
|---|---|
| ECDI 的**外部消费者**：`find_package(ECDI CONFIG REQUIRED)` + `ECDI::ECDI` | 不是 ECDI 的一部分，不随框架一起构建 |
| **协议先于两侧实现**：`protocol/` 是唯一契约，GUI 只看它就能跑 | 不是「先写实现、事后补协议」 |
| **假 core**（Go，零第三方）：真子进程 + 双向管道，但不调模型 | 不是真 agent；不接任何 LLM |
| **评估数据**的产线：`evals/` 下机器可读读数 | 不是「为展示而存在」——**展示品不该单独成仓** |
| **一个能真用的 agent GUI**：接上真 agent 后能拿它干活 | 不是**商业产品**（无 roadmap / 无支持承诺 / 无工期） |

★ **铁律**：**任何对 ECDI 框架的修改都不在本仓库做**——这里只登记需求、只给证据、只判归属。
判「ECDI 缺能力」之前必须先查**基类公共头 + ECDI 源码仓**，且归属结论须有**只改一个自变量的
受控对照**支撑。（这条纪律是被代价换来的：`REQ-01`/`REQ-03`/`REQ-04` 三条都曾误判为 ECDI 缺陷，
复核后全部反转为 harness 侧。）

## 仓库结构

| 目录 | 职责 |
|---|---|
| `protocol/` | **唯一契约**。`PROTOCOL.md`（**v2**）+ `fixtures/`（6 份夹具）+ `validate.py`（**可执行规格**） |
| `gui/` | 独立 CMake 工程 `harness_gui`——外部消费 ECDI。24 个源文件 / ~4.5k 行 |
| `core/` | **假 core**（Go，零第三方）：`main.go` / `script.go` / `session.go`，3 文件 / ~0.6k 行 |
| `evals/` | 机器可读评估读数（JSON）——评估产出的落点 |
| `docs/` | **定位与信任模型**（`positioning.md`）· P1 / P1.5 规格 · **能力需求账本** · GUI 原型（单文件 HTML） |

## 协议要点

- **分帧**：一行一个 JSON，`\n` 结尾；EOF 截断的最后一行**必须报错**，不得静默丢弃。
- **信封**：`{t, seq, ts}`；`seq` 每方向独立、从 0 起、严格递增。
- **不变量 I1–I8**。★ **I4（`seq` 缺口必须显式提示「消息丢失」）是本项目的核心评估钩子**——
  它对应 `Application::PostToUi` 的乱序/丢失语义，是「靠它才看得见」的那一类失败。
  ★ **I8**（v2 新增）：未知 `kind` 必须**中性渲染 + 显式提示**，不得伪装成有语义的形态。
- **`kind` 七个值**（v2）：`user` / `assistant` / **`reasoning`** / `tool_call` / `tool_result` / `system` / `error`。
- **转录的唯一作者是 core**：GUI 不产生转录块，用户输入由 core 回显为 `kind:"user"`。
- **回放两模式**：`instant`（一次性灌入，测稳态渲染）/ `timed`（按 `ts` 真实间隔，测流式刷新）。
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

常用开关：`--cull-probe`（命令分类 / 帧时 / 测量成本）· `--state-probe`（回读按钮使能）·
`--hit-probe`（命中门控受控对照，REQ-04 边界）· `--reasoning-probe`（思维链折叠行为取证）·
`--probe-contrast`（探针专用白底黑字）· `--row-mode autosize|listlayout`（布局 A/B 对照）。
★ 三个 `*-probe` **默认关闭**（既有路径逐位不变）。

## 交付物在哪

| 想要什么 | 去哪看 |
|---|---|
| ★ **定位与信任模型** | `docs/positioning.md`（**两个目的** · 协议不是安全边界 · MCP 决策） |
| **ECDI 能力需求 / 缺陷登记** | `docs/ecdi-capability-requests.md`（**唯一账本**） |
| **评估读数**（机器可读 JSON） | `evals/` |
| **协议契约** | `protocol/PROTOCOL.md`（v2） |
| 阶段规格 | `docs/P1-gui-implementation-spec.md` · `docs/P1.5-child-process-spec.md` |
| GUI 设计原型 | `docs/prototype/`（单文件 HTML） |

## 能力账本现状

| 条目 | 内容 | 状态 |
|---|---|---|
| `REQ-01` | 含 `\n` 的短文本丢换行 | ✅ 闭合（**归属反转**：harness 侧 `needWrap` 只看宽度） |
| `REQ-02` | `tool_call` 的 `summary` 被吞 | ✅ 闭合（harness 侧） |
| `REQ-03` | `PostToUi` 能力已具备，harness 从未走异步投递路径 | 📋 已登记（P2 立项理由） |
| `REQ-04` | 按钮使能缺 API | ✅ 闭合（**归属反转**）；**边界**（禁用 ⇒ 点击不达）亦已取证 |
| `REQ-05` | GDI / GL 的 `LineHeight` 不一致（14px vs 16px） | ✅ 闭合（**真 ECDI 缺陷**，Phase 31 已修）——账本**首条走完「登记 → 修复 → 跨仓库验证 → 回写」闭环**的需求 |
| `REQ-06` | 跨后端基线偏移（原登记「拉丁 +1 · CJK +2」） | ✅ **已裁决结案（2026-10-17，③ 记为已知现状）**；★ 读数订正：112 格实测 Δ∈[−13,+3] 且符号翻转，**+1/+2 只是 SimSun@14 单点** |
| `REQ-07` | （候选）折叠面板仍测量隐藏内容 | ❌ **复核否决**（ECDI 的 Layout 已跳过不可见子节点）——归属 harness 侧 |
| `REQ-08` | 环形进度条需**圆弧原语**（`DrawArc` 不存在） | ✅ **闭合（2026-10-16）**：ECDI **Phase 33** 落地 `DrawArc` + `RingProgressBar`，本项目作为**首个真实消费者**兑现（右栏上下文环，`commandMix.arc > 0` 机读证据） |
| `REQ-09` | `Font::family` **双链语义矛盾**（GDI=族名 / GL=文件名） | ✅ **已闭合（2026-10-17）**：ECDI `Win32FontSource` 补族名反查（禁子串匹配、文件名直查保留且优先）；验收 8 族名 **8/8 两链同 face**，`ecdi_tests` **465/465**。⇒ 我方 `kMonoFamily="Consolas"` **无需改动即恢复等宽** |

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
（未知 `kind` 必须中性渲染并显式提示）。渲染形态（默认折叠？显 token 数？）
**先用 `protocol/fixtures/cot/` 取读数再定**。

## 夹具

| 文件 | 用途 |
|---|---|
| `fixtures/demo-session.jsonl` | 正常路径：完整一场会话（43 条消息） |
| `fixtures/hostile/seq-gap.jsonl` | **I4 负向**：`seq` 缺口 + 未知 `t` + 悬空 `block_end` + 迟到 `delta` |
| `fixtures/hostile/truncated-final-line.jsonl` | **§1.2 负向**：最后一行被截断 |
| `fixtures/realistic/realistic-session.jsonl` | 真实编码会话形态（Markdown / 长日志 / 1213 条消息） |
| `fixtures/scale/scale-transcript-2000.jsonl` | 规模基线（2000 块 / 8906 条消息 / 916 KB） |
| `fixtures/cot/cot-session.jsonl` | **思维链**：长 + 流式 + **默认折叠**（1044 条消息 / 44 块）。★ `hello` 带 `context`、思维链 `block_end` 带 `tokens` |

三个生成器都是**确定性**的（同参 ⇒ 逐字节相同），所以 2000 / 5000 / 20000 是**旋钮**，不是砖：
`gen_scale.py` · `gen_realistic.py` · `gen_cot.py`（`--turns --thinking-chars --kind --context --tokens`）。

## 编码约定

| 文件类型 | 编码 | 行尾 |
|---|---|---|
| 文本（`.md` / `.cpp` / `.h` / `.go` / `.py`） | UTF-8 **带 BOM** | CRLF |
| **`*.jsonl`** | UTF-8 **不带 BOM** | **LF** |
| 配置文件（`.gitignore` / `CMakeLists.txt` / `go.mod` / `*.json`） | UTF-8 不带 BOM | 随该工具预期 |

★ `.jsonl` 的 **LF 是硬要求**（分帧靠 `\n`，CRLF 会污染每行尾字符），已由 `.gitattributes` 钉死。
BOM 不是风格问题：缺 BOM 且含中文 ⇒ MSVC 按 CP936 读 ⇒ `C4819` + 注释乱码。

## 当前状态

- **P1（GUI 骨架）与 P1.5（子进程真路径）均已收口**；ECDI 库指纹 `a1f843f`（MSVC Release）。
- ECDI 自带套件 Debug 与 Release **各 445/445**；回放冒烟 `seqGaps 0 / violations 0`；
  I4 负向注入**每处 `seq` 缺口都产出显式提示**；无僵尸进程。
- **协议 v2 已落地**（思维链一等公民 + I8 + `hello.context` + `block_end.tokens`）。
- ★ **P3（真 agent core）是主线，不是可选收尾**——见 `docs/positioning.md` §2 / §6。
- `status.txt` 是逐轮进度摘要（机器向）；本 README 是人向入口。

## 许可证

[MIT](LICENSE) © 2026 momoya-zh
