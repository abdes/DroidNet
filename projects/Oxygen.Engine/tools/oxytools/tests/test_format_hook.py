"""Exercise the real checking hook without changing the user's Git index."""

from __future__ import annotations

import importlib.util
import json
import os
import shutil
import subprocess
import sys
import tempfile
import time
import unittest
from pathlib import Path

import yaml

PROJECT = Path(__file__).resolve().parents[1]
ENGINE = PROJECT.parent.parent
REPOSITORY = ENGINE.parent.parent
PRE_COMMIT_PYTHON = os.environ.get("OXYTOOLS_TEST_PRE_COMMIT_PYTHON") or (
    sys.executable if importlib.util.find_spec("pre_commit") else None
)


def hook_from(path: Path) -> dict:
    config = yaml.safe_load(path.read_text())
    return next(
        hook
        for repo in config["repos"]
        if repo["repo"] == "local"
        for hook in repo["hooks"]
        if hook["id"] == "oxyformat"
    )


class HookConfigurationTests(unittest.TestCase):
    def test_both_profiles_use_identical_check_only_hook(self):
        hook = hook_from(REPOSITORY / ".pre-commit-config.yaml")
        self.assertEqual(hook, hook_from(ENGINE / ".pre-commit-config.yaml"))
        self.assertNotIn("--fix", hook["entry"])
        self.assertNotIn("--all", hook["entry"])
        self.assertNotIn("uv", hook["entry"])
        self.assertTrue(hook["require_serial"])
        self.assertEqual(hook["stages"], ["pre-commit"])


@unittest.skipUnless(
    PRE_COMMIT_PYTHON and shutil.which("git"), "pre-commit and Git are required"
)
class HookIntegrationTests(unittest.TestCase):
    def test_staged_contents_checked_and_unstaged_contents_restored(self):
        with tempfile.TemporaryDirectory(prefix="oxyformat hook ") as temporary:
            root = Path(temporary)
            engine = root / "projects/Oxygen.Engine"
            tool = engine / "tools/oxytools"
            tool.mkdir(parents=True)
            shutil.copytree(
                PROJECT / "src",
                tool / "src",
                ignore=shutil.ignore_patterns("__pycache__", "*.egg-info"),
            )
            shutil.copy2(PROJECT / "run_oxyformat.py", tool)
            (engine / ".oxytools.json").write_text(
                json.dumps(
                    {"project_roots": ["src/Oxygen"], "exclude": ["**/vendor/**"]}
                )
            )
            (engine / ".clang-format").write_text("BasedOnStyle: LLVM\n")
            (root / ".pre-commit-config.yaml").write_text(
                yaml.safe_dump(
                    {
                        "repos": [
                            {
                                "repo": "local",
                                "hooks": [
                                    hook_from(REPOSITORY / ".pre-commit-config.yaml")
                                ],
                            }
                        ]
                    }
                )
            )
            source = engine / "src/Oxygen/sample.cpp"
            source.parent.mkdir(parents=True)
            source.write_bytes(b"int value;\n")
            relative = source.relative_to(root).as_posix()

            def git(*arguments):
                return subprocess.run(
                    ["git", *arguments], cwd=root, capture_output=True, check=True
                ).stdout

            git("init", "--quiet")
            git("config", "core.autocrlf", "false")
            git("config", "core.safecrlf", "false")
            git("add", ".")
            git(
                "-c",
                "user.name=Oxyformat Test",
                "-c",
                "user.email=oxyformat@example.invalid",
                "-c",
                "core.hooksPath=disabled-hooks",
                "commit",
                "--quiet",
                "-m",
                "test fixture",
            )

            timings = []

            def check(expected):
                index = git("diff", "--cached", "--binary")
                contents = source.read_bytes()
                started = time.perf_counter()
                result = subprocess.run(
                    [PRE_COMMIT_PYTHON, "-m", "pre_commit", "run", "oxyformat"],
                    cwd=root,
                    capture_output=True,
                    text=True,
                    check=False,
                    timeout=120,
                )
                timings.append(round((time.perf_counter() - started) * 1000, 2))
                self.assertEqual(
                    result.returncode, expected, result.stdout + result.stderr
                )
                self.assertEqual(source.read_bytes(), contents)
                self.assertEqual(git("diff", "--cached", "--binary"), index)
                return result.stdout + result.stderr

            # Dirty staged bytes must fail even if the working copy is compliant.
            source.write_bytes(b"int  changed;\n")
            git("add", relative)
            source.write_bytes(b"int changed;\n")
            self.assertIn("Needs formatting", check(1))

            # Dirty unstaged bytes must not make a compliant staged file fail.
            git("add", relative)
            source.write_bytes(b"int  changed;\n")
            self.assertIn("Passed", check(0))

            # Other untracked C++ files must not turn this into a project scan.
            (source.parent / "untracked.cpp").write_bytes(b"int  unrelated;\n")
            self.assertIn("Passed", check(0))

            # A staged excluded source does not get checked or rewritten.
            vendor = source.parent / "vendor/third_party.cpp"
            vendor.parent.mkdir()
            vendor.write_bytes(b"int  vendor;\n")
            git("add", vendor.relative_to(root).as_posix())
            self.assertIn("Passed", check(0))
            self.assertEqual(vendor.read_bytes(), b"int  vendor;\n")
            print(f"Hook timings ms (first includes environment setup): {timings}")


if __name__ == "__main__":
    unittest.main()
