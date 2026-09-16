"""Independent arithmetic/format audit for the exposure contract checkpoint.

This report is a design input, not a renderer or GPU acceptance result. It uses
only Python's binary64 arithmetic and IEEE storage conversions, never production
exposure helpers. Run from the engine root with --output pointing at the audit
directory under out/build-ninja/analysis/vortex/exposure-lightbench.
"""

import argparse
import hashlib
import json
import math
from pathlib import Path
import struct
import subprocess


def storage(value: float, kind: str) -> float:
    try:
        return struct.unpack("<" + kind, struct.pack("<" + kind, value))[0]
    except OverflowError:
        return math.copysign(math.inf, value)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    engine = Path(__file__).resolve().parents[2]
    checks = []

    def check(name: str, passed: bool, **evidence: object) -> None:
        checks.append({"name": name, "passed": passed, **evidence})

    for ev, expected in ((14, 0.25), (15, 0.125), (16, 0.0625)):
        gain = math.ldexp(1.0, -ev)
        actual = storage(storage(4096.0, "f") * storage(gain, "f"), "f")
        check(f"fixed_ev{ev}", actual == expected, gain=gain,
              expected_linear_pixel=expected, binary32_linear_pixel=actual)
    camera_ev = math.log2(11 * 11 * 125)
    check("camera_ev", abs(camera_ev - 13.884647521936682) < 1e-12,
          ev100=camera_ev)
    mass = 512 * 512 * 4095
    check("histogram_uint32_headroom", mass < 2**32,
          maximum_mass=mass, maximum_uint32=2**32 - 1)

    f32_normal = math.ldexp(1.0, -126)
    f32_subnormal = math.ldexp(1.0, -149)
    check("binary32_storage_boundaries",
          storage(f32_normal, "f") == f32_normal
          and storage(f32_subnormal, "f") == f32_subnormal
          and storage(f32_subnormal / 2, "f") == 0,
          minimum_normal=f32_normal, minimum_subnormal=f32_subnormal,
          note="Storage only; shader arithmetic may flush subnormals.")
    half_min = math.ldexp(1.0, -24)
    half_normal = math.ldexp(1.0, -14)
    half_max = 65504.0
    low, high = math.ldexp(1.0, -24), math.ldexp(1.0, 32)
    solar_solid_angle = 2 * math.pi * (1 - math.cos(math.radians(0.545 / 2)))
    solar_disk_luminance = 133312 / solar_solid_angle
    check("earth_reference_solar_disk_fits_domain",
          solar_disk_luminance < high,
          solar_disk_luminance=solar_disk_luminance,
          note="133312 lux and 0.545-degree disk from the lighting reference.")
    p_lower, p_upper = half_min / low, half_max / high
    check("mixed_endpoints_have_no_lossless_fp16_scale", p_lower > p_upper,
          scene_min=low, scene_max=high,
          required_p_min=p_lower, required_p_max=p_upper,
          fp16_minimum_normal=half_normal,
          fp16_available_stops=math.log2(half_max / half_min),
          scene_stops=math.log2(high / low))
    endpoint_trials = []
    for exponent in (-32, -16, 0, 16, 32):
        p = math.ldexp(1.0, exponent)
        stored_low = storage(low * p, "e")
        stored_high = storage(high * p, "e")
        endpoint_trials.append({"p_log2": exponent,
                                "dark_erased": stored_low == 0,
                                "bright_overflow": math.isinf(stored_high)})
    check("sampled_fp16_scales_lose_an_endpoint",
          all(t["dark_erased"] or t["bright_overflow"]
              for t in endpoint_trials), trials=endpoint_trials)
    check("fp32_bootstrap_preserves_endpoints",
          storage(low, "f") == low and storage(high, "f") == high)

    sources = [
        "src/Oxygen/Graphics/Direct3D12/Tools/ShaderBake/CompileProfile.h",
        "src/Oxygen/Graphics/Direct3D12/Tools/ShaderBake/DxcShaderCompiler.cpp",
        "src/Oxygen/Vortex/SceneRenderer/SceneTextures.cpp",
        "src/Oxygen/Data/PakFormat_world.h",
    ]
    report = {
        "report_kind": "exposure_contract_arithmetic_audit",
        "schema_version": 1,
        "renderer_validation": False,
        "source_revision": subprocess.check_output(
            ["git", "rev-parse", "HEAD"], cwd=engine, text=True).strip(),
        "source_sha256": {p: hashlib.sha256((engine / p).read_bytes()).hexdigest()
                          for p in sources},
        "checks": checks,
        "passed": all(c["passed"] for c in checks),
    }
    args.output.mkdir(parents=True, exist_ok=True)
    output = args.output / "arithmetic-audit.json"
    output.write_text(json.dumps(report, indent=2, allow_nan=False) + "\n",
                      encoding="utf-8", newline="\n")
    print(f"{sum(c['passed'] for c in checks)}/{len(checks)} checks passed: {output}")
    raise SystemExit(0 if report["passed"] else 1)


if __name__ == "__main__":
    main()
