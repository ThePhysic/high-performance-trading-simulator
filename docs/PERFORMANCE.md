# Performance methodology

The program reports actual synthetic-workload measurements and preserves logical behavior across execution modes. No throughput target, microsecond guarantee, improvement percentage, or Linux result is implied by the implementation or these instructions. Consult [reports/VALIDATION.md](../reports/VALIDATION.md) and retained report artifacts for what was actually executed.

## Build and collect a comparison

Use a Release build when measuring optimized behavior, and keep Debug/sanitizer results separately:

```sh
cmake -S . -B build/release -DCMAKE_BUILD_TYPE=Release
cmake --build build/release --parallel
ctest --test-dir build/release --output-on-failure

python3 tools/generate_workload.py --output-dir build/workload --profile mixed --count 10000 --seed 42
./build/release/hpts bench --config build/workload/accounts.cfg --input build/workload/commands.txt --mode direct --output build/workload/direct.json
./build/release/hpts bench --config build/workload/accounts.cfg --input build/workload/commands.txt --mode queued --output build/workload/queued.json
```

`direct` executes on the calling thread. `queued` separates a producer, one authoritative execution worker, and a canonical-output consumer through bounded blocking queues. There is still one book writer. The comparison tests the cost/benefit of that topology on equivalent business work; concurrency is not assumed faster. Queue pressure can increase tail latency even if some work overlaps.

Both modes use the same engine and canonical serialization. They report logical trace and final-state digests. After file execution and exact replay comparison, the benchmark suite independently fingerprints the actual event/state files and requires every warmup and measured sample to match that fixed baseline. A digest alone is not proof of collision-free equality or of matching correctness, which comes from separate reference/invariant tests.

## What each number means

| Report field | Measurement and boundary |
|---|---|
| `commands` / `commands_per_second` | All processed commands, including rejections, divided by the timed interval |
| `accepted_orders` / `accepted_orders_per_second` | Accepted original submit commands, including fully crossing submissions; replacements/cancels/controls are excluded |
| `accepted_commands` / `rejected_commands` | Business acceptance/rejection counts across all command types |
| `executions` / `executions_per_second` | Maker/taker trade pairs, counted once per `Trade` event |
| `core_latency_ns` | p50/p95/p99 elapsed nanoseconds around `Engine::process`, including validation, ledgers, allocation, matching, and typed event construction |
| `end_to_end_latency_ns` | p50/p95/p99 from immediately before admission through canonical outcome serialization and trace digest update, entirely in-process |
| `resources.process_cpu_seconds` | Process user+kernel CPU seconds over the benchmark execution interval |
| `resources.peak_rss_bytes` | Process peak resident/working-set memory, including the harness, preloaded commands, samples, ledgers, and final snapshot/serialization |
| `resources.allocations_per_command` | `null`: allocation counts are not instrumented |
| `queue` | Configured capacity, ingress/egress high-water marks, pushes encountering a full queue, and explicit zero dropped commands |

Percentiles use nearest-rank selection on all per-command samples, without sampling or tail extrapolation. Clock measurements include their instrumentation overhead.

The timed interval includes outcome serialization/digest processing. Queued timing also includes worker/sink creation and shutdown. Parsing and preloading the input file, initial engine creation, final invariant traversal, final-state generation, percentile sorting, and report writing occur outside the throughput interval. The reported peak memory is process-wide rather than a core-only incremental allocation measurement; it can include work outside that interval. CPU seconds use the platform process API (`getrusage` on POSIX, `GetProcessTimes` on Windows), so multiple active threads can accumulate more CPU time than elapsed wall time.

Direct full-path latency includes core processing and serialization. Queued full-path latency additionally includes blocking ingress admission, queue waits, execution handoff, and output-queue delay. Neither includes original file parsing, network transport, client processes, or durable output. Unqualified comparisons with published exchange/network latency would therefore be misleading.

A standalone `bench` invocation has `warmup_commands: 0`. The suite runs one unreported complete invocation per mode before measured repetitions. Each invocation starts a new engine/process, so this does not become an internal engine warmup phase or eliminate cold-start effects within a measured run.

The final invariant traversal checks the resulting state outside the timed interval. Full per-step invariant/reference checking belongs to the correctness suite; the benchmark does not silently replace the matching/risk rules with approximate behavior.

## Eight synthetic profiles

The generator writes exactly the requested number of commands, initial account configuration, and `workload.json` with seed, generator/Python version, and SHA-256 input fingerprints. It streams generation and retains at most 128 pending generated IDs. Counts are restricted to 1..1,000,000. Selected output files are overwritten.

| Profile | Workload shape |
|---|---|
| `resting` | Growing buy-side book across a bounded range of prices |
| `crossing` | Alternating new resting buys and matching sells |
| `cancel` | Submission followed by cancellation |
| `replace` | Submission, priority-losing modification, then cancellation |
| `dense` | Many resting orders at four price levels |
| `sparse` | Resting orders across a much wider price range |
| `burst` | Blocks of 128 submissions followed by their cancellations |
| `mixed` | Seeded blocks combining fills, cancellations, replacements, and partial fills |

All input is synthetic. The file has no wall-clock arrival model; `burst` changes order composition/ordering rather than claiming a particular requests-per-second arrival schedule. Actual queued occupancy and blocking are measured from execution.

## Repeated suite and artifacts

