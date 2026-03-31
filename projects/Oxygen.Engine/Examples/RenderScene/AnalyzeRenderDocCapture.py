r"""Baseline Phase K-a RenderDoc analyzer for RenderScene.

Run only inside RenderDoc UI:

PowerShell:
    $env:OXYGEN_RENDERDOC_REPORT_PATH = 'H:/path/k_a_baseline.txt'
    & 'C:/Program Files/RenderDoc/qrenderdoc.exe' --ui-python `
        'H:/projects/DroidNet/projects/Oxygen.Engine/Examples/RenderScene/AnalyzeRenderDocCapture.py' `
        'H:/path/capture.rdc'

Environment variables
---------------------
- `OXYGEN_RENDERDOC_REPORT_PATH`: optional explicit report path.
- `OXYGEN_RENDERDOC_CAPTURE_PATH`: optional explicit capture path.

This baseline analyzer stays bounded on purpose. It only answers whether the
loaded replay-safe capture is usable as automated Phase K-a evidence:

- the VSM shell markers are present from Stage 5 through Stage 15
- the capture reaches `VsmProjectionPass`
- both Stage 15 mask resources are present in the capture
- `VsmProjectionPass` writes both Stage 15 mask resources

Manual sign-off and external test gates remain out of scope for this script and
are reported as manual or blocked even when all automated checks pass.
"""

import sys
import builtins
from pathlib import Path
from typing import Dict, List, Sequence


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

    capture_context = globals().get("pyrenderdoc")
    get_capture_filename = getattr(
        capture_context, "GetCaptureFilename", None
    )
    if callable(get_capture_filename):
        try:
            capture_path = Path(get_capture_filename()).resolve()
            candidates.append(capture_path.parent)
            candidates.extend(capture_path.parents)
        except Exception:
            pass

    search_root = Path.cwd().resolve()
    candidates.append(search_root)
    candidates.extend(search_root.parents)

    seen = set()
    for candidate_root in candidates:
        normalized = str(candidate_root).lower()
        if normalized in seen:
            continue
        seen.add(normalized)

        if (candidate_root / "renderdoc_ui_analysis.py").exists():
            return candidate_root

        candidate = candidate_root / "Examples" / "RenderScene"
        if (candidate / "renderdoc_ui_analysis.py").exists():
            return candidate
    raise RuntimeError(
        "Unable to locate Examples/RenderScene from the RenderDoc script path. "
        "Launch qrenderdoc with the repository script path."
    )


if "pyrenderdoc" in globals():
    builtins.pyrenderdoc = pyrenderdoc


SCRIPT_DIR = resolve_script_dir()
if str(SCRIPT_DIR) not in sys.path:
    sys.path.insert(0, str(SCRIPT_DIR))

from renderdoc_ui_analysis import (  # noqa: E402
    ActionRecord,
    ReportWriter,
    collect_action_records,
    collect_resource_records,
    describe_resource_record,
    find_pass_work_records,
    find_resources_by_substring,
    find_scope_records,
    format_flags,
    inspect_action_resources,
    resource_id_to_name,
    run_ui_script,
    summarize_event_ids,
)


REPORT_SUFFIX = "_analysis_report.txt"
K_A_SHELL_PASSES = (
    "VsmPageRequestGeneratorPass",
    "VsmInvalidationPass",
    "VsmPageManagementPass",
    "VsmPageFlagPropagationPass",
    "VsmPageInitializationPass",
    "VsmShadowRasterizerPass",
    "VsmStaticDynamicMergePass",
    "VsmHzbUpdaterPass",
    "VsmProjectionPass",
)
STAGE15_MASKS = {
    "diagnostic_shadow_mask": "VsmProjectionPass.ShadowMask",
    "directional_shadow_mask": "VsmProjectionPass.DirectionalShadowMask",
}


def append_check(
    report: ReportWriter,
    status: str,
    name: str,
    details: str,
    failures: List[str],
) -> None:
    report.append("[{}] {} | {}".format(status, name, details))
    if status == "FAIL":
        failures.append(name)


def build_shell_summary(
    action_records: Sequence[ActionRecord],
    report: ReportWriter,
    failures: List[str],
) -> None:
    report.append("Automated shell checks")
    for pass_name in K_A_SHELL_PASSES:
        scopes = find_scope_records(action_records, pass_name)
        work = find_pass_work_records(action_records, pass_name)

        append_check(
            report,
            "PASS" if scopes else "FAIL",
            "{} scope".format(pass_name),
            "scope_events={}".format(summarize_event_ids(scopes)),
            failures,
        )
        append_check(
            report,
            "PASS" if work else "FAIL",
            "{} work".format(pass_name),
            "work_events={}".format(summarize_event_ids(work)),
            failures,
        )


