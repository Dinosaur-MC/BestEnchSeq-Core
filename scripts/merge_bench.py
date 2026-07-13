#!/usr/bin/env python3
"""
用法：python merge_bench.py <输入文件> <输出文件>

该工具解析基准测试输出文件，提取每个数据集下各算法的结果，
并在多次运行间取最优值（L 越小越好，L 相同时时间越短越好，
无效值如 no solution / SKIP 不参与比较）。
最终结果写入输出文件。若输出文件已存在且内容将发生变化，
会先将旧文件备份为 <输出文件>.bak。
"""

import sys
import os
import re
from collections import OrderedDict


# ------------------------------------------------------------
# 解析单个文件
# ------------------------------------------------------------
def parse_file(filepath):
    """解析基准输出文件，返回 (header, latest_time, datasets_dict, dataset_order, algo_orders)"""
    if not os.path.exists(filepath):
        return None, None, OrderedDict(), [], {}

    with open(filepath, "r", encoding="utf-8") as f:
        lines = f.readlines()

    header_line = None
    times = []
    datasets = OrderedDict()  # dataset_header -> {algo: record}
    dataset_order = []  # 首次出现顺序
    algo_orders = {}  # dataset_header -> [algo] 顺序

    current_dataset = None
    in_benchmark = False

    for line in lines:
        line = line.strip("\n")
        if not line:
            continue

        # 记录 Benchmark 头部
        if line.startswith("Benchmark running in Release build with program args:"):
            if header_line is None:
                header_line = line
            continue

        # 记录时间戳
        if line.startswith("Time:"):
            time_str = line[len("Time:") :].strip()
            times.append(time_str)
            continue

        # 数据集块开始
        if line.startswith("=== Dataset Benchmark ==="):
            in_benchmark = True
            continue

        # 数据集块结束
        if line.startswith("=== Done ==="):
            in_benchmark = False
            current_dataset = None
            continue

        if not in_benchmark:
            continue

        # 数据集头行：非缩进、包含 ':'，且不是特殊行
        if not line.startswith(" ") and ":" in line:
            current_dataset = line
            if current_dataset not in datasets:
                datasets[current_dataset] = OrderedDict()
                dataset_order.append(current_dataset)
                algo_orders[current_dataset] = []
            continue

        # 缩进的算法行
        if current_dataset is None:
            continue

        # 有效行：  算法名   数值L  ✅  时间ms
        m = re.match(r"^\s+(?P<algo>\w+)\s+(?P<L>\d+)L\s+✅\s+(?P<ms>\d+)ms", line)
        if m:
            algo = m.group("algo")
            rec = {
                "status": "✅",
                "L": int(m.group("L")),
                "time_ms": int(m.group("ms")),
            }
            _update_record(
                datasets[current_dataset], algo_orders[current_dataset], algo, rec
            )
            continue

        # SKIP 行
        m = re.match(r"^\s+(?P<algo>\w+)\s+(?P<status>SKIP.*)", line)
        if m:
            algo = m.group("algo")
            rec = {"status": m.group("status").strip()}
            _update_record(
                datasets[current_dataset], algo_orders[current_dataset], algo, rec
            )
            continue

        # no solution 行
        m = re.match(r"^\s+(?P<algo>\w+)\s+no solution", line)
        if m:
            algo = m.group("algo")
            rec = {"status": "no solution"}
            _update_record(
                datasets[current_dataset], algo_orders[current_dataset], algo, rec
            )
            continue

        # 其他无法识别的行，忽略

    latest_time = max(times) if times else None
    return header_line, latest_time, datasets, dataset_order, algo_orders


def _update_record(dataset_algos, algo_order_list, algo, record):
    """在解析过程中更新一个数据集下某算法的记录（保留首次遇到的最优值）"""
    if algo not in dataset_algos:
        dataset_algos[algo] = record
        algo_order_list.append(algo)
        return

    existing = dataset_algos[algo]
    # 新记录无效而旧记录有效 -> 保留旧记录
    if existing["status"] == "✅" and record["status"] != "✅":
        return
    # 新记录有效而旧记录无效 -> 替换
    if existing["status"] != "✅" and record["status"] == "✅":
        dataset_algos[algo] = record
        return
    # 两者都有效 -> 比较 (L 越小越好，其次时间越短越好)
    if existing["status"] == "✅" and record["status"] == "✅":
        if record["L"] < existing["L"] or (
            record["L"] == existing["L"] and record["time_ms"] < existing["time_ms"]
        ):
            dataset_algos[algo] = record
    # 两者都无效 -> 保留第一个


