"""Probe the final SceneColor output for a named RenderDoc scope."""

import builtins
import os
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


REPORT_SUFFIX = "_named_scope_output_probe.txt"


def resolve_scope_name(capture_path: Path):
    configured = os.environ.get("OXYGEN_RENDERDOC_PASS_NAME", "").strip()
    if configured:
        return configured

    stem = capture_path.stem.lower()
    if "worldnormals" in stem:
        return "Vortex.DebugVisualization.WorldNormals"
    if "scenedepthraw" in stem:
        return "Vortex.DebugVisualization.SceneDepthRaw"
    if "scenedepthlinear" in stem:
        return "Vortex.DebugVisualization.SceneDepthLinear"

    raise RuntimeError("Set OXYGEN_RENDERDOC_PASS_NAME to the scope name to inspect.")


def float4_values(pixel_value):
    return [float(v) for v in list(pixel_value.floatValue)[:4]]


def find_last_draw(controller, scope_name, action_records):
    del controller
    matches = [record for record in action_records if scope_name in record.path]
    draw = None
    for record in matches:
        if record.name == "ID3D12GraphicsCommandList::DrawInstanced()":
            draw = record
    return draw


def build_report(controller, report: ReportWriter, capture_path: Path, report_path: Path):
    rd = renderdoc_module()
    scope_name = resolve_scope_name(capture_path)
    action_records = collect_action_records(controller)
    resource_names = resource_id_to_name(controller)

    draw = find_last_draw(controller, scope_name, action_records)
    report.append("scope_name={}".format(scope_name))
    report.append("capture_path={}".format(capture_path))
    report.append("report_path={}".format(report_path))
    report.append("draw_event={}".format("" if draw is None else draw.event_id))

    if draw is None:
        raise RuntimeError("No draw event found under scope '{}'".format(scope_name))

    controller.SetFrameEvent(draw.event_id, True)
    state = controller.GetPipelineState()
    outputs = state.GetOutputTargets()
    report.append("output_count={}".format(len(outputs)))

    for index, descriptor in enumerate(outputs):
        resource_id = descriptor.resource
        name = resource_names.get(str(resource_id), str(resource_id))
        report.append("output_{}_name={}".format(index, name))
        report.append("output_{}_firstMip={}".format(index, safe_getattr(descriptor, "firstMip", 0)))
        report.append("output_{}_firstSlice={}".format(index, safe_getattr(descriptor, "firstSlice", 0)))

        if name != "SceneColor":
            continue

        texture_desc = None
        for desc in controller.GetTextures():
            if safe_getattr(desc, "resourceId") == resource_id:
                texture_desc = desc
                break

        width = int(safe_getattr(texture_desc, "width", 1) or 1)
        height = int(safe_getattr(texture_desc, "height", 1) or 1)
        sub = rd.Subresource()
        sub.mip = int(safe_getattr(descriptor, "firstMip", 0) or 0)
        sub.slice = int(safe_getattr(descriptor, "firstSlice", 0) or 0)
        sub.sample = 0
        min_value, max_value = controller.GetMinMax(
            resource_id, sub, rd.CompType.Typeless
        )
        center = controller.PickPixel(
            resource_id,
            max(0, min(width - 1, width // 2)),
            max(0, min(height - 1, height // 2)),
            sub,
            rd.CompType.Typeless,
        )
        report.append("scene_color_dims={}x{}".format(width, height))
        report.append("scene_color_min={}".format(float4_values(min_value)))
        report.append("scene_color_max={}".format(float4_values(max_value)))
        report.append("scene_color_center={}".format(float4_values(center)))


def main():
    run_ui_script(REPORT_SUFFIX, build_report)


if __name__ == "__main__":
    main()
