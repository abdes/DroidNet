import shutil
import subprocess
from pathlib import Path

import pytest


REPO_ROOT = Path(__file__).resolve().parents[3]
POWERSHELL = shutil.which("powershell")


def run_powershell(command: str) -> subprocess.CompletedProcess[str]:
    if POWERSHELL is None:
        pytest.skip("powershell is not available")
    return subprocess.run(
        [POWERSHELL, "-NoProfile", "-Command", command],
        cwd=REPO_ROOT,
        text=True,
        capture_output=True,
        check=False,
    )


def source_helper() -> str:
    helper = REPO_ROOT / "tools" / "vortex" / "VortexProofCommon.ps1"
    return f". '{helper}'"


def test_report_status_accepts_explicit_success_and_pass(tmp_path: Path):
    report = tmp_path / "good.report.txt"
    command = (
        f"{source_helper()}; "
        f"@('analysis_result=success', 'overall_verdict=pass') | "
        f"Set-Content -LiteralPath '{report}' -Encoding ascii; "
        f"Assert-VortexProofReportStatus -ReportPath '{report}'"
    )

    result = run_powershell(command)

    assert result.returncode == 0, result.stderr


def test_report_status_rejects_failed_verdict(tmp_path: Path):
    report = tmp_path / "bad.report.txt"
    command = (
        f"{source_helper()}; "
        f"@('analysis_result=success', 'overall_verdict=fail') | "
        f"Set-Content -LiteralPath '{report}' -Encoding ascii; "
        f"Assert-VortexProofReportStatus -ReportPath '{report}'"
    )

    result = run_powershell(command)

    assert result.returncode != 0
    assert "Report verdict mismatch" in (result.stderr + result.stdout)


def test_proof_step_propagates_native_exit_code():
    command = (
        f"{source_helper()}; "
        "Invoke-VortexProofStep "
        "-FilePath 'powershell' "
        "-ArgumentList @('-NoProfile', '-Command', 'exit 7') "
        "-Label 'synthetic step'"
    )

    result = run_powershell(command)

    assert result.returncode != 0
    assert "synthetic step failed with exit code 7" in (
        result.stderr + result.stdout
    )
