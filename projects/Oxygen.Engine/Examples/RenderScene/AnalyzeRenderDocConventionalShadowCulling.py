r"""RenderDoc UI proof script for CSM-4 conventional shadow caster culling.

Run only inside RenderDoc UI:

PowerShell:
    $env:OXYGEN_RENDERDOC_REPORT_PATH = 'H:/path/caster_culling_report.txt'
    & 'C:/Program Files/RenderDoc/qrenderdoc.exe' --ui-python `
        'H:/projects/DroidNet/projects/Oxygen.Engine/Examples/RenderScene/AnalyzeRenderDocConventionalShadowCulling.py' `
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


REPORT_SUFFIX = "_caster_culling_report.txt"
PASS_NAME = "ConventionalShadowCasterCullingPass"
PARTITION_STRUCT = struct.Struct("<8I")
COMMAND_STRUCT = struct.Struct("<5I")
SUMMARY_STRUCT = struct.Struct("<12f12I")
FLAG_VALID = 1 << 0


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
        for binding_source, bindings in (("rw", read_write), ("ro", read_only)):
            binding = find_binding_for_tokens(bindings, required_tokens)
            if binding is not None:
                return {
                    "binding": binding,
                    "binding_source": binding_source,
                    "event_id": action.event_id,
                }
    return None


def read_partition_records(controller, resource_id):
    raw = controller.GetBufferData(resource_id, 0, 0)
    if raw is None:
        return []

    byte_count = len(raw) - (len(raw) % PARTITION_STRUCT.size)
    records = []
    for offset in range(0, byte_count, PARTITION_STRUCT.size):
        unpacked = PARTITION_STRUCT.unpack_from(raw, offset)
        records.append(
            {
                "record_begin": unpacked[0],
                "record_count": unpacked[1],
                "command_uav_index": unpacked[2],
                "count_uav_index": unpacked[3],
                "max_commands_per_job": unpacked[4],
                "partition_index": unpacked[5],
                "pass_mask": unpacked[6],
            }
        )
    return records


def read_uint32_buffer(controller, resource_id):
    raw = controller.GetBufferData(resource_id, 0, 0)
    if raw is None:
        return []
    byte_count = len(raw) - (len(raw) % 4)
    return [
        int.from_bytes(raw[offset : offset + 4], byteorder="little", signed=False)
        for offset in range(0, byte_count, 4)
    ]


def read_command_buffer(controller, resource_id):
    raw = controller.GetBufferData(resource_id, 0, 0)
    if raw is None:
        return []

    byte_count = len(raw) - (len(raw) % COMMAND_STRUCT.size)
    commands = []
    for offset in range(0, byte_count, COMMAND_STRUCT.size):
        unpacked = COMMAND_STRUCT.unpack_from(raw, offset)
        commands.append(
            {
                "draw_index": unpacked[0],
                "vertex_count_per_instance": unpacked[1],
                "instance_count": unpacked[2],
                "start_vertex_location": unpacked[3],
                "start_instance_location": unpacked[4],
            }
        )
    return commands


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
                "flags": unpacked[13],
                "sample_count": unpacked[14],
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
    rd = renderdoc_module()
    action_records = collect_action_records(controller)
    scope_records = find_scope_records(action_records, PASS_NAME)
    work_records = find_pass_work_records(action_records, PASS_NAME)
    resource_names = resource_id_to_name(controller)
    durations = gpu_duration_lookup(controller)

    dispatch_records = [
        record for record in work_records if record.flags & rd.ActionFlags.Dispatch
    ]
    if not dispatch_records:
        raise RuntimeError("No dispatch work events found for {}".format(PASS_NAME))

    partition_binding = find_buffer_binding(
        controller, resource_names, work_records, (PASS_NAME.lower(), "partitions")
    )
    if partition_binding is None:
        raise RuntimeError("No partition buffer binding found for {}".format(PASS_NAME))

    summary_binding = find_buffer_binding(
        controller,
        resource_names,
        work_records,
        ("conventionalshadowreceivermaskpass", "summary"),
    )

    partition_records = read_partition_records(
        controller, partition_binding["binding"]["id"]
    )
    if not partition_records:
        raise RuntimeError("Partition buffer was empty for {}".format(PASS_NAME))
    if len(partition_records) != len(dispatch_records):
        raise RuntimeError(
            "Dispatch/partition mismatch for {}: dispatches={} partitions={}".format(
                PASS_NAME, len(dispatch_records), len(partition_records)
            )
        )

    summary_records = []
    if summary_binding is not None:
        summary_records = read_summary_records(controller, summary_binding["binding"]["id"])

    partition_details = []
    job_totals = []
    job_unique_draws = []
    total_input_draw_records = 0
    total_emitted_draws = 0

    for dispatch_index, action in enumerate(dispatch_records):
        read_only, read_write = collect_compute_bindings(
            controller, resource_names, action.event_id
        )
        count_binding = find_binding_for_tokens(
            read_write, (PASS_NAME.lower(), "commandcounts")
        )
        command_binding = find_binding_for_tokens(
            read_write, (PASS_NAME.lower(), "indirectcommands")
        )
        if count_binding is None:
            raise RuntimeError(
                "No command-count binding found at dispatch event {}".format(
                    action.event_id
                )
            )
        if command_binding is None:
            raise RuntimeError(
                "No indirect-command binding found at dispatch event {}".format(
                    action.event_id
                )
            )

        partition = partition_records[dispatch_index]
        counts = read_uint32_buffer(controller, count_binding["id"])
        if not counts:
            raise RuntimeError(
                "Count buffer was empty at dispatch event {}".format(action.event_id)
            )

        job_count = len(counts)
        if not job_totals:
            job_totals = [0] * job_count
            job_unique_draws = [set() for _ in range(job_count)]
        elif len(job_totals) != job_count:
            raise RuntimeError(
                "Job-count mismatch across partition count buffers: expected {} got {}".format(
                    len(job_totals), job_count
                )
            )

        commands = read_command_buffer(controller, command_binding["id"])
        expected_command_count = job_count * partition["max_commands_per_job"]
        if len(commands) != expected_command_count:
            raise RuntimeError(
                "Command buffer length mismatch for partition {}: expected {} commands, found {}".format(
                    partition["partition_index"], expected_command_count, len(commands)
                )
            )

        partition_total = 0
        for job_index, count in enumerate(counts):
            if count > partition["max_commands_per_job"]:
                raise RuntimeError(
                    "Partition {} job {} count overflow: count={} max={}".format(
                        partition["partition_index"],
                        job_index,
                        count,
                        partition["max_commands_per_job"],
                    )
                )

            partition_total += count
            job_totals[job_index] += count
            command_base = job_index * partition["max_commands_per_job"]
            for slot in range(count):
                job_unique_draws[job_index].add(
                    commands[command_base + slot]["draw_index"]
                )

        total_input_draw_records += partition["record_count"]
        total_emitted_draws += partition_total
        partition_details.append(
            {
                "dispatch_event_id": action.event_id,
                "dispatch_event_gpu_duration_ms": durations.get(action.event_id, 0.0),
                "dispatch_name": action.name,
                "dispatch_path": action.path,
                "partition_index": partition["partition_index"],
                "record_begin": partition["record_begin"],
                "record_count": partition["record_count"],
                "max_commands_per_job": partition["max_commands_per_job"],
                "pass_mask": partition["pass_mask"],
                "count_binding_name": count_binding["name"],
                "command_binding_name": command_binding["name"],
                "counts": counts,
                "partition_total": partition_total,
            }
        )

    unique_draw_counts = [len(entries) for entries in job_unique_draws]
    for job_index, (total_count, unique_count) in enumerate(
        zip(job_totals, unique_draw_counts)
    ):
        if total_count != unique_count:
            raise RuntimeError(
                "Job {} emitted duplicate draw indices: total={} unique={}".format(
                    job_index, total_count, unique_count
                )
            )

    if summary_records and len(summary_records) != len(job_totals):
        raise RuntimeError(
            "Receiver-mask summary job-count mismatch: summary={} counts={}".format(
                len(summary_records), len(job_totals)
            )
        )

    sampled_job_count = 0
    valid_job_count = 0
    if summary_records:
        for record in summary_records:
            if record["flags"] & FLAG_VALID:
                valid_job_count += 1
            if record["sample_count"] > 0:
                sampled_job_count += 1

    full_input_eligibility_job_count = sum(
        1 for value in job_totals if value == total_input_draw_records
    )
    rejected_job_count = sum(1 for value in job_totals if value < total_input_draw_records)
    aggregate_full_replay_draws = len(job_totals) * total_input_draw_records
    total_rejected_draws = aggregate_full_replay_draws - total_emitted_draws

    report.append("analysis_profile=conventional_shadow_caster_culling")
    report.append("capture_path={}".format(capture_path))
    report.append("report_path={}".format(report_path))
    report.append("pass_name={}".format(PASS_NAME))
    report.append("scope_events={}".format(summarize_event_ids(scope_records)))
    report.append("work_events={}".format(summarize_event_ids(work_records)))
    report.append("work_event_count={}".format(len(work_records)))
    report.append("dispatch_event_count={}".format(len(dispatch_records)))
    report.append(
        "partition_binding_name={}".format(partition_binding["binding"]["name"])
    )
    report.append(
        "partition_binding_source={}".format(partition_binding["binding_source"])
    )
    report.append(
        "partition_binding_event_id={}".format(partition_binding["event_id"])
    )
    if summary_binding is not None:
        report.append("summary_binding_name={}".format(summary_binding["binding"]["name"]))
        report.append(
            "summary_binding_source={}".format(summary_binding["binding_source"])
        )
        report.append(
            "summary_binding_event_id={}".format(summary_binding["event_id"])
        )
    report.append("job_count={}".format(len(job_totals)))
    report.append("partition_count={}".format(len(partition_details)))
    report.append("valid_job_count={}".format(valid_job_count))
    report.append("sampled_job_count={}".format(sampled_job_count))
    report.append("input_draw_record_count={}".format(total_input_draw_records))
    report.append("total_emitted_draw_count={}".format(total_emitted_draws))
    report.append(
        "aggregate_full_replay_draw_count={}".format(aggregate_full_replay_draws)
    )
    report.append("total_rejected_draw_count={}".format(total_rejected_draws))
    report.append(
        "aggregate_rejection_ratio={:.6f}".format(
            float(total_rejected_draws) / float(aggregate_full_replay_draws)
            if aggregate_full_replay_draws > 0
            else 0.0
        )
    )
    report.append(
        "average_eligible_draws_per_job={:.6f}".format(
            float(total_emitted_draws) / float(len(job_totals))
            if job_totals
            else 0.0
        )
    )
    report.append(
        "average_eligible_draws_per_sampled_job={:.6f}".format(
            float(total_emitted_draws) / float(sampled_job_count)
            if sampled_job_count > 0
            else 0.0
        )
    )
    report.append(
        "min_eligible_draw_count={}".format(min(job_totals) if job_totals else 0)
    )
    report.append(
        "max_eligible_draw_count={}".format(max(job_totals) if job_totals else 0)
    )
    report.append(
        "full_input_eligibility_job_count={}".format(full_input_eligibility_job_count)
    )
    report.append("rejected_job_count={}".format(rejected_job_count))
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
    report.append("partitions:")
    for detail in partition_details:
        report.append(
            "partition_index={}|record_begin={}|record_count={}|"
            "max_commands_per_job={}|pass_mask=0x{:08x}|dispatch_event_id={}|"
            "dispatch_event_gpu_duration_ms={:.6f}|partition_total={}|"
            "count_binding_name={}|command_binding_name={}|counts={}".format(
                detail["partition_index"],
                detail["record_begin"],
                detail["record_count"],
                detail["max_commands_per_job"],
                detail["pass_mask"],
                detail["dispatch_event_id"],
                detail["dispatch_event_gpu_duration_ms"],
                detail["partition_total"],
                detail["count_binding_name"],
                detail["command_binding_name"],
                ",".join(str(value) for value in detail["counts"]),
            )
        )
    report.blank()
    report.append("jobs:")
    for job_index, eligible_count in enumerate(job_totals):
        report.append(
            "job={}|eligible_draw_count={}|unique_draw_count={}|"
            "rejected_draw_count={}|rejection_ratio={:.6f}".format(
                job_index,
                eligible_count,
                unique_draw_counts[job_index],
                total_input_draw_records - eligible_count,
                float(total_input_draw_records - eligible_count)
                / float(total_input_draw_records)
                if total_input_draw_records > 0
                else 0.0,
            )
        )
    report.flush()


RUN_EXIT_CODE = run_ui_script(REPORT_SUFFIX, build_report)
