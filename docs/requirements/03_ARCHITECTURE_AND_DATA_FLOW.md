# Cohesive architecture and data flow

## Conceptual system

```text
Synthetic event generator    Local CLI / client    Replay log
         |                        |                    |
         +-----------+------------+--------------------+
                     v
            Input adapters / framing
            Parsing + validation
                     v
       Ordered admission / sequencer
            (logical input order)
                     v
      Bounded dispatch / event-processing core
                     v
          Authoritative state owner
          +----------------------------+
          |  Pre-trade risk checks     |
          |  Order state/indexes       |
          |  Per-instrument books      |
          |  Price-time matching       |
          |  Positions/reservations    |
          +----------------------------+
                     v
             Canonical outcomes
       (acks/rejects/fills/book updates)
          /               |              \
     Output log        Client sink      Metrics
          |
          v
   Replay comparison + final state digest
```

This is a **logical** depiction, not a requirement to split every box into a process or microservice.

## Responsibilities that should remain distinct

**Ingress and transport.** Framed network or file input, message size bounds, parsing, client/source identification, and error reporting. It cannot directly mutate the book.

**Sequencing.** Establishes a single order for commands whose results depend on timing; captures the ordering used by replay. TCP ordering is only per connection, so concurrent-client admission needs its own authoritative order.

**Event processing.** Transfers accepted messages into the execution path with explicit ordering, bounded storage and an overload policy. Producer/consumer concurrency belongs here; it must not introduce nondeterministic matching.

**Exchange core.** Owns the mutable order books and risk/account state, validates transitions and applies atomic logical effects of each command. Prefer a **single writer per book or deterministic partition** as the default explanation for correctness; multiwriter matching requires a stronger, testable ordering model.

**Event/output log.** Records enough information to reproduce logical results; separates reproducibility events from best-effort metrics and wall-clock telemetry. If durable crash recovery is claimed, logging acknowledgement and commit semantics need a separate explicit contract.

**Benchmarks and observability.** Measure multiple locations (ingress-to-output and core-only), expose workload and environment, and avoid modifying the truth of matching just to improve a headline number.

## Concurrency without sacrificing order

Possible concurrent work includes parsing, input generation, socket ingestion, output serialization, log compression, or processing **independent books**. The critical path for an individual order book should have a well-defined serial order. A bounded single-producer/single-consumer (SPSC) queue or bounded multi-producer queue is an implementation option, not an architectural obligation.

A benchmark should permit comparing simpler single-thread behavior against more concurrent configurations. If contention, cache bouncing, context switches or scheduling noise make the concurrent version slower or raise p99, that is an important engineering result.

## Data layout and ownership

- Associate stable logical order IDs with a representation that can find/cancel orders efficiently, e.g. ID-to-order lookup plus per-price FIFO structures.
- Order lifetimes need a single authoritative owner; references/iterators into container elements must not outlive erasure or reallocation.
- Support fast best-price access and efficient removal without silently invalidating a price-level FIFO invariant.
- Allocation-aware structures, object pools and arenas are plausible optimizations **after** correctness and baseline profiling; their safety and fragmentation tradeoffs belong in the evidence.
- Structural separation of execution-state events versus externally visible output events makes replay and audits easier.

## Linux/networking dimension

A strong network extension uses nonblocking sockets (`epoll` is a natural Linux option), explicit message framing, partial read/write handling, connection closure/reconnect semantics, bounded buffers and queue-pressure handling. A simple local protocol is sufficient; copying a proprietary exchange wire format is unnecessary. TCP as a dependable local ingress baseline is appropriate. UDP/multicast exploration is an optional systems extension rather than a requirement for exchange commands.

## Replay, logging and failure boundaries

- Distinguish an **in-memory deterministic execution trace** from a **durable recovery log**. The former proves stable logical behavior; the latter additionally needs persistence/commit/fault guarantees.
- Where a command is deemed admitted or acknowledged must align with what is recorded. Otherwise crash/recovery claims may be false.
- Replayed inputs should include initial configuration, risk limits, account state and any state-changing control messages.
- Output sinks and metrics should not become hidden sources of state, especially when they fail or lag.

## Architectural coherence

The basic event-processing engine remains reusable. Order book matching, account/risk state and exchange-specific event encoding are a cohesive domain layer on top. Avoid building an unrelated generic messaging framework and a separate trading app that merely shares a repository.
