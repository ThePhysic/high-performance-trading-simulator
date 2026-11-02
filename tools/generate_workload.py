#!/usr/bin/env python3
"""Generate reproducible synthetic command/config files without external packages."""

from __future__ import annotations

import argparse
import hashlib
import itertools
import json
from pathlib import Path
import random
import sys
from typing import Iterator


VERSION = 1
PROFILES = ("resting", "crossing", "cancel", "replace", "burst", "dense", "sparse", "mixed")


def positive_count(value: str) -> int:
    result = int(value)
    if not 1 <= result <= 1_000_000:
        raise argparse.ArgumentTypeError("count must be between 1 and 1000000")
    return result


def queue_size(value: str) -> int:
    result = int(value)
    if not 1 <= result <= 65_536:
        raise argparse.ArgumentTypeError("queue capacity must be between 1 and 65536")
    return result


def command_bodies(profile: str, seed: int) -> Iterator[str]:
    """Stream bodies (without command IDs), keeping at most 128 order IDs locally.

    All profiles are synthetic. 'burst' means bulk submission/cancellation
    ordering; a file has no arrival-time model. Queued execution supplies queue
    pressure, and the benchmark must describe its actual admission policy.
    """
    rng = random.Random(seed)
    order_ids = itertools.count(1)
    while True:
        mode = rng.choice(("crossing", "cancel", "replace", "partial")) if profile == "mixed" else profile
        quantity = rng.randint(2, 20)
        price = 10_000 + rng.randint(0, 100)
        if mode in ("resting", "dense", "sparse"):
            order_id = next(order_ids)
            if mode == "dense":
                price = 10_000 - rng.randrange(4)
            elif mode == "sparse":
                price = 100_000_000 - rng.randrange(1_000_000) * 97
            else:
                price = 10_000 - rng.randrange(128)
            yield f"submit A {order_id} buy {price} {quantity}"
        elif mode == "crossing":
            buy_id, sell_id = next(order_ids), next(order_ids)
            yield f"submit A {buy_id} buy {price} {quantity}"
            yield f"submit B {sell_id} sell {price} {quantity}"
        elif mode == "cancel":
            order_id = next(order_ids)
            yield f"submit A {order_id} buy {price} {quantity}"
            yield f"cancel A {order_id}"
        elif mode == "replace":
            order_id = next(order_ids)
            yield f"submit A {order_id} buy {price} {quantity}"
            yield f"replace A {order_id} {price + 1} {quantity + 1}"
            yield f"cancel A {order_id}"
        elif mode == "partial":
            buy_id, sell_id = next(order_ids), next(order_ids)
            yield f"submit A {buy_id} buy {price} {quantity}"
            yield f"submit B {sell_id} sell {price} {quantity // 2}"
            yield f"replace A {buy_id} {price} 1"
            yield f"cancel A {buy_id}"
        elif mode == "burst":
            batch = []
            for _ in range(128):
                order_id = next(order_ids)
                batch.append(order_id)
                yield f"submit A {order_id} buy {price} {quantity}"
            for order_id in batch:
                yield f"cancel A {order_id}"
        else:
            raise ValueError(f"unknown profile: {mode}")


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1 << 20), b""):
            digest.update(block)
    return digest.hexdigest()


def write_workload(output_dir: Path, count: int, seed: int, profile: str, queue_capacity: int = 256) -> dict:
    if not 1 <= count <= 1_000_000:
        raise ValueError("count must be between 1 and 1000000")
    if profile not in PROFILES:
        raise ValueError(f"unknown profile: {profile}")
    if not 1 <= queue_capacity <= 65_536:
        raise ValueError("queue capacity must be between 1 and 65536")
    output_dir.mkdir(parents=True, exist_ok=True)
    config_path = output_dir / "accounts.cfg"
    commands_path = output_dir / "commands.txt"
    with config_path.open("w", encoding="utf-8", newline="\n") as stream:
        stream.write("# Synthetic accounts; negative minimum position permits bounded short selling.\n")
        for account in "ABCD":
            stream.write(f"account {account} 10000000000000000 -1000000000000 1000000000000 0\n")
        ledger_capacity = min(count + 128, 1_000_000)
        stream.write(f"limits {max(count, 128)} {ledger_capacity} {ledger_capacity} {queue_capacity}\n")
        # Sparse profile prices require a declared range rather than relying on
        # engine defaults. Aggregate notional stays within the configured cash.
        stream.write("bounds 100000000 1000000 16 32\n")
    with commands_path.open("w", encoding="utf-8", newline="\n") as stream:
        stream.write(f"# synthetic generator={VERSION} profile={profile} seed={seed} count={count}\n")
        for sequence, body in enumerate(itertools.islice(command_bodies(profile, seed), count), 1):
            verb, rest = body.split(" ", 1)
            stream.write(f"{verb} {sequence} {rest}\n")
    manifest = {
        "generator_version": VERSION,
        "profile": profile,
        "seed": seed,
        "command_count": count,
        "queue_capacity": queue_capacity,
        "synthetic": True,
        "arrival_model": "unpaced file; burst profile changes command ordering only",
        "python_version": sys.version.split()[0],
        "config_sha256": sha256_file(config_path),
        "commands_sha256": sha256_file(commands_path),
    }
    (output_dir / "workload.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    return manifest


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output-dir", type=Path, required=True, help="directory for accounts.cfg, commands.txt and workload.json")
    parser.add_argument("--count", type=positive_count, default=10_000, help="exact command count, 1..1000000 (default: 10000)")
    parser.add_argument("--seed", type=int, default=42)
    parser.add_argument("--profile", choices=PROFILES, default="mixed")
    parser.add_argument("--queue-capacity", type=queue_size, default=256)
    args = parser.parse_args()
    try:
        manifest = write_workload(args.output_dir, args.count, args.seed, args.profile, args.queue_capacity)
    except (OSError, ValueError) as exc:
        parser.exit(1, f"generation failed: {exc}\n")
    print(json.dumps({"output_dir": str(args.output_dir), **manifest}, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
