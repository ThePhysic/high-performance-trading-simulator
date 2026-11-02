# Order book, matching, event semantics and deterministic replay

## Canonical input concepts

A useful logical command representation includes:

| Field | Meaning |
|---|---|
| `sequence` | Unique authoritative order of processing (assigned at admission, or defined by input log) |
| `command_id` | Unique identity for request deduplication and audit |
| `account_id` | Simulated owner of request |
| `instrument_id` | Book affected |
| `type` | Submit, cancel, replace, risk control, or other explicitly supported command |
| `order_id` | Stable ID for the submitted/target order |
| `side` | Buy or sell for new orders |
| `price_ticks` | Integer limit price, in configured tick units |
| `quantity` | Positive integer quantity in instrument units |
| `logical_time` | Optional deterministic event time; never depend on wall clock for price-time priority when sequences differ |

Transport envelopes, serialization (binary/text), and physical timestamps can differ from the logical model. Exact file format and protocol are design choices.

## Matching contract

- Buy orders match the best **lowest** available sell price when the best ask is at or below the incoming buy limit.
- Sell orders match the best **highest** available buy price when the best bid is at or above the incoming sell limit.
- At a common price, the **oldest eligible resting order** fills first (price-time FIFO priority).
- The execution price should be the **resting order's price** for a standard continuous limit-book convention; document any alternative.
- Each fill uses `min(incoming_remaining, resting_remaining)` and updates both open order state and relevant account positions/reservations.
- A partially filled incoming order may keep matching the next resting order; remaining limit quantity can rest on its own side if valid.
- A fully filled or cancelled order is removed from the active book and cannot be filled again.
- A single command's emitted outcomes have stable ordering (e.g. acceptance, one or more fills/trades, resulting book updates).
- No trade is emitted for a non-crossing order, and the resting book may not contain crossed bids/asks after a command finishes.

## Order replace/modify semantics

Define queue priority explicitly to avoid a major source of order-book bugs. A conventional choice:

- **Quantity reduction at unchanged price:** may retain FIFO priority, subject to accounting/invariant rules.
- **Price change or increase in remaining quantity:** loses prior priority and is treated as a new arrival at its destination price (or cancels and replaces the original order).
- **Invalid replace:** is rejected without an unintended partial state change.

The precise convention can be chosen differently, but the system must implement and test one consistent, documented policy.

## Identity and lifecycle rules

- Order IDs and command IDs have separate purposes. Retries of the same command must not accidentally create additional fills if the mode promises idempotence.
- Cancels should validate account ownership; unknown/terminal orders should return an explicit deterministic outcome.
- An order may transition through accepted, partially filled, fully filled, cancelled and/or replaced states as defined in the model. Never resurrect terminal quantity silently.
- Validate impossible input (negative or zero values, invalid ticks, unknown instruments/accounts, integer overflow, crossed state corruption).
- For each order at a stable point, total executed quantity plus remaining active quantity is consistent with its accepted/revised quantity and cancellation history; rejected commands do not change this accounting.

## Determinism contract

**Given identical canonical command sequence, initial state, configuration/rules version and any deterministic seeds, the engine produces identical canonical business events and final state.**

This implies:

- Processing order is independent of nondeterministic thread scheduling.
- All externally relevant state-changing controls are recorded, including kill switch, balances, risk limit changes or resets if supported.
- Generated IDs are deterministic or included in the canonical event stream.
- Canonical comparisons exclude non-semantic timestamps, pointer addresses, thread IDs, scheduling timing and non-deterministically ordered hash-map traversal.
- Monetary/price math uses integer or fixed-point units with explicit overflow checks, not binary floating-point values as book prices.
- Equivalent replay runs should match event count/order/content and a stable digest or comparison of final order-book and account state.

When replaying a multi-client network capture, the **sequenced admission log** is the truth: replay does not infer a globally valid input order from per-connection timestamps.

## Market data versus order entry

Exchange order commands are the primary writable events. Derived events such as trades, top-of-book changes and depth updates can serve as synthetic market data for subscribers. External market data and complex data-feed normalization are not needed for the core project; adding them should not blur who owns the authoritative order book.

## Useful invariants

- Best bid is below best ask after command completion whenever both sides have resting liquidity.
- Every active order has positive remaining quantity, valid price/tick, unique ID and exactly one active price-level membership.
- FIFO order at each price is consistent with documented time-priority rules.
- No trade quantity exceeds available incoming or resting quantity; no negative resting quantity.
- Sum of emitted fills matches both sides' executed quantities and resulting positions/cash under the simplified accounting model.
- Rejected operations leave book, risk and account state unchanged unless the explicit outcome contract says otherwise.
- No events silently disappear under load; admitted, rejected, buffered and (if explicitly supported) dropped inputs are distinguishable.
