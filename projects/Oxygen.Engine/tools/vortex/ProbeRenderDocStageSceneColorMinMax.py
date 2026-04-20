"""Probe SceneColor min/max after selected Vortex stages using safe replay APIs."""

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


REPORT_SUFFIX = "_stage_scene_color_minmax_probe.txt"


def float4_values(pixel_value):
    return [float(v) for v in list(pixel_value.floatValue)[:4]]


def find_last_action(action_records, scope_name):
    candidate = None
    for record in action_records:
        if scope_name in record.path and record.name in {
            "ID3D12GraphicsCommandList::DrawInstanced()",
            "ID3D12GraphicsCommandList::Dispatch()",
            "ExecuteIndirect()",
        }:
            candidate = record
    return candidate


def describe_scene_color(controller, resources, event_id):
    rd = renderdoc_module()
    controller.SetFrameEvent(event_id, True)
    state = controller.GetPipelineState()
    for descriptor in state.GetOutputTargets():
        resource = safe_getattr(descriptor, "resource")
        if resource is None:
            continue
        name = resources.get(str(resource), str(resource))
        if name != "SceneColor":
            continue
        sub = rd.Subresource()
        sub.mip = int(safe_getattr(descriptor, "firstMip", 0) or 0)
        sub.slice = int(safe_getattr(descriptor, "firstSlice", 0) or 0)
        sub.sample = 0
        min_value, max_value = controller.GetMinMax(resource, sub, rd.CompType.Typeless)
        return {
            "resource": name,
            "min": float4_values(min_value),
            "max": float4_values(max_value),
        }
    return None


def build_report(controller, report: ReportWriter, capture_path: Path, report_path: Path):
    del capture_path
    del report_path
    actions = collect_action_records(controller)
    resources = resource_id_to_name(controller)

    targets = {
        "stage12_directional": "Vortex.Stage12.DirectionalLight",
        "stage15_sky": "Vortex.Stage15.Sky",
        "stage15_atmosphere": "Vortex.Stage15.Atmosphere",
    }

    for label, scope_name in targets.items():
        record = find_last_action(actions, scope_name)
        report.append(f"{label}_event={'' if record is None else record.event_id}")
        if record is None:
            continue
        sample = describe_scene_color(controller, resources, record.event_id)
        report.append(f"{label}_scene_color_present={str(sample is not None).lower()}")
        if sample is None:
            continue
        report.append(f"{label}_scene_color_min={sample['min']}")
        report.append(f"{label}_scene_color_max={sample['max']}")


def main():
    run_ui_script(REPORT_SUFFIX, build_report)


if __name__ == "__main__":
    main()
