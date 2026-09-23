"""Check a candidate moment table against B's independent point certificates.

This gate checks serialized data and certified anchors. It deliberately does not
claim a full-domain interpolation certificate or physical renderer admission.
"""

from __future__ import annotations

import argparse
import hashlib
import heapq
import json
import math
from pathlib import Path
import struct


ROOT = Path(__file__).resolve().parents[2]
REFERENCE = ROOT / "src/Oxygen/Vortex/Test/Lighting/Reference"
MOMENT_BUDGET = 2e-4
REFERENCE_BUDGET = 1e-5


def mix(first, second, fraction):
    return tuple(a + (b-a)*fraction for a, b in zip(first, second))


def roughness_location(table, roughness):
    position = ((max(roughness, 0.045) - 0.045) / 0.955
                * (table["roughness_nodes"] - 1))
    lower = min(int(position), table["roughness_nodes"] - 2)
    return lower, position-lower


def sample_directional(table, roughness, cosine):
    width = table["view_nodes"]
    position = math.sqrt(cosine) * (width-1)
    column = min(int(position), width-2)
    row, fraction_r = roughness_location(table, roughness)
    values = table["loss_and_bias"]
    first = mix(values[row*width+column], values[row*width+column+1], position-column)
    second = mix(values[(row+1)*width+column], values[(row+1)*width+column+1], position-column)
    return mix(first, second, fraction_r)


def sample_mean(table, roughness):
    row, fraction = roughness_location(table, roughness)
    values = table["mean_loss_and_bias"]
    return mix(values[row], values[row+1], fraction)


def validate_structure(table):
    if table["schema"] != 1 or table["model_revision"] != 1:
        raise ValueError("Unsupported table schema or BRDF model")
    if table["roughness_minimum"] != 0.045:
        raise ValueError("Incorrect roughness floor")
    if table["qualification_status"] != "candidate: interpolation and physical qualification required":
        raise ValueError("Expected an explicitly unqualified candidate")
    if table["view_mapping"] != "sqrt(mu), endpoint nodes" or table["roughness_mapping"] != "linear effective roughness, endpoint nodes":
        raise ValueError("Incorrect table coordinates")
    if table["mean_rule"] != "exact integral of the piecewise-linear float32 directional table":
        raise ValueError("Incorrect mean-moment construction")
    if table["payload_encoding"] != "little-endian binary32 pairs: all moment rows, then means":
        raise ValueError("Incorrect payload encoding")
    if table["reference_refinement_tolerance"] != 1e-8:
        raise ValueError("Unexpected reference refinement tolerance")
    for field in ("maximum_reference_refinement_change", "maximum_domain_projection"):
        if not math.isfinite(table[field]) or not 0 <= table[field] <= 1e-8:
            raise ValueError("Unconverged or invalid reference data")
    if type(table["reference_evaluations"]) is not int or table["reference_evaluations"] <= 0:
        raise ValueError("Missing reference evaluation record")
    width, height = table["view_nodes"], table["roughness_nodes"]
    if any(type(value) is not int or not 2 <= value <= 4097 for value in (width, height)):
        raise ValueError("Invalid table dimensions")
    moments, means = table["loss_and_bias"], table["mean_loss_and_bias"]
    if len(moments) != width*height or len(means) != height:
        raise ValueError("Incomplete moment payload")
    payload = bytearray()
    for pair in moments + means:
        if len(pair) != 2 or any(type(value) not in (int, float) or not math.isfinite(value) for value in pair):
            raise ValueError("Invalid moment pair")
        loss, bias = pair
        if not (0 <= loss <= 1 and 0 <= bias <= 1-loss):
            raise ValueError("Moment outside 0 <= B <= E <= 1")
        encoded = struct.pack("<2f", loss, bias)
        if tuple(pair) != struct.unpack("<2f", encoded):
            raise ValueError("Moment is not the stored binary32 value")
        payload.extend(encoded)
    if any(moments[row*width][0] != 0 for row in range(height)):
        raise ValueError("Grazing E(0)=1 endpoint is not exact")
    if hashlib.sha256(payload).hexdigest() != table["payload_sha256"]:
        raise ValueError("Moment payload hash mismatch")
    if len(table["generator_sha256"]) != 64 or any(value not in "0123456789abcdef" for value in table["generator_sha256"]):
        raise ValueError("Missing generator identity")
    return len(payload)


def compare(table, certificates, mean=False):
    if certificates["model_revision"] != 1:
        raise ValueError("Independent certificate has a different model")
    results = []
    suffix = "_avg" if mean else ""
    for case in certificates["cases"]:
        if mean:
            loss, bias = sample_mean(table, case["roughness"])
        else:
            loss, bias = sample_directional(table, case["roughness"], case["view_cosine"])
        distances = []
        for name, measured in (("E", 1-loss), ("B", bias)):
            radius = case[f"{name}{suffix}_radius"]
            midpoint = case[f"{name}{suffix}_midpoint"]
            if not math.isfinite(radius) or not 0 <= radius <= REFERENCE_BUDGET or not math.isfinite(midpoint):
                raise ValueError("Unqualified independent moment certificate")
            distances.append(math.nextafter(abs(measured-midpoint)+radius, math.inf))
        results.append({"roughness": case["roughness"],
                        "view_cosine": case.get("view_cosine"),
                        "energy_distance_bound": distances[0],
                        "bias_distance_bound": distances[1]})
    return results


