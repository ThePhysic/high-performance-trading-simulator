# High-Performance Trading Simulator

A C++20 event-processing engine and miniature electronic exchange, operated through a local terminal. The engine combines authoritative command sequencing, price-time matching, simulated account/risk controls, bounded processing, and reproducible logical replay. Linux is the intended execution and measurement platform; the implementation also supports native MSVC builds.

This is a systems simulator using synthetic accounts and orders. Live trading, real funds, strategy research, a web interface, and cloud infrastructure are outside scope. TCP ingress and durable crash recovery are optional and are not implemented.

## Build and demonstrate on Linux

Prerequisites: CMake 3.20+, a C++20 compiler, thread support, and Python 3.10+ for the complete test suite and workload tools.

```sh
cmake -S . -B build/debug -DCMAKE_BUILD_TYPE=Debug
cmake --build build/debug --parallel
ctest --test-dir build/debug --output-on-failure

mkdir -p build/demo
./build/debug/hpts run --config examples/accounts.cfg --input examples/demo.commands --output build/demo/events.txt --state build/demo/state.txt --journal build/demo/journal.txt
./build/debug/hpts replay --journal build/demo/journal.txt --expect build/demo/events.txt --expect-state build/demo/state.txt
./build/debug/hpts interactive --config examples/accounts.cfg
```

The example demonstrates FIFO and partial fills, replacement and cancellation, aggregate risk rejection, kill-switch behavior, execution at the resting price, and explicit unknown-order/self-trade rejection. Replay compares exact canonical event and state bytes as well as completion fingerprints. The journal includes configuration, normalized commands, and a required completion footer.

These are reproducible instructions, not a claim that Linux validation has already run. Read the [validation record](reports/VALIDATION.md) for actual executed checks, environment, and remaining limitations.

## What is implemented

- Limit buys/sells on `SIM`, best-price/FIFO matching, partial fills, cancellation, and documented remaining-quantity replacement rules.
- Checked integer prices, quantities, positions, and cash; conservative outstanding-order reservations and position limits.
- A sequenced kill switch, duplicate-command protection, terminal order IDs, and explicit finite-session capacity rejections.
- Single-writer state ownership, bounded producer/consumer queues, and deterministic typed/canonical outcomes.
- Interactive state inspection, file execution, self-contained replay verification, and direct/queued benchmarks.
- An independent vector-scan reference model, deterministic fixtures, seeded differential/invariant tests, queue tests, and CLI failure/replay checks.
- Eight seeded synthetic workload profiles, repeated benchmark collection, and an actual-Linux validation/sanitizer/`perf` helper.

No fixed throughput or latency target is promised. Benchmark results must retain their workload, build, platform, and timing boundaries; the presence of a helper script is not evidence that its checks passed.

## Documentation

| Document | Purpose |
|---|---|
| [Usage](docs/USAGE.md) | Build, terminal commands, configuration, journal/replay, and errors |
| [Design](docs/DESIGN.md) | Matching/risk rules, ownership, capacities, atomicity, and canonical schema |
| [Performance](docs/PERFORMANCE.md) | Metric boundaries, workload methodology, and Linux profiling |
| [Tooling](tools/README.md) | Generator, benchmark suite, and validation helper options |
| [Validation](reports/VALIDATION.md) | Actual verification evidence and known limitations |
| [Goal](GOAL.md) | Project acceptance criteria |
| [Original requirements](docs/requirements/README.md) | Preserved downloaded specification |
