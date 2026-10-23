#!/usr/bin/env python
# ============================================================
# 安装项目 Git 预提交钩子
# ------------------------------------------------------------
# 将 core.hooksPath 指向仓库内的 tools/hooks 目录，
# 确保团队成员或新工作区在执行 git commit 时自动触发本地校验。
# ============================================================
import pathlib
import subprocess
import sys


def main():
    root = pathlib.Path(__file__).resolve().parent.parent
    hooks_dir = root / 'tools' / 'hooks'

    if not hooks_dir.exists():
        print(f"Error: hooks directory not found at {hooks_dir}")
        return 1

    try:
        subprocess.run(['git', 'config', 'core.hooksPath', 'tools/hooks'],
                       cwd=root, check=True)
        print("Success: git config core.hooksPath set to 'tools/hooks'")
    except subprocess.CalledProcessError as e:
        print(f"Failed to configure core.hooksPath: {e}")
        return 1

    print("\nPre-commit hook is now active.")
    print("Testing precommit-check invocation...")
    r = subprocess.run([sys.executable, str(root / 'tools' / 'precommit-check.py')],
                       cwd=root)
    return r.returncode


if __name__ == '__main__':
    sys.exit(main())
