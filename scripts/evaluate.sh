#!/bin/bash
# scripts/evaluate.sh

# locate script directory
script_dir=$(cd "$(dirname "$0")" && pwd)

# locate project root
project_root=$(cd "$script_dir/.." && pwd)
cd "$project_root"

# 默认值
build_dir="build-wsl"
output_dir="$project_root/logs/valgrind"
leak_check=0
program_args=""

# 显示帮助
show_help() {
    cat <<EOF
用法: $0 [build_dir] [options] [-- <program_args>]

位置参数:
  build_dir          可选，构建目录

选项:
  -h, --help          显示帮助
  -o, --output DIR    指定输出目录
  -c, --check         开启泄漏检查

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
        -o|--output)
            if [ -z "$2" ]; then
                echo "错误：-o/--output 需要参数" >&2
                exit 1
            fi
            output_dir="$2"
            shift 2
            ;;
        -c|--check)
            leak_check=1
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

# create output directory
if [ ! -d "$output_dir" ]; then
    mkdir -p "$output_dir"
fi

# configure project
if [ ! -d "$build_dir" ]; then
    cmake -S . -B "$build_dir" -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_COMPILER=clang++
fi

# build project
cmake --build "$build_dir" --config Debug --target forge_benchmark

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
(
    valgrind --tool=callgrind --dump-instr=yes \
        --callgrind-out-file="$output_dir/callgrind.out" \
        "$build_dir/bin/forge_benchmark" -- $program_args
    python3 scripts/parse_callgrind.py "$output_dir/callgrind.out" > "$output_dir/callgrind.out.brief.log"
) 2>&1 > "$output_dir/callgrind_program_out.log" &

# 3. Massif 堆内存分析 + 打印
(
    valgrind --tool=massif --massif-out-file="$output_dir/massif.out" \
        "$build_dir/bin/forge_benchmark" -- $program_args
    ms_print "$output_dir/massif.out" > "$output_dir/massif.out.ms_print"
    python3 scripts/parse_massif.py "$output_dir/massif.out.ms_print" > "$output_dir/massif.out.brief.log"
) 2>&1 > "$output_dir/massif_program_out.log" &

# 等待所有后台任务完成
wait

echo "所有 Valgrind 任务已完成，输出文件位于 $output_dir"

# benchmark
if [ -f "$output_dir/benchmark.log" ]; then
    mv "$output_dir/benchmark.log" "$output_dir/benchmark.log.bak"
fi
"$build_dir/bin/forge_benchmark" $program_args 2>&1 | tee "$output_dir/benchmark.log"
