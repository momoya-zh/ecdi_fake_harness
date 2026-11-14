#!/usr/bin/env python
# ============================================================
# 打包发布产物（预编译 exe + 演示夹具），供 GitHub Release 上传
# ------------------------------------------------------------
# 为什么需要它：本项目最有说服力的东西（工具循环 / ask 门 / 子代理 /
# MCP host）全在一个需要 ECDI 前缀才能编译的 GUI 背后。外部读者没有
# ECDI 就进不来。这里把**已经编译好**的三个 exe 与最少的夹具打成一个
# 自解压即可运行的目录，让人不装 MSVC、不装 ECDI、不花 key 也能先跑起来。
#
# 它不做的事（刻意的）：
#   * 不编译。exe 从既有构建目录取——「能不能编」是另一件事，别混进来。
#   * 不联网、不调 gh。发布是**对外动作**，必须由人执行（见 docs/release-v0.1.0.md）。
#   * 不改仓库里任何已受版本控制的文件。
#
# 用法（仓库根目录，CMD 或 bash 均可）：
#   python tools/make-dist.py                  # 打包
#   python tools/make-dist.py --check-only     # 只体检，不落盘
#   python tools/make-dist.py --version 0.1.0  # 指定版本号（进文件名）
# ============================================================

import argparse
import hashlib
import os
import re
import shutil
import sys
import zipfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# 进包的夹具：每一份都在验收里有明确用途，不是凑数。
FIXTURES = [
    "protocol/fixtures/demo-session.jsonl",
    "protocol/fixtures/subagent/parallel-two-agents.jsonl",
    "protocol/fixtures/jobs/background-task.jsonl",
    "protocol/fixtures/proposal/write-edit-run-asks.jsonl",
    "protocol/fixtures/mcp/status.jsonl",
    "protocol/fixtures/markdown/blocks-session.jsonl",
]

BINARIES = [
    ("harness_gui.exe", ["gui/build/harness_gui.exe",
                         "gui/cmake-build-release-visual-studio/Release/harness_gui.exe",
                         "gui/build/Release/harness_gui.exe"]),
    ("ecdi-core.exe", ["core/bin/ecdi-core.exe"]),
    ("fake-core.exe", ["core/bin/fake-core.exe"]),
]

# 固定 zip 内时间戳 ⇒ 同样的输入打出同样的包（可复核）。
FIXED_TIME = (2026, 11, 10, 0, 0, 0)

RUNTIME_DLLS = re.compile(rb"(VCRUNTIME140(?:_1)?|MSVCP140|MSVCR\d+)\.dll")


def sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def pick(candidates):
    for c in candidates:
        p = os.path.join(ROOT, c.replace("/", os.sep))
        if os.path.isfile(p):
            return p
    return None


def needs_redist(path):
    """GUI 是 /MD 链接的（ECDI 前缀只有 /MD），干净机器上缺 VC++ 运行库就起不来。
    这不是缺陷，但必须在发布说明里说清楚，不能让人下载完点开就闪退还不知道为什么。"""
    with open(path, "rb") as f:
        return sorted(set(m.group(0).decode() for m in RUNTIME_DLLS.finditer(f.read())))


