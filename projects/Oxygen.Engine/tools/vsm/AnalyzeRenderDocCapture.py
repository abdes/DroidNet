r"""VSM-local RenderDoc baseline capture analysis."""

import builtins
import sys
from pathlib import Path


def _script_path():
    current_file = globals().get("__file__")
    if current_file:
        return Path(current_file).resolve()

    # RenderDoc UI Python may not define `__file__` for entry scripts.
    argv = getattr(sys, "argv", None) or getattr(sys, "orig_argv", None) or []
    for arg in argv:
        try:
            candidate = Path(arg)
        except TypeError:
            continue
        if candidate.name == "AnalyzeRenderDocCapture.py":
            return candidate.resolve()

    fallback = Path.cwd() / "tools" / "vsm" / "AnalyzeRenderDocCapture.py"
    if fallback.exists():
        return fallback.resolve()
    raise RuntimeError("Unable to resolve AnalyzeRenderDocCapture.py")


SCRIPT_DIR = _script_path().parent
if str(SCRIPT_DIR.parents[0] / "shadows") not in sys.path:
    sys.path.insert(0, str(SCRIPT_DIR.parents[0] / "shadows"))

if "pyrenderdoc" in globals():
    builtins.pyrenderdoc = pyrenderdoc

from renderdoc_ui_analysis import (  # noqa: E402
    ReportWriter,
    collect_action_records,
    find_scope_records,
    resource_id_to_name,
    run_ui_script,
    summarize_event_ids,
)


REPORT_SUFFIX = "_analysis_report.txt"
VSM_SHELL_PASSES = [
    "VsmPageRequestGeneratorPass",
    "VsmInvalidationPass",
    "VsmPageManagementPass",
    "VsmPageFlagPropagationPass",
    "VsmPageInitializationPass",
    "VsmShadowRasterizerPass",
    "VsmStaticDynamicMergePass",
    "VsmHzbUpdaterPass",
    "VsmProjectionPass",
]


def build_report(controller, report, capture_path, report_path):
    action_records = collect_action_records(controller)
    resource_names = resource_id_to_name(controller)
    present_passes = []
    for pass_name in VSM_SHELL_PASSES:
        if find_scope_records(action_records, pass_name):
            present_passes.append(pass_name)

    projection_scopes = find_scope_records(action_records, "VsmProjectionPass")
    projection_reached = bool(projection_scopes)
    resource_name_values = set(resource_names.values())
    has_directional_mask = any(
        "VsmProjectionPass.DirectionalShadowMask" in name
        for name in resource_name_values
    )
    has_shadow_mask = any(
        "VsmProjectionPass.ShadowMask" in name for name in resource_name_values
    )

    report.append("analysis_profile=vsm_capture_baseline")
    report.append("capture_path={}".format(capture_path))
    report.append("report_path={}".format(report_path))
    report.append("vsm_shell_passes_present={}".format(",".join(present_passes)))
    report.append(
        "vsm_shell_passes_missing={}".format(
            ",".join([name for name in VSM_SHELL_PASSES if name not in present_passes])
        )
    )
    report.append("projection_reached={}".format(str(projection_reached).lower()))
    report.append("directional_mask_present={}".format(str(has_directional_mask).lower()))
    report.append("shadow_mask_present={}".format(str(has_shadow_mask).lower()))
    report.append(
        "projection_scope_events={}".format(summarize_event_ids(projection_scopes))
    )
    report.flush()


RUN_EXIT_CODE = run_ui_script(REPORT_SUFFIX, build_report)
