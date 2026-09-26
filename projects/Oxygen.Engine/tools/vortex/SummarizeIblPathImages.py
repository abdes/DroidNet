"""Check retained IBL image arrays and produce a compact path-comparison preview."""

import hashlib
import json
from pathlib import Path
import sys
import zipfile

import numpy as np
from PIL import Image, ImageDraw


def summarize(root):
    root = Path(root)
    manifest = json.loads((root / "images.json").read_text())
    width, height = manifest["width"], manifest["height"]
    if (width, height) != (128, 96) or len(manifest["cases"]) != 144:
        raise RuntimeError("Incomplete image matrix")
    rows = {row["name"]: row for row in manifest["cases"]}
    if len(rows) != 144 or manifest["display_limits"] != {"peak_code_equivalents": 4, "rms_code_equivalents": 1}:
        raise RuntimeError("Changed visual contract or repeated case")
    cache, hashes = {}, {}

    def image(name, kind):
        filename = f"images-{name}-{kind}.rgba32f"
        if filename not in cache:
            if (root / filename).exists():
                raw = (root / filename).read_bytes()
            else:
                with zipfile.ZipFile(root / "images.zip") as archive:
                    raw = archive.read(filename)
            if len(raw) != width * height * 16:
                raise RuntimeError(f"Invalid image size: {filename}")
            cache[filename] = np.frombuffer(raw, dtype="<f4").reshape(height, width, 4)
            hashes[filename] = hashlib.sha256(raw).hexdigest()
            if not np.isfinite(cache[filename]).all():
                raise RuntimeError(f"Nonfinite image: {filename}")
        return cache[filename]

    def contribution(name, coverage):
        lit, baseline = image(name, "lit"), image(name, "baseline")
        if not np.all(lit[:, :, 3] == coverage) or not np.all(baseline[:, :, 3] == coverage):
            raise RuntimeError(f"Wrong image coverage: {name}")
        rgb = (lit[:, :, :3] - baseline[:, :, :3]) / np.float32(coverage)
        if not np.all(rgb > 0):
            raise RuntimeError(f"Missing positive IBL: {name}")
        return rgb

    maximum_peak, maximum_rms, maximum_raw = 0.0, 0.0, 0.0
    comparisons = []
    preview = Image.new("RGB", (6 * width, 36 + 6 * (height + 22)), (24, 24, 24))
    labels = ImageDraw.Draw(preview)
    labels.text((3, 2), "S0: specified cube | S1: fog only | S2: atmosphere + fog", fill="white")
    labels.text((3, 18), "M0: dielectric | M1: metal | R: roughness | D: deferred | F: forward", fill="white")
    for source in range(3):
        for metal in (0, 1):
            for column, roughness in enumerate(("0.200000", "0.600000", "1.000000")):
                prefix = f"{source}-{metal}-{roughness}"
                deferred = contribution(prefix + "-opaque-deferred-runtime", 1)
                forward = contribution(prefix + "-opaque-forward-runtime", 1)
                for domain, coverage in (("opaque", 1), ("alpha", .5)):
                    for mode in ("deferred", "forward"):
                        runtime = f"{prefix}-{domain}-{mode}-runtime"
                        offscreen = f"{prefix}-{domain}-{mode}-offscreen"
                        expected = deferred if domain == "opaque" and mode == "deferred" else forward
                        for name in (runtime, offscreen):
                            row = rows[name]
                            if row["coverage"] != coverage or row["exposure_ev"] != (0, 1, 9)[source]:
                                raise RuntimeError("Unexpected case inputs")
                            if not np.array_equal(contribution(name, coverage), expected):
                                raise RuntimeError(f"Normalized lighting differs: {name}")
                        if not np.array_equal(image(runtime, "display"), image(offscreen, "display")):
                            raise RuntimeError(f"Runtime/offscreen display differs: {runtime}")
                        displayed = image(runtime, "display")[:, :, :3]
                        if not np.all((displayed > 0) & (displayed < 1)):
                            raise RuntimeError(f"Clipping obscures path comparison: {runtime}")
                    d = image(f"{prefix}-{domain}-deferred-runtime", "display")
                    f = image(f"{prefix}-{domain}-forward-runtime", "display")
                    codes = 255 * (d[:, :, :3].astype("f8") - f[:, :, :3])
                    peak, rms = float(np.max(np.abs(codes))), float(np.sqrt(np.mean(codes ** 2)))
                    if peak > 4 or rms > 1:
                        raise RuntimeError(f"Visible path difference exceeds budget: {prefix}/{domain}")
                    maximum_peak, maximum_rms = max(maximum_peak, peak), max(maximum_rms, rms)
                    comparisons.append({"case": prefix, "domain": domain, "peak_code_equivalents": peak, "rms_code_equivalents": rms})
                    if domain == "opaque":
                        y = 36 + (source * 2 + metal) * (height + 22)
                        for position, (title, pixels) in enumerate((("D", d), ("F", f))):
                            x = (column * 2 + position) * width
                            labels.text((x + 3, y + 3), f"S{source} M{metal} R{roughness[:3]} {title}", fill="white")
                            preview.paste(Image.fromarray(np.round(np.clip(pixels[:, :, :3], 0, 1) * 255).astype("u1")), (x, y + 22))
                maximum_raw = max(maximum_raw, float(np.max(np.abs(forward.astype("f8") - deferred) / np.maximum(1e-5, deferred))))

    references = manifest["packed_normal_references"]
    if len(references) != 18 or len({row["name"] for row in references}) != 18:
        raise RuntimeError("Incomplete precision controls")
    maximum_filter_rgb, maximum_filter_ev = 0.0, 0.0
    for row in references:
        name = row["name"]
        original = name.replace("-packed-normal-reference", "-opaque-deferred-runtime")
        native_d = contribution(original, 1).astype("f8")
        native_f = contribution(name, 1).astype("f8")
        canonical_d = image(name, "canonical-deferred")[:, :, :3].astype("f8")
        canonical_f = image(name, "canonical-forward")[:, :, :3].astype("f8")
        for native, canonical in ((native_d, canonical_d), (native_f, canonical_f)):
            if not np.all(canonical > 0):
                raise RuntimeError("Nonpositive canonical contribution")
            rgb = float(np.max(np.abs(native - canonical) / canonical))
            stops = float(np.max(np.abs(np.log2(native / canonical))))
            if rgb > .0025 or stops > 2 / 1024:
                raise RuntimeError("Existing FP16 filtering gate failed")
            maximum_filter_rgb, maximum_filter_ev = max(maximum_filter_rgb, rgb), max(maximum_filter_ev, stops)
    preview.save(root / "comparison.png")
    result = {"verdict": "pass", "cases": 144, "runtime_offscreen_exact_pairs": 72,
              "forward_translucent_normalized_exact": True, "precision_controls": 18,
              "maximum_display_peak_code_equivalents": maximum_peak,
              "maximum_display_rms_code_equivalents": maximum_rms,
              "maximum_unadjusted_hdr_relative_difference": maximum_raw,
              "maximum_filter_relative_rgb": maximum_filter_rgb, "maximum_filter_stops": maximum_filter_ev,
              "display_limits": manifest["display_limits"], "comparisons": comparisons,
              "scope": "Visual path agreement with retained input-quantization diagnostics; no production quality/performance changes",
              "raw_image_sha256": hashes}
    (root / "summary.json").write_text(json.dumps(result, indent=2) + "\n")
    print(json.dumps({key: value for key, value in result.items() if key not in ("comparisons", "raw_image_sha256")}, indent=2))


if __name__ == "__main__":
    summarize(sys.argv[1])
