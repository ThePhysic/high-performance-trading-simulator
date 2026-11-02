# Workloads and measurements

All tools use Python 3.10 or later and its standard library. Workloads are synthetic and seeded; they do not represent exchange feeds or predict market behavior.

```sh
python3 tools/generate_workload.py --output-dir build/workload --profile mixed --count 10000 --seed 42
./build/hpts run --config examples/accounts.cfg --input examples/demo.commands --output build/demo.events --state build/demo.state --journal build/demo.journal
./build/hpts replay --journal build/demo.journal --expect build/demo.events --expect-state build/demo.state
python3 tools/benchmark_suite.py --binary ./build/hpts --output-dir reports/local --count 10000 --repetitions 3
```

The generator writes exactly the requested number of commands, a bounded-resource account configuration, and a manifest with seed, Python version, and SHA-256 fingerprints. Generation streams commands and holds at most 128 order IDs locally. Counts are capped at one million. Output files in the selected directory are overwritten.

Profiles separate resting growth, crossing fills, cancellations, modifications, dense or sparse price levels, and bulk submission/cancellation ordering. `mixed` combines crossing, cancellation, modification, and partial-fill blocks. `burst` changes ordering; files contain no wall-clock arrival model. Queue occupancy depends on the actual benchmark producer/consumer execution.

The suite first runs and replays each workload using a self-contained journal and exact expected events/state. It then performs one unreported warmup per mode and collects actual benchmark reports over the requested repetitions. All timing samples must agree on logical trace and final-state digests. `suite.json` preserves raw reports and mean/min/max/population-standard-deviation summaries. Direct versus queued runs compare execution topology on identical business inputs; the script makes no assumption that concurrency improves performance.

The environment record includes the binary fingerprint, OS/kernel, available CPU details/affinity, Python, and tool-version output. An available host `c++` command does not prove which compiler built the executable; preserve build configuration and the CLI's compiler metadata with published results. Timings use the boundaries declared by the CLI and do not measure network latency. Resource fields that are unavailable must remain explicitly unavailable.

Use `--help` for generator and suite options. A failed subprocess, replay mismatch, missing report field, command count mismatch, or inconsistent benchmark digest stops the suite with a nonzero exit status. Completed workload results are retained if a later workload fails; `suite.json` has `complete: true` only after every requested workload finishes. The scripts report measured results only and do not assert a throughput target.

On an actual Linux runtime, `sh tools/validate_linux.sh` builds and tests Debug, ASan/UBSan, and Release configurations before collecting the suite. `--tsan` adds a separate thread-sanitized build; `--perf` records real `perf stat`, sample, and report artifacts, failing explicitly if access is unavailable. Environment variables listed in `--help` control workload size and repetitions. The helper's presence is not evidence that Linux checks have run.
