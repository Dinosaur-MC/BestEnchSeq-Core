#!/usr/bin/env python3
"""
用法：
  python merge_bench.py <output_dir>
  python merge_bench.py <input_file> <output_dir> [选项]

解析基准测试输出文件，提取每个数据集下各算法的结果，
在多次运行间取最优值（L 越小越好，L 相同时时间越短越好，
无效值如 no solution / SKIP 不参与比较），
并将最优值写入 <output_dir>/best_benchmark.txt。

若省略 input_file，则扫描 output_dir 中所有 .txt 文件作为输入。

选项:
  -i, --img              渲染趋势图
  -g, --group <分组>     enchs 数分组，如 "1,2;3..4;5..;..6;3,7"
                           ; 分隔组，, 分隔条件，.. 表示范围
                           （默认 "1..6;7..9;10..16;17.."）
  -h, --help             显示帮助信息
"""

import sys
import os
import re
import difflib
import json
import argparse
from collections import OrderedDict


# ------------------------------------------------------------
# 解析单个文件
# ------------------------------------------------------------
def parse_json(content):
    """解析 forge_benchmark --json 的 dataset 结构化输出；映射到与文本解析
    相同的内部结构（解析层改进：JSON 优先 + 文本回退，spec
    2026-08-07-bench-report-json-design.md）。

    status 全集合：ok|skip|no-solution|timeout|failed——仅 ok 为有效记录
    （参与最优比较），其余与文本路径的 no-solution 同级（无效记录）。
    wall（median/p95/best/iterations）随记录保留，供历史/后续图表使用。"""
    import json

    data = json.loads(content)
    datasets = OrderedDict()  # dataset_key -> {algo: record}
    dataset_order = []  # 首次出现顺序
    algo_orders = {}  # dataset_key -> [algo] 顺序

    for ds in data.get("datasets", []):
        name = ds.get("name", "")
        ench_count = ds.get("ench_count", 0)
        max_lvl = ds.get("max_lvl", 0)
        # 重建展示名：与文本解析的 dataset 头同源（"crossbow (4 enchants,
        # max 30L)"），下游 extract_enchs_count/分组/排序逻辑零改动。
        ds_key = f"{name} ({ench_count} enchants, max {max_lvl}L)"
        if ds_key not in datasets:
            datasets[ds_key] = OrderedDict()
            dataset_order.append(ds_key)
            algo_orders[ds_key] = []
        for r in ds.get("results", []):
            algo = r.get("algo", "")
            status = r.get("status", "failed")
            if status == "ok":
                rec = {
                    "status": "✅",
                    "L": int(r.get("L", 0)),
                    "time_ms": int(r.get("comp_ms", 0)),
                }
            elif status == "skip":
                # note 已含 "SKIP (predicted ...)" 前缀（forge 侧同源），直接用
                rec = {"status": r.get("note", "SKIP")}
            elif status == "no-solution":
                rec = {"status": "no solution"}
            else:  # timeout / failed
                rec = {"status": status, "note": r.get("note", "")}
            wall = r.get("wall")
            if wall:
                rec["wall"] = wall
            _update_record(datasets[ds_key], algo_orders[ds_key], algo, rec)

    return None, None, datasets, dataset_order, algo_orders


def parse_file(filepath):
    """解析基准输出文件，返回 (header, latest_time, datasets_dict, dataset_order, algo_orders)。
    JSON 优先（forge_benchmark --json）；否则文本回退（原解析原样保留）。"""
    if not os.path.exists(filepath):
        return None, None, OrderedDict(), [], {}

    with open(filepath, "r", encoding="utf-8") as f:
        content = f.read()

    # JSON 检测：内容以 { 开头（evaluate.sh 另存的 benchmark.json 亦如此）
    if content.lstrip().startswith("{"):
        return parse_json(content)

    lines = content.splitlines()

    header_line = None
    times = []
    datasets = OrderedDict()  # dataset_header -> {algo: record}
    dataset_order = []  # 首次出现顺序
    algo_orders = {}  # dataset_header -> [algo] 顺序

    current_dataset = None
    in_benchmark = False

    for line in lines:
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

    valid1 = rec1.get("status") == "✅" and rec1["L"] > 0
    valid2 = rec2.get("status") == "✅" and rec2["L"] > 0

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
        merged_algo_orders[ds] = sorted(algo_list, key=str.lower)

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
    dataset_order = sorted(dataset_order, key=lambda x: int(re.search(r"(\d+) ench", x).group(1)))
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


