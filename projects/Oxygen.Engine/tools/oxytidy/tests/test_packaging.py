"""Installed entry points and checkout discovery share one Python CLI."""

from __future__ import annotations

import shutil
import subprocess
import sys
import sysconfig
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

from oxytidy.common import ToolError
from oxytidy.workflow import engine_root

PROJECT = Path(__file__).resolve().parents[1]
ENGINE = PROJECT.parent.parent


class PackagingTests(unittest.TestCase):
    def test_editable_install_locates_its_checkout_from_another_directory(self):
        with tempfile.TemporaryDirectory() as directory:
            other = Path(directory)
            (other / ".oxytools.json").write_text("{}", encoding="utf-8")
            with patch("pathlib.Path.cwd", return_value=other):
                self.assertEqual(engine_root(), ENGINE)

    def test_wheel_discovery_uses_working_directory_or_reports_missing_root(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            checkout = root / "engine"
            checkout.mkdir()
            (checkout / ".oxytools.json").write_text("{}", encoding="utf-8")
            installed = root / "site-packages/oxytidy/workflow.py"
            with patch("oxytidy.workflow.__file__", str(installed)):
                with patch("pathlib.Path.cwd", return_value=checkout):
                    self.assertEqual(engine_root(), checkout)
                # The old tool-specific filename is no longer a root marker.
                (root / ".oxytidy.json").write_text("{}", encoding="utf-8")
                with (
                    patch("pathlib.Path.cwd", return_value=root),
                    self.assertRaises(ToolError),
                ):
                    engine_root()

    @unittest.skipUnless(
        shutil.which("pwsh") and shutil.which("uv"), "PowerShell and uv are required"
    )
    def test_console_module_and_wrapper_have_identical_help_and_errors(self):
        console = str(
            Path(sysconfig.get_path("scripts"))
            / ("oxytidy.exe" if sys.platform == "win32" else "oxytidy")
        )
        self.assertIsNotNone(console, "Run tests in the installed project environment")
        commands = [
            [console],
            [sys.executable, "-m", "oxytidy"],
            ["pwsh", "-NoProfile", "-File", str(PROJECT.parent / "cli/oxytidy.ps1")],
        ]
        with tempfile.TemporaryDirectory() as directory:
            for arguments, expected in [
                (["--help"], 0),
                (["--unknown-option"], 2),
                ([], 2),
            ]:
                with self.subTest(arguments=arguments):
                    results = [
                        subprocess.run(
                            [*command, *arguments],
                            cwd=directory,
                            capture_output=True,
                            text=True,
                            check=False,
                            timeout=30,
                        )
                        for command in commands
                    ]
                    for result in results:
                        self.assertEqual(result.returncode, expected, result.stderr)
                        self.assertEqual(result.stdout, results[0].stdout)
                        self.assertEqual(result.stderr, results[0].stderr)


if __name__ == "__main__":
    unittest.main()
