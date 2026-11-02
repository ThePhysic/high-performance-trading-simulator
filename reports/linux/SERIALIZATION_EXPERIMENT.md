# Profile-backed canonical serialization experiment

The Linux baseline profile identified canonical outcome formatting as a material cost: `Outcome::canonical` appears in visible call chains carrying 37.37% of sampled cycle weight, compared with 22.39% for engine processing. The capture contains 727 user-cycle samples with zero lost samples. It covers the whole process, including work outside the timed benchmark loop. The [profile and reproducible symbol analysis](profile-baseline/README.md) explain the installed perf's limitations.

## Change and controls

Baseline source is commit `fec21af`; candidate source is `98ea795`. Only outcome serialization changed: one capacity-hinted destination string, direct escaping, and stack-buffer integer `to_chars` replace locale-aware stream formatting and temporary encoded fields. Matching, risk, snapshot/config serialization, canonical schema, and emitted bytes remain unchanged.

Both Linux binaries use GNU C++ 15.2, `RelWithDebInfo` (`-O2 -g -DNDEBUG`), C++20, and `-fno-omit-frame-pointer`. Binary fingerprints, environment, raw reports, and invocation order are retained in [serialization-comparison.json](serialization-comparison.json). The run used Ubuntu 26.04 under WSL2 on the same host, with no affinity or host isolation. No other project build, sanitizer, or profiling job ran during the comparison.

All eight synthetic profiles use 100,000 commands, seed 42, queue capacity 256, and direct execution. Each binary has one discarded fresh-process invocation per profile and five measured repetitions. Order alternates AB/BA between repetitions. The tool first requires exact event, state, and complete-journal byte equality, verifies each binary's replay, and replays the original journal through the candidate. Every warmup and measured sample must match fingerprints independently calculated from the original output files. All checks passed.

## Observed results

Values are means across five runs. Latency columns are means of the individual runs' nearest-rank p99 values, not pooled percentiles. Throughput includes canonical formatting and trace digest work but excludes parsing, final-state generation, and report writing. These are in-process measurements, with no network or durable-output cost.

| Profile | Baseline commands/s | Candidate commands/s | Ratio | Core p99 us, baseline -> candidate | Full-path p99 us, baseline -> candidate |
|---|---:|---:|---:|---:|---:|
| resting | 299,085 | 454,250 | 1.519 | 7.77 -> 7.04 | 10.80 -> 7.92 |
| crossing | 331,669 | 548,819 | 1.655 | 5.44 -> 4.86 | 8.86 -> 6.09 |
| cancel | 403,400 | 770,497 | 1.910 | 4.81 -> 3.77 | 7.96 -> 4.63 |
| replace | 389,712 | 649,412 | 1.666 | 4.52 -> 4.18 | 7.83 -> 5.34 |
| burst | 402,550 | 751,250 | 1.866 | 5.23 -> 4.29 | 8.21 -> 5.07 |
| dense | 302,348 | 454,533 | 1.503 | 7.56 -> 6.87 | 10.35 -> 7.78 |
| sparse | 228,753 | 333,937 | 1.460 | 12.43 -> 9.09 | 21.09 -> 10.16 |
| mixed | 396,486 | 677,853 | 1.710 | 4.44 -> 4.32 | 7.46 -> 5.39 |

Mean process CPU time decreased in every profile; for example, cancel fell from about 0.230 to 0.120 seconds per 100,000 commands. Process peak RSS was essentially unchanged (roughly 27.6-77.9 MiB across the workload shapes); small profile-level increases/decreases are retained in the raw samples. Allocation counts were not instrumented. No mean p99 regression appeared in this comparison, but individual samples and their variability remain relevant.

The evidence supports improved full-path throughput from canonical formatting on these workloads. Matching code was unchanged, so observed core-latency shifts are incidental effects/noise rather than a claimed matching-algorithm improvement. The trials are short, instrumented, synthetic, and run on a shared WSL host; the ratios do not establish a production exchange speed, worst-case guarantee, or general cross-machine result.

## Reproduce

See [the build and comparison commands](../../docs/PERFORMANCE.md#reproduce-the-serializer-experiment). `tools/compare_binaries.py` retains raw samples, variability, exact-output gates, and binary identity, and marks interrupted or failed comparisons incomplete. Final correctness/sanitizer verification of the candidate is retained separately under [validation](validation/README.md).
