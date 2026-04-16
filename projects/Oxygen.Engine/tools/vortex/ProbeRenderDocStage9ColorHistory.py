"""Temporary Vortex-local Stage 9 color pixel history probe."""

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

from renderdoc_ui_analysis import ReportWriter, collect_action_records, renderdoc_module, resource_id_to_name, run_ui_script  # noqa: E402


REPORT_SUFFIX = "_stage9_color_history.txt"


def build_report(controller, report: ReportWriter, capture_path: Path, report_path: Path):
    rd = renderdoc_module()
    resources = resource_id_to_name(controller)
    actions = collect_action_records(controller)
    draw_events = []
    stage9_draw = None
    for record in actions:
        if record.name == "ID3D12GraphicsCommandList::DrawInstanced()" and "Vortex.Stage9.BasePass" in record.path:
            draw_events.append(record.event_id)
            if stage9_draw is None:
                stage9_draw = record
    if stage9_draw is None:
        raise RuntimeError("No Stage9 draw")

    controller.SetFrameEvent(stage9_draw.event_id, True)
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
    sub.mip = int(target.firstMip)
    sub.slice = int(target.firstSlice)
    sub.sample = 0

    hit_pixels = []
    for x in range(80, 1280, 160):
        for y in range(80, 720, 120):
            history = controller.PixelHistory(target.resource, x, y, sub, rd.CompType.Typeless)
            hit_events = [mod.eventId for mod in history if mod.eventId in draw_events]
            if hit_events:
                hit_pixels.append((x, y, hit_events))
    report.append("draw_events={}".format(",".join(str(v) for v in draw_events)))
    report.append("stage9_draw_hit_pixel_count={}".format(len(hit_pixels)))
    for index, (x, y, events) in enumerate(hit_pixels[:40]):
        report.append("stage9_draw_hit_pixel_{}={},{},{}".format(index, x, y, ",".join(str(v) for v in events)))


def main():
    run_ui_script(REPORT_SUFFIX, build_report)


if __name__ == "__main__":
    main()