def extract_enchs_count(dataset_name):
    """从数据集名称提取附魔数，如 'sword_basic (3 enchants, max 35L):' → 3"""
    m = re.search(r"\((\d+)\s+enchants?[,:)]", dataset_name)
    return int(m.group(1)) if m else None


def normalize_ds_name(dataset_name):
    """去除数据集名称中的 max level 信息，使相同附魔数的数据集统一分组"""
    return re.sub(r",\s*max\s+\d+L", "", dataset_name)


def parse_group_config(config_str: str):
    """
    解析分组配置。

    语法：; 分隔组，, 分隔条件，.. 表示范围。
    示例: "1,2;3..4;5..;..6;3,7"
      → 组1={1,2}, 组2=[3,4], 组3=[5,∞), 组4=(-∞,6], 组5={3,7}
    返回: [(label, [condition_fn, ...]), ...]
    """
    groups = []
    for part in config_str.split(";"):
        part = part.strip()
        if not part:
            continue
        conditions = []
        labels = []
        for cond in part.split(","):
            cond = cond.strip()
            if ".." in cond:
                ends = cond.split("..", 1)
                if ends[0] and ends[1]:
                    lo, hi = int(ends[0]), int(ends[1])
                    conditions.append(lambda x, lo=lo, hi=hi: lo <= x <= hi)
                    labels.append(f"{lo}-{hi}")
                elif ends[0] and not ends[1]:
                    lo = int(ends[0])
                    conditions.append(lambda x, lo=lo: x >= lo)
                    labels.append(f"{lo}+")
                else:
                    hi = int(ends[1])
                    conditions.append(lambda x, hi=hi: x <= hi)
                    labels.append(f"≤{hi}")
            else:
                n = int(cond)
                conditions.append(lambda x, n=n: x == n)
                labels.append(str(n))
        groups.append((",".join(labels), conditions))
    return groups


def group_dataset(dataset_name, groups):
    """返回数据集所属的分组索引，无法提取则返回 -1。groups 由 parse_group_config 返回"""
    count = extract_enchs_count(dataset_name)
    if count is None:
        return -1
    for i, (label, conditions) in enumerate(groups):
        if any(cond(count) for cond in conditions):
            return i
    return -1


