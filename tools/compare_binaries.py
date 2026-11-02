#!/usr/bin/env python3
"""Compare two actual binaries on byte-equivalent, interleaved workloads."""

from __future__ import annotations

import argparse
import datetime
import json
from pathlib import Path
import subprocess

from benchmark_suite import (environment, fnv1a64_file, invoke, read_small_json,
                             summarize, validate_report)
from generate_workload import PROFILES, positive_count, queue_size, write_workload


def same_bytes(first: Path, second: Path) -> bool:
    with first.open("rb") as left, second.open("rb") as right:
        while True:
            a, b = left.read(65536), right.read(65536)
            if a != b:
                return False
            if not a:
                return True


def compare(args: argparse.Namespace) -> dict:
    binaries = {"baseline": args.baseline.resolve(strict=True),
                "candidate": args.candidate.resolve(strict=True)}
    destination = args.output_dir.resolve()
    destination.mkdir(parents=True, exist_ok=True)
    result = {
        "schema_version": 1, "complete": False,
        "collected_at_utc": datetime.datetime.now(datetime.timezone.utc).isoformat(),
        "binaries": {label: environment(binary) for label, binary in binaries.items()},
        "method": {"count": args.count, "seed": args.seed,
                   "repetitions": args.repetitions, "mode": args.mode,
                   "queue_capacity": args.queue_capacity,
                   "warmup_invocations_per_binary_profile": 1,
                   "order": "AB on odd repetitions, BA on even repetitions",
                   "timing": "CLI-defined in-process boundaries; file checks are untimed"},
        "invocation_order": [], "workloads": {},
    }
    summary = destination / "comparison.json"

    def save() -> None:
        summary.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")

    save()
    for profile in args.profiles:
        directory = destination / profile
        manifest = write_workload(directory, args.count, args.seed, profile, args.queue_capacity)
        config, commands = directory / "accounts.cfg", directory / "commands.txt"
        paths = {}
        for label, binary in binaries.items():
            events, state, journal = (directory / f"{label}.{suffix}"
                                      for suffix in ("events", "state", "journal"))
            invoke([str(binary), "run", "--config", str(config), "--input", str(commands),
                    "--output", str(events), "--state", str(state), "--journal", str(journal)], args.timeout)
            invoke([str(binary), "replay", "--journal", str(journal), "--expect", str(events),
                    "--expect-state", str(state)], args.timeout)
            paths[label] = (events, state, journal)
        if not all(same_bytes(paths["baseline"][index], paths["candidate"][index]) for index in (0, 1, 2)):
            raise RuntimeError(f"baseline/candidate canonical bytes differ: {profile}")
        expected = (fnv1a64_file(paths["baseline"][1]), fnv1a64_file(paths["baseline"][0]))
        # Independently replay the original binary's complete journal through
        # the candidate against the original event/state files.
        invoke([str(binaries["candidate"]), "replay", "--journal", str(paths["baseline"][2]),
                "--expect", str(paths["baseline"][0]), "--expect-state", str(paths["baseline"][1])], args.timeout)
        samples: dict[str, list[dict]] = {label: [] for label in binaries}

        def bench(label: str, suffix: str) -> dict:
            report = directory / f"{label}-{suffix}.json"
            invoke([str(binaries[label]), "bench", "--config", str(config), "--input", str(commands),
                    "--mode", args.mode, "--output", str(report)], args.timeout)
            value = read_small_json(report)
            validate_report(value, report, args.count)
            if (value["state_digest"], value["trace_digest"]) != expected:
                raise RuntimeError(f"benchmark differs from verified canonical files: {report}")
            return value

        for label in binaries:
            bench(label, "warmup")
        for repetition in range(1, args.repetitions + 1):
            order = ["baseline", "candidate"] if repetition % 2 else ["candidate", "baseline"]
            for label in order:
                samples[label].append(bench(label, str(repetition)))
                result["invocation_order"].append({"profile": profile, "repetition": repetition, "binary": label})
        summaries = {label: summarize(values) for label, values in samples.items()}
        baseline_rate = summaries["baseline"]["commands_per_second"]["mean"]
        candidate_rate = summaries["candidate"]["commands_per_second"]["mean"]
        result["workloads"][profile] = {
            "manifest": manifest, "exact_event_state_journal_equality": True,
            "candidate_replayed_baseline": True,
            "verified_run_digests": {"state_digest": expected[0], "trace_digest": expected[1]},
            "warmup_digests_checked": True, "samples": samples, "summaries": summaries,
            "candidate_to_baseline_throughput_ratio": candidate_rate / baseline_rate,
        }
        save()
        print(f"verified {profile}: candidate/baseline throughput={candidate_rate / baseline_rate:.3f}", flush=True)
    result["complete"] = True
    save()
    return result


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--baseline", type=Path, required=True)
    parser.add_argument("--candidate", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--count", type=positive_count, default=100000)
    parser.add_argument("--seed", type=int, default=42)
    parser.add_argument("--profiles", nargs="+", choices=PROFILES, default=list(PROFILES))
    parser.add_argument("--repetitions", type=int, choices=range(1, 21), default=5)
    parser.add_argument("--mode", choices=("direct", "queued"), default="direct")
    parser.add_argument("--queue-capacity", type=queue_size, default=256)
    parser.add_argument("--timeout", type=int, default=600)
    args = parser.parse_args()
    if args.timeout <= 0 or len(set(args.profiles)) != len(args.profiles):
        parser.error("timeout must be positive and profiles must be unique")
    try:
        compare(args)
    except (OSError, ValueError, RuntimeError, subprocess.SubprocessError) as error:
        parser.exit(1, f"binary comparison failed: {error}\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
