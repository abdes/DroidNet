"""Reject EX07 lighting admission when native physical probe evidence is incomplete.

The native tests qualify instruments and record physical residuals. This
separate gate requires the renderer to satisfy the physical budget as well.
"""

from __future__ import annotations

import argparse
import json
import math
from pathlib import Path


REQUIRED_PROBES = {
    "PunctualPhotometryProbeQualifiesFactorsAndReportsBoundaryResiduals": 2160,
    "SpotConePrecisionPreservesRotatedAndNarrowBoundaryContributions": 294,
    "DirectBrdfProbeReportsIndependentOracleResiduals": 108,
}


def validate(document: dict) -> None:
    for field in ("failures", "errors", "disabled"):
        if document.get(field) != 0:
            raise ValueError(f"native results have missing/nonzero {field}")
    failures = []
    for name, count in REQUIRED_PROBES.items():
        cases = [
            case
            for suite in document.get("testsuites", [])
            if suite.get("name") == "LightingGpuAbiTest"
            for case in suite.get("testsuite", [])
            if case.get("name") == name
        ]
        try:
            if len(cases) != 1:
                raise ValueError("exactly one completed physical probe is required")
            validate_case(cases[0], count)
        except (ValueError, TypeError) as error:
            failures.append(f"{name}: {error}")
    if failures:
        raise ValueError("\n".join(failures))


def validate_case(case: dict, count: int) -> None:
    if case.get("status") != "RUN" or case.get("result") != "COMPLETED":
        raise ValueError("physical probe did not run to completion")
    if case.get("failures"):
        raise ValueError("physical probe contains test failures")
    if int(case.get("physical_probe_schema", -1)) != 1:
        raise ValueError("unsupported/missing physical probe schema")
    if int(case.get("probe_count", -1)) != count:
        raise ValueError("incomplete physical probe matrix")
    residuals = int(case.get("physical_budget_failures", -1))
    details = json.loads(case.get("physical_failure_details", "null"))
    fraction = float(case.get("maximum_physical_budget_fraction", "nan"))
    if not isinstance(details, list) or len(details) != residuals:
        raise ValueError("inconsistent/missing physical residual evidence")
    if not math.isfinite(fraction) or fraction < 0:
        raise ValueError("invalid/missing physical error measurement")
    if residuals != 0 or fraction > 1:
        raise ValueError(
            f"physical budget FAILED: {residuals} channel results; "
            f"maximum budget fraction {fraction:.9g}"
        )


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("results", type=Path, help="native GoogleTest JSON result")
    args = parser.parse_args()
    try:
        validate(json.loads(args.results.read_text(encoding="utf-8")))
    except (OSError, ValueError, TypeError, KeyError) as error:
        parser.exit(1, f"{error}\n")
    print(f"Physical probe admission passed: {sum(REQUIRED_PROBES.values())} inputs")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