def plot_trends(history: dict[str, list[dict]], output_dir, group_config=None):
    """根据历史记录绘制趋势折线图（可选按 enchs 数分组输出多张图）"""
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

    # 限制绘图点数，裁剪最旧的数据
    MAX_POINTS = 64
    total_before = len(entries)
    offset = 0
    if total_before > MAX_POINTS:
        offset = total_before - MAX_POINTS
        entries = entries[-MAX_POINTS:]

    # 归一化数据集名称（去除 max level 差异）以便统一分组和趋势图
    def _norm(name):
        return re.sub(r",\s*max\s+\d+L", "", name)

    def _best_rec(e, nds, algo):
        """在 entry 中按归一化名称查找 algo 记录，多条时取最优"""
        found = []
        for ds, recs in e.get("datasets", {}).items():
            if _norm(ds) == nds and algo in recs:
                found.append(recs[algo])
        if len(found) <= 1:
            return found[0] if found else None
        # 多条记录取最优（L 越小越好，时间越短越好）
        best = found[0]
        for r in found[1:]:
            if r["L"] < best["L"] or (r["L"] == best["L"] and r["time_ms"] < best["time_ms"]):
                best = r
        return best

    # 收集所有数据集和算法（使用归一化名称）
    norm_to_originals = {}
    for e in entries:
        for ds in e.get("datasets", {}):
            nds = _norm(ds)
            norm_to_originals.setdefault(nds, set()).add(ds)
    all_datasets = sorted(norm_to_originals.keys())
    all_algos = sorted(
        {algo for e in entries for ds in e.get("datasets", {}).values() for algo in ds}
    )

    if not all_datasets or not all_algos:
        print("历史数据为空，跳过趋势图绘制。")
        return

    run_labels = [f"#{offset + i}" for i in range(len(entries))]
    run_numbers = list(range(len(entries)))

    def build_one_chart(datasets, fig_title, chart_suffix):
        """在同一个 figure 中绘制一组数据集的折线图"""
        n = len(datasets)
        if n == 0:
            return

        fig, axes = plt.subplots(
            n, 1, figsize=(10, 3.5 * n), squeeze=False, constrained_layout=True
        )
        if fig_title:
            fig.suptitle(fig_title, fontsize=14, fontweight="bold")

        for idx, ds in enumerate(datasets):
            ax = axes[idx][0]
            ax.set_title(ds, fontsize=12, fontweight="bold")
            ax.set_xlabel("Run")
            ax.set_ylabel("Time (ms)")

            for algo in all_algos:
                vals = []
                for e in entries:
                    rec = _best_rec(e, ds, algo)
                    vals.append(rec["time_ms"] if rec else None)
                pts = [(r, v) for r, v in zip(run_numbers, vals) if v is not None]
                if pts:
                    xs, ys = zip(*pts)
                    ax.plot(xs, ys, marker="o", ms=3.5, label=algo, linewidth=1.5)

            # 第二纵轴：Level 曲线（半透明）
            ax2 = ax.twinx()
            for algo in all_algos:
                vals = []
                for e in entries:
                    rec = _best_rec(e, ds, algo)
                    vals.append(rec["L"] if rec else None)
                pts = [(r, v) for r, v in zip(run_numbers, vals) if v is not None]
                if pts:
                    xs, ys = zip(*pts)
                    ax2.plot(xs, ys, marker="s", ms=3.5, linewidth=1.0, alpha=0.3)
            ax2.set_ylabel("Level")

            # 自适应稀疏横坐标
            n_runs = len(run_numbers)
            if n_runs <= 20:
                tick_step = 1
            elif n_runs <= 50:
                tick_step = 2
            elif n_runs <= 100:
                tick_step = 5
            else:
                tick_step = n_runs // 15
            visible = list(range(0, n_runs, tick_step))
            if visible[-1] != n_runs - 1:
                visible.append(n_runs - 1)
            ax.set_xticks(visible)
            ax.set_xticklabels(
                [run_labels[i] for i in visible], fontsize=7, rotation=45, ha="right"
            )
            ax.legend(fontsize=8, loc="upper left", bbox_to_anchor=(1.08, 1.0))
            if offset == 0:
                ax.set_xlim(left=-0.3)
            else:
                ax.set_xlim(left=run_numbers[0])
            ax.grid(True, alpha=0.3)

        plt.tight_layout(rect=[0, 0, 1, 1])  # 暂时让子图占满整个 figure
        fig.canvas.draw()  # 必须 draw 后才能拿到坐标
        if fig_title:
            # suptitle 是一个 Text 对象，可以通过 fig._suptitle 获取
            suptitle = fig._suptitle
            # 获取 suptitle 在 figure 坐标系中的边界框 (x0, y0, x1, y1)
            bbox = suptitle.get_window_extent(renderer=fig.canvas.get_renderer())
            # 转换成 figure 坐标系（0-1）
            bbox_fig = bbox.transformed(fig.transFigure.inverted())
            # 计算 suptitle 底部到 figure 顶部的距离（比例）
            title_bottom = bbox_fig.y0  # 标题下沿的 y 坐标
        else:
            title_bottom = 1.0

        desired_pad_pt = 16  # 标题与子图之间的固定距离（磅）
        # figure 的高度（单位：英寸）
        fig_height_inches = fig.get_size_inches()[1]
        # figure 的 DPI
        dpi = fig.dpi
        # 将点数转换为英寸
        pad_inches = desired_pad_pt / 72.0  # 1 pt = 1/72 英寸
        # 转换为 figure 坐标系的相对高度
        pad_fig = pad_inches / fig_height_inches

        # 子图区域的上边界：应紧贴在 suptitle 下方，并留出 pad_fig 的间距
        new_top = title_bottom - pad_fig
        # 限制一下，避免负值
        new_top = max(new_top, 0.05)

        fig.subplots_adjust(top=new_top, right=0.82)
        
        chart_path = os.path.join(output_dir, "benchmark" + chart_suffix)
        plt.savefig(chart_path, dpi=150, bbox_inches="tight")
        plt.close()
        print(f"趋势图已保存至: {chart_path}")

    # --- 分组或全量输出 ---
    if group_config:
        # 按 enchs 数分组
        groups = {}  # group_idx -> [dataset_name]
        for ds in all_datasets:
            g = group_dataset(ds, group_config)
            if g >= 0:
                groups.setdefault(g, []).append(ds)

        if not groups:
            print("未匹配到任何分组，跳过趋势图。")
            return

        # 各组内按 enchs 数升序排列
        for g_idx in groups:
            groups[g_idx].sort(key=lambda ds: extract_enchs_count(ds) or 0)

        for g_idx in sorted(groups):
            label, _ = group_config[g_idx]
            build_one_chart(
                groups[g_idx],
                fig_title=f"enchs = {label}",
                chart_suffix=f".trend.{label}.png",
            )
    else:
        build_one_chart(
            sorted(all_datasets, key=lambda ds: extract_enchs_count(ds) or 0),
            fig_title=None,
            chart_suffix=".trend.png",
        )