```sh
python3 tools/benchmark_suite.py --binary ./build/release/hpts --output-dir build/benchmarks --count 10000 --seed 42 --repetitions 3 --profiles resting crossing cancel replace dense sparse burst mixed
```

For each profile the suite:

1. Generates a configuration, commands, and manifest.
2. Runs file execution and verifies a complete self-contained journal against exact event/state files.
3. Runs one discarded invocation per selected mode.
4. Collects the requested benchmark repetitions and requires equal logical fingerprints.
5. Preserves raw reports and mean/min/max/population-standard-deviation summaries in `suite.json`.

The suite's default profile list omits `mixed`; the command above explicitly includes all eight. `--modes`, `--queue-capacity`, `--timeout`, and other options are documented by `--help`. A mode comparison should use the same workload/configuration, seed, compiler/build, and repetitions.

An interrupted or failed suite retains completed measurements but remains `complete: false`; it becomes `true` only after every requested workload succeeds. The tooling does not turn missing data into zeros or label a failed invocation a benchmark result.

Preserve the full environment with published numbers: CPU model and affinity, OS/kernel, compiler and optimization flags, build type, binary fingerprint, thread topology, queue capacities, workload composition/count/seed, run duration, and variability. The suite collects platform/kernel, available CPU details/affinity, Python, binary SHA-256/version, and tool-version output. The CLI records its compiler/platform and available hardware thread count. An arbitrary available `c++ --version` does not prove which compiler built the measured executable; retain the build record/CMake cache as evidence. No affinity, CPU isolation, cache flushing, or NUMA policy is applied automatically.

## Actual Linux validation and profiling

The helper requires an accessible Linux runtime, CMake, a C++20 compiler with the requested sanitizer support, and Python 3.10+:

```sh
sh tools/validate_linux.sh
HPTS_COUNT=10000 HPTS_REPETITIONS=3 sh tools/validate_linux.sh --tsan --perf
```

It builds/tests Debug, ASan/UBSan, and Release configurations and runs a repeated suite. `--tsan` adds a separate ThreadSanitizer build. `--perf` collects actual `perf stat`, sampling, and text-report artifacts for the queued burst case, and fails explicitly if `perf` or permissions are unavailable. Generated results are written under ignored `build/linux-reports/`; build outputs are under `build/linux-*`. Selected generated files are overwritten. Committed historical `reports/` artifacts are not modified. See the helper's `--help` for environment variables.

The helper's existence does not establish that a Linux runtime, sanitizer runtime, or profiler was accessible, or that any check passed. Native Windows verification remains labelled Windows evidence. A requested but unavailable Linux/profiler check is a limitation to resolve and record, not a successful check.

For source-level profiling, a separate `RelWithDebInfo` build can retain symbols:

```sh
cmake -S . -B build/profile -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build/profile --parallel
perf stat -- ./build/profile/hpts bench --config build/workload/accounts.cfg --input build/workload/commands.txt --mode direct
perf record -g -- ./build/profile/hpts bench --config build/workload/accounts.cfg --input build/workload/commands.txt --mode direct
perf report
```

A defensible optimization report starts from a measured baseline, identifies a concrete bottleneck using actual profiling, changes a justified design, verifies equivalent canonical behavior, and repeats identical workloads. Include CPU/memory/p99 regressions along with any throughput gain. Direct/queued mode results are useful design evidence, but do not by themselves establish a specific allocation/cache bottleneck or an optimization speedup. Hardware counters, syscall counts, allocations, and cache/branch behavior require additional actual instrumentation before making those claims.

## Reproduce the serializer experiment

The retained [Linux baseline profile](../reports/linux/profile-baseline/README.md) identifies canonical outcome formatting as a material cost. Commit `fec21af` is the pre-optimization baseline. The candidate changes only outcome formatting to direct escaping/string append and integer `to_chars`; matching, risk, and canonical bytes are preserved. Independent byte fixtures cover numeric extremes and escaping, and the comparison tool checks complete generated event/state/journal equality.

```sh
mkdir -p build
git worktree add --detach build/baseline-source fec21af
cmake -S build/baseline-source -B build/baseline -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCMAKE_CXX_FLAGS=-fno-omit-frame-pointer
cmake --build build/baseline --parallel 4
cmake -S . -B build/candidate -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCMAKE_CXX_FLAGS=-fno-omit-frame-pointer
cmake --build build/candidate --parallel 4
python3 tools/compare_binaries.py --baseline build/baseline/hpts --candidate build/candidate/hpts --output-dir build/comparison --count 100000 --seed 42 --repetitions 5
```

This experiment compares the same compiler, flags, input, timing boundaries, and execution topology. It attributes a measured gain or regression to the serialization change, without claiming faster matching. Five interleaved repetitions and raw CPU/memory/latency reports support assessing variability. Frame-pointer sampling and full-process hardware counts include work outside the CLI's timed execution interval; their scope must remain separate from throughput/core latency. The validation record identifies the actual runs and limitations.

In WSL, use a persistent Linux filesystem for build directories and `perf.data`. This session's `/tmp` build disappeared between invocations, and direct `perf record` mmap output to `/mnt/c` failed. Source and text reports may remain on the Windows-mounted path. The recorded WSL perf lacks ELF/DWARF support; the baseline profile retains raw sample/mmap records, symbol tables, address-to-source mappings, and an independently reproducible symbolizer rather than pretending those reports have native symbol resolution.
