# Engine design and behavioral contract

This document describes the implemented core, rather than a production exchange or a durable recovery service. The public C++20 API is in `include/hpts/engine.hpp`. Linux is the intended execution and measurement platform; the core also builds on MSVC without compiler-specific widened integer types.

## Architecture and ownership

File input, interactive commands, replay, and workload generation converge on the same `Engine::process(Command)` operation. There is one implicit instrument, `SIM`, and one authoritative state owner. Only the execution thread calls `process`, `snapshot`, or `invariant_errors`; the engine has no internal synchronization and is not a concurrent mutation API.

The layers have distinct responsibilities:

- `io.cpp` handles bounded text lines, configuration, integer parsing, and command normalization.
- The caller establishes the processing order and records the normalized commands and outcomes.
- `engine.cpp` owns the books, identity ledgers, accounts, reservations, kill state, and sequence.
- `Outcome` provides typed events and canonical serialization. `Snapshot` exposes a sorted, inspectable final state.
- `BoundedQueue<T>` supplies an optional blocking handoff between input and execution. A mutex establishes publication visibility; condition variables wait for data or capacity. Closing wakes all waiters, rejects future pushes, and allows a consumer to drain already queued items. No lock-free property is claimed.

The queue's full policy is blocking backpressure. Its high-water mark and the number of pushes that encountered a full queue are observable. A successful queue push transfers ownership of a value; it does not imply durable admission or successful business execution. The engine's ordering is its actual call order, not wall-clock arrival time.

## Configuration and quantities

Account IDs are nonempty ASCII letters/digits with `_`, `-`, and `.` permitted, bounded by `max_account_id_length`. Accounts have an initial nonnegative integer cash balance, integer position, and inclusive minimum/maximum position. Initial positions must be inside their limits. Account IDs are unique. Short selling is allowed only to the extent that the configured minimum position permits it.

Prices are positive signed 64-bit integer ticks and quantities are positive signed 64-bit integer units. They must fit the configured `max_price` and `max_quantity`. Every price-times-quantity calculation, including sell submissions, must be representable; addition and subtraction are explicitly checked. Binary floating-point values are not book prices or account quantities.

The sum of all initial cash balances must fit a signed 64-bit integer. With no fees, funding changes, or external cash mutations, each trade transfers cash without creating it. Every account's cash is nonnegative and at most this conserved total, so a seller's cumulative credits remain representable. The execution preflight nevertheless checks individual transfers and position changes before committing them.

The core supports configurable account, active-order, lifetime order-ID, and processed-command-ID limits. The CLI additionally restricts active orders, lifetime order IDs, and command IDs to at most 1,000,000 each; accounts to 1,024; account-ID length to 64; and queue capacity to 65,536. All capacities must be positive. Input lines are limited to 4,096 bytes before their terminating newline. Limits are part of replay configuration.

## Matching, lifecycle, and replacement

Buy orders take the lowest eligible ask while that price is at or below their limit. Sell orders take the highest eligible bid while that price is at or above their limit. Within a price level, the oldest resting order executes first. A fill's quantity is the minimum of incoming and maker remaining quantity; its price is the resting maker's price. An incoming order continues across makers until exhausted or no eligible price remains. Positive residual limit quantity rests on its own side.

An accepted active order has one order ID, one owner, one side, one price, and exactly one FIFO membership. Fully filled and cancelled orders leave the active book. Their IDs remain in the lifetime identity ledger, so they cannot be silently resurrected. Cancellation checks account ownership and returns distinct deterministic `UNKNOWN_ORDER`, `ORDER_TERMINAL`, or `NOT_OWNER` rejections where applicable.

A replacement keeps the order ID, owner, and side. Its quantity is the desired **new remaining quantity**, not a lifetime submitted quantity or a quantity delta. A same-price reduction or exact no-op keeps the original priority. A price change or remaining-quantity increase loses priority and uses the current processing sequence as its new arrival priority; it can immediately execute against opposing liquidity. The old order's reservations are hypothetically released before evaluating its replacement. Any invalid replacement leaves its former remaining quantity, price, reservations, and FIFO position intact.

Self trading is prohibited through whole-command rejection. Preflight follows the exact best-price/FIFO sweep. If the incoming order would reach one of its own account's resting orders before being filled, the entire submit or replace is rejected as `SELF_TRADE`, including any earlier hypothetical fills against other accounts. An own order beyond the point where the incoming quantity would be exhausted does not cause rejection. No makers are skipped and FIFO is preserved.