def build_run_txt(version):
    return (
        "ECDI harness %s - Windows x64\r\n"
        "=====================================\r\n"
        "\r\n"
        "这是什么\r\n"
        "  一个 Go 写的 agent runtime（工具循环 / 权限门 / 子代理 / 后台作业 / MCP host）\r\n"
        "  + 一个用 ECDI 画的 Windows 桌面客户端。两者只说一种带版本号的 JSONL 协议。\r\n"
        "\r\n"
        "  harness_gui.exe  桌面客户端（需要 VC++ 2015-2022 运行库）\r\n"
        "  ecdi-core.exe    真 agent core（要 API key 才跑得动模型）\r\n"
        "  fake-core.exe    脚本化假 core（不调模型，专给回放/回归用）\r\n"
        "  fixtures\\        几份现成的会话流，直接喂给 GUI 就能看\r\n"
        "\r\n"
        "最快看一眼（不需要 key、不需要联网）\r\n"
        "  harness_gui.exe --replay fixtures\\demo-session.jsonl --instant --auto-close 8000\r\n"
        "\r\n"
        "带一个假 core 走真子进程\r\n"
        "  harness_gui.exe --core fake-core.exe --core-args --speed=0.2 --auto-prompt \"hello\"\r\n"
        "\r\n"
        "上真模型（需要一个 OpenAI 兼容端点）\r\n"
        "  set ECDI_CORE_KEY=sk-...\r\n"
        "  harness_gui.exe --core ecdi-core.exe --core-args \"--base-url=https://your-endpoint/v1 --model=your-model\"\r\n"
        "  不给 key 也行：双击 harness_gui.exe，首启会弹设置面板让你填。\r\n"
        "\r\n"
        "注意\r\n"
        "  * 这个 exe 是 /MD 链接的：干净机器上需要 Microsoft Visual C++ 2015-2022\r\n"
        "    Redistributable（x64）。缺了会闪退且没有提示。\r\n"
        "  * 双击启动 = 用户模式；带 --replay / --core / 任一探针 = 开发者模式。\r\n"
        "  * 没有任何模型凭据是本仓库的硬约束：配置文件里放不下 key，key 只进\r\n"
        "    Windows 凭据管理器，或经启动环境变量 ECDI_CORE_KEY 注入 core。\r\n"
        "\r\n"
        "协议与判据在源码仓：protocol\\PROTOCOL.md（v6，I1-I14）。\r\n"
        % version
    )


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--version", default="0.1.0")
    ap.add_argument("--out", default="dist")
    ap.add_argument("--check-only", action="store_true")
    args = ap.parse_args()

    print("== 体检（缺什么就说什么，不猜）==")
    resolved = {}
    missing = []
    for name, cands in BINARIES:
        p = pick(cands)
        if p:
            resolved[name] = p
            print("  OK   %-15s %s  (%.2f MB)" % (name, os.path.relpath(p, ROOT), os.path.getsize(p) / 1e6))
        else:
            missing.append((name, cands[0]))
            print("  MISS %-15s 期望在 %s" % (name, cands[0]))

    absent_fixtures = [f for f in FIXTURES if not os.path.isfile(os.path.join(ROOT, f.replace("/", os.sep)))]
    for f in absent_fixtures:
        print("  MISS fixture        %s" % f)

    if missing or absent_fixtures:
        print("\n缺件，不打包。构建命令（在仓库根目录、已进入 MSVC 环境时）：")
        for name, where in missing:
            if name == "harness_gui.exe":
                print("  cmake -S gui -B gui/build -G Ninja -DCMAKE_BUILD_TYPE=Release -DECDI_PREFIX=third_party/ecdi")
                print("  cmake --build gui/build")
            elif name == "ecdi-core.exe":
                print("  cd core && go build -o bin/ecdi-core.exe ./cmd/ecdi-core")
            elif name == "fake-core.exe":
                print("  cd core && go build -o bin/fake-core.exe .")
        return 2

    gui = resolved["harness_gui.exe"]
    dlls = needs_redist(gui)
    if dlls:
        print("\n  注意 %s 动态依赖 %s —— 发布说明里必须写「需 VC++ 2015-2022 运行库」。"
              % (os.path.basename(gui), ", ".join(dlls)))

    if args.check_only:
        print("\n--check-only：体检完成，未落盘。")
        return 0

    pkg = "ecdi-harness-%s-win64" % args.version
    out_dir = os.path.join(ROOT, args.out)
    stage = os.path.join(out_dir, pkg)
    zpath = os.path.join(out_dir, pkg + ".zip")
    if os.path.isdir(stage):
        shutil.rmtree(stage)
    os.makedirs(os.path.join(stage, "fixtures"), exist_ok=True)

    files = []
    for name, _ in BINARIES:
        dst = os.path.join(stage, name)
        shutil.copy2(resolved[name], dst)
        files.append((name, dst))
    for f in FIXTURES:
        src = os.path.join(ROOT, f.replace("/", os.sep))
        dst = os.path.join(stage, "fixtures", os.path.basename(f))
        shutil.copy2(src, dst)
        files.append((("fixtures/" + os.path.basename(f)), dst))

    run_txt = os.path.join(stage, "RUN.txt")
    with open(run_txt, "w", encoding="utf-8", newline="") as f:
        f.write(build_run_txt(args.version))
    files.append(("RUN.txt", run_txt))

    lic = os.path.join(ROOT, "LICENSE")
    if os.path.isfile(lic):
        shutil.copy2(lic, os.path.join(stage, "LICENSE"))
        files.append(("LICENSE", os.path.join(stage, "LICENSE")))

    sums = []
    for name, path in files:
        sums.append("%s  %s" % (sha256(path), name))
    sums_path = os.path.join(stage, "SHA256SUMS.txt")
    with open(sums_path, "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(sums) + "\n")

    # 打包：条目名带上顶层目录，解压不会撒一地。
    with zipfile.ZipFile(zpath, "w", zipfile.ZIP_DEFLATED) as z:
        for name, path in files + [("SHA256SUMS.txt", sums_path)]:
            info = zipfile.ZipInfo(pkg + "/" + name, date_time=FIXED_TIME)
            info.compress_type = zipfile.ZIP_DEFLATED
            info.external_attr = 0o644 << 16
            with open(path, "rb") as src, z.open(info, "w") as dst:
                shutil.copyfileobj(src, dst)

    print("\n== 产物 ==")
    print("  目录  %s" % os.path.relpath(stage, ROOT))
    print("  压缩  %s  (%.2f MB)" % (os.path.relpath(zpath, ROOT), os.path.getsize(zpath) / 1e6))
    print("  sha256 %s" % sha256(zpath))
    print("\n  上传是**对外动作**，脚本不代劳。命令见 docs/release-v0.1.0.md。")
    print("  上传前先自己解压跑一次：")
    print("    cd %s && %s && %s --replay fixtures\\demo-session.jsonl --instant --auto-close 8000"
          % (args.out, pkg, "harness_gui.exe"))
    return 0


if __name__ == "__main__":
    sys.exit(main())
