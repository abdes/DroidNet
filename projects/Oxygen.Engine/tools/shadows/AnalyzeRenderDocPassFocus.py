r"""Generic RenderDoc UI pass focus analysis.

Runs only inside RenderDoc UI via:
    qrenderdoc.exe --ui-python tools/shadows/AnalyzeRenderDocPassFocus.py <capture.rdc>
"""

import builtins
import sys
from pathlib import Path


def _script_path():
    current_file = globals().get("__file__")
    if current_file:
        return Path(current_file).resolve()

    # RenderDoc UI Python may not define `__file__` for entry scripts.
    # Do not "simplify" this to an unconditional __file__ access unless
    # qrenderdoc has been verified to provide it.
    argv = getattr(sys, "argv", None) or getattr(sys, "orig_argv", None) or []
    for arg in argv:
        try:
            candidate = Path(arg)
        except TypeError:
            continue
        if candidate.name == "AnalyzeRenderDocPassFocus.py":
            return candidate.resolve()

    fallback = Path.cwd() / "tools" / "shadows" / "AnalyzeRenderDocPassFocus.py"
    if fallback.exists():
        return fallback.resolve()
    raise RuntimeError("Unable to resolve AnalyzeRenderDocPassFocus.py")


SCRIPT_DIR = _script_path().parent
if str(SCRIPT_DIR) not in sys.path:
    sys.path.insert(0, str(SCRIPT_DIR))

if "pyrenderdoc" in globals():
    builtins.pyrenderdoc = pyrenderdoc

from renderdoc_ui_analysis import (  # noqa: E402
    PASS_EVENT_LIMIT_ENV,
    PASS_NAME_ENV,
    DEFAULT_PASS_EVENT_LIMIT,
    ReportWriter,
    collect_action_records,
    find_pass_work_records,
    find_scope_records,
    format_flags,
    get_required_env,
    parse_optional_int_env,
    renderdoc_module,
    resource_id_to_name,
    run_ui_script,
    summarize_event_ids,
)


REPORT_SUFFIX = "_pass_focus_report.txt"
PASS_NAME = get_required_env(PASS_NAME_ENV, "RenderDoc pass name")
PASS_EVENT_LIMIT = parse_optional_int_env(PASS_EVENT_LIMIT_ENV)
if PASS_EVENT_LIMIT is None:
    PASS_EVENT_LIMIT = DEFAULT_PASS_EVENT_LIMIT


def binding_dict(resource_names, used_descriptor):
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


def collect_compute_bindings(controller, resource_names, event_id):
    rd = renderdoc_module()
    controller.SetFrameEvent(event_id, True)
    state = controller.GetPipelineState()
    read_only = [
        binding_dict(resource_names, descriptor)
        for descriptor in state.GetReadOnlyResources(rd.ShaderStage.Compute, True)
    ]
    read_write = [
        binding_dict(resource_names, descriptor)
        for descriptor in state.GetReadWriteResources(rd.ShaderStage.Compute, True)
    ]
    return read_only, read_write


def build_report(controller, report, capture_path, report_path):
    action_records = collect_action_records(controller)
    scope_records = find_scope_records(action_records, PASS_NAME)
    work_records = find_pass_work_records(action_records, PASS_NAME)
    resource_names = resource_id_to_name(controller)

    report.append("analysis_profile=generic_pass_focus")
    report.append("capture_path={}".format(capture_path))
    report.append("report_path={}".format(report_path))
    report.append("pass_name={}".format(PASS_NAME))
    report.append("scope_events={}".format(summarize_event_ids(scope_records)))
    report.append("work_events={}".format(summarize_event_ids(work_records)))

    if not work_records:
        raise RuntimeError("No work events found for {}".format(PASS_NAME))

    limited_work_records = work_records[:PASS_EVENT_LIMIT]
    report.append("inspected_work_event_count={}".format(len(limited_work_records)))
    report.blank()
    report.append("work_events_detail:")
    for action in limited_work_records:
        read_only, read_write = collect_compute_bindings(
            controller, resource_names, action.event_id
        )
        report.append(
            "{}|{}|{}|read_only={}|read_write={}".format(
                action.event_id,
                format_flags(action.flags),
                action.path,
                len(read_only),
                len(read_write),
            )
        )
        for binding in read_only:
            report.append(
                "  ro|{}|slot={}|array={}|name={}".format(
                    binding["type"], binding["bind"], binding["array"], binding["name"]
                )
            )
        for binding in read_write:
            report.append(
                "  rw|{}|slot={}|array={}|name={}".format(
                    binding["type"], binding["bind"], binding["array"], binding["name"]
                )
            )
    report.flush()


RUN_EXIT_CODE = run_ui_script(REPORT_SUFFIX, build_report)
