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
import hashlib
import subprocess
base = directory.parent / 'profile-baseline'
base_summary = json.loads((base / 'summary.json').read_text())
manifest = json.loads((directory / 'generation.json').read_text())
workload_hashes = [line.split()[0] for line in (directory / 'workload-hashes.txt').read_text().splitlines() if line.strip()]
if workload_hashes != [manifest['config_sha256'], manifest['commands_sha256']]:
    raise RuntimeError('candidate input hashes differ from baseline manifest')
base_bench = json.loads((base / 'bench-stat.json').read_text())
stat_bench = json.loads((directory / 'bench-stat.json').read_text())
sampled_bench = json.loads((directory / 'bench-sampled.json').read_text())
for result in (stat_bench, sampled_bench):
    for key in ('commands', 'accepted_orders', 'accepted_commands', 'rejected_commands', 'executions', 'trace_digest', 'state_digest'):
        if result[key] != base_bench[key]:
            raise RuntimeError('candidate logical result differs: ' + key)
source_commit = (directory / 'source-commit.txt').read_text().strip()
if source_commit != '98ea79593f8abdc98cf5409ba94627a0749c2996':
    raise RuntimeError('unexpected candidate source commit')
binary = Path('/home/ridle/.cache/hpts-builds/candidate/hpts')
binary_hash = (directory / 'hashes.txt').read_text().split()[0]
if hashlib.sha256(binary.read_bytes()).hexdigest() != binary_hash:
    raise RuntimeError('candidate binary changed after capture')
reported = re.search(r'^# Event count \(approx\.\): ([0-9,]+)$', report, re.M)
if not reported or int(reported[1].replace(',', '')) != total_weight:
    raise RuntimeError('cycle weights do not reconcile with perf headers')
def counters(text):
    result = {}
    for line in text.splitlines():
        item = re.match(r'^\s+([0-9,]+)\s+([\w:-]+)', line)
        if item:
            result[item[2]] = int(item[1].replace(',', ''))
    return result
old_counters = counters((base / 'perf-stat.txt').read_text())
new_counters = counters((directory / 'perf-stat.txt').read_text())
comparison = {
    'baseline_source_commit': (base / 'source-commit.txt').read_text().strip(),
    'candidate_source_commit': source_commit,
    'whole_process_single_run_counters': {key: {'baseline': old_counters[key], 'candidate': new_counters[key], 'candidate_over_baseline': new_counters[key] / old_counters[key]} for key in ('cycles:u', 'instructions:u', 'branches:u', 'branch-misses:u')},
    'inclusive_sampled_cycle_percent': {key: {'baseline': value, 'candidate': summary['inclusive_cycle_weight_percent'][key]} for key, value in base_summary['inclusive_cycle_weight_percent'].items()},
    'sample_counts': {'baseline': base_summary['sample_count'], 'candidate': summary['sample_count']},
    'logical_results_match': True, 'same_input_sha256': True,
    'interpretation': 'Separate short whole-process observations at different times; not a repeated speedup estimate. Interleaved A/B is the performance comparison. Matching/risk code did not change. Inclusive shares are not additive and unchanged work can occupy a larger fraction after another cost shrinks.'
}
summary.update(role='candidate', source_commit=source_commit, binary_sha256=binary_hash, same_input_sha256_as_baseline=True, logical_results_match_baseline=True)
summary['limitations'] = [value for value in summary['limitations'] if not value.startswith('Initial capture')]
summary['limitations'].append('Capture used persistent Linux-native cache and was copied afterward.')
(directory / 'summary.json').write_text(json.dumps(summary, indent=2) + '\n')
(directory / 'comparison.json').write_text(json.dumps(comparison, indent=2) + '\n')
(directory / 'symbolize.py').write_text(Path(__file__).read_text())
hot_offsets = collections.Counter()
for sample in samples:
    for index, (address, dso) in enumerate(sample['frames']):
        if 'hpts::Outcome::canonical' not in symbol(address, dso, index > 0):
            continue
        adjusted = address - int(index > 0)
        for start, end, file_offset, path in maps:
            if path == dso and start <= adjusted < end:
                hot_offsets[adjusted - start + file_offset] += sample['weight']
                break
