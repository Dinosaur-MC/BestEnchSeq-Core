#!/usr/bin/env python3
"""
用法：python merge_bench.py <输入文件> <输出文件> [历史文件]

该工具解析基准测试输出文件，提取每个数据集下各算法的结果，
并在多次运行间取最优值（L 越小越好，L 相同时时间越短越好，
无效值如 no solution / SKIP 不参与比较）。
最终结果写入输出文件。若输出文件已存在且内容将发生变化，
会先将旧文件备份为 <输出文件>.bak，并在控制台打印差异。

若省略历史文件，则默认为 <输出文件>.history.json。
"""

import sys
import os
import re
import difflib
import json
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
        m = re.match(r"^\s+(?P<algo>[\w+]+)\s+(?P<L>\d+)L\s+✅\s+(?P<ms>\d+)ms", line)
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
        m = re.match(r"^\s+(?P<algo>[\w+]+)\s+(?P<status>SKIP.*)", line)
        if m:
            algo = m.group("algo")
            rec = {"status": m.group("status").strip()}
            _update_record(
                datasets[current_dataset], algo_orders[current_dataset], algo, rec
            )
            continue

        # no solution 行
        m = re.match(r"^\s+(?P<algo>[\w+]+)\s+no solution", line)
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
# 打印 unified diff
# ------------------------------------------------------------
def print_diff(old_content, new_content, old_label="旧文件", new_label="新文件"):
    """以 unified diff 格式打印新旧内容差异"""
    old_lines = old_content.splitlines(keepends=True)
    new_lines = new_content.splitlines(keepends=True)
    diff = difflib.unified_diff(
        old_lines, new_lines, fromfile=old_label, tofile=new_label
    )
    diff_text = "".join(diff)
    if diff_text:
        print("--- 内容差异 ---")
        print(diff_text)
        print("----------------")
    else:
        print("内容完全相同（无差异）。")


# ------------------------------------------------------------
# 写入输出文件（带备份和 diff 打印）
# ------------------------------------------------------------
def write_output(path, content):
    if os.path.exists(path):
        with open(path, "r", encoding="utf-8") as f:
            old_content = f.read()
        if old_content == content:
            print("内容无变化，跳过写入。")
            return
        # 打印差异
        print_diff(old_content, content)
        # 备份旧文件
        backup = path + ".bak"
        if os.path.exists(backup):
            os.remove(backup)
        os.rename(path, backup)
        print(f"已备份旧文件至: {backup}")

    with open(path, "w", encoding="utf-8") as f:
        f.write(content)
    print(f"新结果已写入: {path}")


# ------------------------------------------------------------
# 历史记录与趋势图
# ------------------------------------------------------------
HISTORY_FILE_SUFFIX = ".history.json"


def _history_path(output_path):
    """获取历史记录文件路径"""
    return output_path + HISTORY_FILE_SUFFIX


def load_history(path):
    """加载历史记录 JSON"""
    if os.path.exists(path):
        with open(path, "r", encoding="utf-8") as f:
            return json.load(f)
    return {"entries": []}


def save_history(path, history):
    """保存历史记录 JSON"""
    tmp = path + ".tmp"
    with open(tmp, "w", encoding="utf-8") as f:
        json.dump(history, f, indent=2, ensure_ascii=False)
    os.replace(tmp, path)
    print(f"历史记录已保存至: {path}")


