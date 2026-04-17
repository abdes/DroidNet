r"""RenderDoc UI structural analyzer for the VortexBasic runtime capture.

Run via qrenderdoc:

    & 'C:/Program Files/RenderDoc/qrenderdoc.exe' --ui-python `
        'F:/projects/DroidNet/projects/Oxygen.Engine/tools/vortex/AnalyzeRenderDocVortexBasicCapture.py' `
        'F:/projects/DroidNet/projects/Oxygen.Engine/out/build-ninja/analysis/vortex/vortexbasic/runtime/vortexbasic-proof_frame4.rdc'
"""

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

    raise RuntimeError(
        "Unable to locate tools/shadows from the RenderDoc script path. "
        "Launch qrenderdoc with the repository script path."
    )


if "pyrenderdoc" in globals():
    builtins.pyrenderdoc = pyrenderdoc


SCRIPT_DIR = resolve_script_dir()
if str(SCRIPT_DIR) not in sys.path:
    sys.path.insert(0, str(SCRIPT_DIR))

from renderdoc_ui_analysis import (  # noqa: E402
    ReportWriter,
    collect_action_records,
    renderdoc_module,
    run_ui_script,
)


REPORT_SUFFIX = "_vortexbasic_capture_report.txt"

EXPECTED_DRAW_ITEM_COUNT = 2
EXPECTED_STAGE3_DRAW_COUNT = 2
EXPECTED_STAGE3_COLOR_CLEAR_COUNT = 1
EXPECTED_STAGE3_DEPTH_CLEAR_COUNT = 1
EXPECTED_STAGE3_COPY_COUNT = 1
EXPECTED_STAGE9_DRAW_COUNT = 2
EXPECTED_STAGE12_DIRECTIONAL_DRAW_COUNT = 1
EXPECTED_STAGE12_POINT_DRAW_COUNT = 1
EXPECTED_STAGE12_POINT_STENCIL_CLEAR_COUNT = 0
EXPECTED_STAGE12_SPOT_DRAW_COUNT = 1
EXPECTED_STAGE12_SPOT_STENCIL_CLEAR_COUNT = 0
EXPECTED_COMPOSITING_DRAW_COUNT = 1
EXPECTED_STAGE15_SKY_DRAW_COUNT = 1
EXPECTED_STAGE15_ATMOSPHERE_DRAW_COUNT = 1
EXPECTED_STAGE15_FOG_DRAW_COUNT = 1


def records_with_name(action_records, name):
    return [record for record in action_records if record.name == name]


def records_under_prefix(action_records, prefix):
    prefix_with_sep = prefix + " > "
    return [
        record
        for record in action_records
        if record.path.startswith(prefix_with_sep)
    ]


def count_named_records(action_records, name):
    return sum(1 for record in action_records if record.name == name)


def append_exact_count_check(report, label, actual, expected):
    report.append("{}_actual={}".format(label, actual))
    report.append("{}_expected={}".format(label, expected))
    report.append("{}_match={}".format(label, "true" if actual == expected else "false"))
    if actual != expected:
        raise RuntimeError(
            "{} mismatch: expected {}, got {}".format(label, expected, actual)
        )


def append_order_check(report, label, ordered_event_ids):
    is_ordered = all(
        ordered_event_ids[index] < ordered_event_ids[index + 1]
        for index in range(len(ordered_event_ids) - 1)
    )
    report.append("{}_event_ids={}".format(label, ",".join(str(v) for v in ordered_event_ids)))
    report.append("{}_match={}".format(label, "true" if is_ordered else "false"))
    if not is_ordered:
        raise RuntimeError(
            "{} mismatch: event ids are not strictly increasing ({})".format(
                label, ordered_event_ids
            )
        )


