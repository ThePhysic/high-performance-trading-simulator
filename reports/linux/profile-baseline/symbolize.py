from __future__ import annotations

import bisect
import collections
import json
from pathlib import Path
import re
import sys

directory = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(__file__).resolve().parent
raw = (directory / 'raw-samples.txt').read_text()
report = (directory / 'self.txt').read_text()
lost_match = re.search(r'^# Total Lost Samples: ([0-9,]+)$', report, re.M)
count_match = re.search(r'^# Samples: ([0-9,]+) ', report, re.M)
if not lost_match or not count_match:
    raise RuntimeError('perf sample/loss headers are missing')
lost_samples = int(lost_match[1].replace(',', ''))
declared_samples = int(count_match[1].replace(',', ''))
symbols = []
for line in (directory / 'symbols.txt').read_text().splitlines():
    match = re.match(r'^([0-9a-f]+) ([TtWw]) (.+)$', line)
    if match:
        symbols.append((int(match[1], 16), match[3]))
symbols.sort()
offsets = [item[0] for item in symbols]

maps = []
samples = []
current = None
for line in raw.splitlines():
    mapping = re.search(r'PERF_RECORD_MMAP2.*\[0x([0-9a-f]+)\(0x([0-9a-f]+)\) @ (0x[0-9a-f]+|0) .*\]: r-xp (.+)$', line)
    if mapping:
        start, size, file_offset = (int(mapping[i], 16) for i in (1, 2, 3))
        maps.append((start, start + size, file_offset, mapping[4]))
        continue
    header = re.match(r'^hpts\s+\d+\s+[\d.]+:\s+(\d+) cycles:Pu:', line)
    if header:
        current = {'weight': int(header[1]), 'frames': []}
        samples.append(current)
        continue
    frame = re.match(r'^\s+([0-9a-f]+) .*\(([^)]+)\)$', line)
    if current is not None and frame:
        current['frames'].append((int(frame[1], 16), frame[2]))

def symbol(address: int, dso: str, caller: bool) -> str:
    if not dso.endswith('/hpts'):
        return Path(dso).name
    address -= int(caller)
    for start, end, file_offset, path in maps:
        if path == dso and start <= address < end:
            offset = address - start + file_offset
            index = bisect.bisect_right(offsets, offset) - 1
            if index >= 0:
                return symbols[index][1]
    return 'unmapped hpts'

total_weight = sum(sample['weight'] for sample in samples)
if len(samples) != declared_samples or total_weight <= 0:
    raise RuntimeError('raw sample parsing does not reconcile with the perf report')
self_weight = collections.Counter()
inclusive_weight = collections.Counter()
canonical_weight = core_weight = snapshot_weight = 0
for sample in samples:
    frames = [symbol(address, dso, index > 0) for index, (address, dso) in enumerate(sample['frames'])]
    if frames:
        self_weight[frames[0]] += sample['weight']
    for name in set(frames):
        inclusive_weight[name] += sample['weight']
    if any('hpts::Outcome::canonical' in name for name in frames):
        canonical_weight += sample['weight']
    if any('hpts::Engine::process' in name or 'hpts::Engine::Impl::process' in name for name in frames):
        core_weight += sample['weight']
    if any('hpts::Snapshot::canonical' in name for name in frames):
        snapshot_weight += sample['weight']

def ranked(counter):
    return [{'symbol_or_dso': name, 'cycle_weight': weight, 'percent': weight / total_weight * 100}
            for name, weight in counter.most_common(30)]

