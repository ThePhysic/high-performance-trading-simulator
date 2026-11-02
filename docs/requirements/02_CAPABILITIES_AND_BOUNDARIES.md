# Capabilities and system boundaries

## Primary operating modes

**Deterministic offline mode.** Read a known sequence of canonical events, process it through the same authoritative business engine used in other modes, emit a canonical output stream, and report the ending book/account state. This is the ground truth for correctness and regression tests.

**Interactive local simulation.** Accept requests from a simple CLI or local client so that orders, cancellations and risk controls can be exercised without editing input files.

**Performance workload mode.** Generate/load parameterized event streams and measure throughput, memory, and latency under explicit conditions. Performance mode cannot silently disable correctness checks or alter business outcomes without labeling the configuration and its tradeoffs.

**Optional Linux network-ingress mode.** Receive framed messages over a locally bound TCP socket, with nonblocking I/O, validation, connection lifecycle handling and bounded buffers. UDP or multicast may be studied separately, but packet loss/reordering semantics must not be confused with reliable command admission.

## Functional behavior

### Markets and instruments

- At least one instrument with discrete **integer tick** prices and **integer unit** quantities.
- Independent bid (buy) and ask (sell) sides with best-price selection and FIFO priority within a price level.
- Best bid/ask, depth and top-of-book visibility sufficient to inspect behavior and validate invariants.
- Optional multiple independent books using the same engine. Cross-instrument matching or complex orders are not necessary.

### Order lifecycle

- Submit a limit buy/sell with unique order ID, account, instrument, positive price and positive quantity.
- Validate that the account/instrument exists and that the submission passes risk limits.
- Accept resting liquidity or immediately match crossing liquidity; support partial fills and residual resting quantity.
- Cancel an existing order owned by the correct account, with a deterministic result for unknown/already-filled/already-cancelled IDs.
- Modify/replace an existing order according to **explicit queue-priority rules** (see matching specification).
- Reject malformed, duplicate, unauthorized, risk-violating or logically impossible requests without partially mutating book/account state.
- Emit stable event types such as Accepted, Rejected, Cancelled, Replaced, Trade/Fill, and BookUpdate (exact schema is a design choice).

### Risk controls (simulation, not a brokerage risk system)

- Per-account position limits and a conservative buying-power/notional exposure model.
- Outstanding orders influence allowable exposure so many individually valid pending orders cannot bypass a limit in aggregate.
- A deterministic emergency kill switch that blocks new risk-increasing requests while permitting protective actions such as cancellations.
- Clearly defined initial balances/positions, limit settings and state updates after trades/cancels.

### Replay and traceability

- A single canonical sequence of **accepted/processed input commands** and resulting outputs, with deterministic IDs, timestamps semantics and error results.
- Every state-changing input must be replayable, including risk-config/control changes if supported.
- Equal canonical inputs + engine/rule/configuration version + deterministic seed (if relevant) produce equal logical outputs and final book/risk state.
- Human-readable diagnostics/operational timestamps may differ across runs, but must not leak into logical replay comparisons.
- Integrity check(s) or fingerprints of input, emitted events and final state are useful for demonstrating equality.

### Introspection

- Current book depth, open orders, positions, cash/exposure, kill-switch state and last processed sequence.
- Event-processing rate, queue occupancy and backpressure/reject/drop counters (with **drop policy explicitly defined**), process CPU/memory data, and latency histograms/distributions.
- Structured error messages differentiating parse errors, validation errors, risk rejects, state-transition rejects and infrastructure faults.

## Boundaries and assumptions

- The simulator decides how to handle **client resubmission/reconnection**; duplicate command IDs must not accidentally execute twice in any mode claiming idempotent admission.
- A command can be rejected; *rejected* is a valid deterministic business result, not an excuse for silently losing an event.
- The exchange core is the authority for book/account state. Network handlers, benchmark clients and reporters must not mutate that state behind its ordering rules.
- Hardware-level latency determinism is not promised. **Logical determinism** is the promise; real CPU scheduling and network latency vary.
- Synthetic test data is acceptable if identified as synthetic. No need for external trading credentials, licenses or real feeds.
