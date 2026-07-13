import re
import sys
import subprocess
from pathlib import Path
from collections import defaultdict, namedtuple

CallEdge = namedtuple("CallEdge", ["caller", "callee", "count", "cost"])


class CallgrindAnalyzer:
    def __init__(
        self, file: Path, event_index: int = 0, addr2line_bin: str = "addr2line"
    ):
        self.file = file
        self.event_index = event_index
        self.addr2line_bin = addr2line_bin  # 可换成 llvm-symbolizer 等
        self.total_summary = 0
        self.num_events = 0
        self.fn_map = {}  # id -> name
        self.fn_obj = {}  # id -> obj path (用于解析匿名地址)
        self.file_map = {}  # id -> file path
        self.obj_map = {}  # id -> object path

        self.self_cost = defaultdict(int)
        self.incl_cost = defaultdict(int)

        self.call_graph = defaultdict(list)
        self.rev_call_graph = defaultdict(list)

        # 解析时状态
        self.current_fn_id = None
        self.current_fn_name = None
        self.current_obj_id = None
        self.current_obj_path = None
        self.current_file_id = None

        self.pending_callee_id = None
        self.pending_callee_name = None
        self.pending_callee_count = 0
        self.pending_callee_cost = 0

        # 地址解析缓存
        self.addr_cache = {}

        # 正则
        self.re_fn = re.compile(r"^fn=\((\d+)\)\s*(.*)")
        self.re_fl = re.compile(r"^fl=\((\d+)\)\s*(.*)")
        self.re_ob = re.compile(r"^ob=\((\d+)\)\s*(.*)")
        self.re_cfn = re.compile(r"^cfn=\((\d+)\)\s*(.*)")
        self.re_calls = re.compile(r"^calls=(\d+)\s+(\S+)\s+(\d+)")
        self.re_summary = re.compile(r"^summary:\s*(\d+)")
        self.re_events = re.compile(r"^events:\s*(.+)")
        self.re_cost = re.compile(r"^([0-9a-fA-Fx\+\-\*]+\b)?\s+(\d+(?:\s+\d+)*)\s*$")
        self.re_hex_addr = re.compile(r"^0x[0-9a-fA-F]+$")

    def resolve_addr(self, addr: str, obj_path: str) -> str:
        """用 addr2line 解析地址，失败返回原地址"""
        if not obj_path or obj_path == "???" or not addr:
            return addr
        cache_key = (obj_path, addr)
        if cache_key in self.addr_cache:
            return self.addr_cache[cache_key]
        try:
            # 使用 -f -C -e 获取函数名
            result = subprocess.run(
                [self.addr2line_bin, "-f", "-C", "-e", obj_path, addr],
                capture_output=True,
                text=True,
                timeout=2,
            )
            if result.returncode == 0 and result.stdout.strip():
                lines = result.stdout.strip().splitlines()
                if len(lines) >= 1 and lines[0] != "??":
                    name = lines[0].strip()
                    self.addr_cache[cache_key] = name
                    return name
        except Exception:
            pass
        self.addr_cache[cache_key] = addr
        return addr

    def parse(self):
        if not self.file.exists():
            print(f"错误: 文件不存在 - {self.file}")
            return

        with open(self.file, "r", encoding="utf-8") as f:
            for line in f:
                line = line.strip()
                if not line:
                    continue

                if line.startswith("cmd:"):
                    print(f"程序命令: {line[4:].strip()}")
                    continue

                m = self.re_summary.match(line)
                if m:
                    self.total_summary = int(m.group(1))
                    continue

                m = self.re_events.match(line)
                if m:
                    self.num_events = len(m.group(1).split())
                    continue

                # 目标文件
                m = self.re_ob.match(line)
                if m:
                    oid = int(m.group(1))
                    name = m.group(2).strip()
                    self.obj_map[oid] = name
                    self.current_obj_id = oid
                    self.current_obj_path = name if name != "???" else None
                    continue

                # 源文件
                m = self.re_fl.match(line)
                if m:
                    fid = int(m.group(1))
                    name = m.group(2).strip()
                    self.file_map[fid] = name
                    self.current_file_id = fid
                    continue

                # 函数定义
                m = self.re_fn.match(line)
                if m:
                    fn_id = int(m.group(1))
                    name_part = m.group(2).strip()
                    self.current_fn_id = fn_id
                    # 记录所属对象
                    self.fn_obj[fn_id] = self.current_obj_path

                    if name_part:
                        # 如果是纯十六进制地址，尝试解析
                        if self.re_hex_addr.match(name_part) and self.current_obj_path:
                            resolved = self.resolve_addr(
                                name_part, self.current_obj_path
                            )
                            self.fn_map[fn_id] = resolved
                            self.current_fn_name = resolved
                        else:
                            self.fn_map[fn_id] = name_part
                            self.current_fn_name = name_part
                    else:
                        # 无名字，先看历史映射
                        if fn_id in self.fn_map:
                            self.current_fn_name = self.fn_map[fn_id]
                        else:
                            # 尝试从对象+地址解析？此时没有地址，只能给匿名
                            self.current_fn_name = f"fn_{fn_id}"
                            self.fn_map[fn_id] = self.current_fn_name
                    continue

                # 被调用函数目标
                m = self.re_cfn.match(line)
                if m:
                    callee_id = int(m.group(1))
                    name_part = m.group(2).strip()
                    self.pending_callee_id = callee_id
                    if name_part:
                        if self.re_hex_addr.match(name_part) and self.fn_obj.get(
                            callee_id
                        ):
                            # 尝试解析调用目标的地址
                            resolved = self.resolve_addr(
                                name_part, self.fn_obj[callee_id]
                            )
                            self.pending_callee_name = resolved
                            self.fn_map[callee_id] = resolved
                        else:
                            self.pending_callee_name = name_part
                            self.fn_map[callee_id] = name_part
                    else:
                        self.pending_callee_name = self.fn_map.get(
                            callee_id, f"fn_{callee_id}"
                        )
                    continue

                # 调用次数
                m = self.re_calls.match(line)
                if m:
                    count = int(m.group(1))
                    self.pending_callee_count = count
                    # 尝试提取成本（第三个字段）
                    parts = line.split()
                    if len(parts) >= 3:
                        try:
                            self.pending_callee_cost = int(parts[2])
                        except ValueError:
                            self.pending_callee_cost = 0
                    else:
                        self.pending_callee_cost = 0

                    if self.current_fn_name and self.pending_callee_name:
                        edge = CallEdge(
                            self.current_fn_name,
                            self.pending_callee_name,
                            self.pending_callee_count,
                            self.pending_callee_cost,
                        )
                        self.call_graph[self.current_fn_name].append(edge)
                        self.rev_call_graph[self.pending_callee_name].append(
                            self.current_fn_name
                        )
                    continue

                # 成本行
                m = self.re_cost.match(line)
                if m:
                    cost_numbers = list(map(int, m.group(2).split()))
                    if not cost_numbers:
                        continue
                    if self.event_index < len(cost_numbers):
                        cost = cost_numbers[self.event_index]
                    else:
                        cost = 0
                    if self.current_fn_name:
                        self.self_cost[self.current_fn_name] += cost
            # 结束文件读取

    def compute_inclusive_costs(self):
        visited = set()

        def dfs(func):
            if func in visited:
                return self.incl_cost.get(func, 0)
            visited.add(func)
            total = self.self_cost.get(func, 0)
            for edge in self.call_graph.get(func, []):
                total += dfs(edge.callee)
            self.incl_cost[func] = total
            return total

        for func in list(self.self_cost.keys()):
            if func not in visited:
                dfs(func)

    def report(self, max_mem_lines=30):
        self.compute_inclusive_costs()

        print("\n" + "=" * 80)
        print(f"总执行指令数 (Summary): {self.total_summary:,}")
        print("=" * 80)

        # 1. 包含成本 Top 50
        print("\n>>> 1. 包含成本 (Inclusive Ir) Top 50 热点函数")
        sorted_incl = sorted(self.incl_cost.items(), key=lambda x: x[1], reverse=True)
        for i, (name, cost) in enumerate(sorted_incl[:50], 1):
            print(f"{i:3}. {cost:>18,}  {name}")

        # 2. 自身成本 Top 30
        print("\n>>> 2. 自身成本 (Self Ir) Top 30 热点函数")
        sorted_self = sorted(self.self_cost.items(), key=lambda x: x[1], reverse=True)
        for i, (name, cost) in enumerate(sorted_self[:30], 1):
            print(f"{i:3}. {cost:>18,}  {name}")

        # 3. 最热调用边 Top 20
        print("\n>>> 3. 最热调用边 (Top 20)")
        edge_agg = defaultdict(int)
        for caller, edges in self.call_graph.items():
            for edge in edges:
                edge_agg[(caller, edge.callee)] += self.incl_cost.get(edge.callee, 0)
        sorted_edges = sorted(edge_agg.items(), key=lambda x: x[1], reverse=True)
        for i, ((caller, callee), cost) in enumerate(sorted_edges[:20], 1):
            print(f"{i:3}. {cost:>15,}  {caller}  ->  {callee}")

        # 4. 内存相关操作 Top N
        print(f"\n>>> 4. 内存相关操作 (Self Ir) - Top {max_mem_lines}")
        sus_kw = ["reserve", "allocate", "malloc", "_M_allocate", "new"]
        count = 0
        for name, cost in sorted_self:
            if any(k in name.lower() for k in sus_kw):
                print(f"{cost:>18,}  {name}")
                count += 1
                if count >= max_mem_lines:
                    break
        if count == 0:
            print("未检测到明显的内存分配热点。")


if __name__ == "__main__":
    import argparse

    parser = argparse.ArgumentParser(description="Callgrind 解析器")
    parser.add_argument("file", help="Callgrind 输出文件")
    parser.add_argument(
        "--event-index", type=int, default=0, help="事件索引 (默认0=Ir)"
    )
    parser.add_argument("--max-mem", type=int, default=30, help="内存操作最大显示条数")
    parser.add_argument("--addr2line", default="addr2line", help="addr2line 工具路径")
    args = parser.parse_args()

    analyzer = CallgrindAnalyzer(Path(args.file), args.event_index, args.addr2line)
    analyzer.parse()
    analyzer.report(args.max_mem)