def plot_trends(history, hist_path):
    """根据历史记录绘制趋势折线图"""
    try:
        import matplotlib

        matplotlib.use("Agg")
        import matplotlib.pyplot as plt
    except ImportError:
        print("提示: 未安装 matplotlib，跳过趋势图绘制。")
        print("      安装: pip install matplotlib")
        return

    entries = sorted(history["entries"], key=lambda e: e.get("timestamp", ""))
    if len(entries) < 2:
        print("历史数据不足（至少需要 2 条记录），跳过趋势图绘制。")
        return

    # 收集所有数据集和算法
    all_datasets = sorted({ds for e in entries for ds in e.get("datasets", {})})
    all_algos = sorted(
        {algo for e in entries for ds in e.get("datasets", {}).values() for algo in ds}
    )

    if not all_datasets or not all_algos:
        print("历史数据为空，跳过趋势图绘制。")
        return

    run_labels = [f"#{i + 1}" for i in range(len(entries))]
    run_numbers = list(range(len(entries)))
    n = len(all_datasets)

    fig, axes = plt.subplots(n, 1, figsize=(10, 3.5 * n), squeeze=False)

    for idx, ds in enumerate(all_datasets):
        ax = axes[idx][0]
        ax.set_title(ds, fontsize=12, fontweight="bold")
        ax.set_xlabel("Run")
        ax.set_ylabel("Time (ms)")

        for algo in all_algos:
            vals = []
            for e in entries:
                rec = e.get("datasets", {}).get(ds, {}).get(algo)
                vals.append(rec["time_ms"] if rec else None)
            pts = [(r, v) for r, v in zip(run_numbers, vals) if v is not None]
            if pts:
                xs, ys = zip(*pts)
                ax.plot(xs, ys, marker="o", label=algo, linewidth=1.5)

        ax.set_xticks(run_numbers)
        ax.set_xticklabels(run_labels, fontsize=8)
        ax.legend(fontsize=8, loc="upper left", bbox_to_anchor=(1.02, 1.0))
        ax.grid(True, alpha=0.3)

    plt.tight_layout()
    plt.subplots_adjust(right=0.85)
    # 历史文件后缀替换为 .trend.png
    chart_path = hist_path[: -len(HISTORY_FILE_SUFFIX)] + ".trend.png" if hist_path.endswith(HISTORY_FILE_SUFFIX) else hist_path + ".trend.png"
    plt.savefig(chart_path, dpi=150, bbox_inches="tight")
    plt.close()
    print(f"趋势图已保存至: {chart_path}")


# ------------------------------------------------------------
# 主入口
# ------------------------------------------------------------
def main():
    if len(sys.argv) < 3:
        print("用法: python merge_bench.py <输入文件> <输出文件> [历史文件]")
        print("  若省略历史文件，则默认为 <输出文件>.history.json")
        sys.exit(1)

    input_path = sys.argv[1]
    output_path = sys.argv[2]
    hist_path = sys.argv[3] if len(sys.argv) > 3 else _history_path(output_path)

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

    # 写入（内部会自动对比并打印 diff）
    write_output(output_path, output_content)

    # ------------------------------------------------------------
    # 历史记录与趋势图（记录每次输入文件的原始数据）
    # ------------------------------------------------------------
    history = load_history(hist_path)

    # 创建新历史条目：使用输入文件的原始数据，而非合并结果
    new_entry = {"timestamp": in_time if in_time else "N/A", "datasets": {}}
    for ds in in_order:
        new_entry["datasets"][ds] = {}
        for algo in in_algo.get(ds, []):
            rec = in_datasets[ds].get(algo)
            if rec and rec.get("status") == "✅":
                new_entry["datasets"][ds][algo] = {"L": rec["L"], "time_ms": rec["time_ms"]}

    # 输入文件无有效数据则跳过历史记录
    if not in_time or not any(new_entry["datasets"].values()):
        print("输入文件无有效基准数据，跳过历史记录。")
        return

    # 按 benchmark 时间戳去重：同 timestamp 替换（仅内容变化时），不同则追加
    timestamps = [e.get("timestamp") for e in history["entries"]]
    if in_time and in_time in timestamps:
        idx = timestamps.index(in_time)
        if history["entries"][idx].get("datasets") != new_entry.get("datasets"):
            history["entries"][idx] = new_entry
            print(f"更新现有历史记录（timestamp: {in_time}）")
            history["entries"].sort(key=lambda e: e.get("timestamp", ""))
            save_history(hist_path, history)
        else:
            print("输入数据与上次历史记录相同，跳过记录。")
    elif not history["entries"] or history["entries"][-1].get("datasets") != new_entry.get("datasets"):
        history["entries"].append(new_entry)
        history["entries"].sort(key=lambda e: e.get("timestamp", ""))
        save_history(hist_path, history)
    else:
        print("输入数据与上次历史记录相同，跳过记录。")

    # 绘制趋势图
    plot_trends(history, hist_path)


if __name__ == "__main__":
    main()
