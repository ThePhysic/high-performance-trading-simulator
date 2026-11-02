# High-Performance C++ Systems Engine — Event-Driven Trading & Exchange Simulator

**Project idea package** | C++20/23 · Linux · event processing · exchange matching · performance engineering

## The idea in one paragraph

Build a self-contained, Linux-native, high-performance C++ event-processing system whose flagship workload is a miniature electronic exchange. The system accepts sequenced order and control events, validates requests, applies simulated pre-trade risk limits, matches orders using price-time priority, emits execution and book-update events, and can deterministically replay a recorded workload. Its foundation is a reusable systems engine: bounded queues, nonblocking network I/O, explicit ownership and memory behavior, concurrency with clear ordering, observability, failure handling, profiling, and honest latency/throughput measurement. **The exchange is a demanding use case for the C++ engine—not a second, disconnected project.**

## What it should demonstrate

- Strong modern C++ engineering: ownership/lifetimes, RAII, data structures, memory layout, careful error handling, and safe concurrency.
- Linux systems fluency: socket I/O, event loops, threads, synchronization, scheduling effects, process-level performance and debugging.
- Domain correctness: deterministic price-time matching, order lifecycle, partial fills, consistent risk/account state, and auditable event sequences.
- Performance discipline: reproducible throughput, p50/p95/p99 latency, CPU and memory measurements, baseline-to-optimized comparisons, and tradeoff explanations.
- Operational credibility: malformed inputs, overload, disconnects, controlled failure scenarios, no silent event loss, and explicit limits of reliability claims.

## Document map

| File | Covers |
|---|---|
| `01_VISION_AND_OUTCOMES.md` | Problem statement, audience, engineering goals and non-goals |
| `02_CAPABILITIES_AND_BOUNDARIES.md` | End-to-end system behavior and functional scope |
| `03_ARCHITECTURE_AND_DATA_FLOW.md` | Cohesive component model, threading and event flow |
| `04_ORDER_BOOK_MATCHING_AND_REPLAY.md` | Matching semantics, event contract and determinism |
| `05_CPP_LINUX_AND_PERFORMANCE_DEPTH.md` | C++/Linux/concurrency/memory/networking expertise |
| `06_RISK_FAILURE_AND_RECOVERY.md` | Simulated safeguards and failure scenarios |
| `07_VERIFICATION_AND_MEASUREMENT.md` | Correctness properties, test scenarios, benchmarking |
| `08_SCOPE_BOUNDARIES_AND_PORTFOLIO.md` | Essential vs optional scope, distinctiveness, portfolio evidence |
| `09_ILLUSTRATIVE_EVENT_TRACES.md` | Concrete reference behavior for matching, replay, limits |

## Intentional boundaries of this package

This is an **idea and requirements reference**, not an implementation workflow. It specifies the system's purpose, expected observable behavior, critical invariants, and technical skills it should evidence. It does **not** prescribe development phases, a delivery schedule, approval gates, Codex prompts, commit conventions, task assignments, or a required step-by-step sequence. Suggested architectural approaches are described as options where more than one sound design exists.

The project is a simulator and systems portfolio artifact. It never connects to live brokerages or exchanges, moves real money, or makes claims of trading profitability or production certification.
