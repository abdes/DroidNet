"""Temporary Vortex-local Stage 3 depth pixel history probe."""

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

from renderdoc_ui_analysis import ReportWriter, collect_action_records, renderdoc_module, run_ui_script  # noqa: E402


REPORT_SUFFIX = "_stage3_depth_history.txt"


def build_report(controller, report: ReportWriter, capture_path: Path, report_path: Path):
    rd = renderdoc_module()
    actions = collect_action_records(controller)
    stage3_draw = None
    for record in actions:
        if record.name == "ID3D12GraphicsCommandList::DrawInstanced()" and "Vortex.Stage3.DepthPrepass" in record.path:
            stage3_draw = record
            break
    if stage3_draw is None:
        raise RuntimeError("No Stage3 draw")

    controller.SetFrameEvent(stage3_draw.event_id, True)
    state = controller.GetPipelineState()
    depth = state.GetDepthTarget()
    sub = rd.Subresource()
    sub.mip = int(depth.firstMip)
    sub.slice = int(depth.firstSlice)
    sub.sample = 0

    for x, y in [(640, 360), (640, 500), (640, 250), (400, 400), (900, 400), (200, 650), (1100, 650)]:
        history = controller.PixelHistory(depth.resource, x, y, sub, rd.CompType.Depth)
        report.append("pixel_{}_{}_count={}".format(x, y, len(history)))
        if history:
            first = history[0]
            report.append("pixel_{}_{}_first_event={}".format(x, y, first.eventId))
            report.append("pixel_{}_{}_first_passed={}".format(x, y, str(first.Passed()).lower()))
            report.append("pixel_{}_{}_first_methods={}".format(x, y, ",".join(sorted(dir(first)))))
            report.append("pixel_{}_{}_first_repr={}".format(x, y, repr(first)))

    hit_pixels = []
    for x in range(80, 1280, 160):
        for y in range(80, 720, 120):
            history = controller.PixelHistory(depth.resource, x, y, sub, rd.CompType.Depth)
            if any(mod.eventId in (30, 33) for mod in history):
                hit_pixels.append((x, y, [mod.eventId for mod in history]))
    report.append("stage3_draw_hit_pixel_count={}".format(len(hit_pixels)))
    for index, (x, y, events) in enumerate(hit_pixels[:20]):
        report.append("stage3_draw_hit_pixel_{}={},{},{}".format(index, x, y, ",".join(str(v) for v in events)))


def main():
    run_ui_script(REPORT_SUFFIX, build_report)


if __name__ == "__main__":
    main()
