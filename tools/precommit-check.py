# ============================================================
# commit 前最后核对（全部机读，不靠目视）
# ------------------------------------------------------------
# 1. 每个改动文件的编码：**以 HEAD 为基线**，只查漂移，不查"应该带 BOM"
# 2. 反查是否误把不该入库的东西 stage（.scratch/ / .workbuddy/ / 临时锁等）
# 3. 协议与夹具校验（protocol/validate.py）
# 4. 复算构建产物时间戳，确认产物状态
#
# ★ 2026-10-22 订正：
#   protocol/PROTOCOL.md §1.1 明确写着：仓库里 37 个文件带 BOM、102 个不带，
#   这个分布与扩展名无关，规则是「别动它」。
#   判据：BOM 的有无必须与 HEAD 一致；只有「相对 HEAD 漂移」是缺陷。
#   新文件按无 BOM 建议（*.jsonl 严禁 BOM）。
# ============================================================
import io
import os
import pathlib
import subprocess
import sys

sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding='utf-8')
BOM = b'\xef\xbb\xbf'

# 只有这一类有协议依据的硬要求：*.jsonl 必须无 BOM + LF（PROTOCOL §1.2 分帧）
HARD_NO_BOM = ('.jsonl',)


def get_git_root():
    try:
        r = subprocess.run(['git', 'rev-parse', '--show-toplevel'],
                           capture_output=True, text=True, check=True)
        return pathlib.Path(r.stdout.strip())
    except Exception:
        return pathlib.Path(__file__).resolve().parent.parent


def head_bytes(rel, root):
    """HEAD's stored bytes for a path, or None when the file is new/untracked."""
    r = subprocess.run(['git', 'show', 'HEAD:' + rel],
                       capture_output=True, cwd=root)
    return r.stdout if r.returncode == 0 else None


def main():
    root = get_git_root()
    out = subprocess.run(['git', 'status', '--porcelain'],
                         capture_output=True, text=True, cwd=root).stdout
    paths = []
    for line in out.splitlines():
        if len(line) < 4:
            continue
        p = line[3:].strip().strip('"')
        if line[:2].strip() in ('D', 'R'):
            continue          # 删除/重命名不查内容编码
        if p.endswith('/'):
            continue
        paths.append((line[:2].strip(), p))

    print('=== 1. 编码核对（基线 = HEAD，不按扩展名猜）===')
    drift = 0
    for status, f in paths:
        full_path = root / f
        try:
            b = open(full_path, 'rb').read()
        except OSError as exc:
            print('%-52s <unreadable: %s>' % (f, exc))
            continue
        bom = b.startswith(BOM)
        crlf = b.count(b'\r\n')
        lf = b.count(b'\n')

        problems = []
        h = head_bytes(f, root)
        if h is None:
            # 新文件：没有基线可对齐，只在硬规则上判
            if f.endswith(HARD_NO_BOM) and bom:
                problems.append('jsonl-must-not-have-BOM')
        else:
            head_bom = h.startswith(BOM)
            if bom != head_bom:
                problems.append('BOM drift (HEAD=%s worktree=%s)' % (head_bom, bom))

        # 行尾：不算缺陷，只作读数。
        note = ''
        if crlf and not f.endswith(HARD_NO_BOM):
            note = '  (worktree CRLF=%d, index stores LF -- harmless)' % crlf

        print('%-52s %-9s BOM=%-5s CRLF=%-4d LF=%-5d %s%s'
              % (f, 'new' if h is None else 'tracked', bom, crlf, lf,
                 '  <<< ' + ', '.join(problems) if problems else 'ok', note))
        if problems:
            drift += 1

    print('\n  漂移文件数 = %d' % drift)

    print('\n=== 2. 不应入库的路径与锁文件 ===')
    bad = [p for _, p in paths
           if p.startswith(('.scratch/', '.workbuddy/', 'gui/build/', 'third_party/', '.dsh-acl-recovery/'))]
    for p in bad:
        print('  !! forbidden dir in git status: ', p)

    locks_in_paths = [p for _, p in paths if p.rsplit('/', 1)[-1].startswith('.dsh-ops-update-')]
    for p in locks_in_paths:
        print('  !! lock file in git status: ', p)

    # 查磁盘源码目录下是否有遗留锁文件（即使被 .gitignore 忽略，也不应堆在源码旁）
    disk_locks = [str(p.relative_to(root)).replace('\\', '/')
                  for p in root.glob('gui/**/.dsh-ops-update-*.lock')]
    for p in disk_locks:
        if p not in locks_in_paths:
            print('  !! leftover lock file on disk: ', p)

    bad = bad + locks_in_paths + disk_locks
    if not bad:
        print('  (none)')

    print('\n=== 3. 协议与夹具校验 (protocol/validate.py) ===')
    validate_script = root / 'protocol' / 'validate.py'
    val_ret = subprocess.run([sys.executable, str(validate_script)],
                             cwd=root, capture_output=True, text=True)
    if val_ret.returncode != 0:
        print('  !! validate.py failed with exit code', val_ret.returncode)
        print(val_ret.stdout)
        print(val_ret.stderr)
    else:
        print('  protocol/validate.py exit=0 (OK)')

    print('\n=== 4. 构建产物状态 ===')
    import datetime
    # 生成器决定产物落点：Ninja = gui/build/ 根；Visual Studio = gui/build/Release/。
    # 两者都报（2026-10-27 构建器迁 Ninja，旧路径保留兜底）。
    for rel_bin in ['gui/build/harness_gui.exe', 'gui/build/Release/harness_gui.exe']:
        full_bin = root / rel_bin
        try:
            st = full_bin.stat()
            print('%-40s %d B  %s'
                  % (rel_bin, st.st_size,
                     datetime.datetime.fromtimestamp(st.st_mtime).isoformat(timespec='seconds')))
        except OSError:
            print('%-40s MISSING' % rel_bin)

    failed = (drift > 0) or (len(bad) > 0) or (val_ret.returncode != 0)
    return 1 if failed else 0


if __name__ == '__main__':
    sys.exit(main())
