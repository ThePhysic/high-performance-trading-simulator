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

## Measured native topology comparison

The [full benchmark artifact](native-suite.json) contains all raw samples, variability summaries, input manifests/fingerprints, replay verification, and fixed file-run digest baselines. Eight profiles each used 20,000 commands, seed 42, queue capacity 256, three measured repetitions per mode, and one discarded fresh-process invocation per mode. All warmup and measured samples matched the file-run trace/state fingerprints after exact replay. Synthetic workloads, preloaded input, and in-process timing boundaries apply; see [methodology](../docs/PERFORMANCE.md).

Values below are means across three runs. p99 columns are mean per-run p99 core latency in microseconds, not a pooled percentile. Throughput includes serialization and trace digest work.

| Workload | Direct commands/s | Queued commands/s | Direct core p99 us | Queued core p99 us |
|---|---:|---:|---:|---:|
| resting | 123,615 | 143,660 | 5.20 | 8.90 |
| crossing | 107,111 | 116,904 | 4.60 | 7.43 |
| cancel | 128,330 | 129,966 | 3.20 | 6.33 |
| replace | 118,939 | 125,035 | 4.37 | 7.03 |
| dense | 119,065 | 137,280 | 5.70 | 8.60 |
| sparse | 105,102 | 146,254 | 10.90 | 9.93 |
| burst | 137,404 | 143,602 | 3.53 | 6.07 |
| mixed | 113,337 | 116,990 | 4.63 | 7.43 |

This comparison documents a concurrency tradeoff on this specific native host. It does not establish Linux performance, a specific cache/allocation bottleneck, or a profiling-backed optimization gain. No call-stack/hardware-counter profile was collected here. Durations are short and the host was not isolated, so these results should not support broad low-latency claims.

## Required external completion steps

1. Execute the Linux build, Debug/Release tests, relevant ASan/UBSan and concurrency checks, actual CPU profiling, and reproducible Linux benchmarks. Use `tools/validate_linux.sh`; record actual results and investigate failures. Current WSL enumeration returns `E_ACCESSDENIED`; the Docker Linux daemon pipe is absent.
2. Complete independent Astra medium adversarial review and fix/review every valid complaint until the reviewer has no remaining complaints. Native verification does not waive missing required Linux evidence.
3. Create the private GitHub repository, push the reviewed commit without force, and verify visibility plus remote/local commit equality. The configured GitHub account is `ThePhysic`, but this session's CLI API traffic is blocked through the restricted `127.0.0.1:9` proxy. Authentication validity is unverified while connectivity is blocked. No remote repository or push is claimed.

The working repository was created under the session's writable temporary workspace because creating the home-directory project folder was denied. No permissions, proxies, services, or credentials were changed to bypass those restrictions.
