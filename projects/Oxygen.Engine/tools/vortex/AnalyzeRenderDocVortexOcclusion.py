"""RenderDoc UI analyzer for the VortexBasic occlusion proof scene."""

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
    run_ui_script,
)


REPORT_SUFFIX = "_vortex_occlusion_report.txt"
DRAW_NAME = "ID3D12GraphicsCommandList::DrawInstanced()"
DISPATCH_NAME = "ID3D12GraphicsCommandList::Dispatch()"


def records_with_name(action_records, name):
    return [record for record in action_records if record.name == name]


def records_under_prefix(action_records, prefix):
    prefix_with_sep = prefix + " > "
    return [record for record in action_records if record.path.startswith(prefix_with_sep)]


def count_named_records(action_records, name):
    return sum(1 for record in action_records if record.name == name)


def append_exact_count_check(report, label, actual, expected):
    report.append("{}_actual={}".format(label, actual))
    report.append("{}_expected={}".format(label, expected))
    report.append("{}_match={}".format(label, "true" if actual == expected else "false"))
    if actual != expected:
        raise RuntimeError("{} mismatch: expected {}, got {}".format(label, expected, actual))


def append_less_than_check(report, label, actual, reference):
    match = actual < reference
    report.append("{}_actual={}".format(label, actual))
    report.append("{}_reference={}".format(label, reference))
    report.append("{}_match={}".format(label, "true" if match else "false"))
    if not match:
        raise RuntimeError("{} mismatch: expected {} < {}".format(label, actual, reference))


def build_report(controller, report: ReportWriter, capture_path: Path, report_path: Path):
    action_records = collect_action_records(controller)

    stage3_scope_name = "Vortex.Stage3.DepthPrepass"
    stage5_hzb_scope_name = "Vortex.Stage5.ScreenHzbBuild"
    stage5_occlusion_scope_name = "Vortex.Stage5.OcclusionTest"
    stage9_scope_name = "Vortex.Stage9.BasePass"
    stage9_main_scope_name = "Vortex.Stage9.BasePass.MainPass"
    stage20_ground_grid_scope_name = "Vortex.Stage20.GroundGrid"

    stage3_scope = records_with_name(action_records, stage3_scope_name)
    stage5_hzb_scope = records_with_name(action_records, stage5_hzb_scope_name)
    stage5_occlusion_scope = records_with_name(action_records, stage5_occlusion_scope_name)
    stage9_scope = records_with_name(action_records, stage9_scope_name)
    stage20_ground_grid_scope = records_with_name(action_records, stage20_ground_grid_scope_name)

    stage3_records = records_under_prefix(action_records, stage3_scope_name)
    stage5_occlusion_records = records_under_prefix(action_records, stage5_occlusion_scope_name)
    stage9_main_records = records_under_prefix(
        action_records, stage9_scope_name + " > " + stage9_main_scope_name
    )
    stage9_records = stage9_main_records if stage9_main_records else records_under_prefix(
        action_records, stage9_scope_name
    )

    stage3_draw_count = count_named_records(stage3_records, DRAW_NAME)
    stage5_occlusion_dispatch_count = count_named_records(stage5_occlusion_records, DISPATCH_NAME)
    stage9_draw_count = count_named_records(stage9_records, DRAW_NAME)

    report.append("analysis_profile=vortex_occlusion_runtime_capture")
    report.append("capture_path={}".format(capture_path))
    report.append("report_path={}".format(report_path))
    report.append("total_actions={}".format(len(action_records)))

    append_exact_count_check(report, "stage3_scope_count", len(stage3_scope), 1)
    append_exact_count_check(report, "stage5_screen_hzb_scope_count", len(stage5_hzb_scope), 1)
    append_exact_count_check(report, "stage5_occlusion_scope_count", len(stage5_occlusion_scope), 1)
    append_exact_count_check(report, "stage5_occlusion_dispatch_count", stage5_occlusion_dispatch_count, 1)
    append_exact_count_check(report, "stage9_scope_count", len(stage9_scope), 1)
    append_exact_count_check(report, "stage20_ground_grid_scope_count", len(stage20_ground_grid_scope), 0)
    append_exact_count_check(report, "stage3_draw_count", stage3_draw_count, 3)
    append_less_than_check(report, "stage9_draw_count_less_than_stage3", stage9_draw_count, stage3_draw_count)
    append_exact_count_check(report, "stage9_draw_count", stage9_draw_count, 2)


def main():
    run_ui_script(REPORT_SUFFIX, build_report)


main()
