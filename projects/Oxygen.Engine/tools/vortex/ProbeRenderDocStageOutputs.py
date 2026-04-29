"""Probe selected Vortex stage outputs from a RenderDoc capture."""

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


REPORT_SUFFIX = "_stage_output_probe.txt"


def float4_values(pixel_value):
    return [float(v) for v in list(pixel_value.floatValue)[:4]]


def find_output_sample(controller, resource_names, texture_descs, record):
    rd = renderdoc_module()
    controller.SetFrameEvent(record.event_id, True)
    state = controller.GetPipelineState()
    for descriptor in state.GetOutputTargets():
        if safe_getattr(descriptor, "resource") is None:
            continue
        name = resource_names.get(str(descriptor.resource), str(descriptor.resource))
        if name != "SceneColor":
            continue
        texture_desc = texture_descs.get(str(descriptor.resource))
        width = int(safe_getattr(texture_desc, "width", 1) or 1)
        height = int(safe_getattr(texture_desc, "height", 1) or 1)
        sub = rd.Subresource()
        sub.mip = int(safe_getattr(descriptor, "firstMip", 0) or 0)
        sub.slice = int(safe_getattr(descriptor, "firstSlice", 0) or 0)
        sub.sample = 0
        min_value, max_value = controller.GetMinMax(
            descriptor.resource, sub, rd.CompType.Typeless
        )
        center = controller.PickPixel(
            descriptor.resource,
            max(0, min(width - 1, width // 2)),
            max(0, min(height - 1, height // 2)),
            sub,
            rd.CompType.Typeless,
        )
        return {
            "name": name,
            "width": width,
            "height": height,
            "min": float4_values(min_value),
            "max": float4_values(max_value),
            "center": float4_values(center),
        }
    return None


def build_report(controller, report: ReportWriter, capture_path: Path, report_path: Path):
    resource_names = resource_id_to_name(controller)
    texture_descs = {
        str(safe_getattr(desc, "resourceId")): desc
        for desc in controller.GetTextures()
        if safe_getattr(desc, "resourceId") is not None
    }
    action_records = collect_action_records(controller)
    targets = {
        "stage12_directional": "Vortex.Stage12.DirectionalLight",
        "stage12_spot": "Vortex.Stage12.SpotLight",
        "stage12_point": "Vortex.Stage12.PointLight",
        "stage15_sky": "Vortex.Stage15.Sky",
        "debug_basecolor": "Vortex.DebugVisualization.BaseColor",
        "debug_directional_shadow_mask":
            "Vortex.DebugVisualization.DirectionalShadowMask",
    }

    for label, scope_name in targets.items():
        matches = [record for record in action_records if scope_name in record.path]
        draw = None
        for record in matches:
            if record.name in {
                "ID3D12GraphicsCommandList::DrawInstanced()",
                "ExecuteIndirect()",
            }:
                draw = record
        report.append(f"{label}_event={(draw.event_id if draw else '')}")
        if draw is None:
            continue
        sample = find_output_sample(controller, resource_names, texture_descs, draw)
        report.append(f"{label}_present={str(sample is not None).lower()}")
        if sample is None:
            continue
        report.append(f"{label}_dims={sample['width']}x{sample['height']}")
        report.append(f"{label}_min={sample['min']}")
        report.append(f"{label}_max={sample['max']}")
        report.append(f"{label}_center={sample['center']}")


def main():
    run_ui_script(REPORT_SUFFIX, build_report)


if __name__ == "__main__":
    main()
