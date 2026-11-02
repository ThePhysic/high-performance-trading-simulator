# C++, Linux, networking and performance-engineering depth

## Modern C++ foundation

Use modern C++ (C++20 is a sensible baseline; C++23 features are optional) to make core behavior explicit and testable. Areas worth exercising and being able to explain:

- RAII, const-correctness, move semantics and ownership boundaries; avoid accidental copies or dangling references.
- Standard library containers, associative indexing, allocators and container invalidation behavior.
- Lifetimes, alignment, object layout, stack/heap tradeoffs and safe integer arithmetic.
- Templates and compile-time choices where they simplify or materially improve a hot path—not gratuitous metaprogramming.
- Well-defined error results without using exceptions as a normal high-frequency matching control path unless measured and justified.
- Reproducible builds, compiler optimization settings, warnings, sanitizers and debug symbols.

## Data structures and algorithms

The book must solve two different lookup problems: *best price / FIFO at level* and *direct lookup / cancellation by order ID*. Compare sensible representations (e.g. ordered price map plus intrusive/non-intrusive FIFO, indexed structures, flat structures for bounded tick ranges) based on realistic workloads. Discuss time complexity **and** cache locality, pointer chasing, branch behavior and allocation frequency.

A performant design can be simple. A custom pool, skip list, red-black tree or hand-rolled lock-free structure is not a requirement by itself. It becomes valuable only when it improves measurable behavior without weakening correctness.

## Concurrency and synchronization

- Explicitly distinguish **concurrent ingress/serialization** from **single authoritative ordering of a book**.
- Understand producer/consumer queues, thread ownership, atomics, mutexes, condition variables, acquire/release visibility, races and cache-line contention.
- Keep queues bounded; document full-queue behavior (backpressure, explicit rejection or an intentionally measured alternative).
- Expose conditions where contention, false sharing, busy-spinning or context switching worsen tail latency.
- Never claim lock-free correctness without a memory model, safe reclamation/lifetime policy and suitable stress validation.
- Multiple independent instrument books may be partitioned across workers, with a clear rule for risk accounting across those partitions.

## Linux systems dimension

- Linux processes/threads, scheduler interactions and system calls; optional CPU affinity and resource pinning for controlled experiments.
- Nonblocking socket operations and `epoll`, framing, partial reads/writes, connection teardown, socket buffering and queue pressure.
- `perf`, debugger stack traces, sanitizers and other inspectable evidence to identify CPU/call-site bottlenecks, allocation churn, contention, branch/cache effects and syscall overhead.
- Distinguish kernel/network costs from the matching core's in-process cost. Localhost and in-process measurements answer different questions.
- Optionally explore NUMA, huge pages, CPU isolation or networking packet paths only where hardware and measurement justification warrant it.

## Performance measurement vocabulary

**Throughput:** commands/sec, accepted orders/sec or executions/sec—with denominator clearly identified.

**Latency:** define start and stop points (e.g. request ingress -> output emission, admission -> completed match, or core-only match time). Measure p50, p95, p99 and worst-case with meaningful sample size; always label which path a number represents.

**Resource use:** peak/resident memory, bytes/order or per active order, allocations per command, CPU utilization, queue depths, drops/rejects, and where measurable, cache misses and context switches.

**Conditions:** hardware/CPU, OS/kernel, compiler/build flags, process affinity, thread count, transport, workload shape, initial book depth, order mix, warmup, duration and sampling method.

## Workloads that reveal real tradeoffs

- Mostly resting orders versus heavily crossing orders.
- Cancel-heavy, modify-heavy and fill-heavy workloads.
- Sparse prices with many levels versus dense prices with deep FIFO queues.
- Bursty ingress, high queue occupancy and backpressure.
- One instrument versus independent multi-book workloads.
- Varying order count, accounts and risk-check pressure.
- Cold-versus-warm cache, allocation-heavy versus reused-object variants.

A convincing result may be an improvement **or** evidence that an optimization made p99 worse. No arbitrary headline throughput or microsecond promise should be treated as a requirement before measurement.

## Performance vs correctness

Always distinguish a faster approximate mode from an equivalent exact mode if both exist; performance comparisons must state whether the same business contract was preserved. An optimized implementation should be checked against a simple reference model with identical canonical output on the same workloads.
