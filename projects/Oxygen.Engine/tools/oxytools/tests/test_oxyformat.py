"""Formatter contracts, failure isolation, and real LLVM check/fix coverage."""

from __future__ import annotations

import contextlib
import io
import json
import os
import shutil
import stat
import subprocess
import sys
import tempfile
import threading
import unittest
from pathlib import Path
from unittest.mock import patch

from oxyformat.cli import main, parse_args
from oxyformat.engine import (
    Cancelled,
    Formatter,
    Processes,
    find_formatter,
    prepare_style,
)
from oxyformat.selection import select_files
from oxytools.common import ToolError
from oxytools.files import atomic_write
from oxytools.ownership import Ownership

try:
    LLVM = find_formatter(None)
except ToolError:
    LLVM = None

PROJECT = Path(__file__).resolve().parents[1]


class Fixture(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="oxyformat tests ")
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.write(
            ".oxytools.json",
            json.dumps(
                {
                    "project_roots": ["src", "Examples"],
                    "exclude": ["**/vendor/**", "**/generated/**"],
                }
            ),
        )
        self.write(".clang-format", "BasedOnStyle: LLVM\nIndentWidth: 2\n")

    def write(self, name, data):
        path = self.root / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data.encode() if isinstance(data, str) else data)
        return path

    def invoke(self, *args):
        stdout, stderr = io.StringIO(), io.StringIO()
        with contextlib.redirect_stdout(stdout), contextlib.redirect_stderr(stderr):
            code = main(["--project-root", str(self.root), *args])
        return code, stdout.getvalue(), stderr.getvalue()


class SelectionTests(Fixture):
    def test_explicit_files_never_walk_and_overlaps_are_deduplicated(self):
        path = self.write("src/MixedCase.cpp", "int x;\n")
        with patch("oxyformat.selection.os.walk", side_effect=AssertionError("walk")):
            selection = select_files(Ownership.load(self.root), [path, path])
        self.assertEqual(selection.files, [path])
        selection = select_files(Ownership.load(self.root), [self.root / "src", path])
        self.assertEqual(selection.files, [path])

    def test_tests_included_vendor_generated_and_non_cpp_excluded(self):
        source = self.write("src/a.cpp", "int x;\n")
        test = self.write("src/Test/a_test.cpp", "int x;\n")
        example = self.write("Examples/a.h", "int x;\n")
        self.write("src/vendor/a.cpp", "int x;\n")
        self.write("src/generated/a.cpp", "int x;\n")
        self.write("src/a.hlsl", "invalid C++")
        selection = select_files(Ownership.load(self.root), [self.root])
        self.assertEqual(set(selection.files), {source, test, example})

    def test_file_exclusions_do_not_prune_eligible_siblings(self):
        self.write(
            ".oxytools.json",
            json.dumps({"project_roots": ["src"], "exclude": ["**/*.cpp"]}),
        )
        self.write("src/nested/a.cpp", "int x;\n")
        header = self.write("src/nested/a.h", "int x;\n")
        result = select_files(Ownership.load(self.root), [self.root / "src"])
        self.assertEqual(result.files, [header])

    def test_invalid_policy_is_a_setup_error(self):
        self.write(
            ".oxytools.json", '{"project_roots":["src"],"exclude":[],"unknown":true}'
        )
        code, _, error = self.invoke("--all")
        self.assertEqual(code, 2)
        self.assertIn("unknown", error)

    def test_no_arguments_or_conflicting_scope_are_rejected(self):
        for args in (
            [],
            ["--all", "src"],
            ["src", "--jobs", "0"],
            ["src", "--timeout", "nan"],
        ):
            with self.subTest(args=args), contextlib.redirect_stderr(io.StringIO()):
                with self.assertRaises(SystemExit) as error:
                    parse_args(args)
                self.assertEqual(error.exception.code, 2)

    def test_all_excluded_files_need_no_formatter(self):
        self.write("src/vendor/a.cpp", "int x;\n")
        with patch(
            "oxyformat.engine.find_formatter", side_effect=AssertionError("LLVM")
        ):
            code, _, output = self.invoke("src/vendor/a.cpp")
        self.assertEqual(code, 0)
        self.assertIn("1 skipped", output)


