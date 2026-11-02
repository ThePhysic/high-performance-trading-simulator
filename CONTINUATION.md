# Remaining completion work

The implementation and native verification are ready for the remaining external checks. The project goal is still unfinished. Read [GOAL.md](GOAL.md), [actual validation](reports/VALIDATION.md), and the Git history before continuing.

## Preserved state

- C++20 engine, CLI, complete replay journals, bounded queues, reference/invariant tests, failure regressions, workloads, benchmark tools, documentation, and native evidence are committed.
- Astra medium review found one buffered-stdout failure; it was reproduced, fixed, regression tested natively and under AddressSanitizer, and re-reviewed. Astra then reported no further code complaints while withholding overall approval for the external requirements below.
- No private remote, push, Linux result, or call-stack/hardware-counter profile is claimed.
- The working directory is under the allowed Windows temporary workspace because the intended home-directory project path was denied. A Git bundle can preserve the history when moving to a permitted workspace; do not discard the original until the replacement is verified.

## Required actions when access is available

1. Provide a permitted Linux runtime and working authenticated GitHub access. The last checks returned WSL `E_ACCESSDENIED`, no Docker Linux daemon pipe, and a blocked GitHub CLI proxy at `127.0.0.1:9`. Do not change credentials, proxies, or permissions to bypass a session restriction.
2. On Linux, execute `sh tools/validate_linux.sh`; run appropriate concurrency sanitizer checks and actual profiling with the helper's options where supported. Check the POSIX broken-pipe and `/dev/full` regressions, investigate failures, and retain actual build/test/profile artifacts. Document environmental limits rather than claiming skipped checks passed.
3. Use the profile to explain a concrete bottleneck/tradeoff and evaluate a justified change or design alternative on equivalent workloads. Retain baseline conditions, output equivalence, actual performance distributions/resources, and any regressions. Refresh the validation record and make meaningful commits for changes.
4. Ask an independent `gpt-6-astra` agent with medium reasoning effort to review the final implementation and actual Linux evidence. Fix every valid complaint, rerun affected checks, and re-review until it reports no remaining complaints. Existing native-only review is not unconditional completion approval.
5. Resolve the authenticated GitHub account and a suitable repository name. Reuse an existing private repository for this project if present; do not repoint unrelated remotes or change an unrelated repository's visibility. The configured account during development was `ThePhysic`, but connectivity prevented verification.
6. After inspection for credentials/unrelated artifacts and the final review, create a private repository if needed, push the reviewed commit without force, then verify private visibility and exact remote/local commit equality. Publishing is already authorized by the user.
7. Mark the goal complete only when all required implementation, Linux verification/profiling, clean Astra review, and verified private publication are finished. Report the repository link and actual evidence.
