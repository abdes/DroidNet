"""Compact formatter presentation across terminals, pipes, and failures."""

import io
import re
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

from oxyformat.reporting import Reporter
from rich.cells import cell_len
from rich.console import Console


class ReportingTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)

    def test_terminal_groups_paths_without_repeating_status(self):
        for width in (60, 80, 120):
            with self.subTest(width=width):
                stream = io.StringIO()
                console = Console(
                    file=stream, force_terminal=True, width=width, color_system=None
                )
                reporter = Reporter(console)
                reporter.start(
                    self.root, [self.root / "src"], fix=False, all_project=False
                )
                for name in ("[red].h", "other.cpp"):
                    reporter.file(self.root / "src" / name)
                reporter.finish(
                    {"needed": 2, "unchanged": 8}, 10, 0.12, cancelled=False
                )
                text = stream.getvalue()
                self.assertEqual(text.count("Needs formatting"), 1)
                self.assertIn("[red].h", text)
                self.assertIn("2 of 10 files", text)
                self.assertNotIn("0 failed", text)
                self.assertNotIn("0 skipped", text)
                self.assertTrue(
                    all(cell_len(line) <= width for line in text.splitlines())
                )

    def test_pipes_keep_complete_paths_and_separate_context(self):
        errors, output = io.StringIO(), io.StringIO()
        reporter = Reporter(
            Console(file=errors, force_terminal=False),
            Console(file=output, width=40, force_terminal=False),
        )
        path = self.root / ("very-long-directory-" * 8) / "[red].cpp"
        reporter.start(self.root, [path], fix=False, all_project=False)
        reporter.file(path)
        reporter.finish({"needed": 1}, 1, 0.1, cancelled=False)
        self.assertEqual(output.getvalue(), f"Needs formatting: {path}\n")
        self.assertIn("oxyformat  check", errors.getvalue())
        self.assertNotIn("\x1b", output.getvalue() + errors.getvalue())

    def test_clean_empty_failed_and_cancelled_are_distinct(self):
        cases = [
            ({"unchanged": 2}, 2, False, "Clean"),
            ({"skipped": 1}, 0, False, "Nothing to format"),
            ({"failed": 1, "changed": 1}, 2, False, "Incomplete"),
            ({"changed": 1}, 3, True, "Cancelled"),
        ]
        for counts, selected, cancelled, expected in cases:
            with self.subTest(expected=expected):
                stream = io.StringIO()
                reporter = Reporter(Console(file=stream, force_terminal=False))
                reporter.finish(counts, selected, 0.1, cancelled=cancelled)
                text = stream.getvalue()
                self.assertIn(expected, text)
                if cancelled:
                    self.assertIn("1 of 3 checked", text)
                    self.assertIn("retained", text)
                if counts.get("failed"):
                    self.assertIn("1 failed", text)
                    self.assertIn("1 formatted", text)

    def test_no_color_disables_color_even_in_a_terminal(self):
        with patch.dict("os.environ", {"NO_COLOR": "1"}):
            stream = io.StringIO()
            console = Console(file=stream, force_terminal=True, color_system="standard")
            reporter = Reporter(console)
            reporter.start(self.root, [self.root / "src"], fix=True, all_project=False)
            reporter.finish({"changed": 1}, 1, 0.1, cancelled=False)
        codes = [
            int(code)
            for group in re.findall(r"\x1b\[([\d;]*)m", stream.getvalue())
            for code in group.split(";")
            if code
        ]
        self.assertFalse(
            any(code in {*range(30, 39), *range(90, 98)} for code in codes)
        )