class WriteTests(Fixture):
    def formatter(self, path, operation, *, fix=True):
        processes = Processes(1)
        processes.run = operation
        style = self.root / ".clang-format"
        return Formatter("unused", style, style, style.read_bytes(), fix, processes)

    def test_concurrent_source_edit_is_not_overwritten(self):
        path = self.write("src/a.cpp", "int  x;\n")

        def format_and_edit(*_):
            path.write_bytes(b"int user_edit;\n")
            return b"int x;\n"

        result = self.formatter(path, format_and_edit).format(path)
        self.assertEqual(result.status, "failed")
        self.assertEqual(path.read_bytes(), b"int user_edit;\n")

    def test_style_change_prevents_write(self):
        path = self.write("src/a.cpp", "int  x;\n")

        def format_and_edit(*_):
            self.write(".clang-format", "BasedOnStyle: Google\n")
            return b"int x;\n"

        result = self.formatter(path, format_and_edit).format(path)
        self.assertEqual(result.status, "failed")
        self.assertEqual(path.read_bytes(), b"int  x;\n")

    def test_write_failure_preserves_original_and_cleans_temporary(self):
        path = self.write("src/a.cpp", "int  x;\n")
        with patch("oxytools.files.os.replace", side_effect=PermissionError("locked")):
            result = self.formatter(path, lambda *_: b"int x;\n").format(path)
        self.assertEqual(result.status, "failed")
        self.assertEqual(path.read_bytes(), b"int  x;\n")
        self.assertEqual(list(path.parent.glob(".oxytools-*")), [])

    def test_replacement_checks_contents_again_after_preparation(self):
        path = self.write("src/a.cpp", "int user_edit;\n")
        with self.assertRaises(ToolError):
            atomic_write(
                path,
                b"int x;\n",
                stat.S_IMODE(path.stat().st_mode),
                expected=b"int  x;\n",
            )
        self.assertEqual(path.read_bytes(), b"int user_edit;\n")

    def test_filename_case_and_permissions_survive_write(self):
        path = self.write("src/MixedCase.cpp", "int  x;\n")
        mode = stat.S_IMODE(path.stat().st_mode)
        result = self.formatter(path, lambda *_: b"int x;\n").format(path)
        self.assertEqual(result.status, "changed")
        self.assertEqual([p.name for p in path.parent.iterdir()], ["MixedCase.cpp"])
        self.assertEqual(stat.S_IMODE(path.stat().st_mode), mode)

    def test_cancelled_run_dispatches_no_files(self):
        path = self.write("src/a.cpp", "int  x;\n")
        formatter = self.formatter(path, lambda *_: self.fail("dispatched"))
        formatter.processes.cancelled.set()
        self.assertEqual(list(formatter.run([path], 1)), [])
        self.assertEqual(path.read_bytes(), b"int  x;\n")

    def test_process_timeout(self):
        with self.assertRaisesRegex(ToolError, "timed out"):
            Processes(0.02).run([sys.executable, "-c", "import time; time.sleep(10)"])

    def test_cancellation_terminates_an_active_formatter(self):
        processes = Processes(10)
        timer = threading.Timer(0.05, processes.cancelled.set)
        timer.start()
        try:
            with self.assertRaises(Cancelled):
                processes.run([sys.executable, "-c", "import time; time.sleep(10)"])
        finally:
            timer.cancel()
            timer.join()

    def test_wrong_llvm_major_is_rejected_before_style_or_source_access(self):
        processes = Processes(1)
        for major in (22, 24):
            with (
                self.subTest(major=major),
                patch.object(
                    processes,
                    "run",
                    return_value=f"clang-format version {major}.1.0\n".encode(),
                ) as run,
                self.assertRaisesRegex(ToolError, r"23\.x"),
            ):
                prepare_style("unused", self.root, self.root, processes)
            self.assertEqual(run.call_count, 1)


