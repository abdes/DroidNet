"""Contract and real LLVM integration tests for the scoped developer workflow."""

from __future__ import annotations

import contextlib
import io
import json
import os
import shutil
import subprocess
import sys
import tempfile
import threading
import unittest
import venv
from pathlib import Path
from unittest.mock import patch

from oxytidy.common import ToolError, fingerprint, path_key
from oxytidy.compilation import (
    ClangdConfig,
    expand_responses,
    parse_clangd,
    read_database,
    remove_arguments,
    split_command,
)
from oxytidy.execution import Runner, bounded_map
from oxytidy.fixes import apply_fixes, plan_fixes, replaced_bytes
from oxytidy.scope import Scope
from oxytidy.workflow import main, outcome, parse_args, select_ownership_file


class Fixture(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="oxytidy tests ")
        self.root = Path(self.temporary.name)
        self.addCleanup(self.temporary.cleanup)
        self.write(
            ".oxytidy.json",
            json.dumps({"project_roots": ["src"], "exclude": ["src/vendor/**"]}),
        )
        self.write(".clangd", "CompileFlags:\n  CompilationDatabase: .\n")
        self.write(".clang-tidy", 'Checks: "-*,modernize-use-trailing-return-type"\n')

    def write(self, name, content):
        path = self.root / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(content, encoding="utf-8", newline="\n")
        return path

    def scope(self, *paths, tests=False):
        return Scope.load(self.root, list(paths), False, tests)

    def database(self, files, *, arguments=None):
        compiler = (
            str(LLVM / ("clang++.exe" if os.name == "nt" else "clang++"))
            if LLVM
            else "clang++"
        )
        entries = [
            {
                "directory": str(self.root),
                "file": file,
                "arguments": [compiler, "-std=c++20", "-c", file, *(arguments or [])],
                "output": f"Debug/{i}.o",
            }
            for i, file in enumerate(files)
        ]
        self.write("compile_commands.json", json.dumps(entries))
        return entries


class SetupReportingTests(Fixture):
    def test_nonexistent_project_root_is_rejected_without_creating_it(self):
        target = self.root / "absent"
        with self.assertRaisesRegex(
            ToolError, "Project root is not an existing directory"
        ):
            main(["--project-root", str(target), "src"], root=self.root)
        self.assertFalse(target.exists())

    def test_missing_target_policy_uses_logged_tool_policy(self):
        target = self.root / "target"
        target.mkdir()
        self.write("target/src/a.cpp", "int x;\n")
        with (
            contextlib.redirect_stdout(io.StringIO()) as output,
            contextlib.redirect_stderr(output),
            patch("oxytidy.workflow.engine_root", return_value=self.root),
        ):
            code = main(["--project-root", str(target), "src"], root=self.root)
        self.assertEqual(code, 2)
        self.assertIn("@project/.oxytidy.json", output.getvalue())
        self.assertIn("tool/run-location fallback", output.getvalue())
        self.assertIn(".oxytidy.json", output.getvalue())
        self.assertIn("@project/.clangd", output.getvalue())
        self.assertFalse((target / ".oxytidy.json").exists())

    def test_ownership_precedence_does_not_hide_invalid_explicit_or_target_policy(self):
        missing_explicit = self.root / "missing.json"
        with patch("oxytidy.workflow.engine_root") as fallback:
            self.assertEqual(
                select_ownership_file(self.root, str(missing_explicit)),
                (missing_explicit, "explicit --ownership-file"),
            )
            (self.root / ".oxytidy.json").write_text("not JSON", encoding="utf-8")
            self.assertEqual(
                select_ownership_file(self.root, None),
                (self.root / ".oxytidy.json", "target checkout policy"),
            )
        fallback.assert_not_called()

    def test_missing_tool_policy_does_not_invent_configuration(self):
        target = self.root / "target"
        with patch(
            "oxytidy.workflow.engine_root", side_effect=ToolError("unavailable")
        ):
            path, reason = select_ownership_file(target, None)
        self.assertEqual(path, target / ".oxytidy.json")
        self.assertIn("no tool/run-location policy", reason)
        self.assertFalse(target.exists())

    def run_setup(self, *arguments):
        self.write("src/a.cpp", "int x;\n")
        with (
            contextlib.redirect_stdout(io.StringIO()) as output,
            contextlib.redirect_stderr(output),
        ):
            code = main(["src/a.cpp", *arguments], root=self.root)
        summary = json.loads(
            next((self.root / "out/clang-tidy").glob("*/summary.json")).read_text()
        )
        self.assertEqual(code, 2)
        self.assertEqual(summary["status"], "setup_failed")
        self.assertFalse(summary["analysis_started"])
        self.assertIsNone(summary["counts"]["unique_findings"])
        self.assertIn("Analysis did not start", output.getvalue())
        self.assertNotIn("0 unique finding", output.getvalue())
        return output.getvalue()

    def test_missing_clangd_explains_separate_local_configuration(self):
        (self.root / ".clangd").unlink()
        with patch("oxytidy.workflow.resolve_tools") as tools:
            output = self.run_setup()
        tools.assert_not_called()
        self.assertIn("@project/.clangd", output)
        self.assertIn("--clangd-file", output)
        self.assertIn("configured Ninja build tree", output)
        self.assertIn(".clangd.in", output)
        self.assertIn("oxytidy does not configure or build", output)
        self.assertFalse((self.root / ".clangd").exists())

    def test_missing_database_explains_configuration_and_build_override(self):
        with patch("oxytidy.workflow.resolve_tools") as tools:
            output = self.run_setup()
        tools.assert_not_called()
        self.assertIn("@project/compile_commands.json", output)
        self.assertIn("CMAKE_EXPORT_COMPILE_COMMANDS", output)
        self.assertIn("--build-dir", output)
        self.assertFalse((self.root / "compile_commands.json").exists())

    def test_missing_explicit_tidy_config_reports_its_option(self):
        output = self.run_setup("--config-file", "missing.yaml")
        self.assertIn("@project/missing.yaml", output)
        self.assertIn("--config-file", output)


