# Scope boundaries, extensions and portfolio value

## Essential integrated project scope

One coherent repository/system should have:

- A modern C++ event-processing core with explicit logical sequencing and authoritative state ownership.
- A deterministic, correct limit order book and matching engine supporting new orders, cancels, documented modifications, partial fills and trade events.
- A small simulated pre-trade risk layer (position, buying power/reservation, kill switch).
- Input adapters for reproducible synthetic/offline event streams and inspectable output traces.
- Replay equality of logical results and final book/account state.
- Bounded event processing and a defensible concurrency model (even if authoritative matching is single-writer).
- Measurable performance under declared Linux environment and workloads; tests, benchmarks and profiling evidence.
- Clear error, overload and data-integrity behavior without trading real assets.

## Enhancements that remain optional

These can deepen the same engine **if they have a technical justification**, rather than creating a second product:

- Nonblocking Linux TCP server and client workload generator; `epoll` and realistic partial-framing tests.
- Allocation reduction/object pools, custom memory resources or order-structure alternatives supported by measured profiling.
- Independent instrument partitions, multi-core scaling and careful cross-book risk consistency.
- Binary framed protocol, depth/market-data subscriptions and outbound fanout under bounded backpressure.
- Durable journal and crash-consistent recovery with explicit commit/acknowledgement contract.
- CPU affinity/NUMA analysis, cache/branch/memory profiling, controlled scheduler and context-switch experiments.
- Replay snapshots/checkpoints, if demonstrably correct and if useful for large logs.
- Additional order instructions such as immediate-or-cancel or market orders, *only if* core semantics stay clear.

## Not part of this project

- Live trading, real funds, broker credentials, real exchange connectivity.
- Market forecasting, machine learning, historical alpha, order-book prediction or profit/Sharpe/backtest returns.
- Realistic exchange fee schedules, market surveillance, clearing/settlement, cross-venue arbitrage or full exchange fidelity.
- Overly broad multi-service deployment, full UI, large cloud stack or cluster orchestration.
- Unsupported low-latency claims based solely on a synthetic benchmark without workload/hardware disclosure.

## Relationship to the other portfolio projects

**Linux Systems & Reliability Platform:** operational diagnosis, service supervision, fault injection, cgroups, systemd, service/network recovery. The high-performance C++ project should know Linux deeply, but its distinguishing core is **low-level event execution, matching algorithms, latency and data structure performance**.

**Market Microstructure / Quant Research project:** data quality, features, leakage prevention, research, prediction and experimental reproducibility. This project uses a *simulation of exchange mechanics* to study **systems correctness and performance**, not to assert predictive ability. A future connection could feed synthetic market events between them, but neither requires the other.

**Existing backend/reliability projects:** valuable prior evidence for APIs, queues, idempotency and durable workflows; this project adds direct C++ ownership/memory, OS/network I/O, concurrency and CPU/cache profiling evidence rather than reproducing those same application-level components in a new language.

## What makes the result portfolio-worthy

A reader should be able to see:

1. **Working behavior:** a simple end-to-end order trace with matching, fills, cancel/replace, risk rejection and deterministic replay.
2. **Correctness evidence:** documented matching/risk rules, edge cases and randomized invariant tests.
3. **Systems depth:** C++/Linux implementation decisions, concurrency ownership, network/backpressure behavior and memory design.
4. **Honest measurement:** workload configuration, benchmark methodology, latency distributions, throughput and profiling-backed changes.
5. **Reasoned tradeoffs:** why a simpler single-writer model or standard data structure may be better than unnecessary parallelism; where real bottlenecks were found.

### Potential résumé evidence (only after verified)

Examples of the *type* of claim worth earning—not accomplishments to assert in advance:

- Implemented a deterministic C++ limit-order-book engine with price-time matching, partial fills, cancel/replace semantics and simulated pre-trade risk controls.
- Processed **N** synthetic events/sec at **X** p99 core latency on **specified hardware/workload**, with full invariant/replay checks.
- Improved measured cancel-path tail latency by **Y%** after `perf`/allocation profiling while preserving output equivalence.
- Implemented bounded Linux network ingress and explicit overload behavior, verifying deterministic outcomes under generated burst workloads.

## Design choices left deliberately open

- Exact transport format, CLI UX, repo layout and build/test framework.
- Specific container/price-level indexing strategy and allocator design.
- Choice of single-thread/worker architecture beyond the invariant of ordered authoritative mutation.
- Risk limits and initial balances used in fixtures, within checked arithmetic and coherent accounting.
- Whether durable recovery, binary protocols and multi-instrument sharding justify their additional complexity.

These are engineering design choices, **not instructions to follow a particular planning, review or approval workflow**.
