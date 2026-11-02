# Executed validation and remaining acceptance work

This record distinguishes actual native verification from required checks that the current session could not execute. The project goal is not complete: Linux validation/profiling, a final clean Astra review, and verified private GitHub publication remain pending.

## Native Windows verification

Environment: AMD Ryzen 9 7940HS, Windows x64, MSVC 19.51.36260.0, C++20, Visual Studio CMake 4.3.1-msvc1. Release compiler configuration and binary/source fingerprints are retained in [native-build.json](native-build.json). No CPU affinity or scheduling isolation was applied.

- Release CMake build succeeded. CTest passed all three suites: engine/reference (10.96 s), bounded queue (0.09 s), and CLI (2.27 s), total 13.44 s. This initial CLI run preceded the expanded failure corpus; the subsequent expanded native CLI run passed all 14 registered cases, with one expected Linux-only `/dev/full` skip, in 3.410 s.
- The engine/reference suite covers 16 deterministic seeds x 1,200 commands (19,200 commands), stepwise invariants, independent vector-scan results, and exact replay comparisons. Hand fixtures exercise priority, partial fills, replacements, ownership, duplicates, risk, kill controls, self-cross rejection, capacity, and integer boundaries.
- Queue checks include blocking backpressure, close/drain and wakeups, move-only ownership, and 8,000 items across four producers with per-producer FIFO and no loss/duplication.
- A separate full MSVC AddressSanitizer Release CMake build passed all three CTest suites: engine 90.63 s, queue 0.13 s, CLI 8.45 s, total 99.30 s, with no runtime sanitizer findings. CLI ran 14 registered cases with one Linux-only skip. The final benchmark-baseline assertion strengthening was also rerun successfully on the native non-sanitized CLI.
- CLI checks include exact self-contained replay, truncation/footer/trailing-data detection, changed trace/state detection, 100 seeded malformed command cases, size/integer/resource bounds, file aliases, locale environments, interactive inspection, all eight generated profiles, and direct/queued equivalence to actual run output.
- The 22-command demonstration executed and replayed successfully: trace `2fd7789a4a51cdad`, state `994dfc9066d8a373`.

Retained raw logs: [Release CTest](native-release-ctest.log), [AddressSanitizer CTest](native-asan-ctest.log). Native builds emitted MSB8029 about the temporary build directory; the sanitizer link also emitted LNK4302 recommending linker debug information. Neither was a source compiler warning or a runtime finding. Windows ASan is not evidence of Linux ASan/UBSan/TSan coverage.

## Adversarial-review output failure fix

Astra medium review reproduced a buffered stdout error: version, benchmark stdout, run summary, and replay summary could lose all output on a closed pipe while exiting zero. Commit `bb48c7b` adds one checked stdout flush on every successful top-level exit and ignores POSIX SIGPIPE so EPIPE can become the documented error exit. Regression tests first reproduced all four failures against the old native binary; the corrected native binary passes exact exit-code-2 assertions.

The corrected build passed 16 registered native CLI cases (14 passed, two expected Linux-only skips) in 3.392 seconds. An affected-target AddressSanitizer rebuild then passed the same CLI suite in 8.76 seconds, with no runtime findings; [retained fix validation](native-asan-cli-stdout.log). The unchanged engine and queue retain their full prior sanitizer evidence. Linux closed-pipe and `/dev/full` stdout cases remain to execute on Linux. The benchmark artifact below was regenerated with the corrected binary, and its hash matches the refreshed build record.

Astra found no additional concrete matching/risk/replay/concurrency defect in its first review, but explicitly withheld completion approval because actual Linux validation/profiling and private publication are still missing. Re-review of the corrected final implementation is pending.

## Measured native topology comparison

The [full benchmark artifact](native-suite.json) contains all raw samples, variability summaries, input manifests/fingerprints, replay verification, and fixed file-run digest baselines. Eight profiles each used 20,000 commands, seed 42, queue capacity 256, three measured repetitions per mode, and one discarded fresh-process invocation per mode. All warmup and measured samples matched the file-run trace/state fingerprints after exact replay. Synthetic workloads, preloaded input, and in-process timing boundaries apply; see [methodology](../docs/PERFORMANCE.md).

Values below are means across three runs. p99 columns are mean per-run p99 core latency in microseconds, not a pooled percentile. Throughput includes serialization and trace digest work.

| Workload | Direct commands/s | Queued commands/s | Direct core p99 us | Queued core p99 us |
|---|---:|---:|---:|---:|
| resting | 123,837 | 148,734 | 5.13 | 8.13 |
| crossing | 112,267 | 115,126 | 3.97 | 7.47 |
| cancel | 140,215 | 140,071 | 2.93 | 6.37 |
| replace | 127,308 | 143,388 | 3.37 | 6.43 |
| dense | 128,975 | 139,569 | 5.20 | 8.57 |
| sparse | 112,094 | 144,290 | 8.20 | 10.40 |
| burst | 139,648 | 130,384 | 3.37 | 6.80 |
| mixed | 125,427 | 127,504 | 3.37 | 6.37 |

This comparison documents a concurrency tradeoff on this specific native host. It does not establish Linux performance, a specific cache/allocation bottleneck, or a profiling-backed optimization gain. No call-stack/hardware-counter profile was collected here. Durations are short and the host was not isolated, so these results should not support broad low-latency claims.

## Required external completion steps

1. Execute the Linux build, Debug/Release tests, relevant ASan/UBSan and concurrency checks, actual CPU profiling, and reproducible Linux benchmarks. Use `tools/validate_linux.sh`; record actual results and investigate failures. Current WSL enumeration returns `E_ACCESSDENIED`; the Docker Linux daemon pipe is absent.
2. Complete independent Astra medium adversarial review and fix/review every valid complaint until the reviewer has no remaining complaints. Native verification does not waive missing required Linux evidence.
3. Create the private GitHub repository, push the reviewed commit without force, and verify visibility plus remote/local commit equality. The configured GitHub account is `ThePhysic`, but this session's CLI API traffic is blocked through the restricted `127.0.0.1:9` proxy. Authentication validity is unverified while connectivity is blocked. No remote repository or push is claimed.

The working repository was created under the session's writable temporary workspace because creating the home-directory project folder was denied. No permissions, proxies, services, or credentials were changed to bypass those restrictions.
