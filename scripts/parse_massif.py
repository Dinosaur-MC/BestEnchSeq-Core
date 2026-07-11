#!/usr/bin/env python3
"""
快速解析 Massif 的 ms_print 输出文件，提取 ASCII 图表、快照统计和分配热点。
用法: python parse_massif.py massif.out.ms_print.txt
"""

import sys
import re
from pathlib import Path
from collections import defaultdict
from typing import Optional


# ---------- 辅助函数 ----------
def parse_size(size_str: str) -> int:
    """将类似 '25,923,216B' 或 '327,680B' 的字符串转换为字节整数"""
    size_str = size_str.replace(",", "").strip()
    if size_str.endswith("B"):
        num_part = size_str[:-1].strip()
        if num_part.endswith("K"):
            return int(float(num_part[:-1]) * 1024)
        elif num_part.endswith("M"):
            return int(float(num_part[:-1]) * 1024 * 1024)
        elif num_part.endswith("G"):
            return int(float(num_part[:-1]) * 1024 * 1024 * 1024)
        else:
            return int(float(num_part))
    else:
        return int(float(size_str))


def clean_function_name(raw: str) -> str:
    """从分配行中提取干净的函数名"""
    if ":" in raw:
        raw = raw.split(":", 1)[1].strip()
    raw = re.sub(r"\s+\(in\s+.*\)$", "", raw)
    raw = re.sub(r"\s+\(.*\.(?:cpp|h|hpp|cxx|hxx):\d+\)$", "", raw)
    return raw.strip()


def extract_ascii_chart(content: str) -> Optional[str]:
    """从 ms_print 输出的头部提取内存变化 ASCII 图表"""
    lines = content.splitlines()
    start_idx = None
    end_idx = None

    for i, line in enumerate(lines):
        if line.strip() == "MB":
            start_idx = i
            break

    if start_idx is None:
        return None

    for i in range(start_idx + 1, len(lines)):
        if lines[i].strip().startswith("Number of snapshots:"):
            end_idx = i
            break

    if end_idx is None:
        for i in range(start_idx + 1, len(lines)):
            if re.match(r"^-{80,}$", lines[i].strip()):
                end_idx = i
                break

    if start_idx is not None and end_idx is not None and end_idx > start_idx:
        return "\n".join(lines[start_idx:end_idx])
    return None


# ---------- 主解析函数 ----------
def parse_massif(file_path: Path):
    if not file_path.exists():
        print(f"错误: 文件不存在 - {file_path}")
        return

    with open(file_path, "r", encoding="utf-8") as f:
        content = f.read()

    # 1. 提取并打印 ASCII 图表
    chart = extract_ascii_chart(content)
    if chart:
        print("\n" + "=" * 80)
        print("内存使用变化图表 (ASCII):")
        print("=" * 80)
        print(chart)
    else:
        print("\n[警告] 未找到 ASCII 图表部分。")

    # 2. 提取快照表格
    snapshot_pattern = re.compile(
        r"^\s*(\d+)\s+([\d,]+)\s+([\d,]+)\s+([\d,]+)\s+([\d,]+)\s+([\d,]+)$",
        re.MULTILINE,
    )
    snapshots = []
    for match in snapshot_pattern.finditer(content):
        n = int(match.group(1))
        time = int(match.group(2).replace(",", ""))
        total = parse_size(match.group(3))
        useful = parse_size(match.group(4))
        extra = parse_size(match.group(5))
        stacks = parse_size(match.group(6))
        snapshots.append((n, time, total, useful, extra, stacks))

    if not snapshots:
        print("未找到快照数据，请检查文件格式是否正确。")
        return

    total_snapshots = len(snapshots)
    peak_snapshot = max(snapshots, key=lambda x: x[2])
    peak_n, peak_time, peak_total, peak_useful, peak_extra, peak_stacks = peak_snapshot

    print("\n" + "=" * 80)
    print(f"总快照数: {total_snapshots}")
    print(f"峰值内存 (快照 {peak_n}):")
    print(f"  执行时间 (time(i)) : {peak_time:>15,}")
    print(
        f"  总内存 (total)     : {peak_total:>15,} 字节 ({peak_total/1024/1024:.2f} MiB)"
    )
    print(
        f"  有用堆 (useful-heap): {peak_useful:>15,} 字节 ({peak_useful/1024/1024:.2f} MiB)"
    )
    print(f"  额外开销 (extra)   : {peak_extra:>15,} 字节")
    print(f"  栈内存 (stacks)    : {peak_stacks:>15,} 字节")
    print("=" * 80)

    # 3. 提取分配热点
    alloc_pattern = re.compile(r"^->([\d.]+)%\s+\(([^)]+)\)\s+(.*)$", re.MULTILINE)
    func_alloc = defaultdict(int)

    for match in alloc_pattern.finditer(content):
        size_str = match.group(2)
        suffix = match.group(3).strip()
        if not size_str:
            continue
        try:
            size_bytes = parse_size(size_str)
        except:
            continue
        func_name = clean_function_name(suffix)
        if func_name:
            func_alloc[func_name] += size_bytes

    print("\n>>> 按分配总字节数排序的 Top 30 热点函数")
    sorted_funcs = sorted(func_alloc.items(), key=lambda x: x[1], reverse=True)
    for i, (name, bytes_alloc) in enumerate(sorted_funcs[:30]):
        print(f"{i+1:3}. {bytes_alloc:>18,}  {name}")

    # 4. 可疑内存操作
    print("\n>>> 可疑内存操作 (reserve / allocate / malloc / new)")
    sus_keywords = ["reserve", "allocate", "malloc", "new", "_M_allocate"]
    found = False
    for name, bytes_alloc in sorted_funcs:
        if any(k in name.lower() for k in sus_keywords):
            print(f"{bytes_alloc:>18,}  {name}")
            found = True
    if not found:
        print("未在 Top 热点中检测到明显的大规模内存预留/分配函数。")

    # 5. 峰值附近快照
    print("\n>>> 峰值附近快照 (前后各 3 个)")
    peak_idx = next(i for i, (n, _, _, _, _, _) in enumerate(snapshots) if n == peak_n)
    start = max(0, peak_idx - 3)
    end = min(total_snapshots, peak_idx + 4)
    print(
        f"{'n':>4} {'time(i)':>12} {'total(B)':>12} {'useful-heap':>12} {'extra-heap':>10} {'stacks':>10}"
    )
    for i in range(start, end):
        n, t, tot, use, ext, stk = snapshots[i]
        print(f"{n:4} {t:12,} {tot:12,} {use:12,} {ext:10,} {stk:10,}")


if __name__ == "__main__":
    if len(sys.argv) != 2:
        print("用法: python parse_massif.py <massif.out.*.ms_print>")
    else:
        parse_massif(Path(sys.argv[1]))
