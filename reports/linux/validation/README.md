# Actual Linux validation

The unmodified committed candidate was cloned into a persistent native Linux cache and the actual `tools/validate_linux.sh --tsan --perf` helper completed with exit 0. This directory preserves selected text/JSON evidence; executables, generated command/event files, and binary perf data remain in the native cache and are not copied here.

Source commit: `98ea79593f8abdc98cf5409ba94627a0749c2996`.

Invocation: `HPTS_JOBS=4 HPTS_COUNT=20000 HPTS_REPETITIONS=3 timeout 1200s sh tools/validate_linux.sh --tsan --perf`.

All three CTest suites passed in Debug, ASan/UBSan, TSan, and Release. Each engine run includes 11 groups and 16 seeds x 1,200 differential commands. Each CLI run registered 16 cases, with 15 passes and one expected Windows-only case-alias skip; the Linux broken-pipe and `/dev/full` cases executed successfully. No runtime sanitizer findings appeared in the retained logs.

The helper's seven default workload profiles each used 20,000 commands, three repetitions per mode, both direct/queued modes, and one discarded invocation per mode. All measured digests match the replay-verified file baselines. The separate A/B experiment covers all eight profiles.

`perf stat`, `perf record`, and `perf report` succeeded on native Linux storage. The capture reported 1,826 samples. Counters/profile cover the whole benchmark process, not just its timed loop. Installed perf lacks libelf/DWARF support, so this helper's raw report can contain addresses; the separate baseline profile preserves explicit symbol analysis. These results are evidence for this WSL environment and do not imply bare-metal latency guarantees.

See `summary.json` for exact per-suite times, CLI skip counts, binary/source hashes, and flags. Raw CMake caches, compiler flag files, CTest logs, helper output, environment, benchmark samples/summaries, and perf text/JSON are retained beside it.
