#!/usr/bin/env python3
"""Verify workloads by replay, then collect actual repeated CLI benchmark reports."""

from __future__ import annotations

import argparse
import hashlib
import json
import math
import os
from pathlib import Path
import platform
import statistics
import subprocess
import sys
from typing import Any

from generate_workload import PROFILES, positive_count, queue_size, write_workload


def invoke(argv: list[str], timeout: int, *, allow_failure: bool = False) -> subprocess.CompletedProcess:
    result = subprocess.run(argv, capture_output=True, text=True, timeout=timeout, check=False)
    if result.returncode and not allow_failure:
        detail = (result.stderr or result.stdout)[-4000:]
        raise RuntimeError(f"command failed ({result.returncode}): {argv!r}\n{detail}")
    return result


def version_output(argv: list[str]) -> dict:
    try:
        result = invoke(argv, 15, allow_failure=True)
        return {"command": argv, "returncode": result.returncode,
                "output": (result.stdout + result.stderr)[:4000]}
    except (OSError, subprocess.SubprocessError) as exc:
        return {"command": argv, "unavailable": str(exc)}


def read_small_json(path: Path) -> dict:
    if path.stat().st_size > 1 << 20:
        raise RuntimeError(f"benchmark report exceeds 1 MiB: {path}")
    result = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(result, dict):
        raise RuntimeError(f"benchmark report is not an object: {path}")
    return result


def validate_report(sample: dict, path: Path, expected_commands: int) -> None:
    for key in ("commands", "accepted_orders", "executions", "elapsed_seconds", "commands_per_second",
                "core_latency_ns", "end_to_end_latency_ns", "state_digest", "trace_digest", "resources", "queue"):
        if key not in sample:
            raise RuntimeError(f"missing {key} in {path}")
    for key in ("commands", "accepted_orders", "executions"):
        if type(sample[key]) is not int or sample[key] < 0:
            raise RuntimeError(f"invalid nonnegative integer {key} in {path}")
    if sample["commands"] != expected_commands:
        raise RuntimeError(f"command count mismatch in {path}")
    for key in ("elapsed_seconds", "commands_per_second"):
        value = sample[key]
        if type(value) not in (int, float) or not math.isfinite(value) or value < 0:
            raise RuntimeError(f"invalid finite nonnegative {key} in {path}")
    if sample["elapsed_seconds"] <= 0:
        raise RuntimeError(f"nonpositive elapsed_seconds in {path}")
    for key in ("core_latency_ns", "end_to_end_latency_ns"):
        percentiles = sample[key]
        if not isinstance(percentiles, dict):
            raise RuntimeError(f"invalid percentile object {key} in {path}")
        values = []
        for percentile in ("p50", "p95", "p99"):
            value = percentiles.get(percentile)
            if type(value) not in (int, float) or not math.isfinite(value) or value < 0:
                raise RuntimeError(f"invalid {key}.{percentile} in {path}")
            values.append(value)
        if values != sorted(values):
            raise RuntimeError(f"unordered percentiles in {key} in {path}")
    for key in ("state_digest", "trace_digest"):
        if not isinstance(sample[key], str) or not sample[key]:
            raise RuntimeError(f"missing logical fingerprint {key} in {path}")
    for key in ("resources", "queue"):
        if not isinstance(sample[key], dict):
            raise RuntimeError(f"invalid object {key} in {path}")


def numeric_fields(value: Any, prefix: str = "") -> dict[str, float]:
    result: dict[str, float] = {}
    if isinstance(value, dict):
        for key, child in value.items():
            result.update(numeric_fields(child, f"{prefix}.{key}" if prefix else key))
    elif isinstance(value, (int, float)) and not isinstance(value, bool) and math.isfinite(value):
        result[prefix] = float(value)
    return result


def summarize(samples: list[dict]) -> dict:
    flattened = [numeric_fields(sample) for sample in samples]
    common = set(flattened[0]).intersection(*(set(sample) for sample in flattened[1:]))
    result = {}
    for key in sorted(common):
        values = [sample[key] for sample in flattened]
        result[key] = {"samples": len(values), "mean": statistics.mean(values),
                       "min": min(values), "max": max(values),
                       "population_stdev": statistics.pstdev(values)}
    return result


def fingerprint(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1 << 20), b""):
            digest.update(block)
    return digest.hexdigest()


