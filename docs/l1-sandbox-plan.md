# run_command L1 沙盒隔离方案与落地记录

日期：2026-11-05。前批：`docs/ask-proposal-plan.md`（ask 提案卡与 unified diff 预览）。
**2026-11-05 复核修订**：修掉静默 fail-open、Start→Assign 竞态、提案卡 note 被裁切、
测试对 Git Bash PATH 的依赖；下面所有读数已在本机重跑。

## 用户需求与背景

在完成「① ask 门审阅质量升级（提案卡）」后，进入可用性差距评估的第二项：
**② `run_command` 的 OS 级 L1 隔离（Windows Job Object + Restricted Token）**。

**现状痛点**：
- `tools.go` 的历史注释明确指出了此前局限：`exec.CommandContext` 超时只能杀死直接子进程
  （cmd.exe），派生的孙进程（如后台脚本、编译构建子进程）可能残留变成僵尸进程；
- 死循环或失控命令（如 fork 炸弹、超大内存申请）可能打崩宿主机；
- 管理员权限下启动时，子进程直接继承高危特权（如调试特权 `SeDebugPrivilege`）；
- 子进程可以操作宿主剪切板或系统窗口。

## 决策与取舍

1. **L1 定位（收敛爆炸半径）**：
   - **Job Object**：解决生命周期与资源底线（`KILL_ON_JOB_CLOSE` 强制整树清退，内存硬上限
     1024MB，进程并发上限 32，降低 CPU 优先级，UI 剪切板截断）；
   - **Restricted Token**：剥离管理特权，使命令以受限普通用户视角运行；
   - 零第三方依赖：只使用 Go 标准库 `syscall` 与 `unsafe` 动态加载 Win32 API。
2. **fail-closed（复核新增，关键）**：
   - 受限令牌 / 作业创建 / 作业挂接任何一步失败 ⇒ **命令不执行**，返回明确错误；
   - 旧版把 `createSandboxedToken` 的错误吞掉、把 `AssignProcessToJobObject` 的返回值丢弃，
     一旦失败就是"以宿主完整权限裸跑，提案卡却写着已保护"——这是诚信问题，不是容错。
3. **先挂起、挂作业、再恢复（复核新增）**：
   - 作业只自动纳管**挂接之后**派生的子进程；`Start` 到 `Assign` 之间是一个逃逸窗口；
   - 改为 `CREATE_SUSPENDED` 启动 → `AssignProcessToJobObject` → `NtResumeProcess`，
     失败即杀进程返回错误（Go 不暴露主线程句柄，故用 ntdll 的 `NtResumeProcess`）。
4. **提案卡 note 两行（复核新增）**：
   - 提案行是定高 mono Label、`SetWordWrap(false)`、行宽约 688px；
   - 单行 1024MB 默认说明约 1000px，会被裁掉右 1/3（用户看不到"特权/密钥/超时"那半句）；
   - core 按 `\n` 发两行，GUI 逐行建行并计入 `kvLines`。
5. **诚实边界（不假装解决文件/网络沙盒）**：
   - 轻量 L1 沙盒不透明拦截文件系统与网络（AppContainer 会破坏 git/go/python 等开发工具的正常运行）；
   - 残余风险由 **ask 提案卡**在审批时显式呈现，交由人在环把关；
   - 非 Windows 只做进程组退守（整组 `SIGKILL`），**没有**内存/进程硬上限，note 不冒充 Job Object；
   - `positioning.md` §4.3 同步更新为分层隔离模型。

## 落地记录（2026-11-05，含复核修订）

