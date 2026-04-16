"""Temporary Vortex-local RenderDoc texture sampling probe."""

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
    run_ui_script,
)


REPORT_SUFFIX = "_texture_probe.txt"


def build_report(controller, report: ReportWriter, capture_path: Path, report_path: Path):
    rd = renderdoc_module()
    resources = resource_id_to_name(controller)
    action_records = collect_action_records(controller)
    target_action = None
    for record in action_records:
        if record.name == "ID3D12GraphicsCommandList::DrawInstanced()" and "Vortex.Stage9.BasePass" in record.path:
            target_action = record
            break
    if target_action is None:
        raise RuntimeError("Could not find Stage9 draw")

    controller.SetFrameEvent(target_action.event_id, True)
    state = controller.GetPipelineState()
    outputs = state.GetOutputTargets()
    report.append("event_id={}".format(target_action.event_id))
    report.append("output_count={}".format(len(outputs)))
    for index, output in enumerate(outputs):
        name = resources.get(str(output.resource), str(output.resource))
        report.append("output_{}_name={}".format(index, name))
        sub = rd.Subresource()
        sub.mip = output.firstMip
        sub.slice = output.firstSlice
        sub.sample = 0
        min_value, max_value = controller.GetMinMax(output.resource, sub, rd.CompType.Typeless)
        report.append("output_{}_min_float={}".format(index, list(min_value.floatValue)))
        report.append("output_{}_max_float={}".format(index, list(max_value.floatValue)))
        report.append("output_{}_pixelvalue_methods={}".format(index, ",".join(sorted(dir(min_value)))))
        center = controller.PickPixel(output.resource, 640, 360, sub, rd.CompType.Typeless)
        report.append("output_{}_center_float={}".format(index, list(center.floatValue)))


def main():
    run_ui_script(REPORT_SUFFIX, build_report)


if __name__ == "__main__":
    main()
