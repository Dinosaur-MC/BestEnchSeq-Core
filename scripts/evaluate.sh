#!/bin/bash
# scripts/evaluate.sh

# exit on fail
set -e

# locate script directory
script_dir=$(cd "$(dirname "$0")" && pwd)

# locate project root
project_root=$(cd "$script_dir/.." && pwd)
cd "$project_root"

# 默认值
build_dir="build-wsl"
build_project=0
output_dir="$project_root/logs/valgrind"
leak_check=0
callgrind_check=0
massif_check=0
cachegrind_check=0
benchmark_check=0
program_args=""
build_type=""
benchmark_dir="$project_root/logs/benchmarks"

# 显示帮助
show_help() {
    cat <<EOF
用法: $0 [build_dir] <options> [-- <program_args>]

位置参数:
  build_dir             可选，构建目录

选项:
  -h, --help            显示帮助
  -B, --build TYPE      重新构建项目（Debug/Release）
  -o, --output DIR      指定输出目录（默认 $output_dir）
  -a, --all             开启所有分析工具（不包括benchmark）
  -l, --leak_check      开启泄漏检查
  -p, --callgrind       开启 Callgrind 性能分析
  -m, --massif          开启 Massif 堆内存分析
  -c, --cachegrind      开启 CacheGrind 缓存分析
  -b, --benchmark [DIR] 开启基准测试（并指定目录，默认 $benchmark_dir）

分隔符:
  --                    其后的所有参数将原样收集到 <program_args>
EOF
}

while [ $# -gt 0 ]; do
    case "$1" in
        -h|--help)
            show_help
            exit 0
            ;;
        -B|--build)
            build_project=1
            if [ -z "$2" ]; then
                echo "错误：-B/--build 需要参数" >&2
                exit 1
            fi
            if [ "$2" != "Debug" -a "$2" != "Release" ]; then
                echo "错误：-B/--build 参数必须为 Debug 或 Release" >&2
                exit 1
            fi
            build_type="$2"
            shift 2
            ;;
        -o|--output)
            if [ -z "$2" ]; then
                echo "错误：-o/--output 需要参数" >&2
                exit 1
            fi
            output_dir="$2"
            shift 2
            ;;
        -a|--all)
            leak_check=1
            callgrind_check=1
            massif_check=1
            cachegrind_check=1
            shift
            ;;
        -l|--leak_check)
            leak_check=1
            shift
            ;;
        -p|--callgrind)
            callgrind_check=1
            shift
            ;;
        -m|--massif)
            massif_check=1
            shift
            ;;
        -c|--cachegrind)
            cachegrind_check=1
            shift
            ;;
        -b|--benchmark)
            benchmark_check=1
            if [ "$2" != "" -a -d "$2" ]; then
                benchmark_dir="$2"
                shift 2
            else
                shift
            fi
            ;;
        --)
            # 遇到 --，其后的所有参数都属于 program_args
            shift
            program_args=("$@")
            break
            ;;
        -*)
            echo "未知选项: $1" >&2
            exit 1
            ;;
        *)
            # 第一个未被识别的参数视为 build_dir（若尚未设置）
            build_dir="$1"
            shift
            ;;
    esac
done

# 剩下的全部当作 program_args
program_args="$@"

# build/check jobs
if [ $build_project -eq 0 -a $leak_check -eq 0 -a $callgrind_check -eq 0 -a $massif_check -eq 0 -a $cachegrind_check -eq 0 -a $benchmark_check -eq 0 ]; then
    echo "未选择分析工具。请使用 -a/--all 或 -l/--leak_check, -p/--callgrind, -m/--massif, -c/--cachegrind, -b/--benchmark." >&2
    show_help
    exit 1
fi

# create output directory
if [ ! -d "$output_dir" ]; then
    mkdir -p "$output_dir"
fi

