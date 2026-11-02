#!/bin/sh
# Run real Linux checks and optionally perf. No results are supplied in advance.
set -eu

usage() {
    cat <<'EOF'
Usage: sh tools/validate_linux.sh [--tsan] [--perf]

Requires Linux, CMake, a C++20 compiler, and Python 3.10+. Runs Debug tests,
AddressSanitizer/UndefinedBehaviorSanitizer tests, Release tests, and a repeated
benchmark suite. --tsan additionally runs a separate ThreadSanitizer build.
--perf additionally collects perf stat/record/report for the queued burst case.
Optional checks fail explicitly if the runtime or perf permissions are missing.

Environment: HPTS_JOBS (default 2), HPTS_COUNT (default 10000),
HPTS_REPETITIONS (default 3), HPTS_SEED (default 42).
Outputs: build/linux-* and build/linux-reports/ under the project directory.
Generated outputs are overwritten; committed reports/ artifacts are unchanged.
EOF
}

run_tsan=0
run_perf=0
for argument in "$@"; do
    case "$argument" in
        --tsan) run_tsan=1 ;;
        --perf) run_perf=1 ;;
        --help|-h) usage; exit 0 ;;
        *) printf 'Unknown argument: %s\n' "$argument" >&2; usage >&2; exit 2 ;;
    esac
done

if [ "$(uname -s)" != Linux ]; then
    printf '%s\n' 'Linux validation requires a Linux runtime.' >&2
    exit 1
fi

project_root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$project_root"
jobs=${HPTS_JOBS:-2}
count=${HPTS_COUNT:-10000}
repetitions=${HPTS_REPETITIONS:-3}
seed=${HPTS_SEED:-42}
mkdir -p build/linux-reports
{
    uname -a
    cmake --version
    c++ --version
    python3 --version
    printf 'jobs=%s count=%s repetitions=%s seed=%s\n' "$jobs" "$count" "$repetitions" "$seed"
} > build/linux-reports/environment.txt 2>&1

cmake -S . -B build/linux-debug -DCMAKE_BUILD_TYPE=Debug
cmake --build build/linux-debug --parallel "$jobs"
ctest --test-dir build/linux-debug --output-on-failure

cmake -S . -B build/linux-asan -DCMAKE_BUILD_TYPE=Debug -DHPTS_SANITIZERS=address,undefined
cmake --build build/linux-asan --parallel "$jobs"
ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 ctest --test-dir build/linux-asan --output-on-failure

if [ "$run_tsan" -eq 1 ]; then
    cmake -S . -B build/linux-tsan -DCMAKE_BUILD_TYPE=Debug -DHPTS_SANITIZERS=thread
    cmake --build build/linux-tsan --parallel "$jobs"
    TSAN_OPTIONS=halt_on_error=1 ctest --test-dir build/linux-tsan --output-on-failure
fi

cmake -S . -B build/linux-release -DCMAKE_BUILD_TYPE=Release
cmake --build build/linux-release --parallel "$jobs"
ctest --test-dir build/linux-release --output-on-failure
python3 tools/benchmark_suite.py --binary build/linux-release/hpts --output-dir build/linux-reports/benchmarks --count "$count" --repetitions "$repetitions" --seed "$seed"

if [ "$run_perf" -eq 1 ]; then
    if ! command -v perf >/dev/null 2>&1; then
        printf '%s\n' 'Requested perf collection is unavailable: perf was not found.' >&2
        exit 1
    fi
    perf stat -o build/linux-reports/perf-stat.txt -- build/linux-release/hpts bench --config build/linux-reports/benchmarks/burst/accounts.cfg --input build/linux-reports/benchmarks/burst/commands.txt --mode queued --output build/linux-reports/perf-bench.json
    perf record -o build/linux-reports/perf.data -- build/linux-release/hpts bench --config build/linux-reports/benchmarks/burst/accounts.cfg --input build/linux-reports/benchmarks/burst/commands.txt --mode queued --output build/linux-reports/perf-record-bench.json
    perf report --stdio -i build/linux-reports/perf.data > build/linux-reports/perf-report.txt
fi

printf '%s\n' 'Linux validation commands completed successfully; inspect generated evidence and environment before making claims.'