def environment(binary: Path) -> dict:
    metadata = {
        "platform": platform.platform(), "system": platform.system(),
        "kernel": platform.release(), "machine": platform.machine(),
        "processor": platform.processor(), "logical_cpus": os.cpu_count(),
        "python": sys.version, "binary": str(binary), "binary_sha256": fingerprint(binary),
        "binary_version": version_output([str(binary), "--version"]),
        "cmake": version_output(["cmake", "--version"]),
        "host_cxx": version_output(["c++", "--version"]),
        "host_cxx_note": "Host c++ discovery alone does not establish this binary's compiler or build flags; use CLI report/build record.",
    }
    if hasattr(os, "sched_getaffinity"):
        try:
            metadata["cpu_affinity"] = sorted(os.sched_getaffinity(0))
        except OSError as exc:
            metadata["cpu_affinity_unavailable"] = str(exc)
    cpuinfo = Path("/proc/cpuinfo")
    if cpuinfo.is_file():
        try:
            for line in cpuinfo.read_text(encoding="utf-8").splitlines():
                if line.startswith("model name"):
                    metadata["cpu_model"] = line.partition(":")[2].strip()
                    break
        except OSError as exc:
            metadata["cpu_model_unavailable"] = str(exc)
    return metadata


def run_suite(args: argparse.Namespace) -> dict:
    binary = args.binary.resolve(strict=True)
    output_dir = args.output_dir.resolve()
    output_dir.mkdir(parents=True, exist_ok=True)
    suite = {
        "schema_version": 1,
        "complete": False,
        "environment": environment(binary),
        "method": {"repetitions": args.repetitions, "count": args.count,
                   "seed": args.seed, "warmup_runs_per_mode": 1,
                   "modes": args.modes, "timing": "CLI-defined in-process boundaries; no network measurement",
                   "comparison": "direct/queued topology comparison; neither is assumed faster"},
        "workloads": {},
    }
    # An interrupted rerun must not leave an older complete suite masquerading
    # as the current result. Complete workloads are saved incrementally below.
    summary_path = output_dir / "suite.json"
    summary_path.write_text(json.dumps(suite, indent=2) + "\n", encoding="utf-8")
    for profile in args.profiles:
        directory = output_dir / profile
        manifest = write_workload(directory, args.count, args.seed, profile, args.queue_capacity)
        config, commands = directory / "accounts.cfg", directory / "commands.txt"
        events, state, journal = directory / "events.txt", directory / "state.txt", directory / "journal.txt"
        invoke([str(binary), "run", "--config", str(config), "--input", str(commands),
                "--output", str(events), "--state", str(state), "--journal", str(journal)], args.timeout)
        invoke([str(binary), "replay", "--journal", str(journal), "--expect", str(events),
                "--expect-state", str(state)], args.timeout)
        workload = {"manifest": manifest, "replay_verified": True, "modes": {}}
        expected_digests = None
        for mode in args.modes:
            warmup = directory / f"{mode}-warmup.json"
            base = [str(binary), "bench", "--config", str(config), "--input", str(commands), "--mode", mode]
            invoke([*base, "--output", str(warmup)], args.timeout)
            samples = []
            for repetition in range(args.repetitions):
                report = directory / f"{mode}-{repetition + 1}.json"
                invoke([*base, "--output", str(report)], args.timeout)
                sample = read_small_json(report)
                validate_report(sample, report, args.count)
                digests = (sample["state_digest"], sample["trace_digest"])
                if expected_digests is None:
                    expected_digests = digests
                elif digests != expected_digests:
                    raise RuntimeError(f"logical digest mismatch between benchmark repetitions/modes: {report}")
                samples.append(sample)
            workload["modes"][mode] = {"reports": samples, "summary": summarize(samples)}
        if "direct" in args.modes and "queued" in args.modes:
            direct = workload["modes"]["direct"]["summary"]["commands_per_second"]["mean"]
            queued = workload["modes"]["queued"]["summary"]["commands_per_second"]["mean"]
            workload["queued_to_direct_throughput_ratio"] = queued / direct if direct > 0 else None
        suite["workloads"][profile] = workload
        print(f"verified and measured {profile}: {args.count} commands", flush=True)
        # Preserve completed measurements if a later workload fails.
        summary_path.write_text(json.dumps(suite, indent=2) + "\n", encoding="utf-8")
    suite["complete"] = True
    summary_path.write_text(json.dumps(suite, indent=2) + "\n", encoding="utf-8")
    return suite


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--count", type=positive_count, default=10_000)
    parser.add_argument("--seed", type=int, default=42)
    parser.add_argument("--profiles", nargs="+", choices=PROFILES, default=["resting", "crossing", "cancel", "replace", "dense", "sparse", "burst"])
    parser.add_argument("--modes", nargs="+", choices=("direct", "queued"), default=["direct", "queued"])
    parser.add_argument("--repetitions", type=int, choices=range(1, 21), default=3, metavar="1..20")
    parser.add_argument("--queue-capacity", type=queue_size, default=256)
    parser.add_argument("--timeout", type=int, default=600, help="seconds allowed per CLI invocation (default: 600)")
    args = parser.parse_args()
    if args.timeout <= 0:
        parser.error("timeout must be positive")
    if len(set(args.profiles)) != len(args.profiles) or len(set(args.modes)) != len(args.modes):
        parser.error("profiles and modes must not contain duplicates")
    try:
        run_suite(args)
    except (OSError, ValueError, RuntimeError, subprocess.SubprocessError) as exc:
        parser.exit(1, f"benchmark suite failed: {exc}\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
