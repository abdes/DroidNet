"""Temporary Vortex-local pixel history probe."""

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
    safe_getattr,
)


REPORT_SUFFIX = "_pixel_history_probe.txt"


def build_report(controller, report: ReportWriter, capture_path: Path, report_path: Path):
    rd = renderdoc_module()
    actions = collect_action_records(controller)
    resources = resource_id_to_name(controller)

    draw = None
    for record in actions:
        if record.name == "ID3D12GraphicsCommandList::DrawInstanced()" and "Vortex.Stage9.BasePass" in record.path:
            draw = record
            break
    if draw is None:
        raise RuntimeError("No Stage9 draw")

    controller.SetFrameEvent(draw.event_id, True)
    state = controller.GetPipelineState()
    outputs = state.GetOutputTargets()
    target = None
    for output in outputs:
        if resources.get(str(output.resource), "") == "GBufferBaseColor":
            target = output
            break
    if target is None:
        raise RuntimeError("No GBufferBaseColor output")

    sub = rd.Subresource()
    sub.mip = int(safe_getattr(target, "firstMip", 0) or 0)
    sub.slice = int(safe_getattr(target, "firstSlice", 0) or 0)
    sub.sample = 0

    for x, y in [(640, 360), (640, 500), (640, 250), (400, 400), (900, 400)]:
        history = controller.PixelHistory(target.resource, x, y, sub, rd.CompType.Typeless)
        report.append("pixel_{}_{}_count={}".format(x, y, len(history)))
        if history:
            report.append("pixel_{}_{}_first_type={}".format(x, y, type(history[0]).__name__))
            report.append("pixel_{}_{}_first_methods={}".format(x, y, ",".join(sorted(dir(history[0])))))
            report.append("pixel_{}_{}_first_repr={}".format(x, y, repr(history[0])))


def main():
    run_ui_script(REPORT_SUFFIX, build_report)


if __name__ == "__main__":
    main()
