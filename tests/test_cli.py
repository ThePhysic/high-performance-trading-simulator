#!/usr/bin/env python3
"""End-to-end CLI contracts; invoked by CTest as <script> <binary> <repo>."""

from __future__ import annotations

import errno
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


def canonical_file_digest(path: Path) -> str:
    # Independent Python calculation over actual run output; benchmark modes
    # must match the file execution contract, not merely one another.
    digest = 14695981039346656037
    for byte in path.read_bytes():
        digest ^= byte
        digest = (digest * 1099511628211) % (1 << 64)
    return f"{digest:016x}"


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

    def test_missing_run_options_preserve_existing_files(self) -> None:
        for missing in ("--config", "--input", "--output", "--state"):
            with self.subTest(missing=missing):
                directory = self.directory("missing-" + missing.removeprefix("--"))
                paths = {"--config": directory / "accounts.cfg", "--input": directory / "commands.txt",
                         "--output": directory / "events.txt", "--state": directory / "state.txt",
                         "--journal": directory / "journal.txt"}
                contents = {paths["--config"]: self.config.read_bytes(), paths["--input"]: self.demo.read_bytes(),
                            paths["--output"]: b"existing events\n", paths["--state"]: b"existing state\n",
                            paths["--journal"]: b"existing journal\n"}
                for path, content in contents.items():
                    path.write_bytes(content)
                arguments = ["run"]
                for option, path in paths.items():
                    if option != missing:
                        arguments.extend((option, path))
                result = self.invoke(arguments, success=False)
                self.assertEqual(result.returncode, 2, result.stderr)
                for path, content in contents.items():
                    self.assertEqual(path.read_bytes(), content, str(path))

    def test_unusable_run_destinations_preserve_existing_files(self) -> None:
        for option in ("--output", "--state", "--journal"):
            for failure in ("empty", "directory", "missing-parent", "file-parent"):
                with self.subTest(option=option, failure=failure):
                    directory = self.directory(option.removeprefix("--") + "-" + failure)
                    paths = {"--config": directory / "accounts.cfg", "--input": directory / "commands.txt",
                             "--output": directory / "events.txt", "--state": directory / "state.txt",
                             "--journal": directory / "journal.txt"}
                    contents = {paths["--config"]: self.config.read_bytes(), paths["--input"]: self.demo.read_bytes(),
                                paths["--output"]: b"existing events\n", paths["--state"]: b"existing state\n",
                                paths["--journal"]: b"existing journal\n"}
                    blocker = directory / "regular-file"
                    contents[blocker] = b"existing parent blocker\n"
                    for path, content in contents.items():
                        path.write_bytes(content)
                    invalid = {"empty": "", "directory": directory,
                               "missing-parent": directory / "missing" / "result.txt",
                               "file-parent": blocker / "result.txt"}[failure]
                    paths[option] = invalid
                    arguments = ["run"]
                    for key, path in paths.items():
                        arguments.extend((key, path))
                    result = self.invoke(arguments, success=False)
                    self.assertEqual(result.returncode, 2, result.stderr)
                    for path, content in contents.items():
                        self.assertEqual(path.read_bytes(), content, str(path))

    def filesystem_ignores_case(self, directory: Path) -> bool:
        # Probe the actual output filesystem: Linux can use an insensitive
        # mount (for example WSL /mnt/c), and Windows directories can be sensitive.
        probe = directory / "case-probe.txt"
        alternate = directory / "CASE-PROBE.TXT"
        probe.write_bytes(b"filesystem case probe\n")
        if not alternate.exists():
            return False
        self.assertTrue(probe.samefile(alternate))
        self.assertEqual(alternate.read_bytes(), b"filesystem case probe\n")
        return True

    def test_case_insensitive_output_aliases(self) -> None:
        directory = self.directory("case-aliases")
        if not self.filesystem_ignores_case(directory):
            self.skipTest("scratch filesystem distinguishes filename case")
        commands = directory / "Commands.txt"
        config = directory / "Accounts.cfg"
        original_commands, original_config = self.demo.read_bytes(), self.config.read_bytes()
        commands.write_bytes(original_commands)
        config.write_bytes(original_config)
        for source in (commands, config):
            with self.subTest(existing_source=source.name):
                result = self.invoke(["run", "--config", config, "--input", commands,
                                      "--output", source.with_name(source.name.upper()),
                                      "--state", directory / "state"], success=False)
                self.assertEqual(result.returncode, 2, result.stderr)
                self.assertEqual(commands.read_bytes(), original_commands)
                self.assertEqual(config.read_bytes(), original_config)
        for first_option, second_option in (("--output", "--state"), ("--output", "--journal"),
                                             ("--state", "--journal")):
            with self.subTest(first=first_option, second=second_option):
                pair = self.directory("case-aliases/" + first_option[2:] + "-" + second_option[2:])
                paths = {"--output": pair / "events.txt", "--state": pair / "state.txt",
                         "--journal": pair / "journal.txt"}
                paths[first_option], paths[second_option] = pair / "fresh-report.txt", pair / "FRESH-REPORT.TXT"
                self.assertTrue(all(not path.exists() for path in paths.values()))
                arguments = ["run", "--config", config, "--input", commands]
                for option, path in paths.items():
                    arguments.extend((option, path))
                result = self.invoke(arguments, success=False)
                self.assertEqual(result.returncode, 2, result.stderr)
                self.assertEqual(commands.read_bytes(), original_commands)
                self.assertEqual(config.read_bytes(), original_config)
                # Opening the complete output set may create empty placeholders
                # before discovering that two fresh spellings name one file.
                for path in paths.values():
                    if path.exists():
                        self.assertEqual(path.read_bytes(), b"", str(path))

    def test_case_sensitive_output_names_are_distinct(self) -> None:
        directory = self.directory("distinct-case")
        if self.filesystem_ignores_case(directory):
            self.skipTest("scratch filesystem ignores filename case")
        events, state, journal = directory / "report.txt", directory / "REPORT.TXT", directory / "Report.txt"
        for path in (events, state, journal):
            path.write_bytes(b"previous " + path.name.encode("ascii") + b"\n")
        self.assertFalse(events.samefile(state))
        self.assertFalse(events.samefile(journal))
        self.assertFalse(state.samefile(journal))
        self.invoke(["run", "--config", self.config, "--input", self.demo,
                     "--output", events, "--state", state, "--journal", journal])
        self.assertTrue(events.read_bytes().startswith(b"HPTS-EVENTS 1\n"))
        self.assertTrue(state.stat().st_size)
        self.assertTrue(journal.read_bytes().startswith(b"HPTS-JOURNAL 1\n"))
        self.invoke(["replay", "--journal", journal, "--expect", events, "--expect-state", state])

    def assert_link_aliases_rejected(self, kind: str) -> None:
        directory = self.directory(kind + "-aliases")

        def make_alias(target: Path, alias: Path) -> None:
            if kind == "hardlink":
                os.link(target, alias)
            else:
                alias.symlink_to(target)

        probe, probe_alias = directory / "probe", directory / "probe-alias"
        probe.write_bytes(b"link support probe\n")
        try:
            make_alias(probe, probe_alias)
        except NotImplementedError as error:
            self.skipTest(f"{kind} creation is unsupported: {error}")
        except OSError as error:
            unsupported = {errno.EPERM, errno.EACCES, errno.ENOSYS, errno.ENOTSUP, errno.EOPNOTSUPP}
            if error.errno in unsupported or getattr(error, "winerror", None) in (1, 50, 1314):
                self.skipTest(f"{kind} creation is unavailable on scratch filesystem: {error}")
            raise
        self.assertTrue(probe.samefile(probe_alias))
        pairs = [(source, output) for source in ("--config", "--input")
                 for output in ("--output", "--state", "--journal")]
        pairs += [("--output", "--state"), ("--output", "--journal"), ("--state", "--journal")]
        for source, destination in pairs:
            with self.subTest(kind=kind, source=source, destination=destination):
                case = self.directory(kind + "-aliases/" + source[2:] + "-" + destination[2:])
                paths = {"--config": case / "accounts.cfg", "--input": case / "commands.txt",
                         "--output": case / "events.txt", "--state": case / "state.txt",
                         "--journal": case / "journal.txt"}
                contents = {paths["--config"]: self.config.read_bytes(), paths["--input"]: self.demo.read_bytes(),
                            paths["--output"]: b"existing events\n", paths["--state"]: b"existing state\n",
                            paths["--journal"]: b"existing journal\n"}
                for path, content in contents.items():
                    path.write_bytes(content)
                alias = case / "linked-destination.txt"
                make_alias(paths[source], alias)
                self.assertTrue(paths[source].samefile(alias))
                contents[alias] = contents[paths[source]]
                paths[destination] = alias
                arguments = ["run"]
                for option, path in paths.items():
                    arguments.extend((option, path))
                result = self.invoke(arguments, success=False)
                self.assertEqual(result.returncode, 2, result.stderr)
                for path, content in contents.items():
                    self.assertEqual(path.read_bytes(), content, str(path))

    def test_hardlink_output_aliases(self) -> None:
        self.assert_link_aliases_rejected("hardlink")

    def test_symlink_output_aliases(self) -> None:
        self.assert_link_aliases_rejected("symlink")

    @unittest.skipUnless(os.name != "nt" and hasattr(os, "mkfifo"), "requires POSIX FIFO support")
    def test_fifo_destination_fails_promptly_without_truncating_events(self) -> None:
        directory = self.directory("fifo-output")
        fifo = directory / "state.fifo"
        try:
            os.mkfifo(fifo)
        except OSError as error:
            if error.errno in (errno.EPERM, errno.EACCES, errno.ENOSYS, errno.ENOTSUP, errno.EOPNOTSUPP):
                self.skipTest(f"FIFO creation is unavailable on scratch filesystem: {error}")
            raise
        events, journal = directory / "events.txt", directory / "journal.txt"
        contents = {events: b"existing events\n", journal: b"existing journal\n"}
        for path, content in contents.items():
            path.write_bytes(content)
        # No FIFO reader exists. Opening a destination must reject this special
        # file without waiting for a peer or modifying earlier destinations.
        try:
            result = subprocess.run([str(BINARY), "run", "--config", str(self.config), "--input", str(self.demo),
                                     "--output", str(events), "--state", str(fifo), "--journal", str(journal)],
                                    text=True, capture_output=True, cwd=self.workspace, timeout=3, check=False)
        except subprocess.TimeoutExpired:
            self.fail("FIFO destination with no reader did not fail within 3 seconds")
        self.assertEqual(result.returncode, 2, result.stderr)
        for path, content in contents.items():
            self.assertEqual(path.read_bytes(), content, str(path))

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

    def test_closed_stdout_pipe_reports_failure(self) -> None:
        directory = self.directory("closed-stdout")
        events, state, journal = self.run_paths(directory)
        cases = [
            ["--version"],
            ["bench", "--config", self.config, "--input", self.demo, "--mode", "direct"],
            ["run", "--config", self.config, "--input", self.demo,
             "--output", directory / "run-events", "--state", directory / "run-state"],
            ["replay", "--journal", journal, "--expect", events, "--expect-state", state],
        ]
        for arguments in cases:
            with self.subTest(arguments=arguments):
                read_fd, write_fd = os.pipe()
                try:
                    os.close(read_fd)
                    read_fd = None
                    result = subprocess.run([str(BINARY), *map(str, arguments)], stdout=write_fd,
                                            stderr=subprocess.PIPE, cwd=self.workspace,
                                            text=True, timeout=15, check=False)
                finally:
                    if read_fd is not None:
                        os.close(read_fd)
                    os.close(write_fd)
                self.assertEqual(result.returncode, 2,
                                 f"closed stdout must report system error exit 2: {arguments!r}; stderr={result.stderr!r}")

    @unittest.skipUnless(os.name != "nt" and Path("/dev/full").exists(), "requires Linux /dev/full")
    def test_stdout_device_failure_reports_failure(self) -> None:
        for arguments in (["--version"],
                          ["bench", "--config", self.config, "--input", self.demo, "--mode", "direct"]):
            with self.subTest(arguments=arguments):
                with Path("/dev/full").open("wb") as output:
                    result = subprocess.run([str(BINARY), *map(str, arguments)], stdout=output,
                                            stderr=subprocess.PIPE, cwd=self.workspace,
                                            text=True, timeout=15, check=False)
                self.assertEqual(result.returncode, 2,
                                 f"failed stdout device must report system error exit 2: {arguments!r}; stderr={result.stderr!r}")

    def test_direct_and_queued_benchmarks_agree(self) -> None:
        directory = self.directory("benchmarks")
        events, state, _ = self.run_paths(directory)
        expected_trace = canonical_file_digest(events)
        expected_state = canonical_file_digest(state)
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
            self.assertEqual(report["trace_digest"], expected_trace)
            self.assertEqual(report["state_digest"], expected_state)
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

    def test_large_generated_workload_run_and_exact_replay(self) -> None:
        sys.path.insert(0, str(REPOSITORY / "tools"))
        from generate_workload import write_workload
        directory = self.directory("large-generated-mixed")
        manifest = write_workload(directory, 4097, 42, "mixed", queue_capacity=2)
        self.assertEqual(manifest["command_count"], 4097)
        commands = directory / "commands.txt"
        events, state, journal = self.run_paths(directory, config=directory / "accounts.cfg", commands=commands)
        # Every input/output stream spans multiple blocks, including the large
        # state serialization emitted as one write and replayed byte for byte.
        for path in (commands, events, state, journal):
            self.assertGreater(path.stat().st_size, 8192, str(path))
        result = self.invoke(["replay", "--journal", journal, "--expect", events, "--expect-state", state])
        self.assertIn("commands=4097 ", result.stdout)

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
            for mode in workload["modes"].values():
                for report in mode["reports"]:
                    self.assertEqual(report["state_digest"], workload["verified_run_digests"]["state_digest"])
                    self.assertEqual(report["trace_digest"], workload["verified_run_digests"]["trace_digest"])


if __name__ == "__main__":
    unittest.main(verbosity=2)