# ------------------------------------------------------------
# 选择两个记录中的最优者
# ------------------------------------------------------------
def pick_best(rec1, rec2):
    if rec1 is None and rec2 is None:
        return None
    if rec1 is None:
        return rec2
    if rec2 is None:
        return rec1

    valid1 = rec1.get("status") == "✅"
    valid2 = rec2.get("status") == "✅"

    if valid1 and not valid2:
        return rec1
    if valid2 and not valid1:
        return rec2
    if valid1 and valid2:
        if rec1["L"] < rec2["L"] or (
            rec1["L"] == rec2["L"] and rec1["time_ms"] < rec2["time_ms"]
        ):
            return rec1
        return rec2
    # 都无效，保留第一个
    return rec1


# ------------------------------------------------------------
# 合并两个解析结果
# ------------------------------------------------------------
def merge_results(
    in_header,
    in_time,
    in_datasets,
    in_order,
    in_algo_orders,
    out_header,
    out_time,
    out_datasets,
    out_order,
    out_algo_orders,
):
    # 头部：优先使用输入文件的头部
    header = (
        in_header
        or out_header
        or "Benchmark running in Release build with program args: --no-skip"
    )
    # 时间：取最新
    all_times = [t for t in (in_time, out_time) if t]
    latest_time = max(all_times) if all_times else None

    merged_order = []
    merged_algo_orders = {}
    merged_datasets = OrderedDict()

    # 合并数据集顺序（输入优先）
    for ds in in_order + out_order:
        if ds not in merged_datasets:
            merged_datasets[ds] = OrderedDict()
            merged_order.append(ds)
            merged_algo_orders[ds] = []

    for ds in merged_order:
        in_algos = in_algo_orders.get(ds, [])
        out_algos = out_algo_orders.get(ds, [])
        algo_list = []
        for a in in_algos + out_algos:
            if a not in algo_list:
                algo_list.append(a)
        merged_algo_orders[ds] = algo_list

        for algo in algo_list:
            rec_in = in_datasets.get(ds, {}).get(algo)
            rec_out = out_datasets.get(ds, {}).get(algo)
            best = pick_best(rec_in, rec_out)
            if best:
                merged_datasets[ds][algo] = best

    return header, latest_time, merged_datasets, merged_order, merged_algo_orders


# ------------------------------------------------------------
# 生成输出文本
# ------------------------------------------------------------
def generate_output(header, latest_time, datasets, dataset_order, algo_orders):
    blocks = []
    for ds in dataset_order:
        lines = [ds]
        for algo in algo_orders.get(ds, []):
            rec = datasets[ds].get(algo)
            if rec is None:
                continue
            if rec["status"] == "✅":
                lines.append(f"  {algo:<20}{rec['L']}L  ✅ {rec['time_ms']:>5}ms")
            else:
                lines.append(f"  {algo:<20}{rec['status']}")
        blocks.append("\n".join(lines))

    time_str = latest_time if latest_time else "2026-01-01 00:00:00.0"
    output = (
        header
        + "\n"
        + f"Time: {time_str}\n"
        + "=== Dataset Benchmark ===\n\n"
        + "\n\n".join(blocks)
        + "\n=== Done ===\n"
    )
    return output


# ------------------------------------------------------------
# 写入输出文件（带备份）
# ------------------------------------------------------------
def write_output(path, content):
    if os.path.exists(path):
        with open(path, "r", encoding="utf-8") as f:
            old_content = f.read()
        if old_content == content:
            print("内容无变化，跳过写入。")
            return
        backup = path + ".bak"
        if os.path.exists(backup):
            os.remove(backup)
        os.rename(path, backup)
        print(f"已备份旧文件至: {backup}")

    with open(path, "w", encoding="utf-8") as f:
        f.write(content)
    print(f"新结果已写入: {path}")


# ------------------------------------------------------------
# 主入口
# ------------------------------------------------------------
def main():
    if len(sys.argv) != 3:
        print("用法: python merge_bench.py <输入文件> <输出文件>")
        sys.exit(1)

    input_path = sys.argv[1]
    output_path = sys.argv[2]

    # 解析输入文件
    in_header, in_time, in_datasets, in_order, in_algo = parse_file(input_path)
    # 解析输出文件（若存在）
    out_header, out_time, out_datasets, out_order, out_algo = parse_file(output_path)

    # 合并
    header, latest_time, merged_datasets, merged_order, merged_algo = merge_results(
        in_header,
        in_time,
        in_datasets,
        in_order,
        in_algo,
        out_header,
        out_time,
        out_datasets,
        out_order,
        out_algo,
    )

    # 生成最终文本
    HEADER = "Best benchmark ran on Release build"
    output_content = generate_output(
        HEADER, latest_time, merged_datasets, merged_order, merged_algo
    )

    # 写入
    write_output(output_path, output_content)


if __name__ == "__main__":
    main()