def build_report(controller, report: ReportWriter, capture_path: Path, report_path: Path):
    action_records = collect_action_records(controller)

    interesting = []
    keywords = (
        "vortex",
        "basepass",
        "deferred",
        "depthprepass",
        "stage15",
        "sky",
        "atmosphere",
        "fog",
        "composit",
    )
    for record in action_records:
        haystack = "{} {}".format(record.name, record.path).lower()
        if any(keyword in haystack for keyword in keywords):
            interesting.append(record)

    report.append("analysis_profile=vortexbasic_runtime_capture")
    report.append("capture_path={}".format(capture_path))
    report.append("report_path={}".format(report_path))
    report.append("total_actions={}".format(len(action_records)))
    report.append("interesting_actions={}".format(len(interesting)))
    report.append("expected_draw_item_count={}".format(EXPECTED_DRAW_ITEM_COUNT))

    if not interesting:
        raise RuntimeError("No Vortex-related actions were found in the capture")

    stage3_scope_name = "Vortex.Stage3.DepthPrepass"
    stage9_scope_name = "Vortex.Stage9.BasePass"
    stage9_main_pass_scope_name = "Vortex.Stage9.BasePass.MainPass"
    stage12_scope_name = "Vortex.Stage12.DeferredLighting"
    stage12_spot_scope_name = "Vortex.Stage12.SpotLight"
    stage12_point_scope_name = "Vortex.Stage12.PointLight"
    stage12_directional_scope_name = "Vortex.Stage12.DirectionalLight"
    stage15_sky_scope_name = "Vortex.Stage15.Sky"
    stage15_atmosphere_scope_name = "Vortex.Stage15.Atmosphere"
    stage15_fog_scope_name = "Vortex.Stage15.Fog"
    compositing_scope_name = "Vortex.CompositingTask[label=Composite Copy View 1]"

    stage3_scope = records_with_name(action_records, stage3_scope_name)
    stage9_scope = records_with_name(action_records, stage9_scope_name)
    stage12_scope = records_with_name(action_records, stage12_scope_name)
    stage12_spot_scope = records_with_name(action_records, stage12_spot_scope_name)
    stage12_point_scope = records_with_name(action_records, stage12_point_scope_name)
    stage12_directional_scope = records_with_name(
        action_records, stage12_directional_scope_name
    )
    stage15_sky_scope = records_with_name(action_records, stage15_sky_scope_name)
    stage15_atmosphere_scope = records_with_name(
        action_records, stage15_atmosphere_scope_name
    )
    stage15_fog_scope = records_with_name(action_records, stage15_fog_scope_name)
    compositing_scope = records_with_name(action_records, compositing_scope_name)

    append_exact_count_check(report, "stage3_scope_count", len(stage3_scope), 1)
    append_exact_count_check(report, "stage9_scope_count", len(stage9_scope), 1)
    append_exact_count_check(report, "stage12_scope_count", len(stage12_scope), 1)
    append_exact_count_check(report, "stage12_spot_scope_count", len(stage12_spot_scope), 1)
    append_exact_count_check(report, "stage12_point_scope_count", len(stage12_point_scope), 1)
    append_exact_count_check(
        report, "stage12_directional_scope_count", len(stage12_directional_scope), 1
    )
    append_exact_count_check(report, "stage15_sky_scope_count", len(stage15_sky_scope), 1)
    append_exact_count_check(
        report, "stage15_atmosphere_scope_count", len(stage15_atmosphere_scope), 1
    )
    append_exact_count_check(report, "stage15_fog_scope_count", len(stage15_fog_scope), 1)
    append_exact_count_check(report, "compositing_scope_count", len(compositing_scope), 1)

    stage3_records = records_under_prefix(action_records, stage3_scope_name)
    stage9_main_pass_records = records_under_prefix(
        action_records, stage9_scope_name + " > " + stage9_main_pass_scope_name
    )
    stage9_records = (
        stage9_main_pass_records
        if stage9_main_pass_records
        else records_under_prefix(action_records, stage9_scope_name)
    )
    stage12_spot_records = records_under_prefix(
        action_records, stage12_scope_name + " > " + stage12_spot_scope_name
    )
    stage12_point_records = records_under_prefix(
        action_records, stage12_scope_name + " > " + stage12_point_scope_name
    )
    stage12_directional_records = records_under_prefix(
        action_records, stage12_scope_name + " > " + stage12_directional_scope_name
    )
    stage15_sky_records = records_under_prefix(action_records, stage15_sky_scope_name)
    stage15_atmosphere_records = records_under_prefix(
        action_records, stage15_atmosphere_scope_name
    )
    stage15_fog_records = records_under_prefix(action_records, stage15_fog_scope_name)
    compositing_records = records_under_prefix(action_records, compositing_scope_name)

    append_exact_count_check(
        report,
        "stage3_draw_count",
        count_named_records(stage3_records, "ID3D12GraphicsCommandList::DrawInstanced()"),
        EXPECTED_STAGE3_DRAW_COUNT,
    )
    append_exact_count_check(
        report,
        "stage3_color_clear_count",
        count_named_records(
            stage3_records, "ID3D12GraphicsCommandList::ClearRenderTargetView()"
        ),
        EXPECTED_STAGE3_COLOR_CLEAR_COUNT,
    )
    append_exact_count_check(
        report,
        "stage3_depth_clear_count",
        count_named_records(
            stage3_records, "ID3D12GraphicsCommandList::ClearDepthStencilView()"
        ),
        EXPECTED_STAGE3_DEPTH_CLEAR_COUNT,
    )
    append_exact_count_check(
        report,
        "stage3_copy_count",
        count_named_records(stage3_records, "ID3D12GraphicsCommandList::CopyTextureRegion()"),
        EXPECTED_STAGE3_COPY_COUNT,
    )
    append_exact_count_check(
        report,
        "stage9_draw_count",
        count_named_records(stage9_records, "ID3D12GraphicsCommandList::DrawInstanced()"),
        EXPECTED_STAGE9_DRAW_COUNT,
    )
    append_exact_count_check(
        report,
        "stage12_spot_draw_count",
        count_named_records(stage12_spot_records, "ID3D12GraphicsCommandList::DrawInstanced()"),
        EXPECTED_STAGE12_SPOT_DRAW_COUNT,
    )
    append_exact_count_check(
        report,
        "stage12_spot_stencil_clear_count",
        count_named_records(
            stage12_spot_records, "ID3D12GraphicsCommandList::ClearDepthStencilView()"
        ),
        EXPECTED_STAGE12_SPOT_STENCIL_CLEAR_COUNT,
    )
    append_exact_count_check(
        report,
        "stage12_point_draw_count",
        count_named_records(stage12_point_records, "ID3D12GraphicsCommandList::DrawInstanced()"),
        EXPECTED_STAGE12_POINT_DRAW_COUNT,
    )
    append_exact_count_check(
        report,
        "stage12_point_stencil_clear_count",
        count_named_records(
            stage12_point_records, "ID3D12GraphicsCommandList::ClearDepthStencilView()"
        ),
        EXPECTED_STAGE12_POINT_STENCIL_CLEAR_COUNT,
    )
    append_exact_count_check(
        report,
        "stage12_directional_draw_count",
        count_named_records(
            stage12_directional_records, "ID3D12GraphicsCommandList::DrawInstanced()"
        ),
        EXPECTED_STAGE12_DIRECTIONAL_DRAW_COUNT,
    )
    append_exact_count_check(
        report,
        "stage15_sky_draw_count",
        count_named_records(stage15_sky_records, "ID3D12GraphicsCommandList::DrawInstanced()"),
        EXPECTED_STAGE15_SKY_DRAW_COUNT,
    )
    append_exact_count_check(
        report,
        "stage15_atmosphere_draw_count",
        count_named_records(
            stage15_atmosphere_records, "ID3D12GraphicsCommandList::DrawInstanced()"
        ),
        EXPECTED_STAGE15_ATMOSPHERE_DRAW_COUNT,
    )
    append_exact_count_check(
        report,
        "stage15_fog_draw_count",
        count_named_records(stage15_fog_records, "ID3D12GraphicsCommandList::DrawInstanced()"),
        EXPECTED_STAGE15_FOG_DRAW_COUNT,
    )
    append_exact_count_check(
        report,
        "compositing_draw_count",
        count_named_records(
            compositing_records, "ID3D12GraphicsCommandList::DrawInstanced()"
        ),
        EXPECTED_COMPOSITING_DRAW_COUNT,
    )

    append_order_check(
        report,
        "phase03_runtime_stage_order",
        [
            stage3_scope[0].event_id,
            stage9_scope[0].event_id,
            stage12_scope[0].event_id,
            compositing_scope[0].event_id,
        ],
    )
    append_order_check(
        report,
        "stage15_runtime_stage_order",
        [
            stage12_scope[0].event_id,
            stage15_sky_scope[0].event_id,
            stage15_atmosphere_scope[0].event_id,
            stage15_fog_scope[0].event_id,
            compositing_scope[0].event_id,
        ],
    )

    for index, record in enumerate(interesting[:80], start=1):
        report.append(
            "action_{:02d}=event:{} flags:{} name:{} path:{}".format(
                index,
                record.event_id,
                record.flags,
                record.name,
                record.path,
            )
        )


def main():
    def _run(controller, report: ReportWriter, capture_path: Path, report_path: Path):
        build_report(controller, report, capture_path, report_path)

    run_ui_script(REPORT_SUFFIX, _run)


if __name__ == "__main__":
    main()