Acceptance and replacement emit their event first, then one `Trade` event per maker fill in matching order, then a `BookUpdate`. Cancellation emits `Cancelled` then `BookUpdate`. A `BookUpdate` contains the resulting best bid and ask; zero means that side has no liquidity. Full depth and accounts are inspectable through a snapshot. Rejections emit exactly one `Rejected` event.

## Conservative risk and accounting

For each account, let `p` be its filled position, `B` its total open buy quantity, `S` its total open sell quantity, and `R` the sum of `limit_price * remaining_quantity` over its open buys. A proposed order, including its full incoming quantity before any matches, must satisfy:

```text
p + B <= max_position
p - S >= min_position
0 <= R <= cash
```

Both position extremes are checked independently. Opposing outstanding orders do not net away one another's worst-case exposure. Overflow in a potential position or reservation is rejected, rather than wrapped.

An accepted buy reserves its full limit notional. When it fills, reservation is released at the buyer's own limit price, while cash transfers at the actual maker execution price. This preserves price improvement. Buy and sell outstanding quantities are released as they fill, positions move by equal and opposite unit quantities, and cash moves by equal and opposite executed notional. Cancellation and replacement release the appropriate prior reservations. There is no fee, margin, settlement, or collateral model beyond these stated rules.

Risk checks precede the hypothetical matching sweep. Consequently a submission can be rejected conservatively even if one possible immediate execution would appear to lower another measure of exposure. `POSITION_LIMIT`, `BUYING_POWER`, and `ARITHMETIC_OVERFLOW` are explicit outcomes.

## Kill-switch policy

`KillSwitch` is a sequenced, recorded state-changing command. Both activation and deactivation emit a `KillSwitch` event. While enabled:

- All new submissions are rejected as `KILL_SWITCH_ACTIVE`.
- Same-price replacements with nonincreasing remaining quantity are allowed.
- Price changes and remaining-quantity increases are rejected.
- Cancellations and out-of-band state inspection remain available, subject to ordinary identity/admission checks.

The local CLI is a trusted simulation control surface. The switch is not a brokerage emergency control and does not provide external authentication or real-world risk guarantees.

## Sequencing, duplicates, and capacity

Every `process` call consumes a new monotonically increasing sequence, including rejected operations, duplicate retries, and capacity rejections. Sequence exhaustion throws an explicit infrastructure exception. A positive numeric command ID is separate from the sequence and order ID.

Admission metadata behaves as follows:

1. Command ID zero returns `INVALID_COMMAND_ID` and is not retained.
2. A previously retained command ID returns `DUPLICATE_COMMAND` and never executes again.
3. When the command ledger is full, an unseen command ID returns `COMMAND_CAPACITY` without being retained or executing.
4. Otherwise the command ID is retained before business validation. Retrying a business-rejected command therefore returns `DUPLICATE_COMMAND`.

Command-ID retention and sequence consumption are the explicitly permitted metadata effects of a rejection; book, balances, reservations, ownership, and priority do not change. Duplicate outcomes are deterministic rejections, rather than cached copies of an earlier acknowledgement.

Order IDs are positive numeric values. Only successful original submissions consume lifetime order-ID entries, including submissions that fill completely without resting. Reusing one returns `DUPLICATE_ORDER`. A full lifetime order ledger returns `ORDER_ID_CAPACITY` for new submissions; replacements retain the existing ID. Neither identity ledger evicts entries.

`ORDER_CAPACITY` is evaluated against the hypothetical resulting active count, after fully executed makers are removed and any positive incoming residual is added. A fully crossing command may therefore execute when the active book was initially full.

Exhausting the command ledger ends admission of all unseen commands, including new cancellation and kill commands. This is an explicit finite-session resource limit, not a promise that protective commands bypass infrastructure capacity. Inspection remains possible, prior IDs remain protected from reexecution, and the caller must report the rejection. Reinitializing a new session does not promise continuation or recovery of the exhausted session.

## Atomic preflight and failure boundary

Submit/replace handling first builds the complete hypothetical match plan. Account changes occur in staged copies, with checked arithmetic and full risk validation. Canonical event data is allocated before authoritative mutation. New active-order tree nodes, FIFO list nodes, price-level nodes, and lifetime-ID nodes are also allocated in temporary containers.

The commit then uses numeric account assignment, map/set node transfers, list splices, and erasure. It does not allocate memory. A replacement is never implemented by deleting its former live order before knowing that the new operation is valid. Cancellation similarly prepares its outcome and account update before erasure. This makes business/resource rejections atomic without copying the whole book on each command.

