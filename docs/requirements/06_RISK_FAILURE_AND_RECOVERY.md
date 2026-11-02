# Simulated risk controls, failure handling and reliability

## Lightweight but credible risk model

The simulator should enforce basic limits without attempting to reproduce a real clearing broker or margin engine.

**Position limits.** Every simulated account/instrument has a maximum net long/short exposure or explicitly defined absolute-position range. Evaluate a new order conservatively with existing filled position and applicable outstanding orders so orders cannot collectively exceed a limit.

**Buying power / notional.** Account has a configured synthetic cash balance or buying-power allowance. New buy exposure must fit remaining capacity after reservations from open orders. Treat quantities and prices with integer/fixed-point arithmetic and explicit checked multiplication/addition.

**Reservations and fills.** Accepted open orders consume appropriate reservations; cancellations and fills release/update them under a documented model. Reject duplicate reservations or underflow/overflow. A short-selling policy may be simple (disabled or capped) but must be specified.

**Kill switch.** A deterministic, stateful control message blocks new orders or replacements that increase risk exposure. It should still allow cancellations, queries and the clearly defined safe operations required to reduce outstanding exposure. Any reset/reenable action must also be an explicitly sequenced, logged control. This is simulation logic, **not** a real-world emergency control.

**Atomicity.** A rejected command must not leave partial mutation of book, open orders, account balance or reservations. Executions must update both counterparties coherently according to the chosen simplified accounting contract.

## Engineering failure cases worth handling

| Condition | Meaningful expected behavior |
|---|---|
| Malformed/truncated network frame | Safe parse failure; do not mutate book |
| Unknown account/instrument/order | Stable, explicit rejection |
| Duplicate client command | Defined idempotency/rejection rule; never double-fill |
| Invalid replace or cancel | No accidental queue-position or account-state corruption |
| Full inbound queue | Controlled backpressure or explicit reject; no silent omission |
| Slow output consumer | Bounded output behavior with clear admission/output guarantees |
| Client disconnect mid-frame | No partial command execution; defined reconnect semantics |
| Unbounded burst | No uncontrolled growth or unexplained OOM behavior |
| Integer overflow in price × quantity | Detect/reject rather than wrap or corrupt risk accounting |
| Kill switch activates during busy run | Sequenced policy applied consistently to subsequent commands |
| Abnormal termination | Clear distinction between records that were merely processed in memory and those claimed durably committed |
| Log/output write error | Explicit error semantics; do not claim replay/recovery of missing commands |

## Failure injection and controlled fault scenarios

Useful reproducible local experiments include queue saturation, client stalls, malformed frames, duplicate IDs, mid-stream cancellation, log sink failure, worker delay, memory pressure and kill-switch activation while orders are arriving. They should be self-contained and avoid disturbing unrelated system services or other machines.

## Deterministic replay is not automatically crash recovery

- **Logical replay:** run a known complete canonical input log again; verify event trace and final state equality.
- **Crash recovery:** additionally requires defined on-disk persistence, acknowledgement/commit boundaries, checksummed or framed records, truncation handling and recovery of a last consistent state.

The first is core to this project. Durable crash recovery can be a meaningful optional extension if implemented with real evidence. It must not be implicitly promised merely because replay exists.

## Scope of the reliability claim

The simulator should provide safe local failure behavior, controlled overload handling and reproducible logical events. It does **not** need distributed consensus, high-availability replication, exchange-certified risk management, zero-downtime upgrades or internet-facing production hardening.
