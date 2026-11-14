# 发布包 v0.1.0（预编译 Windows x64）

> **这一份是给「不想先装 MSVC 和 ECDI」的人看的。**
> 仓库里最有说服力的部分——工具循环、ask 权限门、子代理、后台作业、MCP host——
> 都藏在一个需要自建 ECDI 安装前缀才能编译的 GUI 后面。发布包把**已经编好**的
> 三个 exe 与最少的夹具打成一个目录，解压即跑。
>
> 状态：**草案（2026-11-10）**。打包由 `tools/make-dist.py` 完成；**上传是用户自己的动作**，
> 本文只给命令，不代跑。

## 包里有什么，为什么是这些

| 文件 | 为什么在这个包里 |
|---|---|
| `harness_gui.exe` | 桌面客户端本体。**/MD 链接 ⇒ 需要 VC++ 2015-2022 Redistributable（x64）**，干净机器缺了会闪退且无提示 |
| `ecdi-core.exe` | 真 agent core（Go，静态，无运行库依赖）。要 `--base-url/--model/--key` 才能跑模型 |
| `fake-core.exe` | 脚本化假 core（Go，静态）。**不调模型**，专给回放与回归用——「不花钱也能看到完整流程」靠它 |
| `fixtures/demo-session.jsonl` | 正常路径的一场完整会话（43 条消息） |
| `fixtures/parallel-two-agents.jsonl` | 子代理并行：两条 `task`、交错 delta、各自收尾 |
| `fixtures/background-task.jsonl` | 后台作业：`job start/output/end` + `job_output`/`job_kill` |
| `fixtures/write-edit-run-asks.jsonl` | ask 提案卡：write/edit 的 unified diff、run 的完整作用域 |
| `fixtures/status.jsonl`（mcp） | MCP 启动状态：2 个正常服务 + 1 个失败 + 1 个禁用 |
| `fixtures/blocks-session.jsonl` | 富内容：heading / fence / list / quote / hr 齐全 |
| `RUN.txt` | 三行上手命令 + 注意事项（给下载者，不是给开发者） |
| `SHA256SUMS.txt` | 逐文件校验和 |

★ **刻意不进包**：`scale-transcript-2000.jsonl`（921 KB，规模基线，只有跑性能读数的人要）、
`realistic-session.jsonl`（真实形态基线，同上）、`PROTOCOL.md`（契约随源码走，发布包不复制一份
会过期的副本）、任何 `evals/` 读数（那是评估资产，不是运行物料）。

## 打这个包（仓库根目录，CMD）

```bat
python tools\make-dist.py --check-only
python tools\make-dist.py --version 0.1.0
```

第一条只体检：三个 exe 在不在、夹具齐不齐、GUI 动态依赖了哪些运行库。缺什么它会**直接说**
该跑哪条构建命令，不会打半成品。

第二条落 `dist\ecdi-harness-0.1.0-win64\` 与同名 `.zip`，并打印**包的** sha256。
zip 内时间戳固定 ⇒ 同样的输入打出同样的包，便于复核。

前置（第一次跑才需要）：

```bat
cmake -S gui -B gui\build -G Ninja -DCMAKE_BUILD_TYPE=Release -DECDI_PREFIX=third_party\ecdi
cmake --build gui\build
cd core && go build -o bin\ecdi-core.exe .\cmd\ecdi-core && go build -o bin\fake-core.exe . && cd ..
```

## 上传前必须自己验一遍

```bat
cd dist
powershell -c "Expand-Archive ecdi-harness-0.1.0-win64.zip -DestinationPath . -Force"
cd ecdi-harness-0.1.0-win64
harness_gui.exe --replay fixtures\demo-session.jsonl --instant --auto-close 8000
echo 退出码 %ERRORLEVEL%
```

期望：窗口出现、转录按夹具长满、8 秒后自己退出、退出码 `0`。
★ **`--auto-close` 不能省**：不带它 app 不会自己退，跑批会挂在那里等人点叉。
★ 这一步是**人工验收**，不是自动化判据——`regress.py` 跑的是源码树的构建产物，不是这个 zip。

## 上传（**对外动作，需要你显式执行**）

```bat
gh release create v0.1.0 dist\ecdi-harness-0.1.0-win64.zip ^
  --title "v0.1.0 - agent core + desktop client, prebuilt" ^
  --notes-file docs\release-v0.1.0.md
```

★ 本仓库的纪律：**commit ≠ push**，**打包 ≠ 发布**。这里的命令我不代跑，也不替你按回车。

## 诚实的边界（发布说明里要原样保留）

1. **需要 VC++ 2015-2022 Redistributable（x64）**。缺了闪退且无提示——这是 `/MD` 链接的
   必然结果（ECDI 安装前缀只提供 `/MD`，混用 `/MT` 会跨 CRT 分配堆内存，比要求装运行库更糟）。
2. **不带 key 只能看回放和假 core**，看不到真模型。真 core 的完整工具循环要有 OpenAI 兼容端点。
   （「不需要 key 也能跑一遍 agent 循环」这条路还没做，见 README §还 不能做什么。）
3. **没有 GIF / 截图**，这一版刻意留空：演示物料要求真跑 + 录屏，还没做。README 里对应的
   那一格是明确的待办，没有拿占位图凑数。
4. **没有代码签名**。Windows SmartScreen 会拦一次「更多信息 → 仍要运行」。
5. 包里**没有** `PROTOCOL.md`；协议与不变量以源码仓为准，避免二手副本过期。
