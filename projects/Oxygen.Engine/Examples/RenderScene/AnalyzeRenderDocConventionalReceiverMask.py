r"""RenderDoc UI proof script for CSM-3 receiver-mask validation.

Run only inside RenderDoc UI:

PowerShell:
    $env:OXYGEN_RENDERDOC_REPORT_PATH = 'H:/path/receiver_mask_report.txt'
    & 'C:/Program Files/RenderDoc/qrenderdoc.exe' --ui-python `
        'H:/projects/DroidNet/projects/Oxygen.Engine/Examples/RenderScene/AnalyzeRenderDocConventionalReceiverMask.py' `
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
    ReportWriter,
    collect_action_records,
    find_pass_work_records,
    find_scope_records,
    format_flags,
    renderdoc_module,
    resource_id_to_name,
    run_ui_script,
    summarize_event_ids,
)


REPORT_SUFFIX = "_receiver_mask_report.txt"
PASS_NAME = "ConventionalShadowReceiverMaskPass"
SUMMARY_STRUCT = struct.Struct("<12f12I")
UINT32_STRUCT = struct.Struct("<I")
FLAG_VALID = 1 << 0
FLAG_EMPTY = 1 << 1
FLAG_HIERARCHY_BUILT = 1 << 2


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


def find_binding_for_tokens(bindings, required_tokens):
    for binding in bindings:
        lowered = binding["name"].lower()
        if all(token in lowered for token in required_tokens):
            return binding
    return None


def find_buffer_binding(controller, resource_names, work_records, required_tokens):
    for action in reversed(work_records):
        read_only, read_write = collect_compute_bindings(
            controller, resource_names, action.event_id
        )
        for binding_source, bindings in (
            ("rw", read_write),
            ("ro", read_only),
        ):
            binding = find_binding_for_tokens(bindings, required_tokens)
            if binding is not None:
                return {
                    "binding": binding,
                    "binding_source": binding_source,
                    "event_id": action.event_id,
                }
    return None


def flag_labels(flags):
    labels = []
    if flags & FLAG_VALID:
        labels.append("VALID")
    if flags & FLAG_EMPTY:
        labels.append("EMPTY")
    if flags & FLAG_HIERARCHY_BUILT:
        labels.append("HIERARCHY_BUILT")
    return ",".join(labels) if labels else "NONE"


def read_summary_records(controller, resource_id):
    raw = controller.GetBufferData(resource_id, 0, 0)
    if raw is None:
        return []

    byte_count = len(raw) - (len(raw) % SUMMARY_STRUCT.size)
    records = []
    for offset in range(0, byte_count, SUMMARY_STRUCT.size):
        unpacked = SUMMARY_STRUCT.unpack_from(raw, offset)
        records.append(
            {
                "target_array_slice": unpacked[12],
                "flags": unpacked[13],
                "sample_count": unpacked[14],
                "occupied_tile_count": unpacked[15],
                "hierarchy_occupied_tile_count": unpacked[16],
                "base_tile_resolution": unpacked[17],
                "hierarchy_tile_resolution": unpacked[18],
                "dilation_tile_radius": unpacked[19],
                "hierarchy_reduction": unpacked[20],
            }
        )
    return records


def read_uint32_buffer(controller, resource_id):
    raw = controller.GetBufferData(resource_id, 0, 0)
    if raw is None:
        return []

    byte_count = len(raw) - (len(raw) % UINT32_STRUCT.size)
    return [
        UINT32_STRUCT.unpack_from(raw, offset)[0]
        for offset in range(0, byte_count, UINT32_STRUCT.size)
    ]


def gpu_duration_lookup(controller):
    rd = renderdoc_module()
    counter_results = controller.FetchCounters([rd.GPUCounter.EventGPUDuration])
    return {result.eventId: result.value.d * 1000.0 for result in counter_results}


def build_report(
    controller, report: ReportWriter, capture_path: Path, report_path: Path
) -> None:
    rd = renderdoc_module()
    action_records = collect_action_records(controller)
    scope_records = find_scope_records(action_records, PASS_NAME)
    work_records = find_pass_work_records(action_records, PASS_NAME)
    resource_names = resource_id_to_name(controller)
    durations = gpu_duration_lookup(controller)

    dispatch_records = [
        record for record in work_records if record.flags & rd.ActionFlags.Dispatch
    ]
    copy_records = [record for record in work_records if record.flags & rd.ActionFlags.Copy]

    if len(dispatch_records) != 5:
        raise RuntimeError(
            "Expected exactly five dispatch work events for {} but found {}".format(
                PASS_NAME, len(dispatch_records)
            )
        )

    clear_event = dispatch_records[0]
    analyze_event = dispatch_records[1]
    dilate_event = dispatch_records[2]
    hierarchy_event = dispatch_records[3]
    finalize_event = dispatch_records[4]

    summary_binding = find_buffer_binding(
        controller, resource_names, work_records, (PASS_NAME.lower(), "summary")
    )
    base_mask_binding = find_buffer_binding(
        controller, resource_names, work_records, (PASS_NAME.lower(), "basemask")
    )
    hierarchy_mask_binding = find_buffer_binding(
        controller,
        resource_names,
        work_records,
        (PASS_NAME.lower(), "hierarchymask"),
    )

    if summary_binding is None:
        raise RuntimeError("No summary buffer binding found for {}".format(PASS_NAME))
    if base_mask_binding is None:
        raise RuntimeError("No base-mask binding found for {}".format(PASS_NAME))
    if hierarchy_mask_binding is None:
        raise RuntimeError(
            "No hierarchy-mask binding found for {}".format(PASS_NAME)
        )

    summary_records = read_summary_records(
        controller, summary_binding["binding"]["id"]
    )
    base_mask = read_uint32_buffer(controller, base_mask_binding["binding"]["id"])
    hierarchy_mask = read_uint32_buffer(
        controller, hierarchy_mask_binding["binding"]["id"]
    )

    if not summary_records:
        raise RuntimeError("Receiver-mask summary buffer was empty")

    expected_base_entries = sum(
        record["base_tile_resolution"] * record["base_tile_resolution"]
        for record in summary_records
    )
    expected_hierarchy_entries = sum(
        record["hierarchy_tile_resolution"] * record["hierarchy_tile_resolution"]
        for record in summary_records
    )
    if len(base_mask) != expected_base_entries:
        raise RuntimeError(
            "Base-mask buffer length mismatch: expected {} uints, found {}".format(
                expected_base_entries, len(base_mask)
            )
        )
    if len(hierarchy_mask) != expected_hierarchy_entries:
        raise RuntimeError(
            "Hierarchy-mask buffer length mismatch: expected {} uints, found {}".format(
                expected_hierarchy_entries, len(hierarchy_mask)
            )
        )

    valid_job_count = 0
    sampled_job_count = 0
    sparse_job_count = 0
    empty_job_count = 0
    hierarchy_built_job_count = 0
    total_occupied_tile_count = 0
    total_full_tile_count = 0
    total_hierarchy_occupied_tile_count = 0
    total_full_hierarchy_tile_count = 0
    total_sampled_job_occupied_tile_count = 0
    total_sampled_job_full_tile_count = 0
    total_sampled_job_hierarchy_occupied_tile_count = 0
    total_sampled_job_full_hierarchy_tile_count = 0
    sampled_occupied_tile_ratios = []
    sampled_hierarchy_tile_ratios = []
    max_dilation_tile_radius = 0
    max_base_tile_resolution = 0
    max_hierarchy_tile_resolution = 0
    per_job_details = []

    base_offset = 0
    hierarchy_offset = 0
    for job_index, record in enumerate(summary_records):
        base_tiles_per_job = (
            record["base_tile_resolution"] * record["base_tile_resolution"]
        )
        hierarchy_tiles_per_job = (
            record["hierarchy_tile_resolution"]
            * record["hierarchy_tile_resolution"]
        )

        base_slice = base_mask[base_offset : base_offset + base_tiles_per_job]
        hierarchy_slice = hierarchy_mask[
            hierarchy_offset : hierarchy_offset + hierarchy_tiles_per_job
        ]
        base_offset += base_tiles_per_job
        hierarchy_offset += hierarchy_tiles_per_job

        occupied_tile_count = sum(1 for value in base_slice if value != 0)
        hierarchy_occupied_tile_count = sum(
            1 for value in hierarchy_slice if value != 0
        )

        if occupied_tile_count != record["occupied_tile_count"]:
            raise RuntimeError(
                "Job {} occupied-tile count mismatch: summary={} raw={}".format(
                    job_index, record["occupied_tile_count"], occupied_tile_count
                )
            )
        if hierarchy_occupied_tile_count != record["hierarchy_occupied_tile_count"]:
            raise RuntimeError(
                "Job {} hierarchy occupied-tile count mismatch: summary={} raw={}".format(
                    job_index,
                    record["hierarchy_occupied_tile_count"],
                    hierarchy_occupied_tile_count,
                )
            )

        occupied_tile_ratio = (
            float(occupied_tile_count) / float(base_tiles_per_job)
            if base_tiles_per_job > 0
            else 0.0
        )
        hierarchy_occupied_tile_ratio = (
            float(hierarchy_occupied_tile_count) / float(hierarchy_tiles_per_job)
            if hierarchy_tiles_per_job > 0
            else 0.0
        )

        total_occupied_tile_count += occupied_tile_count
        total_full_tile_count += base_tiles_per_job
        total_hierarchy_occupied_tile_count += hierarchy_occupied_tile_count
        total_full_hierarchy_tile_count += hierarchy_tiles_per_job
        max_dilation_tile_radius = max(
            max_dilation_tile_radius, record["dilation_tile_radius"]
        )
        max_base_tile_resolution = max(
            max_base_tile_resolution, record["base_tile_resolution"]
        )
        max_hierarchy_tile_resolution = max(
            max_hierarchy_tile_resolution, record["hierarchy_tile_resolution"]
        )

        if record["flags"] & FLAG_VALID:
            valid_job_count += 1
        if record["flags"] & FLAG_EMPTY:
            empty_job_count += 1
        if record["flags"] & FLAG_HIERARCHY_BUILT:
            hierarchy_built_job_count += 1
        if record["sample_count"] > 0:
            sampled_job_count += 1
            total_sampled_job_occupied_tile_count += occupied_tile_count
            total_sampled_job_full_tile_count += base_tiles_per_job
            total_sampled_job_hierarchy_occupied_tile_count += (
                hierarchy_occupied_tile_count
            )
            total_sampled_job_full_hierarchy_tile_count += hierarchy_tiles_per_job
            sampled_occupied_tile_ratios.append(occupied_tile_ratio)
            sampled_hierarchy_tile_ratios.append(hierarchy_occupied_tile_ratio)
            if occupied_tile_count < base_tiles_per_job:
                sparse_job_count += 1

        per_job_details.append(
            {
                "job_index": job_index,
                "slice": record["target_array_slice"],
                "flags": flag_labels(record["flags"]),
                "sample_count": record["sample_count"],
                "occupied_tile_count": occupied_tile_count,
                "full_tile_count": base_tiles_per_job,
                "occupied_tile_ratio": occupied_tile_ratio,
                "hierarchy_occupied_tile_count": hierarchy_occupied_tile_count,
                "hierarchy_full_tile_count": hierarchy_tiles_per_job,
                "hierarchy_occupied_tile_ratio": hierarchy_occupied_tile_ratio,
                "base_tile_resolution": record["base_tile_resolution"],
                "hierarchy_tile_resolution": record["hierarchy_tile_resolution"],
                "dilation_tile_radius": record["dilation_tile_radius"],
                "hierarchy_reduction": record["hierarchy_reduction"],
            }
        )

    report.append("analysis_profile=conventional_shadow_receiver_mask")
    report.append("capture_path={}".format(capture_path))
    report.append("report_path={}".format(report_path))
    report.append("pass_name={}".format(PASS_NAME))
    report.append("scope_events={}".format(summarize_event_ids(scope_records)))
    report.append("work_events={}".format(summarize_event_ids(work_records)))
    report.append("work_event_count={}".format(len(work_records)))
    report.append("copy_event_count={}".format(len(copy_records)))
    report.append("dispatch_event_count={}".format(len(dispatch_records)))
    report.append("copy_events={}".format(summarize_event_ids(copy_records)))
    report.append("clear_event_id={}".format(clear_event.event_id))
    report.append("analyze_event_id={}".format(analyze_event.event_id))
    report.append("dilate_event_id={}".format(dilate_event.event_id))
    report.append("hierarchy_event_id={}".format(hierarchy_event.event_id))
    report.append("finalize_event_id={}".format(finalize_event.event_id))
    report.append(
        "clear_event_gpu_duration_ms={:.6f}".format(
            durations.get(clear_event.event_id, 0.0)
        )
    )
    report.append(
        "analyze_event_gpu_duration_ms={:.6f}".format(
            durations.get(analyze_event.event_id, 0.0)
        )
    )
    report.append(
        "dilate_event_gpu_duration_ms={:.6f}".format(
            durations.get(dilate_event.event_id, 0.0)
        )
    )
    report.append(
        "hierarchy_event_gpu_duration_ms={:.6f}".format(
            durations.get(hierarchy_event.event_id, 0.0)
        )
    )
    report.append(
        "finalize_event_gpu_duration_ms={:.6f}".format(
            durations.get(finalize_event.event_id, 0.0)
        )
    )
    report.append("summary_binding_name={}".format(summary_binding["binding"]["name"]))
    report.append(
        "summary_binding_source={}".format(summary_binding["binding_source"])
    )
    report.append(
        "summary_binding_event_id={}".format(summary_binding["event_id"])
    )
    report.append(
        "base_mask_binding_name={}".format(base_mask_binding["binding"]["name"])
    )
    report.append(
        "base_mask_binding_source={}".format(base_mask_binding["binding_source"])
    )
    report.append(
        "base_mask_binding_event_id={}".format(base_mask_binding["event_id"])
    )
    report.append(
        "hierarchy_mask_binding_name={}".format(
            hierarchy_mask_binding["binding"]["name"]
        )
    )
    report.append(
        "hierarchy_mask_binding_source={}".format(
            hierarchy_mask_binding["binding_source"]
        )
    )
    report.append(
        "hierarchy_mask_binding_event_id={}".format(
            hierarchy_mask_binding["event_id"]
        )
    )
    report.append("job_count={}".format(len(summary_records)))
    report.append("valid_job_count={}".format(valid_job_count))
    report.append("sampled_job_count={}".format(sampled_job_count))
    report.append("sparse_job_count={}".format(sparse_job_count))
    report.append("empty_job_count={}".format(empty_job_count))
    report.append("hierarchy_built_job_count={}".format(hierarchy_built_job_count))
    report.append("total_occupied_tile_count={}".format(total_occupied_tile_count))
    report.append("total_full_tile_count={}".format(total_full_tile_count))
    report.append(
        "aggregate_occupied_tile_ratio={:.6f}".format(
            float(total_occupied_tile_count) / float(total_full_tile_count)
            if total_full_tile_count > 0
            else 0.0
        )
    )
    report.append(
        "total_hierarchy_occupied_tile_count={}".format(
            total_hierarchy_occupied_tile_count
        )
    )
    report.append(
        "total_full_hierarchy_tile_count={}".format(
            total_full_hierarchy_tile_count
        )
    )
    report.append(
        "aggregate_hierarchy_occupied_tile_ratio={:.6f}".format(
            float(total_hierarchy_occupied_tile_count)
            / float(total_full_hierarchy_tile_count)
            if total_full_hierarchy_tile_count > 0
            else 0.0
        )
    )
    report.append(
        "total_sampled_job_occupied_tile_count={}".format(
            total_sampled_job_occupied_tile_count
        )
    )
    report.append(
        "total_sampled_job_full_tile_count={}".format(
            total_sampled_job_full_tile_count
        )
    )
    report.append(
        "sampled_aggregate_occupied_tile_ratio={:.6f}".format(
            float(total_sampled_job_occupied_tile_count)
            / float(total_sampled_job_full_tile_count)
            if total_sampled_job_full_tile_count > 0
            else 0.0
        )
    )
    report.append(
        "total_sampled_job_hierarchy_occupied_tile_count={}".format(
            total_sampled_job_hierarchy_occupied_tile_count
        )
    )
    report.append(
        "total_sampled_job_full_hierarchy_tile_count={}".format(
            total_sampled_job_full_hierarchy_tile_count
        )
    )
    report.append(
        "sampled_aggregate_hierarchy_occupied_tile_ratio={:.6f}".format(
            float(total_sampled_job_hierarchy_occupied_tile_count)
            / float(total_sampled_job_full_hierarchy_tile_count)
            if total_sampled_job_full_hierarchy_tile_count > 0
            else 0.0
        )
    )
    report.append(
        "min_sampled_occupied_tile_ratio={:.6f}".format(
            min(sampled_occupied_tile_ratios) if sampled_occupied_tile_ratios else 0.0
        )
    )
    report.append(
        "max_sampled_occupied_tile_ratio={:.6f}".format(
            max(sampled_occupied_tile_ratios) if sampled_occupied_tile_ratios else 0.0
        )
    )
    report.append(
        "min_sampled_hierarchy_occupied_tile_ratio={:.6f}".format(
            min(sampled_hierarchy_tile_ratios)
            if sampled_hierarchy_tile_ratios
            else 0.0
        )
    )
    report.append(
        "max_sampled_hierarchy_occupied_tile_ratio={:.6f}".format(
            max(sampled_hierarchy_tile_ratios)
            if sampled_hierarchy_tile_ratios
            else 0.0
        )
    )
    report.append("max_dilation_tile_radius={}".format(max_dilation_tile_radius))
    report.append("max_base_tile_resolution={}".format(max_base_tile_resolution))
    report.append(
        "max_hierarchy_tile_resolution={}".format(max_hierarchy_tile_resolution)
    )
    report.append("validated_raw_mask_counts=true")
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
    for detail in per_job_details:
        report.append(
            "job={}|slice={}|flags={}|sample_count={}|"
            "occupied_tile_count={}|full_tile_count={}|occupied_tile_ratio={:.6f}|"
            "hierarchy_occupied_tile_count={}|hierarchy_full_tile_count={}|"
            "hierarchy_occupied_tile_ratio={:.6f}|base_tile_resolution={}|"
            "hierarchy_tile_resolution={}|dilation_tile_radius={}|"
            "hierarchy_reduction={}".format(
                detail["job_index"],
                detail["slice"],
                detail["flags"],
                detail["sample_count"],
                detail["occupied_tile_count"],
                detail["full_tile_count"],
                detail["occupied_tile_ratio"],
                detail["hierarchy_occupied_tile_count"],
                detail["hierarchy_full_tile_count"],
                detail["hierarchy_occupied_tile_ratio"],
                detail["base_tile_resolution"],
                detail["hierarchy_tile_resolution"],
                detail["dilation_tile_radius"],
                detail["hierarchy_reduction"],
            )
        )
    report.flush()


RUN_EXIT_CODE = run_ui_script(REPORT_SUFFIX, build_report)