class CompilationTests(Fixture):
    def test_configuration_filter_precedes_response_expansion(self):
        for representation in ("arguments", "command"):
            for metadata in ("definition", "output"):
                with self.subTest(representation=representation, metadata=metadata):
                    entries = self.database(["src/a.cpp", "src/a.cpp"])
                    response = self.write("debug.rsp", "-DSELECTED=1")
                    entries[0]["arguments"].append("@debug.rsp")
                    entries[1]["arguments"].append("@missing-release.rsp")
                    if metadata == "definition":
                        # The explicit configuration overrides the output path.
                        entries[1]["arguments"].append('-DCMAKE_INTDIR="Release"')
                    else:
                        entries[1]["output"] = "Release/a.obj"
                    if representation == "command":
                        for entry in entries:
                            entry["arguments"][0] = "clang++"
                            entry["command"] = " ".join(entry.pop("arguments"))
                    database = self.write("compile_commands.json", json.dumps(entries))
                    contexts = read_database(
                        database, ClangdConfig(self.root, (), ()), "Debug"
                    )
                    self.assertEqual(len(contexts), 1)
                    self.assertIn("-DSELECTED=1", contexts[0].arguments)
                    self.assertEqual(
                        dict(contexts[0].response_inputs), fingerprint([response])
                    )

    def test_configuration_can_be_discovered_inside_response_file(self):
        entries = self.database(["src/a.cpp", "src/a.cpp"])
        for entry, name in zip(entries, ("Debug", "Release")):
            entry.pop("output")
            self.write(f"{name}.rsp", f"-DCMAKE_INTDIR={name}")
            entry["arguments"].append(f"@{name}.rsp")
        database = self.write("compile_commands.json", json.dumps(entries))
        contexts = read_database(database, ClangdConfig(self.root, (), ()), "Debug")
        self.assertEqual(len(contexts), 1)
        self.assertIn("-DCMAKE_INTDIR=Debug", contexts[0].arguments)

    def test_required_response_files_still_fail_when_missing(self):
        entries = self.database(["src/a.cpp"])
        entries[0]["arguments"].append("@missing.rsp")
        database = self.write("compile_commands.json", json.dumps(entries))
        for configuration in ("Debug", None):
            with (
                self.subTest(configuration=configuration),
                self.assertRaises(FileNotFoundError),
            ):
                read_database(database, ClangdConfig(self.root, (), ()), configuration)

    def test_scope_filter_precedes_response_expansion_and_configuration_checks(self):
        self.write("src/module/a.cpp", "int x;\n")
        entries = self.database(["src/module/a.cpp", "src/elsewhere/b.cpp"])
        entries[1]["arguments"] = ["clang++", "@nonexistent.rsp", "src/elsewhere/b.cpp"]
        entries[1].pop("output")
        database = self.write("compile_commands.json", json.dumps(entries))
        contexts = read_database(
            database,
            ClangdConfig(self.root, (), ()),
            "Debug",
            source_filter=self.scope("src/module").discovery_contains,
        )
        self.assertEqual(
            [context.file for context in contexts], [self.root / "src/module/a.cpp"]
        )

    def test_yaml_block_lists_and_operand_removal(self):
        path = self.write(
            ".clangd",
            "CompileFlags:\n  CompilationDatabase: '.'\n  Add:\n    - '-DVALUE=a,b'\n  Remove:\n    - '-I'\n    - '-W*'\n",
        )
        config = parse_clangd(path)
        self.assertEqual(config.add, ("-DVALUE=a,b",))
        self.assertEqual(
            remove_arguments(
                [
                    "clang++",
                    "-I",
                    "include path",
                    "--include-directory=other",
                    "-Wall",
                    "a.cpp",
                ],
                config.remove,
            ),
            ["clang++", "a.cpp"],
        )

    def test_unknown_and_conditional_rules_rejected(self):
        for text in [
            "CompileFlags: {Remove: [-unknown], CompilationDatabase: .}",
            "If: {PathMatch: '.*'}\nCompileFlags: {CompilationDatabase: .}",
            "CompileFlags: {CompilationDatabase: ., BuiltinHeaders: QueryDriver}",
            "CompileFlags: {CompilationDatabase: ., Add: [], Add: []}",
        ]:
            with self.subTest(text=text), self.assertRaises(ToolError):
                parse_clangd(self.write(".clangd", text))

    def test_xclang_operand_and_windows_flags(self):
        with self.assertRaisesRegex(ToolError, "operand grammar"):
            remove_arguments(
                ["clang++", "-fmodule-map-file", "module.map", "a.cpp"], ("-f*",)
            )
        args = [
            "clang++",
            "-Xclang",
            "-include",
            "-Xclang",
            "prefix.h",
            "/Ipath",
            "/FI",
            "another.h",
            "file.cpp",
        ]
        self.assertEqual(
            remove_arguments(args, ("-include", "/I", "/FI")), ["clang++", "file.cpp"]
        )

    def test_response_file_expansion_and_recursion(self):
        self.write("flags.rsp", '-I "include path" @inner.rsp')
        self.write("inner.rsp", "-DVALUE=42")
        self.assertEqual(
            expand_responses(["clang++", "@flags.rsp", "a.cpp"], self.root),
            ["clang++", "-I", "include path", "-DVALUE=42", "a.cpp"],
        )
        self.write("inner.rsp", "@flags.rsp")
        with self.assertRaises(ToolError):
            expand_responses(["@flags.rsp"], self.root)

    def test_relative_source_and_distinct_contexts(self):
        self.write("src/a.cpp", "int x;")
        entries = self.database(["src/a.cpp", "src/a.cpp"])
        entries[1]["arguments"].append("-DOTHER=1")
        entries.append(dict(entries[0]))
        path = self.write("compile_commands.json", json.dumps(entries))
        contexts = read_database(path, ClangdConfig(self.root, (), ()), "Debug")
        self.assertEqual(len(contexts), 2)
        self.assertEqual(contexts[0].file, self.root / "src/a.cpp")
        self.assertNotEqual(contexts[0].identity, contexts[1].identity)

    def test_unknown_configuration_fails_instead_of_selecting_first(self):
        entries = self.database(["src/a.cpp"])
        entries[0].pop("output")
        path = self.write("compile_commands.json", json.dumps(entries))
        with self.assertRaisesRegex(ToolError, "Cannot establish configuration"):
            read_database(path, ClangdConfig(self.root, (), ()), "Debug")
        self.assertEqual(
            len(read_database(path, ClangdConfig(self.root, (), ()), None)), 1
        )

    def test_command_quoting(self):
        self.assertEqual(
            split_command('"C:/Program Files/LLVM/clang++" -I"a b" file.cpp'),
            ["C:/Program Files/LLVM/clang++", "-Ia b", "file.cpp"],
        )


