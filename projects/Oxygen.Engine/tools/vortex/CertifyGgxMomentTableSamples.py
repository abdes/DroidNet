"""Independently enclose the worst sampled moment-table interpolation errors.

Reuse B's incoming-direction Arb oracle; do not regenerate its existing anchor
matrices. This certifies the selected queries, not every point between them.
"""

from __future__ import annotations

import argparse
from concurrent.futures import ProcessPoolExecutor, as_completed
import hashlib
import json
import math
from pathlib import Path

import flint

import GenerateGgxMomentCertificates as oracle
import InspectGgxMomentTable as inspector
from InspectGgxMomentTable import MOMENT_BUDGET, sample_directional, validate_structure


def certify_query(index, query, sampled):
    if flint.__version__ != "0.9.0" or flint.__FLINT_VERSION__ != "3.6.0":
        raise RuntimeError("Use python-flint==0.9.0 with FLINT 3.6.0")
    oracle.ctx.prec = oracle.PRECISION_BITS
    energy, bias = oracle.certify(query["roughness"], query["view_cosine"])
    e_mid, e_radius = oracle.export_ball(energy)
    b_mid, b_radius = oracle.export_ball(bias)
    return {"selection_index": index, "roughness": query["roughness"],
            "view_cosine": query["view_cosine"],
            "E_midpoint": e_mid, "E_radius": e_radius,
            "B_midpoint": b_mid, "B_radius": b_radius,
            "energy_distance_bound": math.nextafter(abs(1-sampled[0]-e_mid)+e_radius, math.inf),
            "bias_distance_bound": math.nextafter(abs(sampled[1]-b_mid)+b_radius, math.inf)}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("candidate", type=Path)
    parser.add_argument("stencil_report", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--count", type=int, default=8)
    parser.add_argument("--workers", type=int, default=2)
    args = parser.parse_args()
    if not 1 <= args.count <= 32 or not 1 <= args.workers <= 8:
        parser.error("count must be 1..32 and workers 1..8")
    candidate_bytes = args.candidate.read_bytes()
    table = json.loads(candidate_bytes)
    validate_structure(table)
    stencil = json.loads(args.stencil_report.read_text())
    if stencil["candidate_sha256"] != hashlib.sha256(candidate_bytes).hexdigest():
        parser.error("Stencil report belongs to another candidate")
    queries = stencil["midpoint_stencil"]["worst_queries"][:args.count]
    if len(queries) != args.count:
        parser.error("Insufficient ranked stencil queries")
    cases = []
    with ProcessPoolExecutor(max_workers=args.workers) as pool:
        pending = [pool.submit(certify_query, index, query,
                               sample_directional(table, query["roughness"], query["view_cosine"]))
                   for index, query in enumerate(queries)]
        for future in as_completed(pending):
            case = future.result()
            cases.append(case)
            print(f"certified query {case['selection_index']}: r={case['roughness']} mu={case['view_cosine']}", flush=True)
    cases.sort(key=lambda case: case["selection_index"])
    maximum = max(max(case["energy_distance_bound"], case["bias_distance_bound"])
                  for case in cases)
    report = {"model_revision": 1,
              "candidate_sha256": hashlib.sha256(candidate_bytes).hexdigest(),
              "payload_sha256": table["payload_sha256"],
              "stencil_report_sha256": hashlib.sha256(args.stencil_report.read_bytes()).hexdigest(),
              "generator_sha256": hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
              "oracle_sha256": hashlib.sha256(Path(oracle.__file__).read_bytes()).hexdigest(),
              "sampler_sha256": hashlib.sha256(Path(inspector.__file__).read_bytes()).hexdigest(),
              "python_flint_version": flint.__version__, "flint_version": flint.__FLINT_VERSION__,
              "working_precision_bits": oracle.PRECISION_BITS,
              "reference_radius_limit": oracle.RADIUS_LIMIT,
              "moment_budget": MOMENT_BUDGET,
              "maximum_distance_bound": maximum,
              "gate": "pass" if maximum <= MOMENT_BUDGET else "fail",
              "scope": "Independent pointwise enclosures at the ranked worst stencil queries; not a continuous-domain certificate",
              "cases": cases}
    args.output.write_text(json.dumps(report, indent=2) + "\n")
    print(f"Independent sample gate {report['gate']}: maximum distance {maximum:.9g}")
    return 0 if report["gate"] == "pass" else 1


if __name__ == "__main__":
    raise SystemExit(main())
