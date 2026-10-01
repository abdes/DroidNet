"""Verify command discovery in the shell that invoked repository setup."""
import os
import subprocess
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]

@unittest.skipUnless(os.name == "nt", "Windows shell activation")
class ShellInitialization(unittest.TestCase):
    def test_powershell_commands_survive_script_return_and_prompt(self):
        script = (
            f"& '{ROOT / 'init.ps1'}' -NoRestore -NoToolRestore -NoPythonRestore -NoPreCommitHooks; "
            "$null = prompt; "
            "if (!(Get-Command traverse -ErrorAction Stop)) { exit 1 }; "
            "get-artifacts --help; traverse --help"
        )
        result = subprocess.run(["powershell.exe", "-NoProfile", "-Command", script],
                                capture_output=True, text=True, check=True)
        self.assertIn("Ready in this PowerShell session", result.stdout)
        self.assertIn("usage: traverse", result.stdout)
        self.assertIn("usage: get-artifacts", result.stdout)

    def test_cmd_keeps_commands_available_in_the_calling_cmd(self):
        command = (
            f'call "{ROOT / "init.cmd"}" -NoRestore -NoToolRestore -NoPythonRestore -NoPreCommitHooks'
            ' && traverse --help && get-artifacts --help'
        )
        result = subprocess.run(["cmd.exe", "/d", "/q"], input="@echo off\r\n" + command + "\r\nexit %errorlevel%\r\n",
                                capture_output=True, text=True, check=True)
        self.assertIn("Ready in this CMD session", result.stdout)
        self.assertIn("usage: traverse", result.stdout)

    def test_cmd_from_powershell_does_not_claim_parent_activation(self):
        script = f"& '{ROOT / 'init.cmd'}' -NoRestore -NoToolRestore -NoPythonRestore -NoPreCommitHooks"
        result = subprocess.run(["powershell.exe", "-NoProfile", "-Command", script],
                                capture_output=True, text=True, check=True)
        self.assertIn("To enable commands in PowerShell", result.stdout)
        self.assertNotIn("Ready in this", result.stdout)


if __name__ == "__main__":
    unittest.main()