class ScopeAndFixTests(Fixture):
    def test_ownership_and_tests(self):
        project = self.write("src/a.h", "int a;")
        vendor = self.write("src/vendor/a.h", "int a;")
        system = self.write("system/a.h", "int a;")
        tests = self.write("src/Module/Test/a.h", "int a;")
        scope = self.scope("src")
        self.assertTrue(scope.contains(project))
        self.assertFalse(scope.contains(vendor))
        self.assertFalse(scope.contains(system))
        self.assertFalse(scope.contains(tests))
        self.assertTrue(self.scope("src", tests=True).contains(tests))
        self.assertEqual(scope.inputs(), [project])

    def test_invalid_scope_and_cli(self):
        self.write("src/vendor/a.h", "int a;")
        with self.assertRaises(ToolError):
            self.scope("src/vendor/a.h")
        for args in [
            [],
            ["src", "--all"],
            ["src", "--jobs", "0"],
            ["src", "--timeout", "nan"],
            ["src", "--format"],
            ["src", "--fix", "--export-fixes=x"],
        ]:
            with (
                self.subTest(args=args),
                contextlib.redirect_stderr(io.StringIO()),
                self.assertRaises(SystemExit),
            ):
                parse_args(args)

    def diagnostic(self, edits):
        return {
            "id": "id",
            "file": str(self.root / "src/a.h"),
            "line": 1,
            "replacements": edits,
        }

    def edit(self, path="src/a.h", offset=0, length=1, text="z"):
        return {
            "file": str(self.root / path),
            "offset": offset,
            "length": length,
            "text": text,
        }

    def test_fix_deduplication_and_scope_atomicity(self):
        header = self.write("src/a.h", "abc\n")
        vendor = self.write("src/vendor/a.h", "abc\n")
        snapshot = fingerprint([header, vendor])
        record = self.diagnostic([self.edit()])
        edits, skipped = plan_fixes([record, record], self.scope("src"), snapshot)
        self.assertEqual(len(edits[path_key(header)]), 1)
        self.assertEqual(skipped, [])
        mixed = self.diagnostic([self.edit(), self.edit("src/vendor/a.h")])
        edits, skipped = plan_fixes([mixed], self.scope("src"), snapshot)
        self.assertEqual(edits, {})
        self.assertEqual(len(skipped), 1)

    def test_conflicts_and_changed_contents_prevent_batch(self):
        header = self.write("src/a.h", "abc\n")
        snapshot = fingerprint([header])
        with self.assertRaisesRegex(ToolError, "Conflicting"):
            plan_fixes(
                [self.diagnostic([self.edit(text="x"), self.edit(text="y")])],
                self.scope("src"),
                snapshot,
            )
        edits, _ = plan_fixes(
            [self.diagnostic([self.edit()])], self.scope("src"), snapshot
        )
        header.write_text("user changes\n")
        with self.assertRaisesRegex(ToolError, "Contents changed"):
            apply_fixes(edits, snapshot)
        self.assertEqual(header.read_text(), "user changes\n")

    def test_apply_preserves_bom_and_line_endings(self):
        content = b"\xef\xbb\xbfabc\r\n"
        edited, _ = replaced_bytes(
            content, [self.edit(offset=3, length=3, text="xyz\nmore")]
        )
        self.assertEqual(edited, b"\xef\xbb\xbfxyz\r\nmore\r\n")
        with self.assertRaises(ToolError):
            replaced_bytes(b"\xff\xfea\x00", [])

    def test_file_batch_rolls_back_on_io_failure(self):
        first = self.write("src/a.h", "abc\n")
        second = self.write("src/b.h", "abc\n")
        snapshot = fingerprint([first, second])
        edits = {
            path_key(first): [self.edit()],
            path_key(second): [self.edit("src/b.h")],
        }
        from oxytidy.fixes import atomic_write

        calls = 0

        def fail_second(*args):
            nonlocal calls
            calls += 1
            if calls == 2:
                raise OSError("injected failure")
            return atomic_write(*args)

        with (
            patch("oxytidy.fixes.atomic_write", side_effect=fail_second),
            self.assertRaises(OSError),
        ):
            apply_fixes(edits, snapshot)
        self.assertEqual(first.read_text(), "abc\n")
        self.assertEqual(second.read_text(), "abc\n")


