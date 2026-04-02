r"""RenderDoc UI proof script for CSM-2 receiver-analysis validation.

Run only inside RenderDoc UI:

PowerShell:
    $env:OXYGEN_RENDERDOC_REPORT_PATH = 'H:/path/receiver_analysis_report.txt'
    & 'C:/Program Files/RenderDoc/qrenderdoc.exe' --ui-python `
        'H:/projects/DroidNet/projects/Oxygen.Engine/tools/csm/AnalyzeRenderDocConventionalReceiverAnalysis.py' `
        'H:/path/capture.rdc'
"""

import builtins
import struct
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

    capture_context = globals().get("pyrenderdoc")
    get_capture_filename = getattr(capture_context, "GetCaptureFilename", None)
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

        candidate = candidate_root / "tools" / "csm"
        if (candidate / "renderdoc_ui_analysis.py").exists():
            return candidate

        candidate = candidate_root / "Examples" / "RenderScene"
        if (candidate / "renderdoc_ui_analysis.py").exists():
            return candidate

    raise RuntimeError(
        "Unable to locate tools/csm from the RenderDoc script path. "
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
    find_pass_work_records,
    find_scope_records,
    format_flags,
    renderdoc_module,
    resource_id_to_name,
    run_ui_script,
    safe_getattr,
    summarize_event_ids,
)


REPORT_SUFFIX = "_receiver_analysis_report.txt"
PASS_NAME = "ConventionalShadowReceiverAnalysisPass"
ANALYSIS_STRUCT = struct.Struct("<21f3I")
FLAG_VALID = 1 << 0
FLAG_EMPTY = 1 << 1
FLAG_FALLBACK = 1 << 2


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


def find_analysis_binding(controller, resource_names, event_id):
    rd = renderdoc_module()
    controller.SetFrameEvent(event_id, True)
    state = controller.GetPipelineState()
    read_write = [
        binding_dict(resource_names, descriptor)
        for descriptor in state.GetReadWriteResources(rd.ShaderStage.Compute, True)
    ]
    target_tokens = (
        PASS_NAME.lower(),
        ".analysis",
    )
    for binding in read_write:
        lowered = binding["name"].lower()
        if all(token in lowered for token in target_tokens):
            return binding
    return None


def flag_labels(flags):
    labels = []
    if flags & FLAG_VALID:
        labels.append("VALID")
    if flags & FLAG_EMPTY:
        labels.append("EMPTY")
    if flags & FLAG_FALLBACK:
        labels.append("FALLBACK_TO_LEGACY_RECT")
    return ",".join(labels) if labels else "NONE"


def read_analysis_records(controller, resource_id):
    raw = controller.GetBufferData(resource_id, 0, 0)
    if raw is None:
        return []

    byte_count = len(raw) - (len(raw) % ANALYSIS_STRUCT.size)
    records = []
    for offset in range(0, byte_count, ANALYSIS_STRUCT.size):
        unpacked = ANALYSIS_STRUCT.unpack_from(raw, offset)
        records.append(
            {
                "raw_xy_min_max": unpacked[0:4],
                "raw_depth_and_dilation": unpacked[4:8],
                "full_rect_center_half_extent": unpacked[8:12],
                "legacy_rect_center_half_extent": unpacked[12:16],
                "full_depth_and_area_ratios": unpacked[16:20],
                "full_depth_ratio": unpacked[20],
                "sample_count": unpacked[21],
                "target_array_slice": unpacked[22],
                "flags": unpacked[23],
            }
        )
    return records


def gpu_duration_lookup(controller):
    rd = renderdoc_module()
    counter_results = controller.FetchCounters([rd.GPUCounter.EventGPUDuration])
    return {result.eventId: result.value.d * 1000.0 for result in counter_results}


