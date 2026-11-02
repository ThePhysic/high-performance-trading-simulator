# Correctness, testing and performance evidence

## What counts as working

The system should be demonstrably correct, not just able to print plausible trades. A suite of small deterministic fixtures and larger generated workloads should establish:

- Price-time matching, correct best-price choice, FIFO within each level and resting-price trade convention.
- Partial fills across multiple resting orders; residual quantity handling.
- Cancel/replace semantics including retention/loss of queue priority.
- Correct handling of duplicates, invalid commands, stale cancels, unknown accounts, insufficient buying power and kill-switch transitions.
- Consistent positions, cash/reservations and order lifecycle across all resulting fills.
- Input order preserved across any concurrent ingress, and reproducible final outputs.
- Clean behavior under malformed frames, overload and unexpected client disconnections.

## Testing approaches

**Unit/contract tests.** Focused order book, matching, risk accounting, serialization, queue and protocol tests with expected outputs.

**Reference oracle.** A simple, deliberately unoptimized, single-thread matching model can serve as a truth reference against a faster implementation. Compare canonical event traces and final state.

**Generated/stateful testing.** Seeded randomized sequences of submits, cancels, replaces, risk controls and bursty commands. After every step, check invariants and reconcile executed quantities.

**Replay regression.** Same log and configuration yields byte-equivalent canonical logical events or an explicitly versioned canonical representation, plus identical final-state digest. Run under different ingress/worker schedules where feasible.

**Sanitizers/fuzzing.** AddressSanitizer, UndefinedBehaviorSanitizer and ThreadSanitizer where supported; fuzz message parsing and malformed command sequences; verify no leaks/use-after-free/data races in tested configurations.

**Failure/overload tests.** Queue capacity, slow consumer, malformed framing, disconnects, log errors, large inputs, overflow, and determinism after a controlled restart from the same complete input log.

**Complexity/performance tests.** Sweep event mix, price-level density, active orders, instrument count and thread topology; report algorithmic shape and measured cost.

## Core invariants as testable properties

1. Every accepted active order appears in exactly one valid book side/price level and has positive remaining quantity.
2. The book is internally ordered by best price and FIFO priority; no crossed resting state remains after processing each command.
3. Trades have positive quantities and cannot overfill resting/incoming orders.
4. Cancelled/terminal orders cannot be executed again.
5. Order/account/reservation accounting reconciles with all emitted trades and cancellations.
6. Rejections do not mutate authoritative state unexpectedly.
7. Identical canonical inputs/configuration give identical canonical outputs and final state.
8. Every admitted command receives its defined outcome; any backpressure/reject/transport failure is distinguishable from a business result.

## Meaningful benchmarks

| Metric | Distinction to preserve |
|---|---|
| Throughput | Commands processed/sec vs orders accepted/sec vs executions/sec |
| p50/p95/p99 core latency | Sequenced command at engine entry -> all matching effects complete |
| p50/p95/p99 end-to-end | Client/request boundary -> completed response (includes transport and queueing) |
| Tail under bursts | Behavior at high queue occupancy and with slow output sinks |
| Memory and allocation | RSS/peak memory, active orders, allocations/op or bytes/op |
| CPU and kernel impact | Utilization, context switches, syscall rate, and profiling evidence where available |
| Correctness under load | Same outcomes and invariants as the low-rate reference workload |

Benchmark reports should state OS/kernel, CPU, compiler version/flags, build type, core affinity, thread count, transport, sampling method, dataset size, initial book depth, event composition and run-to-run variability. Avoid comparing different business behaviors without qualification.

## What a strong performance story looks like

A reproducible baseline is measured, profiling identifies a specific bottleneck (e.g. excessive allocations in a cancel-heavy run), a justified change is made, and identical workloads demonstrate the gain and any regressions in correctness, memory, CPU or p99. A result that uncovers a real tradeoff is valid evidence too. **No fabricated numbers, undefined speedup claims, or irrelevant synthetic comparisons.**

## Observable demonstration

One local workload can show a few book snapshots, accepted/rejected requests, partial fills and a kill-switch rejection; then replay produces the same canonical trace and final digest. A separate benchmark run reports relevant p50/p95/p99, throughput and resources with a clearly labeled dataset and hardware configuration.