def build_stage15_summary(
    controller,
    action_records: Sequence[ActionRecord],
    report: ReportWriter,
    failures: List[str],
) -> None:
    resource_records = collect_resource_records(controller)
    resource_names = resource_id_to_name(controller)
    projection_work = find_pass_work_records(action_records, "VsmProjectionPass")
    projection_inspections = [
        (action, inspect_action_resources(controller, resource_names, action))
        for action in projection_work
    ]

    report.blank()
    report.append("Stage 15 publication checks")

    append_check(
        report,
        "PASS" if projection_work else "FAIL",
        "VsmProjectionPass reached",
        "work_events={}".format(summarize_event_ids(projection_work)),
        failures,
    )

    for check_name, resource_snippet in STAGE15_MASKS.items():
        matching_resources = find_resources_by_substring(
            resource_records, resource_snippet
        )
        append_check(
            report,
            "PASS" if matching_resources else "FAIL",
            "{} resource present".format(check_name),
            "resources={}".format(
                "; ".join(
                    describe_resource_record(resource)
                    for resource in matching_resources[:4]
                )
                if matching_resources
                else "<none>"
            ),
            failures,
        )

        writer_events = [
            action
            for action, details in projection_inspections
            if any(
                resource_snippet.lower() in line.lower()
                for line in details.writable_lines()
            )
        ]
        append_check(
            report,
            "PASS" if writer_events else "FAIL",
            "{} written by VsmProjectionPass".format(check_name),
            "writer_events={}".format(summarize_event_ids(writer_events)),
            failures,
        )

    if projection_inspections:
        report.append("Projection pass evidence")
        for action, details in projection_inspections:
            report.append(
                "event {} [{}] {}".format(
                    action.event_id,
                    format_flags(action.flags),
                    action.path,
                )
            )
            for line in details.writable_lines():
                if "VsmProjectionPass." in line:
                    report.append("  writes={}".format(line))
            for line in details.compute_ro:
                if "VsmProjectionPass." in line or "VsmShadowRenderer." in line:
                    report.append("  compute_ro={}".format(line))


def build_manual_gates(report: ReportWriter) -> None:
    report.blank()
    report.append("Manual or external gates")
    report.append(
        "[MANUAL] Virtual Shadow Mask sign-off | Replay confirms Stage 15 mask "
        "publication, but the actual debug-image checkpoint still requires a "
        "manual engine rerun and review."
    )
    report.append(
        "[BLOCKED] Analytic bridge GPU gate | "
        "VsmShadowRendererBridgeGpuTest."
        "ExecutePreparedViewShellMatchesAnalyticFloorShadowClassificationForTwoBoxes "
        "is still outside this RenderDoc replay workflow."
    )
    report.append(
        "[BLOCKED] Live-run stabilization | This replay-safe capture proves the "
        "analysis workflow against one late-frame capture, but it does not prove "
        "the live engine is stable enough to rerun the manual checkpoint."
    )


def build_report(
    controller, report: ReportWriter, capture_path: Path, report_path: Path
) -> None:
    action_records = collect_action_records(controller)
    failures: List[str] = []

    report.append("analysis_profile=k_a_baseline")
    report.append("capture_path={}".format(capture_path))
    report.append("report_path={}".format(report_path))
    report.append("root_action_count={}".format(len(controller.GetRootActions())))
    report.append("action_count={}".format(len(action_records)))
    report.blank()

    build_shell_summary(action_records, report, failures)
    build_stage15_summary(controller, action_records, report, failures)
    build_manual_gates(report)

    report.blank()
    report.append("Verdict")
    report.append(
        "automation_result={}".format(
            "fail" if failures else "pass"
        )
    )
    if failures:
        report.append("automation_failures={}".format(", ".join(failures)))
        report.append(
            "baseline_capture_verdict=insufficient_k_a_automation_evidence"
        )
    else:
        report.append("automation_failures=<none>")
        report.append(
            "baseline_capture_verdict=valid_automated_k_a_evidence"
        )
    report.append("phase_k_a_status_recommendation=in_progress")
    report.append(
        "phase_k_a_status_reason=manual_virtual_shadow_mask_signoff_and_external_"
        "bridge_validation_still_pending"
    )
    report.flush()


RUN_EXIT_CODE = run_ui_script(REPORT_SUFFIX, build_report)