def build_report(
    controller, report: ReportWriter, capture_path: Path, report_path: Path
) -> None:
    action_records = collect_action_records(controller)
    scope_records = find_scope_records(action_records, PASS_NAME)
    work_records = find_pass_work_records(action_records, PASS_NAME)
    resource_names = resource_id_to_name(controller)
    durations = gpu_duration_lookup(controller)

    report.append("analysis_profile=conventional_shadow_receiver_analysis")
    report.append("capture_path={}".format(capture_path))
    report.append("report_path={}".format(report_path))
    report.append("pass_name={}".format(PASS_NAME))
    report.append("scope_events={}".format(summarize_event_ids(scope_records)))
    report.append("work_events={}".format(summarize_event_ids(work_records)))

    if not work_records:
        raise RuntimeError("No work events found for {}".format(PASS_NAME))

    analysis_event = work_records[-1]
    binding = find_analysis_binding(controller, resource_names, analysis_event.event_id)
    if binding is None:
        raise RuntimeError(
            "No analysis UAV binding found on event {}".format(
                analysis_event.event_id
            )
        )

    records = read_analysis_records(controller, binding["id"])
    sampled_records = [record for record in records if record["sample_count"] > 0]
    valid_records = [record for record in records if record["flags"] & FLAG_VALID]
    full_area_ratios = [
        record["full_depth_and_area_ratios"][2] for record in sampled_records
    ]
    legacy_area_ratios = [
        record["full_depth_and_area_ratios"][3] for record in sampled_records
    ]
    depth_ratios = [record["full_depth_ratio"] for record in sampled_records]
    first_three_sampled = sampled_records[:3]
    first_three_area_ratios = [
        record["full_depth_and_area_ratios"][2] for record in first_three_sampled
    ]

    report.append("analysis_event_id={}".format(analysis_event.event_id))
    report.append(
        "analysis_event_gpu_duration_ms={:.6f}".format(
            durations.get(analysis_event.event_id, 0.0)
        )
    )
    report.append("analysis_binding_name={}".format(binding["name"]))
    report.append("job_count={}".format(len(records)))
    report.append("valid_job_count={}".format(len(valid_records)))
    report.append("sampled_job_count={}".format(len(sampled_records)))
    report.append(
        "min_full_area_ratio={:.6f}".format(
            min(full_area_ratios) if full_area_ratios else 0.0
        )
    )
    report.append(
        "max_full_area_ratio={:.6f}".format(
            max(full_area_ratios) if full_area_ratios else 0.0
        )
    )
    report.append(
        "min_legacy_area_ratio={:.6f}".format(
            min(legacy_area_ratios) if legacy_area_ratios else 0.0
        )
    )
    report.append(
        "max_legacy_area_ratio={:.6f}".format(
            max(legacy_area_ratios) if legacy_area_ratios else 0.0
        )
    )
    report.append(
        "min_full_depth_ratio={:.6f}".format(
            min(depth_ratios) if depth_ratios else 0.0
        )
    )
    report.append(
        "max_full_depth_ratio={:.6f}".format(
            max(depth_ratios) if depth_ratios else 0.0
        )
    )
    report.append(
        "min_first_three_full_area_ratio={:.6f}".format(
            min(first_three_area_ratios) if first_three_area_ratios else 0.0
        )
    )
    report.blank()
    report.append("scope_events_detail:")
    for action in scope_records:
        report.append(
            "{}|{:.6f}|{}|{}".format(
                action.event_id,
                durations.get(action.event_id, 0.0),
                action.name,
                action.path,
            )
        )
    report.blank()
    report.append("work_events_detail:")
    for action in work_records:
        report.append(
            "{}|{:.6f}|{}|{}|{}".format(
                action.event_id,
                durations.get(action.event_id, 0.0),
                format_flags(action.flags),
                action.name,
                action.path,
            )
        )
    report.blank()
    report.append("jobs:")
    for index, record in enumerate(records):
        report.append(
            "job={}|slice={}|flags={}|sample_count={}|"
            "raw_xy_min={:.6f},{:.6f}|raw_xy_max={:.6f},{:.6f}|"
            "raw_depth_min={:.6f}|raw_depth_max={:.6f}|"
            "xy_dilation_margin={:.6f}|depth_dilation_margin={:.6f}|"
            "full_area_ratio={:.6f}|legacy_area_ratio={:.6f}|full_depth_ratio={:.6f}".format(
                index,
                record["target_array_slice"],
                flag_labels(record["flags"]),
                record["sample_count"],
                record["raw_xy_min_max"][0],
                record["raw_xy_min_max"][1],
                record["raw_xy_min_max"][2],
                record["raw_xy_min_max"][3],
                record["raw_depth_and_dilation"][0],
                record["raw_depth_and_dilation"][1],
                record["raw_depth_and_dilation"][2],
                record["raw_depth_and_dilation"][3],
                record["full_depth_and_area_ratios"][2],
                record["full_depth_and_area_ratios"][3],
                record["full_depth_ratio"],
            )
        )
    report.flush()


RUN_EXIT_CODE = run_ui_script(REPORT_SUFFIX, build_report)
