"""Validate and summarize the opt-in native HDR allocation inventory."""

import argparse
import hashlib
import json
import re
import struct
from pathlib import Path


PHASES = (
    "bootstrap_one", "steady_one", "mixed_two", "steady_two_retained",
    "recovery_two_retained", "released_before_fence", "retired_cached",
)


def category(name):
    if name == "SceneColor":
        return "accumulation"
    if name == "ResolvedSceneColor":
        return "resolved_color"
    if "SkyView" in name:
        return "sky_view"
    if "Aerial" in name:
        return "aerial_perspective"
    if "IntegratedLightScattering" in name:
        return "fog"
    if "AtmosphereTransmittance" in name or "AtmosphereMultiScattering" in name:
        return "canonical_luts"
    return "other_texture"


def decode_certificate(words):
    data = struct.pack("<96I", *map(int, words.split(",")))
    return {
        "flags": struct.unpack_from("<I", data, 48)[0],
        "first_product": struct.unpack_from("<I", data, 52)[0],
        "failure_kind": struct.unpack_from("<I", data, 56)[0],
        "candidate_rgb_relative": struct.unpack_from("<f", data, 272)[0],
        "candidate_rgb_absolute": struct.unpack_from("<f", data, 276)[0],
        "fog_transmittance_gradient": struct.unpack_from("<3f", data, 240),
        "fog_candidate_error": struct.unpack_from("<4f", data, 336),
    }


def summarize(path):
    report = json.loads(path.read_text(encoding="utf-8"))
    if report["failures"] or report.get("errors", 0):
        raise ValueError(f"Failed native accounting report: {path}")
    rows = []
    for suite in report["testsuites"]:
        for test in suite["testsuite"]:
            if test["status"] != "RUN" or test["result"] != "COMPLETED":
                continue
            if "main_width" not in test:
                continue
            width, height = int(test["main_width"]), int(test["main_height"])
            temporal = bool(int(test["temporal_fog"]))
            phases = []
            canonical_ids = set()
            for phase in PHASES:
                resources = []
                for key, value in test.items():
                    match = re.fullmatch(re.escape(phase) + r"_texture_(\d+)", key)
                    if not match:
                        continue
                    name, *fields = value.split(";")
                    resource = {k: int(v) for k, v in (f.split("=") for f in fields)}
                    resource.update(id=int(match[1]), name=name, category=category(name))
                    if resource["hdr_raw_bytes"]:
                        assert resource["format"] in (39, 42), resource
                        bpp = 8 if resource["format"] == 39 else 16
                        raw = sum(max(resource["width"] >> mip, 1)
                                  * max(resource["height"] >> mip, 1)
                                  * max(resource["depth"] >> mip, 1)
                                  * resource["layers"] * resource["samples"] * bpp
                                  for mip in range(resource["mips"]))
                        assert raw == resource["hdr_raw_bytes"], resource
                        assert raw <= resource["placement_bytes"], resource
                        actual = resource["same_desc_fp16_bytes" if bpp == 8
                                          else "same_desc_fp32_bytes"]
                        assert actual == resource["placement_bytes"], resource
                    if resource["category"] in ("accumulation", "canonical_luts"):
                        assert resource["format"] == 42, resource
                    if resource["category"] == "canonical_luts":
                        canonical_ids.add(resource["id"])
                    resources.append(resource)
                assert resources, (path, phase)
                groups = {}
                for resource in resources:
                    group = groups.setdefault(resource["category"],
                                              {"count": 0, "placed_bytes": 0, "raw_bytes": 0})
                    group["count"] += 1
                    group["placed_bytes"] += resource["placement_bytes"]
                    group["raw_bytes"] += resource["hdr_raw_bytes"]
                totals = {key: int(test[f"{phase}_{key}"]) for key in (
                    "placed_bytes", "hdr_placed_bytes", "hdr_raw_bytes",
                    "pool_families", "leased_families", "retained_extracts")}
                assert sum(r["placement_bytes"] for r in resources) == totals["placed_bytes"]
                hdr = [r for r in resources if r["hdr_raw_bytes"]]
                assert sum(r["placement_bytes"] for r in hdr) == totals["hdr_placed_bytes"]
                assert sum(r["hdr_raw_bytes"] for r in hdr) == totals["hdr_raw_bytes"]
                assert any(r["category"] == "accumulation" and r["width"] == width
                           and r["height"] == height for r in resources)
                certificates = {key.removeprefix(phase + "_status_words_"):
                                decode_certificate(value) for key, value in test.items()
                                if key.startswith(phase + "_status_words_")}
                phases.append({"name": phase, **totals, "groups": groups,
                               "certificates": certificates, "resources": resources})
            assert len(canonical_ids) == 2, canonical_ids
            assert phases[3]["retained_extracts"] == phases[4]["retained_extracts"] == 2
            assert phases[-1]["leased_families"] == 0
            peak = int(test["peak_hdr_bytes"])
            assert peak >= max(p["hdr_placed_bytes"] for p in phases)
            assert phases[-1]["hdr_placed_bytes"] < peak
            for certificate in phases[3]["certificates"].values():
                assert certificate["flags"] & 7 == (3 if temporal else 5), certificate
            rows.append({"source": str(path), "sha256": hashlib.sha256(path.read_bytes()).hexdigest(),
                         "width": width, "height": height, "temporal_fog": temporal,
                         "adapter_luid": [int(test["adapter_luid_low"]), int(test["adapter_luid_high"])],
                         "peak_hdr_bytes": peak, "peak_hdr_iteration": int(test["peak_hdr_iteration"]),
                         "peak_traced_texture_bytes": int(test["peak_traced_texture_bytes"]),
                         "phases": phases})
    if not rows:
        raise ValueError(f"No executed accounting test in {path}")
    return rows


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("reports", nargs="+", type=Path)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    cases = [case for path in args.reports for case in summarize(path)]
    result = {"status": "pass", "cases": cases, "scope": (
        "Committed D3D12 texture footprints observed after fixture-output creation. "
        "Weak tracking and native resource deduplication preserve lifetimes. Peaks are sampled "
        "at every traced texture creation; snapshots distinguish retained and cached families. "
        "Same-descriptor format footprints are device queries, not extra allocations. "
        "Buffers, descriptor heaps, caller output targets and unrelated backend baseline are excluded.")}
    args.output.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    lines = ["# Native HDR allocation measurements", "", result["scope"], "",
             "| Resolution | Temporal fog | Checkpoint | HDR MiB | Raw HDR MiB | Families | Leased |",
             "| --- | --- | --- | ---: | ---: | ---: | ---: |"]
    for case in cases:
        for phase in case["phases"]:
            lines.append(f"| {case['width']}x{case['height']} | {case['temporal_fog']} | "
                         f"{phase['name']} | {phase['hdr_placed_bytes']/2**20:.3f} | "
                         f"{phase['hdr_raw_bytes']/2**20:.3f} | {phase['pool_families']} | "
                         f"{phase['leased_families']} |")
    lines.append("")
    for case in cases:
        lines.append(f"- {case['width']}x{case['height']}, temporal fog {case['temporal_fog']}: "
                     f"creation-time HDR peak {case['peak_hdr_bytes']/2**20:.3f} MiB "
                     f"at iteration {case['peak_hdr_iteration']}.")
    args.output.with_suffix(".md").write_text("\n".join(lines) + "\n", encoding="utf-8")
    print(f"Validated {len(cases)} allocation cases: {args.output}")


if __name__ == "__main__":
    main()
