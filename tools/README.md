# Workloads and measurements

All tools use Python 3.10 or later and its standard library. Workloads are synthetic and seeded; they do not represent exchange feeds or predict market behavior.

```sh
cmake -S . -B build/debug -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON
cmake --build build/debug --parallel 2
mkdir -p build/demo
./build/debug/hpts run --config examples/accounts.cfg --input examples/demo.commands --output build/demo/events.txt --state build/demo/state.txt --journal build/demo/journal.txt
./build/debug/hpts replay --journal build/demo/journal.txt --expect build/demo/events.txt --expect-state build/demo/state.txt

cmake -S . -B build/release -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
cmake --build build/release --parallel 2
python3 tools/generate_workload.py --output-dir build/workload --profile mixed --count 10000 --seed 42
./build/release/hpts bench --config build/workload/accounts.cfg --input build/workload/commands.txt --mode direct --output build/workload/direct.json
python3 tools/benchmark_suite.py --binary ./build/release/hpts --output-dir build/benchmarks --count 10000 --repetitions 3
```

The generator writes exactly the requested number of commands, a bounded-resource account configuration, and a manifest with seed, Python version, and SHA-256 fingerprints. Generation streams commands and holds at most 128 order IDs locally. Counts are capped at one million. Output files in the selected directory are overwritten.

Profiles separate resting growth, crossing fills, cancellations, modifications, dense or sparse price levels, and bulk submission/cancellation ordering. `mixed` combines crossing, cancellation, modification, and partial-fill blocks. `burst` changes ordering; files contain no wall-clock arrival model. Queue occupancy depends on the actual benchmark producer/consumer execution.

The suite first runs and replays each workload using a self-contained journal and exact expected events/state. It then performs one unreported warmup per mode and collects actual benchmark reports over the requested repetitions. All timing samples must agree on logical trace and final-state digests. `suite.json` preserves raw reports and mean/min/max/population-standard-deviation summaries. Direct versus queued runs compare execution topology on identical business inputs; the script makes no assumption that concurrency improves performance.

The environment record includes the binary fingerprint, OS/kernel, available CPU details/affinity, Python, and tool-version output. An available host `c++` command does not prove which compiler built the executable; preserve build configuration and the CLI's compiler metadata with published results. Timings use the boundaries declared by the CLI and do not measure network latency. Resource fields that are unavailable must remain explicitly unavailable.

Use `--help` for generator and suite options. A failed subprocess, replay mismatch, missing report field, command count mismatch, or inconsistent benchmark digest stops the suite with a nonzero exit status. Completed workload results are retained if a later workload fails; `suite.json` has `complete: true` only after every requested workload finishes. The scripts report measured results only and do not assert a throughput target.

On an actual Linux runtime, `sh tools/validate_linux.sh` builds and tests Debug, ASan/UBSan, and Release configurations before collecting the suite. `--tsan` adds a separate thread-sanitized build; `--perf` records real `perf stat`, sample, and report artifacts, failing explicitly if access is unavailable. Builds and generated reports stay under ignored `build/linux-*` and `build/linux-reports/`; selected generated files are overwritten, while committed `reports/` artifacts are untouched. Environment variables listed in `--help` control workload size and repetitions. Actual recorded results are linked in [Validation](../reports/VALIDATION.md).

## Compare two implementations

```sh
python3 tools/compare_binaries.py --baseline build/baseline/hpts --candidate build/candidate/hpts --output-dir build/comparison --count 100000 --repetitions 5
```

The comparator uses all eight profiles by default. Before measuring, it runs and replays each binary and requires exact event, state, and complete-journal byte equality. It also replays the original journal through the candidate. Warmup and measured benchmark fingerprints must match the independently fingerprinted original output files.

First create the two builds using the [serializer experiment instructions](../docs/PERFORMANCE.md#reproduce-the-serializer-experiment). Tools overwrite selected output files. Generated examples use ignored `build/` paths; preserve published reports separately when deliberately recording new evidence.

Measured invocations alternate baseline/candidate order on odd repetitions and candidate/baseline on even repetitions. Both binaries process the same generated input; the report retains binary/environment fingerprints, invocation order, raw reports, variability, and the candidate/baseline mean throughput ratio. This does not assume the candidate is faster. Inspect CPU, memory, and both latency distributions for regressions. The default mode is `direct`; `--mode queued` is available separately. A failed or interrupted comparison remains `complete: false`.