class ExecutionTests(Fixture):
    def test_timeout_shorter_than_poll_interval_is_enforced(self):
        marker = self.root / "child-finished"
        code = f"import pathlib,time; time.sleep(0.025); pathlib.Path({str(marker)!r}).write_text('finished')"
        result = Runner(0.01).run([sys.executable, "-c", code], self.root)
        self.assertEqual(result.status, "timed_out")
        self.assertEqual(result.returncode, 124)
        self.assertFalse(marker.exists())

    def test_source_text_io_preserves_line_endings(self):
        text = "first\r\nsecond\nthird\r\n"
        result = Runner(None).run(
            [
                sys.executable,
                "-c",
                "import sys; sys.stdout.buffer.write(sys.stdin.buffer.read())",
            ],
            self.root,
            input_text=text,
            preserve_newlines=True,
        )
        self.assertEqual(result.returncode, 0)
        self.assertEqual(result.stdout, text)

    def test_timeout_kills_descendants(self):
        marker = self.root / "descendant-survived"
        child = (
            "import time,pathlib; time.sleep(1.5); pathlib.Path("
            + repr(str(marker))
            + ").write_text('alive')"
        )
        parent = (
            "import subprocess,sys,time; subprocess.Popen([sys.executable,'-c',"
            + repr(child)
            + "]); time.sleep(20)"
        )
        result = Runner(0.3).run([sys.executable, "-c", parent], self.root)
        self.assertEqual(result.status, "timed_out")
        threading.Event().wait(1.6)
        self.assertFalse(marker.exists())

    @unittest.skipUnless(
        shutil.which("pwsh") and shutil.which("uv"), "PowerShell and uv are required"
    )
    def test_powershell_uses_active_or_path_python_and_forwards_arguments(self):
        project = Path(__file__).resolve().parents[1]
        launcher = self.root / "tools/cli/oxytidy.ps1"
        launcher.parent.mkdir(parents=True)
        shutil.copy2(project.parent / "cli/oxytidy.ps1", launcher)
        isolated = self.root / "tools/oxytidy"
        isolated.mkdir()
        for name in ("pyproject.toml", "README.md"):
            shutil.copy2(project / name, isolated / name)
        self.write("tools/oxytidy/src/oxytidy/__init__.py", "")
        self.write(
            "tools/oxytidy/src/oxytidy/__main__.py",
            "from .cli import main\nraise SystemExit(main())\n",
        )
        self.write(
            "tools/oxytidy/src/oxytidy/cli.py",
            "import json,sys,os\ndef main():\n print(json.dumps({'args':sys.argv[1:],'cwd':os.getcwd(),'prefix':sys.prefix})); return 7\n",
        )
        arguments = [
            "src/a.h",
            "--checks=-*,modernize-*",
            "--timeout",
            "1.5",
            "--clang-tidy-bin",
            'path with "quotes"/clang-tidy',
            "",
            "--",
            "-leading",
            "trailing\\",
            "$literal;not a command",
        ]
        active = self.root / "active"
        fallback = self.root / "default-python"
        for path in (active, fallback):
            venv.EnvBuilder(with_pip=False).create(path)
        scripts = "Scripts" if os.name == "nt" else "bin"
        environment = os.environ.copy()
        environment.pop("UV_PROJECT_ENVIRONMENT", None)
        environment["PATH"] = str(fallback / scripts) + os.pathsep + environment["PATH"]
        command = ["pwsh", "-NoProfile", "-File", str(launcher), *arguments]
        for selected in (active, fallback):
            with self.subTest(selected=selected.name):
                if selected == active:
                    environment["VIRTUAL_ENV"] = str(active)
                else:
                    environment.pop("VIRTUAL_ENV", None)
                first = subprocess.run(
                    command,
                    cwd=self.root,
                    env=environment,
                    capture_output=True,
                    text=True,
                    timeout=60,
                    check=False,
                )
                self.assertEqual(first.returncode, 7, first.stderr)
                self.assertEqual(
                    json.loads(first.stdout),
                    {"args": arguments, "cwd": str(self.root), "prefix": str(selected)},
                )
                self.assertFalse((isolated / ".venv").exists())
                self.assertNotIn("VIRTUAL_ENV", first.stderr)
                second = subprocess.run(
                    command,
                    cwd=self.root,
                    env={**environment, "UV_OFFLINE": "1"},
                    capture_output=True,
                    text=True,
                    timeout=30,
                    check=False,
                )
                self.assertEqual(second.returncode, 7, second.stderr)
                self.assertEqual(first.stdout, second.stdout)
                self.assertEqual(second.stderr, "")
        environment["VIRTUAL_ENV"] = str(self.root / "invalid-active")
        failed = subprocess.run(
            command,
            cwd=self.root,
            env=environment,
            capture_output=True,
            text=True,
            timeout=15,
            check=False,
        )
        self.assertEqual(failed.returncode, 2)
        self.assertIn("Analysis did not start", failed.stderr)
        self.assertEqual(failed.stdout, "")

    def test_timeout_and_cancel(self):
        result = Runner(0.15).run(
            [sys.executable, "-c", "import time; time.sleep(20)"], self.root
        )
        self.assertEqual(result.status, "timed_out")
        runner = Runner(None)
        timer = threading.Timer(0.2, runner.cancelled.set)
        timer.start()
        try:
            result = runner.run(
                [sys.executable, "-c", "import time; time.sleep(20)"], self.root
            )
        finally:
            timer.cancel()
        self.assertEqual(result.status, "cancelled")

    def test_bounded_workers_preserve_exceptions(self):
        def worker(i):
            if i == 1:
                raise ToolError("failure")
            return i

        values = dict(bounded_map(worker, range(3), 2, Runner(None)))
        self.assertIsInstance(values[1], ToolError)
        self.assertEqual(values[2], 2)

    def test_exit_policies(self):
        warnings = [{"level": "warning"}]
        self.assertEqual(outcome([], [], False, "none", warnings), ("findings", 0))
        self.assertEqual(outcome([], [], False, "warning", warnings), ("findings", 1))
        self.assertEqual(outcome([], ["gap"], False, "none", []), ("incomplete", 2))
        self.assertEqual(outcome([], [], True, "none", []), ("cancelled", 130))