# configure project
if [ ! -d "$build_dir" -o "$build_type" != "" ]; then
    cmake -S . -B "$build_dir" -G Ninja -DCMAKE_BUILD_TYPE="$build_type" -DCMAKE_CXX_COMPILER=clang++ -DBUILD_BENCHMARKS=ON
    cmake -S plugins -B build-wsl/plugins -G Ninja -DCMAKE_BUILD_TYPE="$build_type" -DCMAKE_CXX_COMPILER=clang++
fi

# build project
if [ $build_project -eq 1 ]; then
    cmake --build "$build_dir" --config "$build_type" --target forge_benchmark
    cmake --build build-wsl/plugins --config "$build_type" --target all
fi

# ----------------------------------------------
# 并行执行四个 Valgrind 任务（各自写入独立输出文件）
# ----------------------------------------------

# 1. 内存泄漏检测 (leak check)
if [ $leak_check -eq 1 ]; then
    valgrind --leak-check=full --show-leak-kinds=all --track-origins=yes --verbose \
    --log-file="$output_dir/valgrind.log" \
    -- "$build_dir/bin/forge_benchmark" --algo-dir "$build_dir/plugins" $program_args \
    2>&1 > "$output_dir/valgrind_program_out.txt" &
fi

# 2. Callgrind 性能分析 + 解析
if [ $callgrind_check -eq 1 ]; then
    (
        valgrind --tool=callgrind --dump-instr=yes \
            --callgrind-out-file="$output_dir/callgrind.out" \
            -- "$build_dir/bin/forge_benchmark" --algo-dir "$build_dir/plugins" $program_args
        python3 scripts/parse_callgrind.py "$output_dir/callgrind.out" > "$output_dir/callgrind.out.brief.log"
    ) 2>&1 > "$output_dir/callgrind_program_out.txt" &
fi

# 3. Massif 堆内存分析 + 打印
if [ $massif_check -eq 1 ]; then
    (
        valgrind --tool=massif --massif-out-file="$output_dir/massif.out" \
            -- "$build_dir/bin/forge_benchmark" --algo-dir "$build_dir/plugins" $program_args
        ms_print "$output_dir/massif.out" > "$output_dir/massif.out.ms_print"
        python3 scripts/parse_massif.py "$output_dir/massif.out.ms_print" > "$output_dir/massif.out.brief.log"
    ) 2>&1 > "$output_dir/massif_program_out.txt" &
fi

# 4. CacheGrind 缓存分析 + 打印
if [ $cachegrind_check -eq 1 ]; then
    (
        valgrind --tool=cachegrind --cache-sim=yes --cachegrind-out-file="$output_dir/cachegrind.out" \
            -- "$build_dir/bin/forge_benchmark" --algo-dir "$build_dir/plugins" $program_args
        python3 scripts/parse_cachegrind.py "$output_dir/cachegrind.out" > "$output_dir/cachegrind.out.brief.log"
    ) 2>&1 > "$output_dir/cachegrind_program_out.txt" &
fi

# 等待所有后台任务完成
wait

# benchmark
if [ $benchmark_check -eq 1 ]; then
    if [ -f "$benchmark_dir/benchmark.txt" ]; then
        mv "$benchmark_dir/benchmark.txt" "$benchmark_dir/benchmark.txt.bak"
    fi
    echo "Benchmark running in $build_type build with program args: $program_args" | tee "$benchmark_dir/benchmark.txt"
    # --json：文本（benchmark.txt，人类日志 + 回退解析）与纯 JSON
    # （benchmark.json，解析层 JSON 优先路径）双输出
    "$build_dir/bin/forge_benchmark" --algo-dir "$build_dir/plugins" $program_args --json 2>&1 \
        | tee -a "$benchmark_dir/benchmark.txt" > "$benchmark_dir/fb_full.txt"
    awk 'f{print} /^=== Done ===$/{f=1}' "$benchmark_dir/fb_full.txt" > "$benchmark_dir/benchmark.json"
    rm -f "$benchmark_dir/fb_full.txt"
    python3 scripts/bench_report.py "$benchmark_dir/benchmark.json" "$benchmark_dir" --img
fi
