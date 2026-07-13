import sys
from pathlib import Path
from collections import defaultdict
import argparse

EVENTS = ["Ir", "I1mr", "ILmr", "Dr", "D1mr", "DLmr", "Dw", "D1mw", "DLmw"]


def parse_cachegrind(file: Path, sort_by="Ir", top_n=30, max_mem_lines=20):
    if not file.exists() or not file.is_file():
        print(f"错误: 文件不存在或不是文件 - {file}")
        return

    current_fn = None
    func_events = defaultdict(lambda: {e: 0 for e in EVENTS})
    total_events = {e: 0 for e in EVENTS}

    with open(file, "r", encoding="utf-8") as f:
        for line in f:
            line = line.strip()
            if not line:
                continue

            if line.startswith("cmd:"):
                print(f"程序命令: {line[4:].strip()}")

            if line.startswith("summary:"):
                parts = line.split()
                for i, e in enumerate(EVENTS):
                    if i + 1 < len(parts):
                        try:
                            total_events[e] = int(parts[i + 1])
                        except ValueError:
                            pass
                continue

            if line.startswith("fn="):
                current_fn = line[3:].strip()
                continue

            if line and line[0].isdigit():
                parts = line.split()
                if len(parts) >= 10 and current_fn:
                    try:
                        for i, e in enumerate(EVENTS):
                            func_events[current_fn][e] += int(parts[i + 1])
                    except ValueError:
                        continue

    print("\n" + "=" * 80)
    print("总事件计数 (Total Events):")
    for e in EVENTS:
        print(f"  {e:>6}: {total_events[e]:>18,}")
    print("=" * 80)

    sorted_funcs = sorted(
        func_events.items(), key=lambda x: x[1][sort_by], reverse=True
    )
    top = sorted_funcs[:top_n]

    print(f"\n>>> 按 {sort_by} 排序的 Top {top_n} 热点函数")
    header = f"{'#':>4}  {'Function Name':<60}"
    for e in EVENTS:
        header += f"  {e:>10}"
    print(header)
    print("-" * len(header))

    for idx, (name, events) in enumerate(top, 1):
        short_name = name if len(name) <= 58 else name[:55] + "..."
        line = f"{idx:>4}  {short_name:<60}"
        for e in EVENTS:
            line += f"  {events[e]:>10,}"
        print(line)

    # 内存操作部分，带截断
    sus_keywords = ["reserve", "allocate", "malloc", "new", "_M_allocate"]
    matched = [
        (name, events)
        for name, events in sorted_funcs
        if any(k in name.lower() for k in sus_keywords)
    ]

    print(
        f"\n>>> 自动匹配的异常内存操作 (Reserve / Allocate / Malloc) - 前 {min(max_mem_lines, len(matched))} 条"
    )
    found_sus = False
    for idx, (name, events) in enumerate(matched[:max_mem_lines], 1):
        print(f"{events['Ir']:>18,}  {name}")
        found_sus = True

    if not found_sus:
        print("未在热点中检测到明显的大规模内存预留/分配函数。")
    elif len(matched) > max_mem_lines:
        print(f"... (仅显示前 {max_mem_lines} 条，共 {len(matched)} 条匹配)")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="解析 Cachegrind 输出文件")
    parser.add_argument("file", help="Cachegrind 输出文件路径")
    parser.add_argument(
        "--sort-by", default="Ir", choices=EVENTS, help="排序事件 (默认: Ir)"
    )
    parser.add_argument(
        "--top-n", type=int, default=50, help="热点函数显示数量 (默认: 50)"
    )
    parser.add_argument(
        "--max-mem", type=int, default=20, help="内存操作显示数量 (默认: 20)"
    )
    args = parser.parse_args()

    parse_cachegrind(
        Path(args.file),
        sort_by=args.sort_by,
        top_n=args.top_n,
        max_mem_lines=args.max_mem,
    )
