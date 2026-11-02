# Terminal usage

The application is `hpts`. All state is local to one process/session and one simulated instrument, `SIM`. Commands use positive numeric command/order IDs, ASCII account IDs, integer tick prices, and integer unit quantities.

## Build and test

On Linux, provide CMake 3.20+, a C++20 compiler, thread support, and Python 3.10+:

```sh
cmake -S . -B build/debug -DCMAKE_BUILD_TYPE=Debug
cmake --build build/debug --parallel
ctest --test-dir build/debug --output-on-failure

cmake -S . -B build/release -DCMAKE_BUILD_TYPE=Release
cmake --build build/release --parallel
ctest --test-dir build/release --output-on-failure
```

CMake can build the C++ components without Python, but then omits `cli_contract`. A complete test run includes `engine_contract`, `queue_contract`, and `cli_contract`; use `ctest --test-dir build/debug -N` to inspect the configured tests. The workload/benchmark tooling also requires Python 3.10+ and uses only its standard library.

For multi-configuration generators, select the configuration with `cmake --build BUILD_DIR --config Debug` and `ctest --test-dir BUILD_DIR -C Debug`; the executable may be in a `Debug` subdirectory. Native MSVC is supported, but native Windows results do not establish Linux behavior or performance.

See [reports/VALIDATION.md](../reports/VALIDATION.md) for executed evidence. The build commands above do not imply that Linux checks have run.

## Help and operating modes

```text
hpts --help
hpts --version
hpts interactive --config FILE
hpts run --config FILE --input FILE --output FILE --state FILE [--journal FILE]
hpts replay --journal FILE --expect FILE --expect-state FILE
hpts bench --config FILE --input FILE [--mode direct|queued] [--output FILE]
```

Options are explicit name/value pairs. Unknown, repeated, or missing options are errors. File arguments must name distinct files, including resolved aliases where the platform can detect them. Output files are created or truncated; create parent directories before running. The program does not publish anything, open network ports, or contact a brokerage.

## Configuration

The example `examples/accounts.cfg` uses these directives:

```text
account A 1000000 -100 100 0
account B 1000000 -100 100 0
account C 1000 0 10 0
limits 128 512 256 32
bounds 1000000 1000000 16 32
```

Their fields are:

```text
account ID CASH MIN_POSITION MAX_POSITION INITIAL_POSITION
limits MAX_ACTIVE_ORDERS MAX_COMMAND_IDS MAX_ORDER_IDS QUEUE_CAPACITY
bounds MAX_PRICE MAX_QUANTITY MAX_ACCOUNTS MAX_ACCOUNT_ID_LENGTH
```

At least one unique account is required. Cash is nonnegative, initial position is within the inclusive position range, and the sum of initial cash must fit a signed 64-bit integer. A negative minimum position permits bounded short selling. IDs contain ASCII letters/digits, `_`, `-`, or `.`. The `limits` and `bounds` directives can appear at most once each; their omitted values use `EngineConfig`/`AppConfig` defaults.

All capacities are positive. The CLI hard caps active orders, command IDs, and lifetime order IDs at 1,000,000 each; accounts at 1,024; account-ID length at 64; and queue capacity at 65,536. The CLI also caps each run, replay, interactive session, or benchmark at 1,000,000 parsed commands. Lines may contain at most 4,096 bytes before the newline. Blank lines are ignored and `#` starts a comment.

For the full reservation equations, replacement policy, checked arithmetic, and finite-session admission behavior, see [DESIGN.md](DESIGN.md).

## Interactive simulation

```sh
./build/debug/hpts interactive --config examples/accounts.cfg
```

Enter commands such as:

```text
submit 1 A 1 buy 100 10
submit 2 B 2 sell 100 4
book
accounts
replace 3 A 1 100 5
cancel 4 A 1
kill 5 on
submit 6 A 3 buy 99 1
kill 7 off
state
quit
```

The initial buy rests. The sell fills four units against it at 100, leaving six. The replacement sets the remaining quantity to five and retains FIFO because it reduces quantity at the same price. Cancellation removes the residual. The later submission is rejected while the kill switch is enabled.

The exact command grammar is:

```text
submit COMMAND_ID ACCOUNT ORDER_ID buy|sell PRICE QUANTITY
cancel COMMAND_ID ACCOUNT ORDER_ID
replace COMMAND_ID ACCOUNT ORDER_ID PRICE NEW_REMAINING_QUANTITY
kill COMMAND_ID on|off
```

