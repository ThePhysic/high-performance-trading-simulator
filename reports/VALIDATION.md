# Executed validation

This record links actual execution evidence to the project requirements. The retained historical implementation tests and performance measurements use source commit `98ea79593f8abdc98cf5409ba94627a0749c2996`; the initial published revision `ab5cf7858bcab85266da2908d7c315ef3ac02ff9` added documentation and reports without changing those implementation sources. Earlier presentation-only revisions preserved implementation sources and tests. Commit `15a1d8b` later introduced file-handling improvements, modifying `src/main.cpp`, `CMakeLists.txt`, and `tests/test_cli.py`, and adding `src/files.cpp` and `include/hpts/files.hpp`. The shell validation helper now directs generated output to ignored `build/linux-reports/`; historical logs keep their original paths and source identity.

The later output-admission fixes replace path-based file reopening with retained-handle identity checks and validate all required options before output setup. They add preservation, filesystem-case, link-alias, large replay, and FIFO regressions without changing core matching/risk behavior, canonical output semantics, or the historical benchmark evidence below. New [CI runs](https://github.com/ThePhysic/high-performance-trading-simulator/actions/workflows/ci.yml) validate their respective revisions; historical results and case/skip counts in this record belong to their original tested commit. See [file admission](../docs/DESIGN.md#file-admission-and-output-ownership) for the exact setup/error guarantees.

## Linux correctness and sanitizers

The actual helper ran successfully on a clean native Linux clone:

```sh
HPTS_JOBS=4 HPTS_COUNT=20000 HPTS_REPETITIONS=3 sh tools/validate_linux.sh --tsan --perf
```

Environment: Ubuntu 26.04.1, WSL2 kernel `6.18.54.1-microsoft-standard-WSL2`, GNU C++ 15.2, CMake 4.2.3, Python 3.14.4, AMD Ryzen 9 7940HS. Source commit, compiler/cache flags, binary hashes, full helper log, four CTest logs, and exact results are retained in [Linux validation](linux/validation/README.md) and [summary.json](linux/validation/summary.json).

| Configuration | CTest suites passed | Engine fixture groups | CLI passed / skipped |
|---|---:|---:|---:|
| Debug | 3/3 | 11/11 | 15 / 1 |
| ASan/UBSan | 3/3 | 11/11 | 15 / 1 |
| TSan | 3/3 | 11/11 | 15 / 1 |
| Release | 3/3 | 11/11 | 15 / 1 |

The one Linux skip is the Windows-only case-insensitive path check. Linux broken-pipe, `/dev/full` file-output and stdout failures, journal truncation/tamper, parse/overflow/resource errors, interactive operations, and benchmark equivalence checks executed. ASan/UBSan and TSan produced no runtime findings.

Each engine suite checks 16 seeds x 1,200 stateful commands against an independent vector-scan model, verifies invariants after each step, and checks exact replay. Hand fixtures cover matching/FIFO, resting prices, partial fills, cancel/replace priority and atomicity, ownership, duplicates, reservations/accounting, kill controls, self-cross rejection, capacity, and integer/config extremes. Independent canonical fixtures cover numeric limits, booleans, empty/escaped/NUL/high-byte fields, enum spelling/defaults, and event ordinals through 10.

Queue checks include blocking backpressure, close/drain, all waiter wakeups, move-only values, and 8,000 items from four producers with per-producer FIFO and no loss/duplication. The CLI corpus includes 100 seeded malformed commands and all eight generated profiles.

The 22-command demonstration runs and replays exactly: trace `2fd7789a4a51cdad`, state `994dfc9066d8a373`. Replay compares actual canonical bytes and requires a complete self-contained journal.

## Actual Linux performance evidence

The helper completed seven default profiles x 20,000 commands x three repetitions x two modes: 42 measured reports, plus discarded warmup invocations. All measured digests match independently fingerprinted run outputs after exact replay. Its full [benchmark suite](linux/validation/benchmark-suite.json), actual perf counter/sample reports, environment, and method are retained. The capture reported 1,826 samples and zero lost samples.

A separate [profile-backed serializer experiment](linux/SERIALIZATION_EXPERIMENT.md) covers all eight profiles with 100,000 commands, five interleaved repetitions per binary, exact event/state/journal equality, and cross-version replay. The original `fec21af` binary and candidate `98ea795` binary share GNU 15.2 RelWithDebInfo flags and direct execution. Mean measured throughput ratios range from 1.460 to 1.910; raw CPU, memory, both latency distributions, variability, input and binary fingerprints, and invocation order are in [serialization-comparison.json](linux/serialization-comparison.json).

The [baseline profile](linux/profile-baseline/README.md) has 727 user-cycle samples with zero loss and identifies outcome formatting as material. The [candidate profile](linux/profile-candidate/README.md) has 517 samples with zero loss; visible serializer sampled-cycle share falls from 37.37% to 12.96%, and a separate full-process counter observation records fewer instructions. These are short separate whole-process captures, not a repeated speedup estimate or evidence of changed matching logic. The interleaved experiment is the source for throughput claims.

Benchmarks are synthetic and in-process, and the host was not isolated or pinned. No network latency, durable-output cost, allocation-count improvement, production exchange rate, or worst-case guarantee is claimed. See [methodology](../docs/PERFORMANCE.md) for boundaries and tradeoffs. The installed perf lacks ELF/DWARF support; retained raw sample/mmap records, symbols, address-to-source mappings, and reproducible symbolizers make the stated interpretation inspectable.

## Final native Windows portability check

The same implementation commit was rebuilt independently with MSVC 19.51, C++20, Release optimization, and serial VS18 CMake. All three CTest suites passed: engine 11.34 s (11/11 groups), queue 0.10 s, CLI 4.89 s (14 passed, two Linux-only skips), total 16.45 s. No source compiler warnings; MSBuild emitted its existing temporary-directory warning.

Retained [build identity and source/binary hashes](native-final/build.json), [full build/test log](native-final/build-and-test.log), and [CTest details](native-final/ctest-details.log) prove this final portability check. Earlier `native-*` artifacts at this directory's top level document the historical native implementation at commit `814aa56`; their source hashes and measured performance refer to that earlier version, not the optimized final binary. Historical Windows ASan results are retained, while final memory/race/UB claims use the Linux sanitizer runs above.

## Output-failure regression history

Development review identified buffered stdout false success. Checked successful exits fixed the native case; actual Linux testing then exposed recursive flushing through tied stderr, which was fixed in `fec21af`. The exercised stdout failures return the documented error exit 2 on Linux and Windows. The subsequent serializer experiment preserves the output contract and has independent byte fixtures and baseline comparisons.

## Hosted CI and public evidence

Current hosted checks are visible in [GitHub Actions](https://github.com/ThePhysic/high-performance-trading-simulator/actions/workflows/ci.yml); their configuration and scope are documented in [CI](../docs/CI.md). A workflow file is not itself a passing result. Assess the linked run's revision, individual jobs, and conclusion; platform-specific skips remain visible in CTest output. CI does not rerun the historical performance experiment or enforce speed thresholds.

The [first verified hosted run](https://github.com/ThePhysic/high-performance-trading-simulator/actions/runs/41390129509) tested revision `b742dcf7f8c34533529a4ec32b2d4a100917c272`: all five jobs passed, and each ran all three CTest targets. This confirms that run, not every future revision; the live workflow page and badge show current status.

The repository is [publicly available](https://github.com/ThePhysic/high-performance-trading-simulator). No separate independent review attestation is published; this record relies on reproducible checks and retained evidence.
