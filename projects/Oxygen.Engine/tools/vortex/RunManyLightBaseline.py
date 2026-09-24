"""Run the bounded EX07D scene presets through the existing native benchmark.

Requires numpy, Pillow and jsonschema. Qualification compares production images
with the complete-list fallback before admitting each scene to timing. Resume
reuses completed rows only when the executable, runtime DLLs, shaders and recipe
identities match. No accepted MultiView/shadow/overhead campaign is invoked.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import math
import os
from pathlib import Path
import shutil
import subprocess
import time

import jsonschema
import numpy as np
from PIL import Image

ROOT = Path(__file__).resolve().parents[2]
SCHEMA = json.loads((Path(__file__).parent / "schemas/many-light-baseline.schema.json").read_text())


def presets() -> dict[str, dict]:
    rows = {f"sparse-{count}": {"lights": count} for count in (0, 1, 31, 32, 33, 64, 256, 1024, 4096)}
    rows.update({f"dense-{count}": {"lights": count, "distribution": "dense"} for count in (33, 64, 256)})
    rows.update({f"irrelevant-{count}": {"lights": count, "distribution": "irrelevant"} for count in (1024, 4096)})
    rows["moving-1024"] = {"moving": True}
    rows["moving-4096"] = {"lights": 4096, "moving": True}
    rows["4k-1024"] = {"width": 3840, "height": 2160}
    rows["4k-4096"] = {"lights": 4096, "width": 3840, "height": 2160}
    rows["4k-dense-256"] = {"lights": 256, "distribution": "dense", "width": 3840, "height": 2160}
    rows["two-view-1024"] = {"secondary_view": True}
    rows["orthographic-1024"] = {"projection": "orthographic"}
    rows["shadows-1-1"] = {"point_shadows": 1, "spot_shadows": 1}
    rows["shadows-4-8"] = {"point_shadows": 4, "spot_shadows": 8}
    rows["shadows-5-9"] = {"point_shadows": 5, "spot_shadows": 9}
    rows["shadows-moving"] = {"point_shadows": 4, "spot_shadows": 8, "moving": True}
    rows["shadows-finite"] = {"point_shadows": 1, "spot_shadows": 1, "source_radius_m": 0.25}
    rows["shadows-wide"] = {"point_shadows": 1, "spot_shadows": 1, "source_radius_m": 0.25,
                            "spot_outer_half_angle_radians": math.pi / 2}
    return rows


def save(path: Path, value) -> None:
    path.write_text(json.dumps(value, indent=2, allow_nan=False) + "\n", encoding="utf-8", newline="\n")


def digest(path: Path) -> str:
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def hardware_sample() -> dict:
    tool = shutil.which("nvidia-smi")
    if not tool:
        return {"available": False}
    query = "name,driver_version,pstate,temperature.gpu,power.draw,clocks.current.graphics,clocks.current.memory,utilization.gpu,memory.used"
    result = subprocess.run([tool, f"--query-gpu={query}", "--format=csv"], capture_output=True, text=True, check=True)
    return {"available": True, "unix_time": time.time(), "csv": result.stdout.strip()}


def freeze(executable: Path, output: Path) -> dict:
    paths = [executable, *sorted(executable.parent.glob("Oxygen.*.dll")),
             ROOT / "bin/Oxygen/Release/production/shaders.bin",
             ROOT / "src/Oxygen/Vortex/Test/Lighting/LightingWorkloads.json"]
    identity = {str(path.relative_to(ROOT)): digest(path) for path in paths}
    checkpoint = output / "checkpoint.json"
    if checkpoint.exists():
        prior = json.loads(checkpoint.read_text())
        if prior["hashes"] != identity:
            raise RuntimeError("Baseline identity changed; preserve this evidence and use a new output directory")
        return prior
    head = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip()
    diff = subprocess.check_output(["git", "diff", "--binary"], cwd=ROOT)
    (output / "working-tree.patch").write_bytes(diff)
    sources = [*sorted((ROOT / "src/Oxygen/Vortex/Benchmarks").glob("ManyLightBaseline*")),
               *sorted((ROOT / "src/Oxygen/Vortex/Test/Support").glob("LightingWorkload*")),
               ROOT / "src/Oxygen/Vortex/Test/Lighting/LightingWorkloads.json",
               ROOT / "src/Oxygen/Graphics/Direct3D12/Shaders/Vortex/Services/Shadows/DirectionalShadowCommon.hlsli",
               ROOT / "src/Oxygen/Vortex/Upload/RingBufferStaging.cpp",
               ROOT / "src/Oxygen/Vortex/Lighting/Internal/DeferredLightConstantsPublisher.cpp",
               ROOT / "src/Oxygen/Graphics/Common/ResourceRegistry.cpp",
               Path(__file__).resolve()]
    source_hashes = {}
    for source in sources:
        relative = source.relative_to(ROOT)
        destination = output / "source" / relative
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(source, destination)
        source_hashes[str(relative)] = digest(source)
    value = {"git_head": head, "hashes": identity, "source_hashes": source_hashes, "hardware_before": hardware_sample(),
             "python": os.sys.version, "scope": "Native offscreen renderer; existing accepted baselines credited separately"}
    save(checkpoint, value)
    return value


def compare_images(reference: Path, candidate: Path, report_name: str = "image-comparison.json") -> dict:
    expected = json.loads((reference / "manifest.json").read_text())
    observed = json.loads((candidate / "manifest.json").read_text())
    if not expected["complete"] or not observed["complete"]:
        raise RuntimeError("Cannot qualify incomplete image evidence")
    results = []
    if len(expected["images"]) != len(observed["images"]):
        raise RuntimeError("Missing comparison images")
    for left, right in zip(expected["images"], observed["images"], strict=True):
        for key in ("phase", "view", "width", "height", "pre_exposure"):
            if left[key] != right[key]:
                raise RuntimeError(f"Unmatched image {key}")
        shape = (right["height"], right["width"], 4)
        a = np.memmap(reference / left["file"], dtype="<f4", mode="r", shape=shape)[..., :3]
        b = np.memmap(candidate / right["file"], dtype="<f4", mode="r", shape=shape)[..., :3]
        if not np.isfinite(a).all() or not np.isfinite(b).all():
            raise RuntimeError("Nonfinite reference or candidate image")
        budget = 0.005 * np.abs(a) + 2e-5
        fraction = np.abs(b - a) / budget
        failed = int(np.count_nonzero(fraction > 1.0))
        results.append({"image": right["file"], "failed_channels": failed,
                        "max_budget_fraction": float(np.max(fraction)), "channels": int(a.size)})
        # Fixed display transform, used only for the inspection PNG, never comparison.
        display = np.clip(b, 0, None)
        display = np.power(display / (1.0 + display), 1.0 / 2.2)
        Image.fromarray(np.uint8(np.clip(display * 255, 0, 255))).save(candidate / right["file"].replace(".rgba32f", ".png"))
    value = {"complete": True, "reference": str(reference), "candidate": str(candidate),
             "passed": all(row["failed_channels"] == 0 for row in results),
             "budget": "0.5% relative + 2e-5 absolute", "images": results}
    save(candidate / report_name, value)
    if not value["passed"]:
        raise RuntimeError(f"Image qualification failed: {candidate}")
    return value


def run_case(executable: Path, output: Path, name: str, family: str, variant: dict,
             mode: str, resume: bool, capture_tool: Path | None = None,
             runtime_environment: dict | None = None) -> Path:
    directory = output / f"{name}-{family}" / mode
    request = {"schema_version": 1, "case": name, "family": family,
               "reference": mode == "reference", "measure": mode == "baseline",
               "lights": 1024, "width": 1920, "height": 1080, "moving": False,
               "secondary_view": False, "point_shadows": 0, "spot_shadows": 0,
               "source_radius_m": 0.0, "spot_outer_half_angle_radians": 0.6,
               "distribution": "sparse", "projection": "perspective",
               "warmup_frames": 120 if mode == "baseline" else 12,
               "sample_frames": 240, "output": str(directory), **variant}
    if request["moving"]:
        request["warmup_frames"] = 240 if mode == "baseline" else 12
        request["sample_frames"] = 480
    if name == "sparse-1024":
        request["sample_frames"] = 480
    jsonschema.Draft202012Validator(SCHEMA).validate(request)
    manifest = directory / "manifest.json"
    if resume and manifest.exists():
        existing = json.loads(manifest.read_text())
        if existing["request"] != request or not existing["complete"]:
            raise RuntimeError(f"Cannot resume mismatched or incomplete row {directory}")
        test = json.loads((directory / "test.json").read_text())
        cases = [case for suite in test["testsuites"] for case in suite["testsuite"]]
        if (test["tests"] != 1 or test["failures"] or test["errors"] or len(cases) != 1
                or cases[0]["status"] != "RUN" or cases[0]["result"] != "COMPLETED"):
            raise RuntimeError(f"Cannot resume failed native validation: {directory}")
        if capture_tool is not None and mode == "baseline":
            trace = directory / "native.tracy"
            if not trace.is_file() or trace.stat().st_size == 0:
                raise RuntimeError(f"Missing Tracy evidence: {directory}")
        return directory
    if directory.exists():
        raise RuntimeError(f"Preserve existing attempt; use --resume or a new output directory: {directory}")
    directory.mkdir(parents=True)
    request_path = directory / "request.json"
    save(request_path, request)
    environment = os.environ.copy()
    for key, value in (runtime_environment or {}).items():
        if value is None:
            environment.pop(key, None)
        else:
            environment[key] = value
    environment["OXYGEN_MANY_LIGHT_REQUEST"] = str(request_path)
    command = [str(executable), "--gtest_also_run_disabled_tests",
               "--gtest_filter=ManyLightBaseline.DISABLED_RenderAndMeasure",
               f"--gtest_output=json:{directory / 'test.json'}", "-v=-1"]
    save(directory / "launch.json", {"command": command, "cwd": str(ROOT),
                                    "runtime_environment": runtime_environment,
                                    "hardware_before": hardware_sample()})
    print(f"{name} {family}: {mode}", flush=True)
    capture = None
    capture_log = None
    try:
        if capture_tool is not None and mode == "baseline":
            capture_log = (directory / "tracy.log").open("w", encoding="utf-8")
            capture = subprocess.Popen([str(capture_tool), "-a", "127.0.0.1", "-o", str(directory / "native.tracy")],
                                       stdout=capture_log, stderr=subprocess.STDOUT,
                                       creationflags=subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0)
        with (directory / "run.log").open("w", encoding="utf-8") as stream:
            result = subprocess.run(command, cwd=ROOT, env=environment, stdout=stream, stderr=subprocess.STDOUT)
        if capture is not None:
            if capture.wait(timeout=60) != 0 or not (directory / "native.tracy").exists():
                raise RuntimeError(f"Tracy capture failed: {directory / 'tracy.log'}")
    finally:
        if capture is not None and capture.poll() is None:
            capture.terminate()
            capture.wait(timeout=10)
        if capture_log is not None:
            capture_log.close()
    save(directory / "hardware-after.json", hardware_sample())
    if result.returncode != 0 or not manifest.exists():
        raise RuntimeError(f"Native run failed ({result.returncode}): {directory / 'run.log'}")
    return directory


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--executable", type=Path, help="Explicit benchmark executable; otherwise select a built Release preset")
    parser.add_argument("--build-tree", help="Constrain automatic executable selection to this build tree")
    parser.add_argument("--cases", nargs="+", choices=list(presets()), default=list(presets()))
    parser.add_argument("--families", nargs="+", choices=("forward", "deferred"), default=("forward", "deferred"),
                        help="Measure the same scene in both supported rendering families")
    parser.add_argument("--qualify-only", action="store_true")
    parser.add_argument("--resume", action="store_true")
    parser.add_argument("--tracy-capture", type=Path, help="Matching Tracy capture executable; captures each baseline run with the same instrumentation")
    args = parser.parse_args()
    output = args.output.resolve()
    runtime_environment = None
    if args.executable:
        if args.build_tree:
            parser.error("--executable and --build-tree cannot be combined")
        executable = args.executable.resolve()
    else:
        selector = ROOT / "tools/cli/BuildSelection.ps1"
        command = ["pwsh", "-NoProfile", "-File", str(selector), "-AsJson", "-Config", "Release",
                   "-RequiredExecutables", "Oxygen.Vortex.Lighting.Benchmarks.exe"]
        if args.build_tree:
            command += ["-BuildTree", args.build_tree]
        if args.tracy_capture:
            command += ["-Tracy"]
        selected = subprocess.run(command, cwd=ROOT, capture_output=True, text=True, check=True)
        selection = json.loads(selected.stdout)
        executable = Path(selection["BuildRoot"]) / "bin" / selection["Config"] / "Oxygen.Vortex.Lighting.Benchmarks.exe"
        runtime_environment = selection["RuntimeEnvironment"]
        print(f"Using {selection['BuildPreset']}: {executable}", flush=True)
    output.mkdir(parents=True, exist_ok=True)
    freeze(executable, output)
    for name in args.cases:
        for family in args.families:
            freeze(executable, output)
            reference = run_case(executable, output, name, family, presets()[name], "reference", args.resume, runtime_environment=runtime_environment)
            qualification = run_case(executable, output, name, family, presets()[name], "qualification", args.resume, runtime_environment=runtime_environment)
            compare_images(reference, qualification)
            if not args.qualify_only:
                baseline = run_case(executable, output, name, family, presets()[name], "baseline", args.resume,
                                    args.tracy_capture.resolve() if args.tracy_capture else None, runtime_environment=runtime_environment)
                compare_images(reference, baseline)
    freeze(executable, output)
    print("Requested cases completed and image comparisons passed.", flush=True)


if __name__ == "__main__":
    main()
