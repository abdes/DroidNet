r"""VSM-local Stage 15 mask analysis."""

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
        if candidate.name == "AnalyzeRenderDocStage15Masks.py":
            return candidate.resolve()

    fallback = Path.cwd() / "tools" / "vsm" / "AnalyzeRenderDocStage15Masks.py"
    if fallback.exists():
        return fallback.resolve()
    raise RuntimeError("Unable to resolve AnalyzeRenderDocStage15Masks.py")


SCRIPT_DIR = _script_path().parent
if str(SCRIPT_DIR.parents[0] / "shadows") not in sys.path:
    sys.path.insert(0, str(SCRIPT_DIR.parents[0] / "shadows"))

if "pyrenderdoc" in globals():
    builtins.pyrenderdoc = pyrenderdoc

from renderdoc_ui_analysis import (  # noqa: E402
    ReportWriter,
    collect_action_records,
    find_pass_work_records,
    find_scope_records,
    renderdoc_module,
    resource_id_to_name,
    run_ui_script,
    summarize_event_ids,
)


REPORT_SUFFIX = "_stage15_masks_report.txt"
PASS_NAME = "VsmProjectionPass"


def _binding_dict(resource_names, used_descriptor):
    descriptor = used_descriptor.descriptor
    resource_id = descriptor.resource
    return {
        "id": resource_id,
        "name": resource_names.get(str(resource_id), str(resource_id)),
        "stage": used_descriptor.access.stage,
        "bind": used_descriptor.access.index,
        "array": used_descriptor.access.arrayElement,
        "type": used_descriptor.access.type,
    }


def _collect_compute_bindings(controller, resource_names, event_id):
    rd = renderdoc_module()
    controller.SetFrameEvent(event_id, True)
    state = controller.GetPipelineState()
    read_only = [
        _binding_dict(resource_names, descriptor)
        for descriptor in state.GetReadOnlyResources(rd.ShaderStage.Compute, True)
    ]
    read_write = [
        _binding_dict(resource_names, descriptor)
        for descriptor in state.GetReadWriteResources(rd.ShaderStage.Compute, True)
    ]
    return read_only, read_write


def build_report(controller, report, capture_path, report_path):
    action_records = collect_action_records(controller)
    scope_records = find_scope_records(action_records, PASS_NAME)
    work_records = find_pass_work_records(action_records, PASS_NAME)
    resource_names = resource_id_to_name(controller)

    if not work_records:
        raise RuntimeError("No work events found for {}".format(PASS_NAME))

    directional_writers = []
    shadow_writers = []
    for action in work_records:
        read_only, read_write = _collect_compute_bindings(
            controller, resource_names, action.event_id
        )
        del read_only
        for binding in read_write:
            name = binding["name"]
            if "VsmProjectionPass.DirectionalShadowMask" in name:
                directional_writers.append((action.event_id, name))
            if "VsmProjectionPass.ShadowMask" in name:
                shadow_writers.append((action.event_id, name))

    report.append("analysis_profile=vsm_stage15_masks")
    report.append("capture_path={}".format(capture_path))
    report.append("report_path={}".format(report_path))
    report.append("pass_name={}".format(PASS_NAME))
    report.append("scope_events={}".format(summarize_event_ids(scope_records)))
    report.append("work_events={}".format(summarize_event_ids(work_records)))
    report.append(
        "directional_mask_writer_count={}".format(len(directional_writers))
    )
    report.append("shadow_mask_writer_count={}".format(len(shadow_writers)))
    report.blank()
    report.append("directional_mask_writers:")
    for event_id, name in directional_writers:
        report.append("{}|{}".format(event_id, name))
    report.blank()
    report.append("shadow_mask_writers:")
    for event_id, name in shadow_writers:
        report.append("{}|{}".format(event_id, name))
    report.flush()


RUN_EXIT_CODE = run_ui_script(REPORT_SUFFIX, build_report)
