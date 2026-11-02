# Project vision and desired outcomes

## The problem it explores

Performance-sensitive trading infrastructure is hard because **fast** is not sufficient: event order, state transitions, financial quantities, control messages, failure handling, and reproducibility must remain correct. A low-latency system that occasionally misorders two orders or loses a cancellation is not trustworthy. Likewise, a correct simulator with no resource bounds or measurement discipline does not establish low-level systems competence.

This project brings those concerns together in a single C++/Linux system. An exchange-like application makes decisions and data structures visible; a reusable event-processing core supplies the low-level engineering challenge.

## End product

A command-line-operated, headless simulated exchange running locally on Linux, with an event-processing engine that:

1. Ingests structured order/control messages from replay files and optionally a local network client.
2. Assigns or preserves a well-defined **authoritative input order**.
3. Validates message structure, identities, and legal state transitions.
4. Applies a documented pre-trade risk model for simulated accounts.
5. Maintains one or more limit order books with price-time priority.
6. Produces deterministic acknowledgements, rejects, fills, trades and relevant book/state updates.
7. Supports exact logical replay of a recorded run under a declared configuration.
8. Exposes operational telemetry and reproducible performance benchmarks.
9. Handles malformed input and bounded-resource conditions without corrupting authoritative state.

A reference workload can be generated locally; production market-data feeds and proprietary exchange protocols are unnecessary.

## What makes it a systems project

The project should make C++ and Linux choices technically consequential rather than decorative. Examples: choosing book indexes for realistic access patterns; proving who owns each mutable data structure; bounding memory under bursts; separating network framing from state mutation; managing event queues and backpressure; measuring allocator and cache effects; diagnosing slow p99 tails using profilers; and showing where concurrency helps or hurts.

The foundational engine should be understandable independently of trading: an ingress path, ordered event handling, bounded handoffs, an execution core, a structured output path, and reproducible instrumentation. The limit order book and simulated risk controls provide an unusually rich workload for that engine.

## Desired technical outcomes

- **Correctness you can explain:** stable matching outcomes, auditable event ordering, tests for invariants and edge cases, no floating-point price ambiguity.
- **Determinism you can demonstrate:** same normalized input stream plus versioned rules/configuration yields the same logical output stream and final state.
- **Practical performance evidence:** reported throughput and end-to-end as well as component latency distributions, alongside hardware/build/workload conditions.
- **C++ expertise:** ownership, copy/move choices, allocation costs, data layout, containers, atomics, synchronization and undefined-behavior avoidance.
- **Linux expertise:** nonblocking sockets and event multiplexing, threads, process and scheduler effects, system calls, debugging and profiling.
- **Engineering judgment:** baseline versus optimized implementations; explicit tradeoffs among simplicity, correctness, memory, tail latency and throughput.

## Target-role relevance

Designed to provide technically defensible discussion material for quant SWE, core development, low-latency engineering and systems roles, including those previously considered at **HRT, DRW, SIG, IMC, Jane Street, Seven Research and Quadeye**. It also supports general infrastructure/SWE roles by showcasing concurrency, reliability, profiling and disciplined software design. No company-specific stack or interview guarantee is implied.

## Non-goals

- Live exchange connectivity, brokerage APIs, real-money trading or external order routing.
- Trading strategies, forecasting alpha, profitable backtesting or market-making profitability claims.
- Exchange regulatory compliance, financial certification, or production-grade clearing/settlement.
- A large frontend, distributed microservice fleet, Kubernetes requirement, or unnecessary cloud deployment.
- Adding low-level complexity solely to use a fashionable technique (e.g., lock-free everything, custom allocators everywhere).
- Recreating the separate Linux Systems & Reliability Platform project: this project **uses** Linux to implement a performance-sensitive event system, rather than primarily supervising/repairing OS services.