| 批 | 范围 | 状态 |
|---|---|---|
| core | `sandbox.go`（配置 + 两行 note + 平台能力分流）、`sandbox_windows.go`（Job Object、受限令牌、挂起→挂接→恢复、超时整树强退、Wait 兜底）、`sandbox_other.go`（进程组退守、输出捕获、诚实 note） | ✅ 本批 |
| tools | `tools.go:toolRunCommand` 接入沙盒执行器，移除旧版裸跑逻辑；`diff.go:runCommandDetail` 接入动态沙盒 note | ✅ 本批 |
| flags | `main.go` 接入 `--sandbox`、`--sandbox-memory-mb`、`--sandbox-max-procs`、`--sandbox-drop-privs` 四项启动参数 | ✅ 本批 |
| 单测 | `sandbox_test.go` 5 案（note 两行/平台诚实、普通命令、禁用回退、特权剥离、超时整树）+ `sandbox_windows_test.go` 2 案（后台孙进程随作业清理、内存上限拒绝超额分配）⇒ **7/7 PASS** | ✅ 本批 |
| 契约与文档 | `positioning.md` §4.3 分层隔离契约；本文件；README 参数与声明；P3 规格 §4.1 旧声明订正 | ✅ 本批 |
| 提案卡 | `AppTranscript.cpp` note 按行渲染、`kvLines` 逐行计数；fixture note 同步为两行 | ✅ 本批 |

## 验证读数（2026-11-05 复核，本机实测）

- **单测**：`go test ./cmd/ecdi-core/ -run TestSandbox -v` ⇒ **7/7 PASS**（含新增两案）。
- **特权剥离**：同一 `%SystemRoot%\System32\whoami.exe /priv`，沙盒外 6 项、沙盒内 1 项
  （仅 `SeChangeNotifyPrivilege`）。
- **整树强杀**：超时 0.5s 的 `powershell Start-Sleep` 在 0.50s 内退出；`Process.Start` /
  `Start-Process` / `start /b` 派生的孙进程在作业终止后全部退出（`GetExitCodeProcess` 判活）。
- **命令退出后的后台进程**：PowerShell 启动 `ping -n 30` 后立即退出，工具返回时作业句柄关闭，
  孙进程随 `KILL_ON_JOB_CLOSE` 被杀（新单测守住）。
- **内存上限**：256MB 作业里 `New-Object byte[] 512MB` ⇒ `OutOfMemoryException`（新单测守住）。
- **进程上限**：cap=4 时连发 10 个 `Start-Process` 只成功 4 个（复核用临时探针，未进单测）。
- **真实负载**：默认配置下沙盒内 `go build ./...` 通过；GOCACHE/GOMODCACHE 可读写。
- **提案卡**：`--proposal-probe` 正向 `askRows=3 detailRows=3 diffRows=24 kvRows=5 matched=true`；
  负对照（无 detail）`detailRows=0 kvRows=0`。两行 note 实测宽 **512 / 520px**，可用行宽
  688px（默认窗口）/ ≈608px（最小窗口）⇒ 不裁切。
- **校验**：`go build ./...` / `go vet ./...` 干净；`GOOS=linux`、`GOOS=darwin` 交叉编译通过；
  `protocol/validate.py` 15/15；`tools/precommit-check.py` 编码漂移归零；
  `tools/regress.py` **全量 PASS**（`--valign-probe` 7/7；同批复核顺手修掉它
  `lround` 的奇高假阴性，见 `ProbeFlow.cpp` 2026-11-05 注释）。

## 已声明未做 / 另批

- **L2 物理隔离**（网络/全盘）仍由运行者用容器/VM 保证；本仓库只做到 L1。
- **非 Windows**：只有进程组 + `SIGKILL`，无内存/进程硬上限（note 已如实写）。
- **后台长任务（③）**：`run_command` 仍阻塞一轮对话（默认 60s、上限 300s），
  没有 job_id / 输出回读 / 取消入口——下一项，可复用本批的作业句柄。
- **GUI 设置面板**未暴露 `--sandbox*` 参数；默认开启，自定义需手工 `--core-args`。
- `below-normal` 优先级与 1GB 默认上限会改变既有构建的体感/上限，已在 README 说明；
  大项目若撞上限，用 `--sandbox-memory-mb` 调整。