def compare_midpoints(table, reference_path):
    reference = json.loads(reference_path.read_text())
    validate_structure(reference)
    width, height = table["view_nodes"], table["roughness_nodes"]
    fine_width, fine_height = reference["view_nodes"], reference["roughness_nodes"]
    if (fine_width, fine_height) != (2*width-1, 2*height-1):
        raise ValueError("Dense reference must bisect every table interval")
    worst = []
    count = failures = 0
    maximum = 0.0
    for row in range(fine_height):
        roughness = 0.045 + 0.955*row/(fine_height-1)
        for column in range(fine_width):
            expected = reference["loss_and_bias"][row*fine_width+column]
            if row % 2 == 0 and column % 2 == 0:
                if expected != table["loss_and_bias"][(row//2)*width+column//2]:
                    raise ValueError("Nested reference disagrees at an original table node")
                continue
            cosine = (column/(fine_width-1))**2
            actual = sample_directional(table, roughness, cosine)
            error_e, error_b = (abs(a-b) for a, b in zip(actual, expected))
            error = max(error_e, error_b)
            maximum = max(maximum, error)
            count += 1
            # Reserve the frozen reference allowance plus binary32 export
            # rounding. This diagnostic is not an independent enclosure of
            # every newly queried CPU reference value.
            if error + REFERENCE_BUDGET + 2**-25 > MOMENT_BUDGET:
                failures += 1
            item = (error, row, column, error_e, error_b)
            if len(worst) < 32:
                heapq.heappush(worst, item)
            elif item > worst[0]:
                heapq.heapreplace(worst, item)
    return {"reference_sha256": hashlib.sha256(reference_path.read_bytes()).hexdigest(),
            "queries": count, "failed_queries": failures,
            "maximum_observed_difference": maximum,
            "reserved_reference_allowance": REFERENCE_BUDGET,
            "binary32_export_allowance": 2**-25,
            "gate": "pass" if failures == 0 else "fail",
            "scope": "All cell centers and edge midpoints; not a continuous-domain bound",
            "worst_queries": [
                {"roughness": 0.045+0.955*row/(fine_height-1),
                 "view_cosine": (column/(fine_width-1))**2,
                 "energy_difference": error_e, "bias_difference": error_b}
                for _, row, column, error_e, error_b in sorted(worst, reverse=True)]}


def inspect(path, refined_reference=None):
    table = json.loads(path.read_text())
    size = validate_structure(table)
    directional_path = REFERENCE / "GgxMomentCertificates.json"
    mean_path = REFERENCE / "GgxMeanMomentCertificates.json"
    directional = compare(table, json.loads(directional_path.read_text()))
    means = compare(table, json.loads(mean_path.read_text()), mean=True)
    if len(directional) != 55 or len(means) != 6:
        raise ValueError("Incomplete independent anchor matrices")
    maximum = max(max(case["energy_distance_bound"], case["bias_distance_bound"])
                  for case in directional + means)
    report = {"inspector_sha256": hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
            "candidate_sha256": hashlib.sha256(path.read_bytes()).hexdigest(),
            "payload_sha256": table["payload_sha256"], "payload_bytes": size,
            "directional_certificate_sha256": hashlib.sha256(directional_path.read_bytes()).hexdigest(),
            "mean_certificate_sha256": hashlib.sha256(mean_path.read_bytes()).hexdigest(),
            "anchor_budget": MOMENT_BUDGET, "maximum_anchor_distance_bound": maximum,
            "anchor_gate": "pass" if maximum <= MOMENT_BUDGET else "fail",
            "qualification_status": "candidate",
            "scope": "Certified anchor queries only; full-domain interpolation and native physical qualification remain required",
            "directional_queries": directional, "mean_queries": means}
    if refined_reference is not None:
        report["midpoint_stencil"] = compare_midpoints(table, refined_reference)
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("candidate", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--refined-reference", type=Path)
    args = parser.parse_args()
    try:
        report = inspect(args.candidate, args.refined_reference)
    except (OSError, ValueError, KeyError, TypeError, struct.error) as error:
        parser.exit(1, f"{error}\n")
    args.output.write_text(json.dumps(report, indent=2) + "\n")
    print(f"Anchor gate {report['anchor_gate']}: maximum distance {report['maximum_anchor_distance_bound']:.9g}")
    print(report["scope"])
    stencil = report.get("midpoint_stencil")
    if stencil is not None:
        print(f"Midpoint stencil {stencil['gate']}: {stencil['queries']} queries, maximum difference {stencil['maximum_observed_difference']:.9g}")
    return 0 if report["anchor_gate"] == "pass" and (stencil is None or stencil["gate"] == "pass") else 1


if __name__ == "__main__":
    raise SystemExit(main())