# ------------------------------------------------------------
# 主入口
# ------------------------------------------------------------
def main():
    parser = argparse.ArgumentParser(
        usage="%(prog)s [options] [input_file] <output_dir>",
        description="合并基准测试输出并生成趋势图",
    )
    parser.add_argument(
        "positionals",
        nargs="*",
        metavar="[input_file] <output_dir>",
        help="设置输入文件和输出目录（省略输入时扫描输出目录中的 .txt）",
    )
    parser.add_argument("-i", "--img", action="store_true", help="渲染趋势图")
    parser.add_argument(
        "-g",
        "--group",
        default="1..6;7..9;10..16;17..",
        help='enchs 数分组，如 "1,2;3..4;5..;..6;3,7"' '（默认 "1..6;7..9;10..16;17.."）',
    )
    args = parser.parse_args()

    # 解析位置参数
    if len(args.positionals) == 1:
        output_dir = args.positionals[0]
        input_file = None
    elif len(args.positionals) == 2:
        input_file = args.positionals[0]
        output_dir = args.positionals[1]
    else:
        parser.error("需要 1 或 2 个位置参数: [输入文件] 输出目录")

    os.makedirs(output_dir, exist_ok=True)
    hist_path = os.path.join(output_dir, "benchmark_history.json")
    group_config = parse_group_config(args.group) if args.img else None
    history = {}

    # 收集输入文件列表
    if input_file:
        input_files = [input_file]
    else:
        import glob

        all_txt = sorted(glob.glob(os.path.join(output_dir, "*.txt")))
        input_files = [
            f for f in all_txt if os.path.basename(f) != "best_benchmark.txt"
        ]
    if input_files:
        print(f"从目录加载 {len(input_files)} 个基准文件…")

        # 逐个合并输入文件
        for i, input_path in enumerate(input_files):
            if len(input_files) > 1:
                print(f"\n--- 合并文件 ({i + 1}/{len(input_files)}): {input_path}")

            # 解析输入文件
            in_header, in_time, in_datasets, in_order, in_algo = parse_file(input_path)
            # 解析输出文件（若存在）
            out_header, out_time, out_datasets, out_order, out_algo = parse_file(
                os.path.join(output_dir, "best_benchmark.txt")
            )

            # 合并
            header, latest_time, merged_datasets, merged_order, merged_algo = (
                merge_results(
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
            )

            # 生成最终文本
            HEADER = "Best benchmark ran on Release build"
            output_content = generate_output(
                HEADER, latest_time, merged_datasets, merged_order, merged_algo
            )

            # 写入
            write_output(os.path.join(output_dir, "best_benchmark.txt"), output_content)

            # 加载 / 创建历史记录
            history = load_history(hist_path)

            # 归一化已有历史记录中的数据集名称（去除 max level，兼容旧纪录）
            history_changed = False
            for entry in history.get("entries", []):
                old_ds = entry.get("datasets", {})
                new_ds = {}
                for ds, algos in old_ds.items():
                    nds = normalize_ds_name(ds)
                    if nds not in new_ds:
                        new_ds[nds] = {}
                    for algo, rec in algos.items():
                        existing = new_ds[nds].get(algo)
                        if existing is None:
                            new_ds[nds][algo] = rec
                        else:
                            # 取最优
                            if rec["L"] < existing["L"] or (
                                rec["L"] == existing["L"] and rec["time_ms"] < existing["time_ms"]
                            ):
                                new_ds[nds][algo] = rec
                if new_ds != old_ds:
                    entry["datasets"] = new_ds
                    history_changed = True
            if history_changed:
                save_history(hist_path, history)
                print("已迁移历史记录（归一化数据集名称）")

            # 创建新历史条目（使用归一化名称，去除 max level 差异）
            new_entry = {"timestamp": in_time if in_time else "N/A", "datasets": {}}
            for ds in in_order:
                nds = normalize_ds_name(ds)
                if nds not in new_entry["datasets"]:
                    new_entry["datasets"][nds] = {}
                nds_algos = new_entry["datasets"][nds]
                algos = sorted(in_algo.get(ds, []), key=str.lower)
                for algo in algos:
                    rec = in_datasets[ds].get(algo)
                    if rec and rec.get("status") == "✅":
                        existing = nds_algos.get(algo)
                        if existing is None:
                            nds_algos[algo] = {
                                "L": rec["L"],
                                "time_ms": rec["time_ms"],
                            }
                        else:
                            # 多条时取最优（L 越小越好，时间越短越好）
                            if rec["L"] < existing["L"] or (
                                rec["L"] == existing["L"] and rec["time_ms"] < existing["time_ms"]
                            ):
                                nds_algos[algo] = {
                                    "L": rec["L"],
                                    "time_ms": rec["time_ms"],
                                }

            # 输入文件无有效数据则跳过历史记录
            if not in_time or not any(new_entry["datasets"].values()):
                print("输入文件无有效基准数据，跳过历史记录。")
                continue

            # 按 benchmark 时间戳去重
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
            elif not history["entries"] or history["entries"][-1].get(
                "datasets"
            ) != new_entry.get("datasets"):
                history["entries"].append(new_entry)
                history["entries"].sort(key=lambda e: e.get("timestamp", ""))
                save_history(hist_path, history)
            else:
                print("输入数据与上次历史记录相同，跳过记录。")
    else:
        print("输入目录中未找到有效基准文件，跳过合并。")

    # 趋势图
    if args.img and history:
        print("\n--- 生成趋势图 ---")
        plot_trends(history, output_dir, group_config=group_config)


if __name__ == "__main__":
    main()
