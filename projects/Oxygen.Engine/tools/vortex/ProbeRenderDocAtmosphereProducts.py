"""Probe atmosphere-related RenderDoc resources in a Vortex capture."""

import builtins
import sys
from pathlib import Path


def resolve_script_dir():
    script_candidates = []
    current_file = globals().get("__file__")
    if current_file:
        script_candidates.append(Path(current_file))

    argv = getattr(sys, "argv", None) or getattr(sys, "orig_argv", None) or []
    for arg in argv:
        try:
            candidate = Path(arg)
        except TypeError:
            continue
        if candidate.suffix.lower() == ".py":
            script_candidates.append(candidate)

    candidates = []
    for script_candidate in script_candidates:
        resolved = script_candidate.resolve()
        candidates.append(resolved.parent)
        candidates.extend(resolved.parents)

    search_root = Path.cwd().resolve()
    candidates.append(search_root)
    candidates.extend(search_root.parents)

    seen = set()
    for candidate_root in candidates:
        normalized = str(candidate_root).lower()
        if normalized in seen:
            continue
        seen.add(normalized)

        candidate = candidate_root / "tools" / "shadows"
        if (candidate / "renderdoc_ui_analysis.py").exists():
            return candidate

    raise RuntimeError("Unable to locate tools/shadows from the RenderDoc script path.")


if "pyrenderdoc" in globals():
    builtins.pyrenderdoc = pyrenderdoc


SCRIPT_DIR = resolve_script_dir()
if str(SCRIPT_DIR) not in sys.path:
    sys.path.insert(0, str(SCRIPT_DIR))

from renderdoc_ui_analysis import (  # noqa: E402
    ReportWriter,
    collect_action_records,
    renderdoc_module,
    resource_id_to_name,
    safe_getattr,
    run_ui_script,
)


REPORT_SUFFIX = "_atmosphere_products_probe.txt"


def float4_values(pixel_value):
    return [float(v) for v in list(pixel_value.floatValue)[:4]]


def sample_texture(controller, resource, subresource, width, height):
    del width
    del height
    rd = renderdoc_module()
    min_value, max_value = controller.GetMinMax(resource, subresource, rd.CompType.Typeless)
    return {
        "min": float4_values(min_value),
        "max": float4_values(max_value),
    }


def build_report(controller, report: ReportWriter, capture_path: Path, report_path: Path):
    rd = renderdoc_module()
    action_records = collect_action_records(controller)
    resource_names = resource_id_to_name(controller)
    textures = list(controller.GetTextures())
    texture_by_id = {
        str(safe_getattr(desc, "resourceId")): desc
        for desc in textures
        if safe_getattr(desc, "resourceId") is not None
    }

    interesting_textures = []
    for resource_id, name in resource_names.items():
        if any(token in name for token in (
            "AtmosphereTransmittanceLut",
            "AtmosphereMultiScatteringLut",
            "AtmosphereSkyViewLut",
            "AtmosphereCameraAerialPerspective",
            "DistantSkyLightLut",
        )):
            interesting_textures.append((resource_id, name))

    interesting_textures.sort(key=lambda item: item[1])
    report.append(f"capture_path={capture_path}")
    report.append(f"interesting_texture_count={len(interesting_textures)}")
    for index, (resource_id, name) in enumerate(interesting_textures):
        desc = texture_by_id.get(resource_id)
        width = int(safe_getattr(desc, "width", 0) or 0)
        height = int(safe_getattr(desc, "height", 0) or 0)
        depth = int(safe_getattr(desc, "depth", 0) or 0)
        report.append(f"texture_{index}_name={name}")
        report.append(f"texture_{index}_dims={width}x{height}x{depth}")

    targets = {
        "sky_view_dispatch": "Vortex.Environment.AtmosphereSkyViewLut",
        "camera_aerial_dispatch": "Vortex.Environment.AtmosphereCameraAerialPerspective",
    }
    for label, scope_name in targets.items():
        target_event = None
        for record in action_records:
            if scope_name in record.path and record.name == "ID3D12GraphicsCommandList::Dispatch()":
                target_event = record.event_id
                break
        report.append(f"{label}_event={(target_event or '')}")
        if target_event is None:
            continue
        controller.SetFrameEvent(target_event, True)
        for resource_id, name in interesting_textures:
            desc = texture_by_id.get(resource_id)
            if desc is None:
                continue
            width = int(safe_getattr(desc, "width", 0) or 0)
            height = int(safe_getattr(desc, "height", 0) or 0)
            if width <= 0 or height <= 0:
                continue
            sub = rd.Subresource()
            sub.mip = 0
            sub.slice = 0
            sub.sample = 0
            sample = sample_texture(
                controller,
                safe_getattr(desc, "resourceId"),
                sub,
                width,
                height,
            )
            key = f"{label}_{name}"
            report.append(f"{key}_min={sample['min']}")
            report.append(f"{key}_max={sample['max']}")


def main():
    run_ui_script(REPORT_SUFFIX, build_report)


if __name__ == "__main__":
    main()
