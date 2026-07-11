import sys
from pathlib import Path
from collections import defaultdict

def parse_callgrind(file: Path):
    """
    快速解析 Callgrind 输出文件，提取关键的性能数据。
    """
    if not file.exists() or not file.is_file():
        print(f"错误: 文件不存在或不是文件 - {file}")
        return

    # 状态变量
    current_fn_id = None
    current_fn_name = None
    fn_map = {}  # fn_id -> demangled_name

    # 统计数据结构
    func_ir = defaultdict(int)  # 函数 -> 自耗指令数 (Ir)
    call_counts = defaultdict(int)  # 统计每个函数被调用的总次数

    # 调用链临时状态
    callee_id = None
    callee_name = None

    total_ir = 0

    with open(file, "r", encoding="utf-8") as f:
        for line in f:
            line = line.strip()
            if not line:
                continue

            # 1. 提取总指令摘要
            if line.startswith("summary:"):
                total_ir = int(line.split(":")[1].strip())
                continue

            # 2. 提取函数定义 (fn)
            if line.startswith("fn=("):
                # 修复：先找到左括号和右括号，再安全地提取中间的数字
                start_paren = line.find("(")
                end_paren = line.find(")")
                if end_paren != -1:
                    try:
                        # 关键修复：line[start_paren+1:end_paren] 仅截取数字部分
                        fn_id = int(line[start_paren + 1 : end_paren])
                        fn_name = line[end_paren + 1 :].strip()

                        current_fn_id = fn_id
                        # 如果未提供函数名，则用 ID 代替
                        current_fn_name = fn_name if fn_name else f"fn_{fn_id}"
                        fn_map[current_fn_id] = current_fn_name
                        continue
                    except ValueError:
                        # 应对极少数格式极其特殊的行
                        continue

            # 3. 提取被调用函数目标 (cfn)
            if line.startswith("cfn=("):
                start_paren = line.find("(")
                end_paren = line.find(")")
                if end_paren != -1:
                    try:
                        # 关键修复：同样方法提取数字
                        callee_id = int(line[start_paren + 1 : end_paren])
                        callee_name = line[end_paren + 1 :].strip()
                        # 如果当前行没给出名字，去之前缓存的映射表里找
                        if not callee_name:
                            callee_name = fn_map.get(callee_id, f"cfn_{callee_id}")
                        continue
                    except ValueError:
                        continue

            # 4. 提取调用次数与关系 (calls)
            if line.startswith("calls="):
                parts = line.split()
                if len(parts) >= 2 and current_fn_name and callee_name:
                    count = int(parts[0].split("=")[1])
                    call_counts[callee_name] += count
                continue

            # 5. 提取当前函数内执行的指令数 (cost)
            # 格式: 0x... 0 count 或者 +offset 0 count
            parts = line.split()
            if len(parts) == 3 and parts[1] == "0":
                if parts[0].startswith("0x") or parts[0].startswith("+"):
                    cost = int(parts[2])
                    if current_fn_name:
                        func_ir[current_fn_name] += cost

    # ========== 输出总结报告 ==========
    print("\n" + "=" * 80)
    print(f"总执行指令数 (Total Ir): {total_ir:,}")
    print("=" * 80)

    print("\n>>> 1. 按自身执行指令 (Ir) 排序的 Top 30 热点函数")
    sorted_funcs = sorted(func_ir.items(), key=lambda x: x[1], reverse=True)
    for i, (name, ir) in enumerate(sorted_funcs[:30]):
        print(f"{i+1:3}. {ir:>18,}  {name}")

    print("\n>>> 2. 按被调用次数 (Calls) 排序的 Top 30 热点函数")
    sorted_calls = sorted(call_counts.items(), key=lambda x: x[1], reverse=True)
    for i, (name, count) in enumerate(sorted_calls[:30]):
        print(f"{i+1:3}. {count:>18,}  {name}")

    print("\n>>> 3. 自动匹配的异常内存操作 (Reserve / Allocate / Malloc)")
    sus_keywords = ["reserve", "allocate", "malloc", "new", "_M_allocate"]
    found_sus = False
    for name, ir in sorted_funcs:
        if any(k in name.lower() for k in sus_keywords):
            print(f"{ir:>18,}  {name}")
            found_sus = True
    if not found_sus:
        print("未在 Top 热点中检测到明显的大规模内存预留/分配函数。")


if __name__ == "__main__":
    if len(sys.argv) != 2:
        print("用法: python parse_callgrind.py <callgrind_output_file>")
    else:
        parse_callgrind(Path(sys.argv[1]))