hot = [f'0x{offset:x}' for offset, _ in hot_offsets.most_common(12)]
if hot:
    (directory / 'hot-addresses.txt').write_text(subprocess.check_output(['addr2line', '-a', '-C', '-f', '-i', '-e', str(binary), *hot], text=True))
commands = (base / 'commands.txt').read_text().replace('profile-baseline', 'profile-candidate').replace('/baseline/hpts', '/candidate/hpts')
commands = '\n'.join(line for line in commands.splitlines() if 'generate_workload.py' not in line)
(directory / 'commands.txt').write_text('# Identical baseline workload reused; hashes independently verified.\n' + commands + '\n')
canonical = comparison['inclusive_sampled_cycle_percent']['Outcome::canonical']
core = comparison['inclusive_sampled_cycle_percent']['Engine::process_or_Impl::process']
(directory / 'README.md').write_text(f'''# Actual Linux candidate profile

Candidate source commit: `{source_commit}`. Binary SHA-256: `{binary_hash}`. Exact source hashes, compiler/build settings, commands, environment, and input hashes are retained. The same 300,000-command mixed workload (seed 42, queue capacity 256) and direct mode were used. Input SHA-256s, all logical counts, trace `{stat_bench['trace_digest']}`, and state `{stat_bench['state_digest']}` match the baseline.

The valid capture has {len(samples)} user-cycle samples, {lost_samples} lost, and {total_weight:,} total sampled-cycle weight. `summary.json` reports canonical outcome serialization in call chains carrying {canonical['candidate']:.2f}% of cycle weight, versus {canonical['baseline']:.2f}% in the earlier baseline. Engine processing occupies {core['candidate']:.2f}% versus {core['baseline']:.2f}%. Inclusive shares are not additive; a larger relative share does not establish a regression in unchanged matching code.

`comparison.json` retains full-process hardware counters from separate stat invocations and the sampled shares. These are single short observations at different times, without affinity/isolation, not repeated speedup estimates. Use the independent interleaved A/B experiment for performance gains and variability. The serializer occupies a smaller visible share; that is consistent with removing stream-based numeric formatting, not evidence that risk/matching logic changed or a universal gain.

Both binary builds use GNU 15.2 RelWithDebInfo (`-O2 -g -DNDEBUG`) with `-fno-omit-frame-pointer`; sampling is 499 Hz. The entire CLI process is sampled, including parsing/preload, execution/output/digest, final invariants/snapshot, sorting, and report generation. It is not a core-only or precisely timed-window profile. Short captures and incomplete system-library frame-pointer chains limit precision.

Installed perf has libelf/DWARF unwinding disabled. As in the baseline, `symbolize.py` maps retained MMAP2 ranges and GNU nm text symbols, adjusts caller return addresses, and uses sampled-cycle periods. It reconciles raw sample/loss counts and cycle weights with perf headers. Shared-library frames are grouped by DSO; nearest-symbol lookup can attribute PLT/gaps to preceding symbols, so small incidental shares should not be overinterpreted. `hot-addresses.txt` retains GNU addr2line mappings for hot serializer frames. Text/raw records and debug-symbol mappings make the interpretation reviewable.

Capture data was written on persistent Linux-native cache storage and copied afterward. Raw `perf.data` remains locally present and ignored by Git; textual records are retained. No kernel settings, sysctls, services, or permissions were changed.
''')
print(json.dumps({'samples': len(samples), 'lost_samples': lost_samples, 'inclusive': summary['inclusive_cycle_weight_percent'], 'counter_comparison': comparison['whole_process_single_run_counters']}, indent=2))