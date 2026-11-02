# Project scope and acceptance criteria

Provide a locally operated, Linux-oriented C++20 event-processing engine whose main workload is a miniature electronic exchange. A single authoritative writer owns the book and simulated accounts; terminal, file, and benchmark modes use the same business engine.

The implemented scope is judged by observable behavior and reproducible evidence:

- Interactive submit/cancel/replace and book/account inspection; best-price/FIFO matching, partial fills, and explicit replacement priority.
- Checked integer prices, quantities, positions, and cash; conservative outstanding-order reservations, limits, and sequenced kill controls.
- Complete command-file journals and replay with identical canonical events and final state under the same inputs/rules/configuration.
- Bounded handoffs and identity ledgers, clear state ownership, and explicit parse, duplicate, capacity, and output-failure behavior.
- Independent reference comparisons, deterministic fixtures, stateful invariant tests, meaningful sanitizer checks, and the existing CTest contracts in CI.
- Reproducible synthetic workloads and profiling-backed analysis of throughput, p50/p95/p99 latency, CPU, and memory, with precise timing/build/environment qualifiers.
- Copyable build/test/demo commands, documented design tradeoffs, and retained evidence that distinguishes recorded experiments from hosted CI results.

TCP ingress and durable crash recovery are optional extensions and are not implemented. Live trading, real funds, profitability/strategy research, a frontend, and a cloud service are outside scope. Neither production certification nor a fixed performance target is claimed.

See [Usage](docs/USAGE.md), [Design](docs/DESIGN.md), [Performance](docs/PERFORMANCE.md), and [Validation](reports/VALIDATION.md). The original [historical requirements](docs/requirements/README.md) remain unchanged for provenance.
