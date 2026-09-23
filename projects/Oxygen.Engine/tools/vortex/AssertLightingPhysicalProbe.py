"""Validate completeness of model-2 lighting measurements.

Approximation errors are reported to the user, not rejected against historical
model-1 budgets. Native correctness failures and malformed evidence still fail.
"""

import argparse
import json
import math
from pathlib import Path

REQUIRED_PROBES = {
    "GrazingResponseRemainsFiniteWithoutClippingTheGgxPeak": 18,
    "PunctualPhotometryProbeQualifiesFactorsAndReportsBoundaryResiduals": 2160,
    "Fp32ConeProfileReportsRotatedBoundaryError": 294,
    "DirectBrdfProbeReportsIndependentOracleResiduals": 108,
}

REQUIRED_QUALIFICATIONS = {
    "EnergyTextureMatchesFilteringAndReportsReferenceError": (
        "energy_queries", 54, ["maximum_energy_error", "maximum_bias_error"]),
    "AnalyticEmitterReportsIndependentGeometryDeviation": (
        "finite_emitter_cases", 96, ["maximum_finite_emitter_reference_deviation_scale"]),
    "ViewDependentCompensationReportsReciprocityDeviation": (
        "reciprocity_queries", 1800, ["maximum_reciprocity_deviation_scale"]),
    "IntegratedGpuLobesReportEnergyAndIndirectError": (
        "integrated_material_cases", 90,
        ["maximum_furnace_error", "maximum_indirect_error", "maximum_refinement_change"]),
}


def validate_case(case, count):
    if case.get("status") != "RUN" or case.get("result") != "COMPLETED" or case.get("failures"):
        raise ValueError("measurement did not complete successfully")
    if int(case.get("lighting_model_revision", -1)) != 2:
        raise ValueError("measurement is not for production model 2")
    if int(case.get("lighting_measurement_schema", -1)) != 2:
        raise ValueError("missing or unsupported measurement schema")
    if int(case.get("probe_count", -1)) != count:
        raise ValueError("incomplete probe matrix")
    residuals = int(case.get("reference_budget_exceedances", -1))
    details = json.loads(case.get("reference_deviation_details", "null"))
    fraction = float(case.get("maximum_reference_deviation_scale", "nan"))
    if not isinstance(details, list) or len(details) != residuals:
        raise ValueError("inconsistent reference deviation evidence")
    if not math.isfinite(fraction) or fraction < 0:
        raise ValueError("invalid reference deviation measurement")


def validate(document):
    if any(document.get(field) != 0 for field in ("failures", "errors", "disabled")):
        raise ValueError("native results contain missing/nonzero failures, errors or disabled tests")
    cases = [case for suite in document.get("testsuites", [])
             if suite.get("name") == "LightingGpuAbiTest"
             for case in suite.get("testsuite", [])]
    def find(name):
        matches = [case for case in cases if case.get("name") == name]
        if len(matches) != 1:
            raise ValueError(f"{name}: exactly one completed measurement is required")
        return matches[0]
    for name, count in REQUIRED_PROBES.items():
        validate_case(find(name), count)
    for name, (count_field, count, metrics) in REQUIRED_QUALIFICATIONS.items():
        case = find(name)
        if (case.get("status") != "RUN" or case.get("result") != "COMPLETED"
                or case.get("failures") or int(case.get("lighting_model_revision", -1)) != 2):
            raise ValueError(f"{name}: measurement did not complete for model 2")
        if int(case.get(count_field, -1)) != count:
            raise ValueError(f"{name}: incomplete matrix")
        for metric in metrics:
            value = float(case.get(metric, "nan"))
            if not math.isfinite(value) or value < 0:
                raise ValueError(f"{name}: missing or invalid {metric}")
    emitter = find("AnalyticEmitterReportsIndependentGeometryDeviation")
    measurements = json.loads(emitter.get("finite_emitter_measurements", "null"))
    if not isinstance(measurements, list) or len(measurements) != 288:
        raise ValueError("finite-emitter per-case comparison is incomplete")
    for row in measurements:
        if any(not math.isfinite(float(row.get(key, "nan"))) or float(row[key]) < 0
               for key in ("reference", "measured")):
            raise ValueError("invalid finite-emitter sample")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("results", type=Path)
    args = parser.parse_args()
    try:
        validate(json.loads(args.results.read_text(encoding="utf-8")))
    except (OSError, ValueError, TypeError, KeyError) as error:
        parser.exit(1, f"{error}\n")
    print("Model-2 lighting measurements complete; quality/performance acceptance belongs to the user.")


if __name__ == "__main__":
    main()
