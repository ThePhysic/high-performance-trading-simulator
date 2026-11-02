#!/usr/bin/env python3
"""End-to-end CLI contracts; invoked by CTest as <script> <binary> <repo>."""

from __future__ import annotations

import json
import os
from pathlib import Path
import random
import subprocess
import sys
import unittest
import uuid


if len(sys.argv) != 3:
    raise SystemExit("usage: test_cli.py <hpts binary> <repository root>")
BINARY = Path(sys.argv[1]).resolve()
REPOSITORY = Path(sys.argv[2]).resolve()
sys.argv = [sys.argv[0]]


class CliContract(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.workspace = REPOSITORY / "build" / "cli-contract-validation" / ("run-" + uuid.uuid4().hex)
        cls.workspace.mkdir(parents=True, exist_ok=True)
        cls.config = REPOSITORY / "examples" / "accounts.cfg"
        cls.demo = REPOSITORY / "examples" / "demo.commands"

    def directory(self, name: str) -> Path:
        path = self.workspace / name
        path.mkdir(parents=True, exist_ok=True)
        return path

    def invoke(self, arguments: list[str], *, success: bool = True, stdin: str | None = None,
               environment: dict[str, str] | None = None) -> subprocess.CompletedProcess:
        result = subprocess.run([str(BINARY), *map(str, arguments)], input=stdin, text=True,
                                capture_output=True, cwd=self.workspace, timeout=45, check=False, env=environment)
        message = f"arguments={arguments!r}\nstdout={result.stdout[-3000:]}\nstderr={result.stderr[-3000:]}"
        if success:
            self.assertEqual(result.returncode, 0, message)
        else:
            self.assertNotEqual(result.returncode, 0, message)
        return result

    def run_paths(self, directory: Path, *, config: Path | None = None, commands: Path | None = None) -> tuple[Path, Path, Path]:
        events, state, journal = directory / "events.txt", directory / "state.txt", directory / "journal.txt"
        self.invoke(["run", "--config", config or self.config, "--input", commands or self.demo,
                     "--output", events, "--state", state, "--journal", journal])
        self.assertTrue(events.is_file())
        self.assertTrue(state.is_file())
        self.assertTrue(journal.is_file())
        return events, state, journal

    def test_help_version_and_argument_errors(self) -> None:
        self.assertIn("hpts", self.invoke(["--help"]).stdout.lower())
        self.assertTrue(self.invoke(["--version"]).stdout.strip())
        for arguments in (["unknown"], ["run"], ["--unknown"], ["bench", "--mode", "invalid"],
                          ["run", "--config", self.config, "--input", self.demo, "--input", self.demo]):
            with self.subTest(arguments=arguments):
                self.invoke(arguments, success=False)

    def test_self_contained_replay_and_tamper_detection(self) -> None:
        directory = self.directory("replay")
        source_config, source_commands = directory / "source.cfg", directory / "source.commands"
        source_config.write_bytes(self.config.read_bytes())
        source_commands.write_bytes(self.demo.read_bytes())
        events, state, journal = self.run_paths(directory, config=source_config, commands=source_commands)
        self.assertTrue(journal.read_text(encoding="utf-8").startswith("HPTS-JOURNAL 1"))
        source_config.write_text("original configuration is no longer available\n", encoding="utf-8")
        source_commands.write_text("original command source is no longer available\n", encoding="utf-8")
        self.invoke(["replay", "--journal", journal, "--expect", events, "--expect-state", state])
        changed_events = directory / "changed-events.txt"
        changed_events.write_bytes(events.read_bytes() + b"TAMPER\n")
        self.invoke(["replay", "--journal", journal, "--expect", changed_events, "--expect-state", state], success=False)
        changed_state = directory / "changed-state.txt"
        changed_state.write_bytes(state.read_bytes() + b"TAMPER\n")
        self.invoke(["replay", "--journal", journal, "--expect", events, "--expect-state", changed_state], success=False)
        changed_journal = directory / "changed-journal.txt"
        changed_journal.write_text("HPTS-JOURNAL 999\nBEGIN\n", encoding="utf-8")
        self.invoke(["replay", "--journal", changed_journal, "--expect", events, "--expect-state", state], success=False)

    def test_journal_requires_completion_footer_and_no_trailing_commands(self) -> None:
        directory = self.directory("journal-completion")
        events, state, journal = self.run_paths(directory)
        lines = journal.read_text(encoding="utf-8").splitlines(keepends=True)
        self.assertTrue(lines[-1].startswith("END "))
        truncated = directory / "truncated.journal"
        truncated.write_text("".join(lines[:-1]), encoding="utf-8", newline="\n")
        self.invoke(["replay", "--journal", truncated, "--expect", events, "--expect-state", state], success=False)
        extra = directory / "extra-command.journal"
        extra.write_text("".join(lines) + "submit 999 A 999 buy 100 1\n", encoding="utf-8", newline="\n")
        self.invoke(["replay", "--journal", extra, "--expect", events, "--expect-state", state], success=False)
        footer = lines[-1].split()
        footer[1] = str(int(footer[1]) + 1)
        wrong_count = directory / "wrong-count.journal"
        wrong_count.write_text("".join(lines[:-1]) + " ".join(footer) + "\n", encoding="utf-8", newline="\n")
        self.invoke(["replay", "--journal", wrong_count, "--expect", events, "--expect-state", state], success=False)

    def test_malformed_input_and_config_are_errors(self) -> None:
        directory = self.directory("malformed")
        command_file = directory / "commands.txt"
        for content in ("submit 1 A 1 buy not-a-number 1\n", "submit -1 A 1 buy 100 1\n",
                        "submit 18446744073709551616 A 1 buy 100 1\n",
                        "submit 1 A 1 buy 9223372036854775808 1\n",
                        "submit 1 A 1 buy 100 1 extra\n", "unknown 1\n", "x" * 100_000 + "\n"):
            with self.subTest(content=content[:80]):
                command_file.write_text(content, encoding="utf-8")
                self.invoke(["run", "--config", self.config, "--input", command_file,
                             "--output", directory / "events", "--state", directory / "state"], success=False)
        bad_config = directory / "bad.cfg"
        for content in ("account A nope -10 10 0\n", "account A 1000 -10 10 0\nlimits 0 100 100 8\n",
                        "account A 1000 10 -10 0\n", "account A 1000 -10 10 0\naccount A 1000 -10 10 0\n"):
            with self.subTest(config=content):
                bad_config.write_text(content, encoding="utf-8")
                self.invoke(["run", "--config", bad_config, "--input", self.demo,
                             "--output", directory / "events", "--state", directory / "state"], success=False)

    def test_seeded_malformed_command_corpus(self) -> None:
        directory = self.directory("malformed-corpus")
        commands = directory / "commands.txt"
        rng = random.Random(90317)
        for case in range(100):
            if case % 3 == 0:
                line = f"unsupported_{rng.getrandbits(64):016x} {rng.randrange(10000)}"
            else:
                parts = ["submit", str(case + 1), "A", str(case + 1), "buy", str(rng.randrange(1, 1000)), "1"]
                if case % 3 == 1:
                    del parts[rng.randrange(1, len(parts))]
                else:
                    parts.append(str(rng.getrandbits(32)))
                line = " ".join(parts)
            with self.subTest(case=case):
                commands.write_text(line + "\n", encoding="utf-8")
                self.invoke(["run", "--config", self.config, "--input", commands,
                             "--output", directory / "events", "--state", directory / "state"], success=False)

    def test_business_rejections_are_recorded_without_process_failure(self) -> None:
        directory = self.directory("business-rejects")
        commands = directory / "commands.txt"
        commands.write_text("submit 1 C 1 buy 100 11\nsubmit 1 A 2 buy 100 1\ncancel 2 A 999\n", encoding="utf-8")
        events, state, journal = self.run_paths(directory, commands=commands)
        self.assertTrue(events.stat().st_size)
        self.invoke(["replay", "--journal", journal, "--expect", events, "--expect-state", state])

    def test_output_aliases_and_open_failures(self) -> None:
        directory = self.directory("path-errors")
        commands = directory / "commands.txt"
        config = directory / "accounts.cfg"
        command_text, config_text = self.demo.read_bytes(), self.config.read_bytes()
        commands.write_bytes(command_text)
        config.write_bytes(config_text)
        common = ["run", "--config", config, "--input", commands]
        for arguments in (["--output", commands, "--state", directory / "state"],
                          ["--output", config, "--state", directory / "state"],
                          ["--output", directory / "same", "--state", directory / "same"],
                          ["--output", directory / "events", "--state", directory / "state", "--journal", commands],
                          ["--output", directory, "--state", directory / "state"]):
            with self.subTest(arguments=arguments):
                self.invoke([*common, *arguments], success=False)
                self.assertEqual(commands.read_bytes(), command_text)
                self.assertEqual(config.read_bytes(), config_text)

    @unittest.skipUnless(os.name == "nt", "case-insensitive path contract is Windows-specific")
    def test_windows_output_case_aliases(self) -> None:
        directory = self.directory("case-aliases")
        commands = directory / "Commands.txt"
        original = self.demo.read_bytes()
        commands.write_bytes(original)
        self.invoke(["run", "--config", self.config, "--input", commands,
                     "--output", directory / "COMMANDS.TXT", "--state", directory / "state"], success=False)
        self.assertEqual(commands.read_bytes(), original)
        first, second = directory / "fresh-report.txt", directory / "FRESH-REPORT.TXT"
        self.assertFalse(first.exists())
        self.assertFalse(second.exists())
        self.invoke(["run", "--config", self.config, "--input", commands,
                     "--output", first, "--state", second], success=False)
        self.assertFalse(first.exists())
        self.assertFalse(second.exists())

    @unittest.skipUnless(os.name != "nt" and Path("/dev/full").exists(), "requires Linux /dev/full")
    def test_midstream_output_failure_never_completes_journal(self) -> None:
        directory = self.directory("output-full")
        commands = directory / "commands.txt"
        # Enough output to exceed a normal file stream buffer and fail during
        # event emission rather than only at stream destruction.
        commands.write_text("".join(f"submit {value} A {value} buy 100 1\n" for value in range(1, 301)), encoding="utf-8")
        journal = directory / "journal.txt"
        self.invoke(["run", "--config", self.config, "--input", commands,
                     "--output", "/dev/full", "--state", directory / "state", "--journal", journal], success=False)
        if journal.exists():
            self.assertFalse(any(line.startswith("END ") for line in journal.read_text(encoding="utf-8").splitlines()))

    def test_locale_environment_does_not_change_canonical_results(self) -> None:
        directory = self.directory("locale-environment")
        results = []
        for index, locale_name in enumerate(("C", "de_DE.UTF-8")):
            events, state = directory / f"events-{index}", directory / f"state-{index}"
            environment = {**os.environ, "LC_ALL": locale_name, "LANG": locale_name}
            self.invoke(["run", "--config", self.config, "--input", self.demo,
                         "--output", events, "--state", state], environment=environment)
            results.append((events.read_bytes(), state.read_bytes()))
        self.assertEqual(results[0], results[1])

    def test_direct_and_queued_benchmarks_agree(self) -> None:
        directory = self.directory("benchmarks")
        reports = []
        for mode in ("direct", "queued"):
            report_path = directory / f"{mode}.json"
            self.invoke(["bench", "--config", self.config, "--input", self.demo, "--mode", mode, "--output", report_path])
            report = json.loads(report_path.read_text(encoding="utf-8"))
            self.assertEqual(report["commands"], 22)
            for key in ("core_latency_ns", "end_to_end_latency_ns"):
                values = [report[key][quantile] for quantile in ("p50", "p95", "p99")]
                self.assertEqual(values, sorted(values))
                self.assertGreaterEqual(values[0], 0)
            self.assertGreater(report["elapsed_seconds"], 0)
            self.assertGreater(report["commands_per_second"], 0)
            reports.append(report)
        self.assertEqual(reports[0]["state_digest"], reports[1]["state_digest"])
        self.assertEqual(reports[0]["trace_digest"], reports[1]["trace_digest"])
        self.assertEqual(reports[0]["accepted_orders"], reports[1]["accepted_orders"])
        self.assertEqual(reports[0]["executions"], reports[1]["executions"])

    def test_interactive_inspection_and_controls(self) -> None:
        result = self.invoke(["interactive", "--config", self.config], stdin=(
            "help\nbook\naccounts\nstate\nsubmit 1 A 1 buy 100 2\n"
            "replace 2 A 1 100 1\nkill 3 on\nsubmit 4 A 2 buy 100 1\n"
            "cancel 5 A 1\nkill 6 off\nstate\nquit\n"))
        self.assertTrue(result.stdout.strip())

    def test_all_generated_profiles_run_and_replay(self) -> None:
        sys.path.insert(0, str(REPOSITORY / "tools"))
        from generate_workload import PROFILES, write_workload
        for profile in PROFILES:
            with self.subTest(profile=profile):
                directory = self.directory(f"generated-{profile}")
                manifest = write_workload(directory, 22, 42, profile, queue_capacity=2)
                self.assertEqual(manifest["command_count"], 22)
                events, state, journal = self.run_paths(directory, config=directory / "accounts.cfg", commands=directory / "commands.txt")
                self.invoke(["replay", "--journal", journal, "--expect", events, "--expect-state", state])

    def test_benchmark_suite_collects_real_reports(self) -> None:
        directory = self.directory("suite")
        result = subprocess.run([sys.executable, str(REPOSITORY / "tools" / "benchmark_suite.py"),
                                 "--binary", str(BINARY), "--output-dir", str(directory), "--count", "30",
                                 "--profiles", "crossing", "replace", "--repetitions", "1", "--queue-capacity", "2"],
                                capture_output=True, text=True, timeout=120, check=False)
        self.assertEqual(result.returncode, 0, result.stdout[-2000:] + result.stderr[-2000:])
        suite = json.loads((directory / "suite.json").read_text(encoding="utf-8"))
        self.assertTrue(suite["complete"])
        self.assertEqual(set(suite["workloads"]), {"crossing", "replace"})
        for workload in suite["workloads"].values():
            self.assertTrue(workload["replay_verified"])
            self.assertEqual(set(workload["modes"]), {"direct", "queued"})


if __name__ == "__main__":
    unittest.main(verbosity=2)