@unittest.skipUnless(LLVM, "clang-format 23 is required")
class LLVMTests(Fixture):
    def test_oxygen_include_spelling_is_checked_and_fixed(self):
        path = self.write("src/a.cpp", '#include "Oxygen/Alpha.h"\n')
        code, _, _ = self.invoke("src/a.cpp")
        self.assertEqual(code, 1)
        self.assertEqual(path.read_text(), '#include "Oxygen/Alpha.h"\n')
        code, _, error = self.invoke("src/a.cpp", "--fix")
        self.assertEqual(code, 0, error)
        self.assertEqual(path.read_text(), "#include <Oxygen/Alpha.h>\n")

    def test_check_fix_check_and_idempotence(self):
        path = self.write("src/a.cpp", "int  main( ){return  0;}\n")
        original = path.read_bytes()
        code, output, error = self.invoke("src/a.cpp")
        self.assertEqual(code, 1, error)
        self.assertIn(f"Needs formatting: {path}", output)
        self.assertNotIn("return", output)
        self.assertEqual(path.read_bytes(), original)
        code, output, error = self.invoke("src/a.cpp", "--fix")
        self.assertEqual(code, 0, error)
        self.assertIn("Formatted", error)
        self.assertIn("1 file", error)
        expected = path.read_bytes()
        modified = path.stat().st_mtime_ns
        code, _, error = self.invoke("src/a.cpp")
        self.assertEqual(code, 0, error)
        code, _, error = self.invoke("src/a.cpp", "--fix")
        self.assertEqual(code, 0, error)
        self.assertEqual(path.read_bytes(), expected)
        self.assertEqual(path.stat().st_mtime_ns, modified)

    def test_invalid_encoding_and_missing_input_do_not_block_valid_files(self):
        invalid = self.write("src/bad.cpp", b"\xff\xfei\x00")
        good = self.write("src/good.cpp", "int  x;\n")
        code, _, output = self.invoke(
            "src/bad.cpp", "src/missing.cpp", "src/good.cpp", "--fix", "--jobs", "1"
        )
        self.assertEqual(code, 2)
        self.assertIn("2 failed", output)
        self.assertEqual(invalid.read_bytes(), b"\xff\xfei\x00")
        self.assertEqual(good.read_bytes(), b"int x;\n")

    def test_incomplete_cpp_fails_without_blocking_other_files(self):
        bad = self.write("src/bad.cpp", "int f() { return (; }\n")
        good = self.write("src/good.cpp", "int  x;\n")
        code, output, error = self.invoke("src", "--fix")
        self.assertEqual(code, 2, output + error)
        self.assertEqual(bad.read_bytes(), b"int f() { return (; }\n")
        self.assertEqual(good.read_bytes(), b"int x;\n")

    def test_root_config_overrides_nested_styles_and_ignore_files(self):
        path = self.write("src/nested/a.cpp", "int f() {\nreturn 0;\n}\n")
        self.write("src/.clang-format", "DisableFormat: true\n")
        self.write("src/.clang-format-ignore", "*\n")
        code, _, error = self.invoke("src/nested/a.cpp", "--fix")
        self.assertEqual(code, 0, error)
        self.assertEqual(path.read_bytes(), b"int f() { return 0; }\n")

    def test_invalid_root_config_stops_all_writes(self):
        path = self.write("src/a.cpp", "int  x;\n")
        self.write(".clang-format", "NotAClangOption: true\n")
        code, _, error = self.invoke("src", "--fix")
        self.assertEqual(code, 2)
        self.assertIn("NotAClangOption", error)
        self.assertEqual(path.read_bytes(), b"int  x;\n")

    def test_bom_and_configured_line_endings(self):
        self.write(".clang-format", "BasedOnStyle: LLVM\nLineEnding: CRLF\n")
        path = self.write("src/a.cpp", b"\xef\xbb\xbfint  x;\r\n")
        code, _, error = self.invoke("src", "--fix")
        self.assertEqual(code, 0, error)
        self.assertEqual(path.read_bytes(), b"\xef\xbb\xbfint x;\r\n")

    def test_root_style_cannot_inherit_an_external_parent(self):
        path = self.write("src/a.cpp", "int  x;\n")
        self.write(".clang-format", "BasedOnStyle: InheritParentConfig\n")
        code, _, error = self.invoke("src", "--fix")
        self.assertEqual(code, 2)
        self.assertIn("must not inherit", error)
        self.assertEqual(path.read_bytes(), b"int  x;\n")

    def test_all_includes_tests_and_obeys_exclusions_without_build_tree(self):
        test = self.write("src/Test/a.cpp", "int  x;\n")
        vendor = self.write("src/vendor/a.cpp", "int  x;\n")
        self.write("Examples/a.h", "int x;\n")
        code, _, error = self.invoke("--all", "--fix")
        self.assertEqual(code, 0, error)
        self.assertEqual(test.read_bytes(), b"int x;\n")
        self.assertEqual(vendor.read_bytes(), b"int  x;\n")

    def test_cwd_paths_for_hook_are_explicit(self):
        path = self.write("src/a.cpp", "int x;\n")
        with patch("pathlib.Path.cwd", return_value=self.root.parent):
            code, _, error = self.invoke(
                str(path.relative_to(self.root.parent)), "--paths-from-cwd"
            )
        self.assertEqual(code, 0, error)

    def test_launchers_agree_and_do_not_install_during_runs(self):
        path = self.write("src/a.cpp", "int  x;\n")
        commands = [
            [sys.executable, "-m", "oxyformat"],
            [sys.executable, str(PROJECT / "run_oxyformat.py")],
        ]
        if shutil.which("pwsh"):
            commands.append(
                [
                    "pwsh",
                    "-NoProfile",
                    "-File",
                    str(PROJECT.parent / "cli/oxyformat.ps1"),
                ]
            )
        for command in commands:
            with self.subTest(command=command):
                result = subprocess.run(
                    [*command, "--project-root", str(self.root), str(path)],
                    capture_output=True,
                    text=True,
                    timeout=30,
                    env={**os.environ, "UV_OFFLINE": "1"},
                    check=False,
                )
                self.assertEqual(result.returncode, 1, result.stderr)
                self.assertIn(f"Needs formatting: {path}", result.stdout)
                self.assertNotIn("install", result.stderr.lower())


if __name__ == "__main__":
    unittest.main()