tidy = os.environ.get("OXYTIDY_TEST_LLVM") or shutil.which("clang-tidy")
if not tidy and Path("C:/Program Files/LLVM/bin/clang-tidy.exe").exists():
    tidy = "C:/Program Files/LLVM/bin/clang-tidy.exe"
LLVM = Path(tidy).resolve().parent if tidy else None


@unittest.skipUnless(
    LLVM, "Set OXYTIDY_TEST_LLVM to a clang-tidy executable for LLVM integration tests"
)
class LLVMTests(Fixture):
    def invoke(self, *args, implicit_root=None):
        logs = self.root / "logs"
        before = set(logs.glob("*/summary.json")) if logs.exists() else set()
        with (
            contextlib.redirect_stdout(io.StringIO()) as output,
            contextlib.redirect_stderr(output),
        ):
            code = main(
                [
                    *args,
                    "--clang-tidy-bin",
                    str(LLVM / ("clang-tidy.exe" if os.name == "nt" else "clang-tidy")),
                    "--log-dir",
                    str(logs),
                    "--summary-only",
                ],
                root=implicit_root or self.root,
            )
        created = set(logs.glob("*/summary.json")) - before
        self.assertEqual(len(created), 1, output.getvalue())
        summary = json.loads(created.pop().read_text())
        self.last_output = output.getvalue()
        return code, summary

    def source_pair(self):
        self.write("src/a.h", "inline int f() { return 1; }\n")
        self.write("src/a.cpp", '#include "a.h"\nint main() { return f(); }\n')
        self.database(["src/a.cpp"])

    def test_configuration_response_files_are_expanded_and_invalidate_reuse(self):
        self.write(
            "src/a.cpp", "#if FEATURE\nint f(){return 1;}\n#else\nint value;\n#endif\n"
        )
        self.database(["src/a.cpp"])
        for source in ("clangd", "ExtraArgs", "ExtraArgsBefore"):
            with self.subTest(source=source):
                self.write(
                    ".clangd",
                    "CompileFlags:\n  CompilationDatabase: .\n"
                    + ('  Add: ["@flags.rsp"]\n' if source == "clangd" else ""),
                )
                self.write(
                    ".clang-tidy",
                    'Checks: "-*,modernize-use-trailing-return-type"\n'
                    + (f'{source}: ["@flags.rsp"]\n' if source != "clangd" else ""),
                )
                response = self.write("flags.rsp", "-DFEATURE=0")
                code, report = self.invoke("src/a.cpp", "--incremental")
                self.assertEqual(code, 0, report)
                self.assertEqual(report["counts"]["unique_findings"], 0)
                self.write("flags.rsp", "-DFEATURE=1")
                code, report = self.invoke("src/a.cpp", "--incremental")
                self.assertEqual(code, 0, report)
                self.assertEqual(report["counts"]["reused"], 0)
                self.assertEqual(report["counts"]["unique_findings"], 1)
                self.assertIn(path_key(response), report["analysis_snapshot"])
                code, report = self.invoke("src/a.cpp", "--incremental")
                self.assertEqual(code, 0, report)
                self.assertEqual(report["counts"]["reused"], 1)

    def test_folded_yaml_checks_accept_overrides_without_hiding_unknown_checks(self):
        self.source_pair()
        self.write(
            ".clang-tidy",
            "Checks: >\n  modernize-*,\n  -modernize-use-trailing-return-type\n",
        )
        code, report = self.invoke(
            "src/a.h", "--checks=-*,modernize-use-trailing-return-type"
        )
        self.assertEqual(code, 0, report)
        self.assertEqual(report["counts"]["unique_findings"], 1)
        effective = json.loads(
            (Path(report["results"][0]["folder"]) / "effective.clang-tidy").read_text()
        )
        self.assertTrue(
            effective["Checks"].endswith("-*,modernize-use-trailing-return-type")
        )
        self.assertNotIn("\n", effective["Checks"])
        code, report = self.invoke("src/a.h", "--checks=-*,not-a-real-oxytidy-check")
        self.assertEqual(code, 2, report)
        self.assertIn("unknown check", report["coverage_gaps"][0]["reason"])

    def test_corrupt_cache_records_are_discarded_without_crashing(self):
        self.source_pair()
        self.invoke("src/a.h", "--incremental")
        cache = next((self.root / "out/clang-tidy/cache").glob("*.json"))
        for corruption in ("envelope", "diagnostic", "note", "replacement"):
            with self.subTest(corruption=corruption):
                value = json.loads(cache.read_text())
                if corruption == "envelope":
                    value = []
                elif corruption == "diagnostic":
                    value["result"]["diagnostics"] = [{}]
                elif corruption == "note":
                    value["result"]["diagnostics"][0]["notes"] = [
                        {"message": "missing location"}
                    ]
                else:
                    value["result"]["diagnostics"][0]["replacements"] = [{"file": "x"}]
                cache.write_text(json.dumps(value))
                code, report = self.invoke("src/a.h", "--incremental")
                self.assertEqual(code, 0, report)
                self.assertEqual(report["counts"]["reused"], 0)
                self.assertEqual(report["counts"]["unique_findings"], 1)
                self.assertIn("Cache ignored", self.last_output)

    def test_formatter_preserves_crlf_and_utf8_bom(self):
        self.source_pair()
        header = self.root / "src/a.h"
        header.write_bytes(b"\xef\xbb\xbfinline int f(){return 1;}\r\n")
        self.write(".clang-format", "BasedOnStyle: LLVM\nLineEnding: CRLF\n")
        code, report = self.invoke("src/a.h", "--fix", "--format")
        self.assertEqual(code, 0, report)
        content = header.read_bytes()
        self.assertTrue(content.startswith(b"\xef\xbb\xbf"))
        self.assertTrue(content.endswith(b"\r\n"))
        self.assertNotIn(b"\n", content.replace(b"\r\n", b""))
        self.assertIn(b"auto f() -> int", content)

    def test_explicit_project_root_overrides_the_tools_checkout(self):
        self.source_pair()
        code, report = self.invoke(
            "--project-root",
            os.path.relpath(self.root, Path.cwd()),
            "src/a.h",
            "--fail-on",
            "warning",
            implicit_root=self.root / "not-the-target",
        )
        self.assertEqual(code, 1, report)
        self.assertEqual(report["project_root"], str(self.root))
        self.assertEqual(report["build_dir"], str(self.root))
        self.assertEqual(report["diagnostics"][0]["file"], str(self.root / "src/a.h"))
        for label in (
            "Python",
            "Tool",
            "@project",
            "Ownership",
            "target checkout policy",
            "Clangd",
            "Compile DB",
            "CompilationDatabase from .clangd",
            "clang-tidy",
            "clang-scan-deps",
            "@run",
            "Cache",
            "report only",
            "Check configuration",
            "Source mapping",
        ):
            self.assertIn(label, self.last_output)

    def test_explicit_ownership_policy_applies_to_target_root(self):
        self.source_pair()
        policy = self.write(
            "policy/ownership.json", (self.root / ".oxytidy.json").read_text()
        )
        (self.root / ".oxytidy.json").unlink()
        code, report = self.invoke(
            "--project-root",
            str(self.root),
            "--ownership-file",
            os.path.relpath(policy, Path.cwd()),
            "src/a.h",
            implicit_root=self.root / "not-the-target",
        )
        self.assertEqual(code, 0, report)
        self.assertEqual(report["ownership_file"], str(policy))
        self.assertEqual(report["counts"]["unique_findings"], 1)
        self.assertFalse((self.root / ".oxytidy.json").exists())

    def test_project_root_does_not_remap_foreign_database_entries(self):
        self.source_pair()
        foreign = self.write("foreign/a.cpp", "int different;\n")
        entries = self.database([str(foreign)])
        code, report = self.invoke("--project-root", str(self.root), "src/a.cpp")
        self.assertEqual(code, 2, report)
        self.assertFalse(report["analysis_started"])
        self.assertEqual(
            json.loads((self.root / "compile_commands.json").read_text()), entries
        )

    def test_tool_policy_fallback_analyzes_target_sources_and_preserves_target_config(
        self,
    ):
        self.source_pair()
        tool_root = self.root / "tool-checkout"
        self.write(
            "tool-checkout/.oxytidy.json", (self.root / ".oxytidy.json").read_text()
        )
        (self.root / ".oxytidy.json").unlink()
        with patch("oxytidy.workflow.engine_root", return_value=tool_root):
            code, report = self.invoke("--project-root", str(self.root), "src/a.h")
        self.assertEqual(code, 0, report)
        self.assertEqual(report["counts"]["unique_findings"], 1)
        self.assertEqual(report["clangd_file"], str(self.root / ".clangd"))
        self.assertEqual(
            report["compilation_database"], str(self.root / "compile_commands.json")
        )
        self.assertEqual(report["ownership_file"], str(tool_root / ".oxytidy.json"))
        self.assertIn("fallback", report["ownership_selection"])
        self.assertIn("fallback", self.last_output)
        self.assertFalse((self.root / ".oxytidy.json").exists())

    def test_header_only_warning_and_fix_verification(self):
        self.source_pair()
        code, report = self.invoke("src/a.h", "--fail-on", "warning")
        self.assertEqual(code, 1, report)
        self.assertEqual(report["counts"]["unique_findings"], 1, report)
        code, report = self.invoke("src/a.h", "--fix")
        self.assertEqual(code, 0, report)
        self.assertEqual(report["status"], "clean", report)
        self.assertIn("auto f() -> int", (self.root / "src/a.h").read_text())
        self.assertEqual(len(report["verification"]), 1)

    def test_configured_warning_errors_are_fixable_findings(self):
        self.source_pair()
        self.write(
            ".clang-tidy",
            'Checks: "-*,modernize-use-trailing-return-type"\nWarningsAsErrors: "*"\n',
        )
        code, report = self.invoke("src/a.h")
        self.assertEqual(code, 1, report)
        self.assertEqual(report["status"], "findings")
        code, report = self.invoke("src", "--fix")
        self.assertEqual(code, 0, report)
        self.assertEqual(report["status"], "clean")

    def test_third_party_and_system_headers_never_reported_or_fixed(self):
        self.source_pair()
        vendor = self.write(
            "src/vendor/lib.h", "inline int vendor_function() { return 2; }\n"
        )
        system = self.write(
            "system/lib.h", "inline int system_function() { return 3; }\n"
        )
        self.write(
            "src/a.cpp",
            '#include "a.h"\n#include "vendor/lib.h"\n#include <lib.h>\nint main() { return f()+vendor_function()+system_function(); }\n',
        )
        self.database(["src/a.cpp"], arguments=["-isystem", str(system.parent)])
        before = fingerprint([vendor, system])
        code, report = self.invoke("src", "--fix")
        self.assertEqual(code, 0, report)
        self.assertEqual(before, fingerprint([vendor, system]))
        self.assertFalse(
            any(
                "vendor" in path or "system" in path
                for path in report["fixes"]["applied_files"]
            )
        )

    def test_shared_header_deduplicated_and_basename_logs_unique(self):
        self.write("src/shared.h", "inline int f() { return 1; }\n")
        for folder in ["a", "b"]:
            self.write(
                f"src/{folder}/same.cpp",
                '#include "../shared.h"\nint g() { return f(); }\n',
            )
        self.database(["src/a/same.cpp", "src/b/same.cpp"])
        code, report = self.invoke("src/shared.h")
        self.assertEqual(code, 0, report)
        self.assertEqual(report["counts"]["unique_findings"], 1, report)
        self.assertEqual(len(report["diagnostics"][0]["origins"]), 2)
        self.assertEqual(len({result["folder"] for result in report["results"]}), 2)
        code, report = self.invoke("src/shared.h", "--fix")
        self.assertEqual(code, 0, report)
        self.assertEqual(len(report["fixes"]["applied_files"]), 1)

    def test_header_consumers_outside_selected_directory_are_not_discovered(self):
        self.write("src/include/shared.h", "inline int f() { return 1; }\n")
        self.write(
            "src/consumer.cpp", '#include "include/shared.h"\nint g() { return f(); }\n'
        )
        self.database(["src/consumer.cpp"])
        code, report = self.invoke("src/include", "--list-files")
        self.assertEqual(code, 0, report)
        self.assertEqual(report["selected_count"], 0)
        self.assertEqual(
            report["unreached_headers"], [str(self.root / "src/include/shared.h")]
        )
        self.assertEqual(report["discovery_scope"], [str(self.root / "src/include")])
        self.assertEqual(report["coverage_gaps"], [])
        self.assertIn("module coverage gap", self.last_output)

    def test_unreached_header_is_module_gap_but_context_cap_is_still_incomplete(self):
        self.source_pair()
        self.write("src/orphan.h", "int x;")
        code, report = self.invoke("src/orphan.h", "--fix")
        self.assertEqual(code, 0, report)
        self.assertEqual(report["status"], "no_analysis")
        self.assertFalse(report["analysis_started"])
        self.assertEqual(report["unreached_headers"], [str(self.root / "src/orphan.h")])
        self.assertEqual(report["fixes"]["applied_files"], [])
        self.assertIn("Analysis did not run", self.last_output)
        self.write("src/b.cpp", "int other;")
        self.database(["src/a.cpp", "src/b.cpp"])
        code, report = self.invoke("src/a.cpp", "src/b.cpp", "--max-files", "1")
        self.assertEqual(code, 2, report)

    def test_module_scope_ignores_other_modules_and_does_not_block_fixes_for_orphan_header(
        self,
    ):
        header = self.write("src/module/shared.h", "inline int f() { return 1; }\n")
        orphan = self.write(
            "src/module/orphan.h", "inline int unchecked() { return 2; }\n"
        )
        self.write("src/module/a.cpp", '#include "shared.h"\nint g(){return f();}\n')
        outside = self.write(
            "src/other/consumer.cpp",
            '#include "../module/shared.h"\n#error do not analyze this module\n',
        )
        self.database(["src/module/a.cpp", "src/other/consumer.cpp"])
        untouched = fingerprint([orphan, outside])
        code, report = self.invoke("src/module", "--fix", "--fail-on", "warning")
        self.assertEqual(code, 0, report)
        self.assertEqual(report["status"], "clean")
        self.assertEqual(report["selected_count"], 1)
        self.assertEqual(report["unreached_headers"], [str(orphan)])
        self.assertEqual(report["coverage_gaps"], [])
        self.assertEqual(report["discovery_scope"], [str(self.root / "src/module")])
        self.assertIn("auto f() -> int", header.read_text())
        self.assertEqual(fingerprint([orphan, outside]), untouched)
        self.assertNotIn("consumer.cpp", self.last_output)

    def test_standalone_header_uses_only_its_parent_directory_and_reports_only_the_header(
        self,
    ):
        self.write("src/module/a.h", "inline int f() { return 1; }\n")
        source = self.write(
            "src/module/a.cpp", '#include "a.h"\nint g(){return f();}\n'
        )
        self.write("src/other/a.cpp", '#include "../module/a.h"\n#error out of scope\n')
        self.database(["src/module/a.cpp", "src/other/a.cpp"])
        source_before = source.read_bytes()
        code, report = self.invoke("src/module/a.h", "--fix")
        self.assertEqual(code, 0, report)
        self.assertEqual(report["discovery_scope"], [str(self.root / "src/module")])
        self.assertEqual(report["selected_count"], 1)
        self.assertEqual(source.read_bytes(), source_before)

    def test_missing_source_compile_command_remains_failure(self):
        self.source_pair()
        self.write("src/unconfigured.cpp", "int x;\n")
        code, report = self.invoke("src/unconfigured.cpp")
        self.assertEqual(code, 2, report)
        self.assertEqual(report["status"], "incomplete")
        self.assertIn(
            "No matching compile command", report["coverage_gaps"][0]["reason"]
        )

    def test_scope_without_test_consumers_reports_module_gap_until_tests_are_included(
        self,
    ):
        header = self.write("src/module/a.h", "inline int f(){return 1;}\n")
        self.write(
            "src/module/Test/a.cpp", '#include "../a.h"\nint main(){return f();}\n'
        )
        self.database(["src/module/Test/a.cpp"])
        code, report = self.invoke("src/module/a.h")
        self.assertEqual(code, 0, report)
        self.assertEqual(report["status"], "no_analysis")
        self.assertEqual(report["unreached_headers"], [str(header)])
        code, report = self.invoke("src/module/a.h", "--include-tests")
        self.assertEqual(code, 0, report)
        self.assertEqual(report["unreached_headers"], [])
        self.assertEqual(report["selected_count"], 1)

    def test_failed_discovery_does_not_label_unreached_headers_as_module_gap(self):
        self.source_pair()
        self.write("src/a.cpp", '#include "missing.h"\n')
        code, report = self.invoke("src/a.h")
        self.assertEqual(code, 2, report)
        self.assertFalse(report["header_discovery_complete"])
        self.assertIn("reachability is unresolved", self.last_output)

    def test_header_reachability_is_refreshed_after_include_fixes(self):
        header = self.write(
            "src/unused.h", "#pragma once\ninline int unused(){return 1;}\n"
        )
        self.write("src/a.cpp", '#include "unused.h"\nint main(){return 0;}\n')
        self.database(["src/a.cpp"])
        code, report = self.invoke("src", "--fix", "--checks=-*,misc-include-cleaner")
        self.assertEqual(code, 0, report)
        self.assertNotIn('#include "unused.h"', (self.root / "src/a.cpp").read_text())
        self.assertEqual(report["unreached_headers"], [str(header)])
        self.assertTrue(report["header_discovery_complete"])
        self.assertIn("Analyzed contexts clean", self.last_output)

    def test_incremental_reuse_and_dependency_config_argument_invalidation(self):
        self.source_pair()
        self.invoke("src/a.h", "--incremental")
        code, report = self.invoke("src/a.h", "--incremental", "--fail-on", "warning")
        self.assertEqual(code, 1, report)
        self.assertEqual(report["counts"]["reused"], 1)
        self.write("src/a.h", "inline int f() { return 2; }\n")
        _, report = self.invoke("src/a.h", "--incremental")
        self.assertEqual(report["counts"]["reused"], 0)
        self.write(".clang-tidy", 'Checks: "-*,readability-identifier-naming"\n')
        _, report = self.invoke("src/a.h", "--incremental")
        self.assertEqual(report["counts"]["reused"], 0)
        self.database(["src/a.cpp"], arguments=["-DCHANGED=1"])
        _, report = self.invoke("src/a.h", "--incremental")
        self.assertEqual(report["counts"]["reused"], 0)

    def test_compile_failure_blocks_all_fixes(self):
        self.source_pair()
        original = (self.root / "src/a.h").read_bytes()
        self.write("src/b.cpp", "invalid C++ code;")
        self.database(["src/a.cpp", "src/b.cpp"])
        code, report = self.invoke("src", "--fix")
        self.assertEqual(code, 2, report)
        self.assertEqual((self.root / "src/a.h").read_bytes(), original)

    def test_project_exclusion_and_test_inclusion(self):
        self.source_pair()
        self.write(
            ".clang-tidy",
            'Checks: "-*,modernize-use-trailing-return-type"\nExcludeHeaderFilterRegex: "a[.]h$"\n',
        )
        _, report = self.invoke("src/a.h")
        self.assertEqual(report["counts"]["unique_findings"], 0)
        self.write("src/Test/a.cpp", "int test_function() { return 1; }\n")
        self.database(["src/a.cpp", "src/Test/a.cpp"])
        code, report = self.invoke("src", "--include-tests", "--list-files")
        self.assertEqual(code, 0, report)
        self.assertEqual(report["selected_count"], 2)

    def test_export_and_format_modes(self):
        self.source_pair()
        target = self.root / "proposals.json"
        before = (self.root / "src/a.h").read_bytes()
        code, report = self.invoke("src/a.h", "--export-fixes", str(target))
        self.assertEqual(code, 0, report)
        self.assertEqual((self.root / "src/a.h").read_bytes(), before)
        self.assertTrue(json.loads(target.read_text())["proposed"])
        self.write(".clang-format", "BasedOnStyle: LLVM\nIndentWidth: 2\n")
        code, report = self.invoke("src/a.h", "--fix", "--format")
        self.assertEqual(code, 0, report)
        self.assertEqual(report["status"], "clean")

    def test_new_dependency_and_force_invalidate_reuse(self):
        self.source_pair()
        self.write(
            "src/a.cpp",
            '#if __has_include("new.h")\n#include "new.h"\n#endif\n#include "a.h"\nint main(){return f();}\n',
        )
        self.invoke("src/a.cpp", "--incremental")
        self.write("src/new.h", "inline int new_function(){return 2;}\n")
        _, report = self.invoke("src/a.cpp", "--incremental")
        self.assertEqual(report["counts"]["reused"], 0)
        _, report = self.invoke("src/a.cpp", "--incremental", "--force")
        self.assertEqual(report["counts"]["reused"], 0)

    def test_distinct_build_contexts_and_tool_identity_invalidation(self):
        self.source_pair()
        entries = self.database(["src/a.cpp", "src/a.cpp"])
        entries[1]["arguments"].append("-DSECOND=1")
        self.write("compile_commands.json", json.dumps(entries))
        _, report = self.invoke("src/a.h", "--incremental")
        self.assertEqual(report["counts"]["executed"], 2)
        self.assertEqual(len(report["diagnostics"][0]["origins"]), 2)
        from oxytidy.workflow import resolve_tools

        def changed_version(*args):
            tidy, scanner, formatter, versions = resolve_tools(*args)
            versions["clang-tidy"]["binary_hash"] = "changed-tool-identity"
            return tidy, scanner, formatter, versions

        with patch("oxytidy.workflow.resolve_tools", side_effect=changed_version):
            _, report = self.invoke("src/a.h", "--incremental")
        self.assertEqual(report["counts"]["reused"], 0)


if __name__ == "__main__":
    unittest.main()
