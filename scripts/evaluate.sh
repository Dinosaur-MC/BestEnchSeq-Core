#!/bin/bash
# scripts/evaluate.sh

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
benchmark_check=0
program_args=""

# 显示帮助
show_help() {
    cat <<EOF
用法: $0 [build_dir] [options] [-- <program_args>]

位置参数:
  build_dir          可选，构建目录

选项:
  -h, --help          显示帮助
  -b, --build         重新构建项目
  -o, --output DIR    指定输出目录
  -a, --all           开启所有分析工具
  -c, --check         开启泄漏检查
  --callgrind         开启 Callgrind 性能分析
  --massif            开启 Massif 堆内存分析
  --benchmark         开启基准测试

分隔符:
  --                  其后的所有参数将原样收集到 <program_args>
EOF
}

while [ $# -gt 0 ]; do
    case "$1" in
        -h|--help)
            show_help
            exit 0
            ;;
        -b|--build)
            build_project=1
            shift
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
            benchmark_check=1
            shift
            ;;
        -c|--check)
            leak_check=1
            shift
            ;;
        --callgrind)
            callgrind_check=1
            shift
            ;;
        --massif)
            massif_check=1
            shift
            ;;
        -b|--benchmark)
            benchmark_check=1
            shift
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

# check jobs
if [ $leak_check -eq 0 -a $callgrind_check -eq 0 -a $massif_check -eq 0 -a $benchmark_check -eq 0 ]; then
    echo "未选择分析工具。请使用 -a 或 -c/--check, --callgrind, --massif, -b/--benchmark." >&2
    exit 1
fi

# create output directory
if [ ! -d "$output_dir" ]; then
    mkdir -p "$output_dir"
fi

# configure project
if [ ! -d "$build_dir" ]; then
    cmake -S . -B "$build_dir" -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_COMPILER=clang++
fi

# build project
if [ $build_project -eq 1 ]; then
    cmake --build "$build_dir" --config Debug --target forge_benchmark
fi

# ----------------------------------------------
# 并行执行三个 Valgrind 任务（各自写入独立输出文件）
# ----------------------------------------------

# 1. 内存泄漏检测 (leak check)
if [ $leak_check -eq 1 ]; then
    valgrind --leak-check=full --show-leak-kinds=all --track-origins=yes --verbose \
    --log-file="$output_dir/valgrind.log" \
    "$build_dir/bin/forge_benchmark" -- $program_args \
    2>&1 > "$output_dir/valgrind_program_out.log" &
fi

# 2. Callgrind 性能分析 + 解析
if [ $callgrind_check -eq 1 ]; then
    (
        valgrind --tool=callgrind --dump-instr=yes \
            --callgrind-out-file="$output_dir/callgrind.out" \
            "$build_dir/bin/forge_benchmark" -- $program_args
        python3 scripts/parse_callgrind.py "$output_dir/callgrind.out" > "$output_dir/callgrind.out.brief.log"
    ) 2>&1 > "$output_dir/callgrind_program_out.log" &
fi

# 3. Massif 堆内存分析 + 打印
if [ $massif_check -eq 1 ]; then
    (
        valgrind --tool=massif --massif-out-file="$output_dir/massif.out" \
            "$build_dir/bin/forge_benchmark" -- $program_args
        ms_print "$output_dir/massif.out" > "$output_dir/massif.out.ms_print"
        python3 scripts/parse_massif.py "$output_dir/massif.out.ms_print" > "$output_dir/massif.out.brief.log"
    ) 2>&1 > "$output_dir/massif_program_out.log" &
fi

# 等待所有后台任务完成
wait

# benchmark
if [ $benchmark_check -eq 1 ]; then
    if [ -f "$output_dir/benchmark.log" ]; then
        mv "$output_dir/benchmark.log" "$output_dir/benchmark.txt.bak"
    fi
    "$build_dir/bin/forge_benchmark" $program_args 2>&1 | tee "$output_dir/benchmark.txt"
fi
