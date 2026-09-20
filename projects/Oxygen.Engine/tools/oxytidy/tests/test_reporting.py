"""Rendering contracts for terminals, narrow screens, and redirected output."""

from __future__ import annotations

import io
import re
import sys
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

from oxytidy.reporting import Reporter
from oxytidy.workflow import parse_args
from rich.cells import cell_len
from rich.console import Console


class ReportingTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="oxytidy-render-")
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)

    def summary(self):
        return {
            "options": vars(parse_args(["src/Oxygen/Base", "--include-tests"])),
            "project_root": str(self.root),
            "project_root_selection": "explicit --project-root",
            "tool_package": str(self.root / "tools/oxytidy/src/oxytidy"),
            "python_prefix": sys.prefix,
            "python_interpreter": sys.executable,
            "working_directory": str(self.root),
            "run_dir": str(self.root / "out/clang-tidy/run-example"),
            "cache_dir": str(self.root / "out/clang-tidy/cache"),
            "status": "setup_failed",
            "analysis_started": False,
            "error": f"Missing clangd configuration: {self.root / '.clangd'}\nSelect an existing configuration with --clangd-file PATH.",
        }

    def test_setup_error_precedes_context_at_narrow_and_wide_widths(self):
        for width in (60, 80, 120):
            with self.subTest(width=width):
                stream = io.StringIO()
                console = Console(file=stream, width=width, force_terminal=False)
                reporter = Reporter(console)
                summary = self.summary()
                reporter.start(summary)
                reporter.ownership(
                    self.root / ".oxytools.json", "target checkout policy"
                )
                reporter.finish(summary)
                text = stream.getvalue()
                self.assertLess(text.index("Setup failed"), text.index("Paths"))
                self.assertIn("Analysis did not start", text)
                self.assertIn("@project/.clangd", text)
                self.assertIn("@run/summary.json", text)
                self.assertNotIn("0 unique", text)
                self.assertNotIn("\x1b", text)
                self.assertTrue(
                    all(cell_len(line) <= width for line in text.splitlines())
                )

    def test_no_color_suppresses_colors_even_in_terminal_rendering(self):
        with patch.dict("os.environ", {"NO_COLOR": "1"}):
            stream = io.StringIO()
            console = Console(
                file=stream, width=80, force_terminal=True, color_system="standard"
            )
            reporter = Reporter(console)
            reporter.start(self.summary())
            reporter.finish(self.summary())
        codes = [
            int(code)
            for group in re.findall(r"\x1b\[([\d;]*)m", stream.getvalue())
            for code in group.split(";")
            if code
        ]
        self.assertFalse(
            any(code in {*range(30, 39), *range(90, 98)} for code in codes)
        )

    def test_redirected_progress_has_no_animation_or_stdout_leak(self):
        errors, output = io.StringIO(), io.StringIO()
        reporter = Reporter(
            Console(file=errors, force_terminal=False),
            Console(file=output, force_terminal=False),
        )
        reporter.start_phase("Analysis", 2)
        reporter.advance(failed=True)
        reporter.advance()
        reporter.stop_phase()
        self.assertIn("2/2 contexts | 1 failed", errors.getvalue())
        self.assertNotIn("\x1b", errors.getvalue())
        self.assertNotIn("\r", errors.getvalue())
        self.assertEqual(output.getvalue(), "")

    def test_piped_diagnostics_preserve_absolute_paths_on_one_line(self):
        errors, output = io.StringIO(), io.StringIO()
        reporter = Reporter(
            Console(file=errors, force_terminal=False),
            Console(file=output, width=40, force_terminal=False),
        )
        path = self.root / ("long-directory-" * 8) / "[red].cpp"
        reporter.aliases["@project"] = self.root
        reporter.diagnostics(
            [
                {
                    "file": str(path),
                    "line": 12,
                    "column": 5,
                    "level": "warning",
                    "check": "test-check",
                    "message": "literal [red] markup",
                    "notes": [],
                }
            ]
        )
        self.assertTrue(output.getvalue().splitlines()[0].startswith(f"{path}:12:5"))
        self.assertIn("literal [red] markup", output.getvalue())
        self.assertEqual(errors.getvalue(), "")
        self.assertNotIn("\x1b", output.getvalue())

    def test_path_aliases_do_not_consume_sibling_prefixes_or_markup(self):
        stream = io.StringIO()
        reporter = Reporter(Console(file=stream, width=100, force_terminal=False))
        reporter.aliases["@project"] = self.root / "engine"
        sibling = self.root / "engine-other" / "file.h"
        self.assertEqual(reporter.compact(str(sibling)), str(sibling))
        reporter.rows([("Header", self.root / "engine/[red].h")])
        self.assertIn("@project/[red].h", stream.getvalue())


if __name__ == "__main__":
    unittest.main()
