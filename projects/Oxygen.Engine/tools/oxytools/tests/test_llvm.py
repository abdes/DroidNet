"""Release-family gates shared by analysis and formatting."""

from __future__ import annotations

import unittest
from pathlib import Path
from unittest.mock import Mock, patch

from oxytidy.execution import ProcessResult
from oxytidy.workflow import parse_args, resolve_tools
from oxytools.common import ToolError
from oxytools.llvm import require_version


class VersionTests(unittest.TestCase):
    def test_supported_minor_patch_and_vendor_version_banners(self):
        for version in (
            "clang-format version 23.1.0",
            "Ubuntu clang-format version 23.2.9",
            "LLVM (http://llvm.org/):\n  LLVM version 23.1.1\n  Optimized build.",
            "clang-scan-deps version 23.99.123",
        ):
            with self.subTest(version=version):
                require_version("LLVM tool", version)

    def test_unsupported_and_unrecognized_versions(self):
        for version in (
            "clang-format version 22.1.8",
            "LLVM version 24.1.0",
            "LLVM version 230.1.0",
            "LLVM version unknown",
            "",
        ):
            with self.subTest(version=version):
                with self.assertRaisesRegex(ToolError, r"clang-format 23\.x"):
                    require_version("clang-format", version)


class AnalysisVersionTests(unittest.TestCase):
    def resolve(self, versions):
        args = parse_args(["--all", "--fix", "--format"])
        runner = Mock()

        def run(command, *_):
            if command[1] == "--version":
                output = f"LLVM version {versions[command[0]]}"
            else:
                output = " ".join(
                    (
                        "--export-fixes",
                        "--verify-config",
                        "--dump-config",
                        "--config-file",
                        "--use-color",
                    )
                )
            return ProcessResult(command, 0, output, "", 0.0, "completed")

        runner.run.side_effect = run
        with (
            patch("oxytidy.workflow.executable", side_effect=lambda name, *_: name),
            patch("oxytidy.workflow.file_hash", return_value="test-hash"),
        ):
            return resolve_tools(args, runner, Path("unused"))

    def test_all_tools_accept_different_23_minor_patch_versions(self):
        versions = {
            "clang-tidy": "23.1.0",
            "clang-scan-deps": "23.1.1",
            "clang-format": "23.2.0",
        }
        *_, recorded = self.resolve(versions)
        self.assertEqual(set(recorded), set(versions))

    def test_each_tool_must_be_from_the_required_release_family(self):
        for tool in ("clang-tidy", "clang-scan-deps", "clang-format"):
            for rejected in ("22.1.8", "24.1.0", "unknown"):
                with self.subTest(tool=tool, version=rejected):
                    versions = dict.fromkeys(
                        ("clang-tidy", "clang-scan-deps", "clang-format"), "23.1.0"
                    )
                    versions[tool] = rejected
                    with self.assertRaisesRegex(ToolError, rf"{tool} 23\.x"):
                        self.resolve(versions)

    def test_matching_old_tools_are_not_accepted(self):
        versions = dict.fromkeys(
            ("clang-tidy", "clang-scan-deps", "clang-format"), "22.1.8"
        )
        with self.assertRaisesRegex(ToolError, r"clang-tidy 23\.x"):
            self.resolve(versions)
