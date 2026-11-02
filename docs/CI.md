# Continuous integration

[Hosted runs and current status](https://github.com/ThePhysic/high-performance-trading-simulator/actions/workflows/ci.yml) · [Workflow source](../.github/workflows/ci.yml)

The [first confirmed hosted run](https://github.com/ThePhysic/high-performance-trading-simulator/actions/runs/41390129509), at revision `b742dcf7f8c34533529a4ec32b2d4a100917c272`, passed all five jobs and all three CTest targets in each job. Later revisions must be assessed from their own runs; the README badge follows current `main`.

The workflow runs on pushes, pull requests, and manual dispatch. Every job builds the C++20 application with `BUILD_TESTING=ON`, Python 3.12, and the existing `engine_contract`, `queue_contract`, and `cli_contract` targets. CTest failures fail the job; each test has a 180-second limit and each job a 15-minute limit.

| Job | Hosted image | Compiler / configuration |
|---|---|---|
| Linux Debug | Ubuntu 24.04 | GCC 13 / Debug |
| Linux Release | Ubuntu 24.04 | GCC 13 / Release |
| ASan/UBSan | Ubuntu 26.04 | GCC 15 / Debug, `address,undefined` |
| TSan | Ubuntu 26.04 | GCC 15 / Debug, `thread` |
| Windows Release | Windows 2025 | MSVC / Release |

Sanitizers use separate builds/jobs. Linux runs skip the Windows-only path-case check; Windows skips Linux `/dev/full` checks. The remaining contracts, including reference-model sequences, bounded queues, malformed inputs, broken pipes, replay, and generated workloads, still run. No failing job is converted into success with `continue-on-error`.

Workflow permissions are limited to `contents: read`. Official checkout/Python actions are pinned to exact commits, checkout credentials are not persisted, and no broker credentials, repository secrets, cloud deployment, or `perf` permissions are needed. The small workload runs inside the CLI contract are correctness checks, with no timing threshold. Hosted CI is not a repeat of the [recorded optimization experiment](../reports/linux/SERIALIZATION_EXPERIMENT.md).

## Reproduce the ordinary Linux checks

```sh
cmake -S . -B build/debug -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON
cmake --build build/debug --parallel 2
ctest --test-dir build/debug --output-on-failure --timeout 180
cmake -S . -B build/release -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
cmake --build build/release --parallel 2
ctest --test-dir build/release --output-on-failure --timeout 180
```

For local sanitizer/profiling commands and their runtime requirements, see [Performance](PERFORMANCE.md). The [validation record](../reports/VALIDATION.md) preserves historical source identity and platform-specific results. Current hosted success must be checked on the linked run; it cannot be inferred from these commands or an older test log.

## Licensing

No software license is specified. Selecting one requires the repository owner's decision; CI and public visibility do not make that decision.
