"""树结构顺序校验：证明「把一个建树大函数拆成若干 builder」没有改变执行期行为。

为什么需要这个工具
------------------
门 2（split_verify）只能证明"函数体没变"。一旦要动函数体（比如把 980 行的
BuildMainArea 按分节注释切成 7 个 builder），门 2 就失效了——它会把整段报成
"变了"，而那正是预期结果。

这时真正要守的契约是两条：
  ① **树结构**：`AddChild` / `RemoveChild` 的**执行顺序**不变（顺序 = 父子关系 = 命中测试 = 绘制层序）；
  ② **句柄写入**：`m_hud.xxx = ptr.get()` 必须在对应 `AddChild` **之前**（AddChild 转移所有权，
     先 AddChild 后取裸指针 = 悬空指针）。

做法：把新文件里对"同文件内成员函数"的调用**在调用点原地展开**，重建出执行顺序，
再与拆前的线性函数逐行比对。文件里的书写顺序无所谓——只有执行顺序算数。

用法
----
    python tools/build_order_check.py --before <拆前快照.cpp> --after <拆后文件.cpp> --entry BuildMainArea

    # 快照怎么来：动手前 `cp gui/src/AppUiMain.cpp .scratch/AppUiMain.before.cpp`

退出码：0 = 一致；1 = 有差异（会打印 unified diff）。
"""
import argparse
import difflib
import pathlib
import re
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import split_verify as sv

# 只关心"结构性"调用；SetLayout 不在此列（它不改父子关系，且会在 builder 里重复出现）
TREE_CALL = re.compile(r'\b(AddChild|RemoveChild)\s*\(')
# 句柄写入：m_hud.foo = ...get()  /  m_xxx = ...get()
HANDLE = re.compile(r'^\s*(m_hud\.\w+|m_\w+)\s*=\s*.*\.get\(\)')
# 同文件内的成员函数调用（HarnessApp::X 的定义在本文件里）
MEMBER_CALL = re.compile(r'\b([A-Z][A-Za-z0-9_]*)\s*\(')


def load(path):
    p = pathlib.Path(path)
    funcs = {}
    for name, s, e, _t in sv.parse_functions(p):
        if '::' in name:
            funcs.setdefault(name.split('::')[-1], (s, e))
    return p.read_text(encoding='utf-8').split('\n'), funcs


def normalize(line):
    """抹平「纯搬运」必然产生的差异，只保留语义内容。"""
    s = line.strip()
    # 局部句柄 → 成员句柄（同一对象，换个名字）
    s = s.replace('m_counting->', 'counting->')
    s = s.replace('main.AddChild', 'main->AddChild')
    # 参数改名（settingsSheetOwned 的暂存改成函数返回值）
    s = s.replace('settingsSheetOwned', '<SHEET>').replace('settingsSheet', '<SHEET>')
    s = re.sub(r'\s+', ' ', s)
    return s


def expand(lines, funcs, name, depth=0, seen=()):
    """把 name 的函数体展开成"执行顺序"的语句序列（同文件成员调用原地内联）。"""
    if depth > 8 or name in seen:
        sys.exit(f'!! 展开深度异常或自递归：{name}')
    s, e = funcs[name]
    out = []
    for line in lines[s:e + 1]:
        m = MEMBER_CALL.search(line)
        callee = m.group(1) if m else None
        if callee and callee != name and callee in funcs and not line.lstrip().startswith('//'):
            out.extend(expand(lines, funcs, callee, depth + 1, seen + (name,)))
        else:
            out.append(line)
    return out


def extract(seq_lines):
    tree, handles = [], []
    for line in seq_lines:
        if TREE_CALL.search(line):
            tree.append(normalize(line))
        elif HANDLE.match(line):
            handles.append(normalize(line))
    return tree, handles


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--before', required=True, help='拆前的源文件快照')
    ap.add_argument('--after', required=True, help='拆后的源文件')
    ap.add_argument('--entry', default='BuildMainArea', help='入口函数名（拆前它是完整的大函数）')
    args = ap.parse_args()

    b_lines, b_funcs = load(args.before)
    a_lines, a_funcs = load(args.after)
    if args.entry not in b_funcs:
        sys.exit(f'!! 拆前快照里找不到 {args.entry}')
    if args.entry not in a_funcs:
        sys.exit(f'!! 拆后文件里找不到 {args.entry}')

    bs, be = b_funcs[args.entry]
    tree_b, handle_b = extract(b_lines[bs:be + 1])
    tree_a, handle_a = extract(expand(a_lines, a_funcs, args.entry))

    ok = True
    for label, before, after in (('树结构（AddChild/RemoveChild）', tree_b, tree_a),
                                 ('句柄写入（m_hud.* = ....get()）', handle_b, handle_a)):
        if before == after:
            print(f'  {label:34s} 一致（{len(before)} 条，逐行相同）')
        else:
            ok = False
            print(f'  {label:34s} DIFF（{len(before)} → {len(after)}）')
            for l in difflib.unified_diff(before, after, lineterm='', n=3):
                print('     ', l)

    print('RESULT:', 'PASS' if ok else 'FAIL')
    return 0 if ok else 1


if __name__ == '__main__':
    sys.exit(main())
