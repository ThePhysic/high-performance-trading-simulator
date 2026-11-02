# Actual Linux baseline profile

This profile was collected before changing canonical serialization. It is evidence for choosing a small serializer experiment, not evidence of an optimization gain. Source commit: `fec21af8afde76139782a3c6fd890462f81b0c97`; binary SHA-256: `f5a692cb94641bbe7dccbf2820eb70382438a8d2e4285465869f0d395226b42e`. Exact source hashes, initial tracked-source status, GNU/CMake settings, generated input fingerprints, and commands are retained alongside this report.

The binary was built on Ubuntu with GNU C++ 15.2, `RelWithDebInfo` (`-O2 -g -DNDEBUG`) and `-fno-omit-frame-pointer`. The synthetic `mixed` profile contains 300,000 commands, seed 42, queue capacity 256, direct execution. All commands were accepted, with 54,776 executions. Counters and sampling were separate invocations; both produced trace `eeadadf0210d56d0` and state `90b17c3466701e9f`.

## Recorded evidence

- `perf-stat.txt`: 3,416,129,488 user cycles, 8,430,470,514 user instructions, 7,214,968 branch misses (0.43%), and 1.893442669 seconds for the entire process. The CLI execution interval was 0.862893994 seconds; it excludes preload/final-state work. These are instrumented measurements, not a repeated performance comparison.
- `perf-record-nativefs-stderr.txt`, `self.txt`, `callgraph.txt`: 727 user-space cycle samples, zero lost samples, 3,313,152,733 total sampled cycle weight. Raw `perf.data` is retained locally and ignored by Git; `raw-samples.txt` retains decoded sample/mmap records for review.
- `summary.json`: 37.37% of sampled cycle weight has `Outcome::canonical` in its visible call chain; 22.39% has `Engine::process`/`Impl::process`; 2.49% has `Snapshot::canonical`. These inclusive groups are not additive.
- `hot-addresses.txt`: GNU `addr2line` maps hot executable addresses to the integer/bool `ostream` formatting in `Outcome::canonical` at `engine.cpp` lines 173/176/177/179. `libstdc++` accounts for 50.45% of self cycle weight across the full process, which also includes input parsing and final state generation.

## Interpretation and limits

Canonical outcome serialization is material enough to justify testing locale-independent `to_chars` plus string append, while preserving exact output bytes, escaping, signed/unsigned extremes, boolean representation, and event ordering. The profile does not justify changing risk/matching rules or asserting a speedup in advance.

Sampling covers the entire CLI process, including input parsing/preload, execution, serialization/digest, final invariant/state work, sorting, and report generation. It is not a core-only or timed-window profile. This is one short capture, without CPU affinity or host isolation, so percentages are approximate and no confidence interval is claimed.

The installed WSL `perf` has `libelf`, DWARF, and unwind support disabled (see `perf-build-options.txt`). Therefore its reports show raw addresses. `symbolize.py` independently maps the retained executable `MMAP2` ranges and GNU `nm` text symbols, using sampled-cycle periods and caller return-address adjustment. Shared-library frames are grouped by DSO; frame-pointer chains through system libraries may be incomplete. Nearest-symbol lookup can attribute PLT/gaps to preceding names, so small incidental function shares are not interpreted. The source-line mappings and raw records allow checking the principal serializer finding.

An initial attempt to write the capture directly to `/mnt/c` failed with `Bad address` (`perf-record-stderr.txt`). The valid capture was written to persistent Linux home-cache storage and copied afterward. No kernel settings, sysctls, services, or permissions were changed. `perf_event_paranoid` remained 2.
