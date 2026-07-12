import sys
from pathlib import Path
from collections import defaultdict

# 事件字段定义（与 Cachegrind 输出顺序一致）
EVENTS = ["Ir", "I1mr", "ILmr", "Dr", "D1mr", "DLmr", "Dw", "D1mw", "DLmw"]


def parse_cachegrind(file: Path, sort_by="Ir", top_n=30):
    """
    快速解析 Cachegrind 输出文件，提取每个函数的所有事件计数。

    参数:
        file: 输入文件路径
        sort_by: 用于排序的事件名（默认 Ir）
        top_n: 显示前 N 个函数
    """
    if not file.exists() or not file.is_file():
        print(f"错误: 文件不存在或不是文件 - {file}")
        return

    # 状态变量
    current_fn = None
    func_events = defaultdict(lambda: {e: 0 for e in EVENTS})  # 函数名 -> 事件计数
    total_events = {e: 0 for e in EVENTS}  # 总计事件

    with open(file, "r", encoding="utf-8") as f:
        for line in f:
            line = line.strip()
            if not line:
                continue

            # 1. 程序命令信息
            if line.startswith("cmd:"):
                print(f"程序命令: {line[4:].strip()}")

            # 2. 提取总计行
            if line.startswith("summary:"):
                parts = line.split()
                # summary: Ir I1mr ILmr Dr D1mr DLmr Dw D1mw DLmw
                for i, e in enumerate(EVENTS):
                    if i + 1 < len(parts):
                        try:
                            total_events[e] = int(parts[i + 1])
                        except ValueError:
                            pass
                continue

            # 3. 记录当前函数名
            if line.startswith("fn="):
                current_fn = line[3:].strip()
                continue

            # 4. 解析事件行（行号开头的行）
            if line and line[0].isdigit():
                parts = line.split()
                # 行号 Ir I1mr ILmr Dr D1mr DLmr Dw D1mw DLmw
                if len(parts) >= 10 and current_fn:
                    try:
                        # 累加所有事件
                        for i, e in enumerate(EVENTS):
                            func_events[current_fn][e] += int(parts[i + 1])
                    except ValueError:
                        continue

    # ========== 输出总结报告 ==========
    print("\n" + "=" * 80)
    print("总事件计数 (Total Events):")
    for e in EVENTS:
        print(f"  {e:>6}: {total_events[e]:>18,}")
    print("=" * 80)

    # 按指定事件排序
    sorted_funcs = sorted(
        func_events.items(), key=lambda x: x[1][sort_by], reverse=True
    )
    top = sorted_funcs[:top_n]

    print(f"\n>>> 按 {sort_by} 排序的 Top {top_n} 热点函数")
    # 表头
    header = f"{'#':>4}  {'Function Name':<60}"
    for e in EVENTS:
        header += f"  {e:>10}"
    print(header)
    print("-" * len(header))

    for idx, (name, events) in enumerate(top, 1):
        # 截断过长函数名以保持对齐
        short_name = name if len(name) <= 58 else name[:55] + "..."
        line = f"{idx:>4}  {short_name:<60}"
        for e in EVENTS:
            line += f"  {events[e]:>10,}"
        print(line)

    # 额外提示：如果有疑似内存分配函数，可以输出
    print("\n>>> 自动匹配的异常内存操作 (Reserve / Allocate / Malloc)")
    sus_keywords = ["reserve", "allocate", "malloc", "new", "_M_allocate"]
    found_sus = False
    for name, events in sorted_funcs:
        if any(k in name.lower() for k in sus_keywords):
            print(f"{events['Ir']:>18,}  {name}")
            found_sus = True
    if not found_sus:
        print("未在 Top 热点中检测到明显的大规模内存预留/分配函数。")


if __name__ == "__main__":
    if len(sys.argv) != 2:
        print("用法: python parse_cachegrind.py <cachegrind_output_file>")
        print("可选: 可通过修改脚本中的 sort_by 参数改变排序事件")
    else:
        parse_cachegrind(Path(sys.argv[1]), sort_by="Ir", top_n=30)