summary = {
    'sample_count': len(samples),
    'total_cycle_weight': total_weight,
    'lost_samples': lost_samples,
    'event': 'cycles:Pu',
    'frequency_requested_hz': 499,
    'workload': {'profile': 'mixed', 'commands': 300000, 'seed': 42, 'mode': 'direct'},
    'inclusive_cycle_weight_percent': {
        'Outcome::canonical': canonical_weight / total_weight * 100,
        'Engine::process_or_Impl::process': core_weight / total_weight * 100,
        'Snapshot::canonical': snapshot_weight / total_weight * 100,
    },
    'top_self': ranked(self_weight),
    'top_inclusive': ranked(inclusive_weight),
    'method': 'Parse retained perf script samples and executable MMAP2 load ranges. Map executable addresses to nearest preceding text/weak symbol from GNU nm --defined-only; subtract one from caller return addresses. Shared-library frames are grouped by DSO because this perf build has libelf disabled. Weights are sampled cycle periods, not raw sample counts.',
    'scope': 'Entire CLI process: parsing/preload, execution, canonical output/digest, final invariants/state, report. Percentages are not limited to the timed benchmark interval. Frame-pointer callers in system libraries may be incomplete.',
    'limitations': ['Single short capture; no uncertainty estimate or statistical significance claim.', 'Nearest-symbol lookup may attribute PLT/gaps to a preceding symbol; small incidental symbol percentages should not be overinterpreted.', 'No CPU affinity/isolation applied.', 'No kernel/sysctl/permission changes.', 'Initial capture on /mnt/c failed with Bad address; valid capture used Linux home-cache storage and was copied afterward.'],
}
(directory / 'summary.json').write_text(json.dumps(summary, indent=2) + '\n')
(directory / 'symbolize.py').write_text(Path(__file__).read_text())
(directory / 'commands.txt').write_text('''# Profiling commands actually executed; build settings are retained in build-cache.txt.
python3 /mnt/c/Users/ridle/high-performance-trading-simulator/tools/generate_workload.py --output-dir /mnt/c/Users/ridle/high-performance-trading-simulator/reports/generated/linux-profile --count 300000 --seed 42 --profile mixed
perf stat -e cycles:u,instructions:u,branches:u,branch-misses:u,task-clock,context-switches,cpu-migrations,page-faults -o /mnt/c/Users/ridle/high-performance-trading-simulator/reports/linux/profile-baseline/perf-stat.txt -- /home/ridle/.cache/hpts-builds/baseline/hpts bench --config /mnt/c/Users/ridle/high-performance-trading-simulator/reports/generated/linux-profile/accounts.cfg --input /mnt/c/Users/ridle/high-performance-trading-simulator/reports/generated/linux-profile/commands.txt --mode direct --output /mnt/c/Users/ridle/high-performance-trading-simulator/reports/linux/profile-baseline/bench-stat.json
perf record -F 499 -g --call-graph fp -o /home/ridle/.cache/hpts-profile-baseline/perf.data -- /home/ridle/.cache/hpts-builds/baseline/hpts bench --config /mnt/c/Users/ridle/high-performance-trading-simulator/reports/generated/linux-profile/accounts.cfg --input /mnt/c/Users/ridle/high-performance-trading-simulator/reports/generated/linux-profile/commands.txt --mode direct --output /mnt/c/Users/ridle/high-performance-trading-simulator/reports/linux/profile-baseline/bench-sampled.json
perf report --stdio --no-children --sort=dso,symbol --percent-limit 0.5 -i /home/ridle/.cache/hpts-profile-baseline/perf.data
perf report --stdio -g graph --percent-limit 1 -i /home/ridle/.cache/hpts-profile-baseline/perf.data
perf script --show-mmap-events -i /home/ridle/.cache/hpts-profile-baseline/perf.data
nm -n -C --defined-only /home/ridle/.cache/hpts-builds/baseline/hpts
python3 reports/linux/profile-baseline/symbolize.py
''')
source_commit = (directory / 'source-commit.txt').read_text().strip()
binary_hash = (directory / 'hashes.txt').read_text().split()[0]
(directory / 'README.md').write_text(f'''# Actual Linux baseline profile

This profile was collected before changing canonical serialization. It is evidence for choosing a small serializer experiment, not evidence of an optimization gain. Source commit: `{source_commit}`; binary SHA-256: `{binary_hash}`. Exact source hashes, initial tracked-source status, GNU/CMake settings, generated input fingerprints, and commands are retained alongside this report.

The binary was built on Ubuntu with GNU C++ 15.2, `RelWithDebInfo` (`-O2 -g -DNDEBUG`) and `-fno-omit-frame-pointer`. The synthetic `mixed` profile contains 300,000 commands, seed 42, queue capacity 256, direct execution. All commands were accepted, with 54,776 executions. Counters and sampling were separate invocations; both produced trace `eeadadf0210d56d0` and state `90b17c3466701e9f`.

## Recorded evidence

- `perf-stat.txt`: 3,416,129,488 user cycles, 8,430,470,514 user instructions, 7,214,968 branch misses (0.43%), and 1.893442669 seconds for the entire process. The CLI execution interval was 0.862893994 seconds; it excludes preload/final-state work. These are instrumented measurements, not a repeated performance comparison.
- `perf-record-nativefs-stderr.txt`, `self.txt`, `callgraph.txt`: 727 user-space cycle samples, zero lost samples, 3,313,152,733 total sampled cycle weight. Raw `perf.data` is retained locally and ignored by Git; `raw-samples.txt` retains decoded sample/mmap records for review.
- `summary.json`: {summary['inclusive_cycle_weight_percent']['Outcome::canonical']:.2f}% of sampled cycle weight has `Outcome::canonical` in its visible call chain; {summary['inclusive_cycle_weight_percent']['Engine::process_or_Impl::process']:.2f}% has `Engine::process`/`Impl::process`; {summary['inclusive_cycle_weight_percent']['Snapshot::canonical']:.2f}% has `Snapshot::canonical`. These inclusive groups are not additive.
- `hot-addresses.txt`: GNU `addr2line` maps hot executable addresses to the integer/bool `ostream` formatting in `Outcome::canonical` at `engine.cpp` lines 173/176/177/179. `libstdc++` accounts for {summary['top_self'][0]['percent']:.2f}% of self cycle weight across the full process, which also includes input parsing and final state generation.

## Interpretation and limits

Canonical outcome serialization is material enough to justify testing locale-independent `to_chars` plus string append, while preserving exact output bytes, escaping, signed/unsigned extremes, boolean representation, and event ordering. The profile does not justify changing risk/matching rules or asserting a speedup in advance.

Sampling covers the entire CLI process, including input parsing/preload, execution, serialization/digest, final invariant/state work, sorting, and report generation. It is not a core-only or timed-window profile. This is one short capture, without CPU affinity or host isolation, so percentages are approximate and no confidence interval is claimed.

The installed WSL `perf` has `libelf`, DWARF, and unwind support disabled (see `perf-build-options.txt`). Therefore its reports show raw addresses. `symbolize.py` independently maps the retained executable `MMAP2` ranges and GNU `nm` text symbols, using sampled-cycle periods and caller return-address adjustment. Shared-library frames are grouped by DSO; frame-pointer chains through system libraries may be incomplete. Nearest-symbol lookup can attribute PLT/gaps to preceding names, so small incidental function shares are not interpreted. The source-line mappings and raw records allow checking the principal serializer finding.

An initial attempt to write the capture directly to `/mnt/c` failed with `Bad address` (`perf-record-stderr.txt`). The valid capture was written to persistent Linux home-cache storage and copied afterward. No kernel settings, sysctls, services, or permissions were changed. `perf_event_paranoid` remained 2.
''')
print(json.dumps({key: summary[key] for key in ('sample_count', 'total_cycle_weight', 'inclusive_cycle_weight_percent')}, indent=2))