Command IDs identify requests; order IDs identify order lifecycles. Both must be positive and fit unsigned 64-bit integers. IDs need not be numerically consecutive or sorted. Reusing a retained command ID produces a duplicate rejection; reusing an accepted original order ID cannot recreate its terminal order. Replacement keeps the order ID and side. Its quantity is a new **remaining** quantity.

Interactive queries do not consume processing sequences or command IDs:

| Query | Output |
|---|---|
| `book` | Active orders by side, best price, then FIFO, with remaining quantity and priority |
| `accounts` | Cash, position, limits, reservations, kill state, and current sequence |
| `state` | Full canonical snapshot, including identity ledgers |
| `help` | Command help |
| `quit` | End the session |

Ordinary command parse errors print `input error` and allow another line. Business rejections print canonical outcomes and continue. Input-line overflow, stream failures, engine/infrastructure exceptions, and the hard session bound terminate with an error instead of continuing from uncertain state. EOF ends an otherwise valid session.

The interactive mode does not create a replay journal. Use `run` for recorded demonstrations.

## File execution and exact replay

```sh
mkdir -p build/demo
./build/debug/hpts run --config examples/accounts.cfg --input examples/demo.commands --output build/demo/events.txt --state build/demo/state.txt --journal build/demo/journal.txt
./build/debug/hpts replay --journal build/demo/journal.txt --expect build/demo/events.txt --expect-state build/demo/state.txt
```

The input file accepts the same state-changing commands as interactive mode, plus comments/blank lines. Interactive queries are not file commands. A successful run writes canonical outcome events, a final state snapshot, and, if requested, a self-contained journal. Its summary prints the processed count, trace fingerprint, and state fingerprint.

The journal structure is:

```text
HPTS-JOURNAL 1
account ...
limits ...
bounds ...
BEGIN
submit ...
cancel ...
kill ...
END COMMAND_COUNT TRACE_DIGEST STATE_DIGEST
```

The header contains the initial configuration; the commands are normalized copies in actual processing order, including commands that produce business rejections. A completion footer is written only after all input is processed, invariants pass, and event/state streams are flushed. A successful stream flush is not a durable storage commit.

Replay requires **all three** of `--journal`, `--expect`, and `--expect-state`. It reconstructs the initial configuration, processes commands in the recorded order, compares the exact canonical event and state bytes, verifies both completion fingerprints/count, rejects extra expected bytes or journal commands after completion, and prints a success summary only when all checks agree. FNV-1a fingerprints are reproducibility checks, not cryptographic authentication.

A missing or invalid `END` footer is incomplete input, even if a prefix of events looks valid. A run failing on malformed/truncated input or an output error may leave partial event/state files and a journal without completion. Preserve such outputs only as diagnostics; do not treat them as a complete recorded run. Crash recovery from a partial journal is not implemented.

## Rejections, resources, and exit status

Syntax-valid commands can be rejected for unknown account/order, wrong ownership, duplicate identity, invalid price/quantity, arithmetic overflow, position/buying-power limits, self trading, active-order capacity, lifetime ID capacity, or kill state. Their `Rejected` events have stable reasons, and they do not partially change book/account/risk state. Sequences and newly retained command IDs are explicit admission metadata and can advance on a business rejection.

The identity ledgers never evict old entries. At command-ID capacity, unseen requests, including cancels and kill commands, receive `COMMAND_CAPACITY` and do not execute; out-of-band inspection remains available. This is a finite-session boundary, not an emergency bypass. A rejected retry never creates another fill.

| Result | Exit status |
|---|---:|
| Help/version, successful run/replay/benchmark, normally ended interactive session | `0` |
| A completed run containing business/capacity rejections | `0` |
| Bad arguments/configuration, file-mode parse/read errors, replay mismatch/incompleteness, output errors, or infrastructure/session failure | `2` |

Scripts should check exit status and, for replayable runs, require a complete journal and verified expected files. Event output is the authority for individual command acceptance; a process exit code of zero does not mean every order was accepted.

## Workloads and benchmarks

```sh
python3 tools/generate_workload.py --output-dir build/workload --profile mixed --count 10000 --seed 42
./build/release/hpts bench --config build/workload/accounts.cfg --input build/workload/commands.txt --mode direct --output build/workload/direct.json
./build/release/hpts bench --config build/workload/accounts.cfg --input build/workload/commands.txt --mode queued --output build/workload/queued.json
```

`direct` uses one thread; `queued` uses bounded ingress and egress with one execution owner. Both must produce the same logical trace and final state for identical inputs/configuration. The JSON records actual counts, timing distributions, process resources, queue pressure, fingerprints, and available environment metadata. No result is a network-latency measurement. See [PERFORMANCE.md](PERFORMANCE.md) before interpreting or publishing numbers.
