# Illustrative deterministic event traces

The following are **behavioral examples**, not required wire schemas. Prices and quantities are integer ticks/units. Accounts and instruments are synthetic; examples omit risk reservations unless they are being demonstrated. Within each example, inputs are processed in sequence.

## A. Price-time priority and partial fills

Initial book: empty. `BUY` bids rest unless crossing a sell.

| Sequence | Command | Expected logical effect |
|---:|---|---|
| 1 | A submits BUY 10 @ 100 (id B1) | B1 rests, 10 remaining |
| 2 | B submits BUY 7 @ 100 (id B2) | B2 rests behind B1 at 100 |
| 3 | C submits SELL 12 @ 100 (id S1) | Fill 10 with B1 at 100; fill 2 with B2 at 100 |

After sequence 3: B1 fully filled; B2 has 5 remaining at bid 100; S1 fully filled. The earlier B1 matched first despite identical prices. The event trace has two fills in that order.

## B. Best price first, then time

Start with resting asks: S1 SELL 5 @ 103, followed later by S2 SELL 4 @ 102, then S3 SELL 4 @ 102. Incoming B1 BUY 6 @ 103 should fill S2 for 4 @ 102 and then S3 for 2 @ 102; S1 at 103 is untouched. Best price outranks earlier arrival at a worse price.

## C. Modification and priority

Initial resting bids at 100: B1 BUY 8, followed by B2 BUY 6.

- If B1 reduces remaining quantity from 8 to 5 at the **same** price and the chosen policy retains priority, B1 stays before B2.
- If B1 changes price to 101, B1 is removed from the FIFO at 100 and gets a new priority position at 101.
- If B1 increases quantity at the same price, the conventional policy used in this package removes/renews time priority. The exact implemented rule must remain consistent in tests and replay.

## D. Cancellation and invalid operations

| Sequence | Command | Expected logical effect |
|---:|---|---|
| 1 | A submits BUY 3 @ 100 (B1) | Accepted and resting |
| 2 | A cancels B1 | Cancelled; no active quantity |
| 3 | B submits SELL 3 @ 100 (S1) | S1 rests: cannot fill cancelled B1 |
| 4 | A cancels B1 again | Deterministic terminal-order response; no state change |
| 5 | A resubmits prior `command_id` from sequence 1 | Explicit duplicate behavior; no second accepted B1 |

## E. Risk limit and kill switch

Assume A has zero net position, a conservative maximum net long exposure of 10 units, and adequate synthetic cash. If A already holds a BUY 8 open order, a new BUY 5 that would raise reserved potential long exposure to 13 is rejected. If A cancels the BUY 8, the reservation is released and a BUY 5 may be accepted. A sequenced kill-switch activation then blocks further new risk-increasing BUY/SELL orders, but A can cancel an existing order. All control events and resulting rejects must reproduce on replay.

## F. Replay equality

Given identical ordered inputs `[submit B1, submit S1, cancel unknown, activate kill switch, submit B2]`, initial accounts and instrument settings, and a documented engine/risk rules version:

- Run 1 and Run 2 produce identical canonical acknowledgement/reject/trade events in identical sequence.
- Final depth, open orders, account state and kill-switch state are equal.
- Logical trace digest and final state digest match.
- Real timestamps, thread scheduling and wall-clock duration are **not** required to match.

## G. Overload and malformed input

A malformed frame is rejected without reaching the business core. During a burst that fills an ingress buffer, the system uses a documented backpressure or reject response; it does not silently process a random subset and claim all commands were accepted. Replay inputs must reflect only what was actually admitted into the authoritative sequence.