The sequence and command ledger are updated before domain preflight. An allocation exception while preparing a command can therefore leave admission metadata advanced even though domain changes have not committed. There is no promised ability to resume a session after an allocation or other process exception. Callers must stop processing, report the infrastructure fault, and avoid claiming complete output, replay evidence, or durable commitment for that run. The command log and output sink have their own failure boundary: flushing a C++ stream is not a durable commit or a crash-recovery protocol.

The system does not use `fsync`, checksummed persistent commit records, distributed consensus, or crash-consistent snapshots. Logical replay of a complete valid journal is supported; recovery from interrupted writes is not claimed. A truncated or failed run must not be presented as an equivalent complete replay.

## Canonical schema, version 1

Canonical output explicitly uses the classic C++ locale and decimal integers, except for hexadecimal fingerprints. Records terminate with `\n`. Empty string fields are `~`; safe ASCII identifier characters are literal, and other bytes are percent-encoded with uppercase hexadecimal digits. Empty and encoded literal strings are distinguishable. Wall-clock times, pointers, thread IDs, and hash-container iteration are absent.

An outcome begins with:

```text
OUTCOME sequence command_id accepted reason
EVENT sequence ordinal type order_id maker_order_id account_id maker_account_id side price quantity best_bid best_ask enabled reason
```

The `EVENT` record repeats for each event, with zero-based ordinals. Booleans are `0` or `1`. Accepted outcome reason is `OK`. For trades, the ordinary order/account fields identify the incoming taker and the maker fields identify the resting counterparty. Fields irrelevant to an event retain their declared zero/empty/default-side values; these are still serialized. A rejection omits the untrusted account string and records its stable reason.

A snapshot begins with `STATE 1 sequence kill_switch`, then emits the following categories in order:

```text
ACCOUNT id cash min_position max_position position reserved_cash reserved_buy_quantity reserved_sell_quantity
ORDER id account_id side price remaining priority
COMMAND_ID id
ORDER_ID id
```

Accounts are sorted by account ID; active orders and both identity ledgers are sorted numerically. The snapshot includes sequence, reservations, priority, and identity ledgers, not just book depth. This makes final-state comparisons sensitive to future retry/lifecycle behavior.

`canonical_config` starts with:

```text
CONFIG 1 SIM max_active_orders max_order_ids max_commands max_accounts max_account_id_length max_price max_quantity
INITIAL_ACCOUNT id cash min_position max_position position
```

The initial-account records are sorted by ID. Callers compare the canonical configuration as well as outcomes and final state. The separate text journal representation includes its own version marker and normalized input commands, so replay reproduces the sequence actually processed, including business rejections and kill controls.

The snapshot digest is 64-bit FNV-1a over its canonical bytes, printed as 16 lowercase hexadecimal digits. It is a reproducibility fingerprint with possible collisions, not a cryptographic integrity or tamper-resistance guarantee. Exact canonical-byte comparison is the stronger equality check and should accompany digest reporting.

## Data structures, complexity, and validation

The core uses ordered price maps with descending bids and ascending asks, FIFO linked lists at each price, and an ordered order-ID map whose entries retain their FIFO iterators. The identity ledgers are ordered sets. No custom allocator, lock-free structure, object pool, or allocation-free hot path is claimed.

- Best-price access uses a price-map beginning iterator.
- Order lookup is `O(log N)`, price-level lookup/insertion is `O(log P)`, and removal of a known FIFO node is `O(1)`.
- A match sweep visits the makers it would consume and the eligible price levels. Commit also performs indexed lookups/removals for those makers.
- Command deduplication is `O(log C)`; lifetime order-ID checks are `O(log H)`.
- Persistent logical storage is `O(A + N + P + C + H)`, for accounts, active orders, levels, command IDs, and lifetime order IDs. Per-command plans/events are bounded by the number of active makers. Snapshots and canonical representations allocate additional memory proportional to the configured state limits.

`invariant_errors()` independently traverses the live structures to check resource caps, uncrossed resting prices, correct side/price membership, strictly increasing FIFO priority, one membership per active ID, positive valid remaining quantities, known owners and lifetime IDs, recomputed reservations, account risk constraints, and conserved cash. It is an explicit diagnostic operation, rather than an unlabelled cost inside the measured matching path. Separate fixtures, reference comparisons, seeded sequences, and replay checks supply behavior-level evidence. Performance reports must state whether invariant diagnostics were included in a timed path.
