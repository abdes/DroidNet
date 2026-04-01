"""RenderDoc UI proof script for LightCullingPass validation.

Run only inside RenderDoc UI:

PowerShell:
    $env:OXYGEN_RENDERDOC_REPORT_PATH = 'H:/path/light_culling_report.txt'
    & 'C:/Program Files/RenderDoc/qrenderdoc.exe' --ui-python `
        'H:/projects/DroidNet/projects/Oxygen.Engine/Examples/RenderScene/AnalyzeRenderDocLightCulling.py' `
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
    format_flags,
    renderdoc_module,
    resource_id_to_name,
    run_ui_script,
    safe_getattr,
    summarize_event_ids,
)


REPORT_SUFFIX = "_light_culling_report.txt"
LIGHT_CULLING_PASS_NAME = "LightCullingPass"
SHADER_PASS_NAME = "ShaderPass"
VSM_PASS_NAME = "VsmPageRequestGeneratorPass"
FORBIDDEN_LIGHT_CULLING_TOKENS = (
    "depthprepass",
    "screenhzbbuildpass",
    "closesthistory",
    "furthesthistory",
    "forward_hdr_depth",
    "scene_depth_derivatives",
)


def resources_by_id(controller):
    lookup = {}
    get_resources = getattr(controller, "GetResources", None)
    if not callable(get_resources):
        return lookup
    for resource in get_resources():
        resource_id = safe_getattr(resource, "resourceId")
        if resource_id is not None:
            lookup[str(resource_id)] = resource
    return lookup


def resource_length(resource):
    return (
        safe_getattr(resource, "length")
        or safe_getattr(resource, "byteLength")
        or safe_getattr(resource, "byteSize")
        or 0
    )


def find_raw_action(controller, event_id):
    def walk(action):
        if safe_getattr(action, "eventId") == event_id:
            return action
        for child in safe_getattr(action, "children", []) or []:
            found = walk(child)
            if found is not None:
                return found
        return None

    get_root_actions = getattr(controller, "GetRootActions", None)
    if not callable(get_root_actions):
        return None
    for root in get_root_actions():
        found = walk(root)
        if found is not None:
            return found
    return None


def dispatch_dimensions(action):
    if action is None:
        return None

    direct = safe_getattr(action, "dispatchDimension")
    if direct is not None:
        if isinstance(direct, (tuple, list)) and len(direct) >= 3:
            return int(direct[0]), int(direct[1]), int(direct[2])
        x = safe_getattr(direct, "x")
        y = safe_getattr(direct, "y")
        z = safe_getattr(direct, "z")
        if x is not None and y is not None and z is not None:
            return int(x), int(y), int(z)

    x = safe_getattr(action, "dispatchDimensionX", safe_getattr(action, "dispatchX"))
    y = safe_getattr(action, "dispatchDimensionY", safe_getattr(action, "dispatchY"))
    z = safe_getattr(action, "dispatchDimensionZ", safe_getattr(action, "dispatchZ"))
    if x is not None and y is not None and z is not None:
        return int(x), int(y), int(z)

    return None


def bound_resource(resource_names, used_descriptor):
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


def stringify_binding(binding):
    return "{} [stage={}, bind={}, array={}, type={}]".format(
        binding["name"],
        binding["stage"],
        binding["bind"],
        binding["array"],
        binding["type"],
    )


def collect_event_bindings(controller, resource_names, event_id):
    rd = renderdoc_module()
    controller.SetFrameEvent(event_id, True)
    state = controller.GetPipelineState()
    return {
        "pixel_ro": [
            bound_resource(resource_names, descriptor)
            for descriptor in state.GetReadOnlyResources(rd.ShaderStage.Pixel, True)
        ],
        "compute_ro": [
            bound_resource(resource_names, descriptor)
            for descriptor in state.GetReadOnlyResources(rd.ShaderStage.Compute, True)
        ],
        "pixel_rw": [
            bound_resource(resource_names, descriptor)
            for descriptor in state.GetReadWriteResources(rd.ShaderStage.Pixel, True)
        ],
        "compute_rw": [
            bound_resource(resource_names, descriptor)
            for descriptor in state.GetReadWriteResources(rd.ShaderStage.Compute, True)
        ],
    }


def first_named_binding(bindings, target_name):
    lowered = target_name.lower()
    for binding in bindings:
        if binding["name"].lower() == lowered:
            return binding
    return None


def matching_bindings(bindings, target_name):
    lowered = target_name.lower()
    return [binding for binding in bindings if binding["name"].lower() == lowered]


def forbidden_bindings(bindings):
    matches = []
    for binding in bindings:
        lowered = binding["name"].lower()
        if any(token in lowered for token in FORBIDDEN_LIGHT_CULLING_TOKENS):
            matches.append(binding)
    return matches


def read_uint_buffer(controller, resource_id):
    raw = controller.GetBufferData(resource_id, 0, 0)
    if raw is None:
        return ()
    byte_count = len(raw) - (len(raw) % 4)
    if byte_count <= 0:
        return ()
    return struct.unpack("<{}I".format(byte_count // 4), raw[:byte_count])


def analyze_light_grid(controller, cluster_grid_id, light_list_id):
    grid_words = read_uint_buffer(controller, cluster_grid_id)
    light_indices = read_uint_buffer(controller, light_list_id)
    if len(grid_words) % 2 != 0:
        grid_words = grid_words[:-1]

    cluster_entries = [
        (grid_words[index], grid_words[index + 1])
        for index in range(0, len(grid_words), 2)
    ]
    cluster_count = len(cluster_entries)
    light_index_count = len(light_indices)
    inferred_cap = (
        light_index_count // cluster_count if cluster_count > 0 else 0
    )

    non_empty = []
    total_reported_refs = 0
    max_count = 0
    saturated_count = 0
    out_of_bounds_count = 0
    unique_lights = set()

    for cluster_index, (offset, count) in enumerate(cluster_entries):
        if count == 0:
            continue
        total_reported_refs += count
        max_count = max(max_count, count)
        if inferred_cap > 0 and count == inferred_cap:
            saturated_count += 1
        end = offset + count
        if end > light_index_count:
            out_of_bounds_count += 1
            sample_values = list(light_indices[offset:light_index_count])
        else:
            sample_values = list(light_indices[offset:end])
        unique_lights.update(sample_values)
        non_empty.append(
            {
                "cluster_index": cluster_index,
                "offset": offset,
                "count": count,
                "sample": sample_values[:8],
            }
        )

    return {
        "cluster_grid_bytes": len(grid_words) * 4,
        "light_index_bytes": len(light_indices) * 4,
        "cluster_count": cluster_count,
        "light_index_count": light_index_count,
        "inferred_max_lights_per_cell": inferred_cap,
        "non_empty_count": len(non_empty),
        "total_reported_refs": total_reported_refs,
        "max_count": max_count,
        "saturated_count": saturated_count,
        "out_of_bounds_count": out_of_bounds_count,
        "unique_light_count": len(unique_lights),
        "samples": non_empty[:8],
    }


def first_event_for_pass(action_records, pass_name, flag_substring):
    for record in find_pass_work_records(action_records, pass_name):
        if flag_substring in format_flags(record.flags):
            return record
    return None


def gpu_duration_ms(controller, event_id):
    rd = renderdoc_module()
    counter_results = controller.FetchCounters([rd.GPUCounter.EventGPUDuration])
    for result in counter_results:
        if result.eventId == event_id:
            return result.value.d * 1000.0
    return None


def append_binding_block(report, label, bindings):
    report.append("{}_count={}".format(label, len(bindings)))
    for binding in bindings:
        report.append("{}={}".format(label, stringify_binding(binding)))


def build_report(controller, report: ReportWriter, capture_path: Path, report_path: Path):
    action_records = collect_action_records(controller)
    resource_names = resource_id_to_name(controller)
    resource_lookup = resources_by_id(controller)

    light_culling_events = find_pass_work_records(action_records, LIGHT_CULLING_PASS_NAME)
    shader_draw = first_event_for_pass(action_records, SHADER_PASS_NAME, "Draw")
    vsm_dispatch = first_event_for_pass(action_records, VSM_PASS_NAME, "Dispatch")

    report.append("analysis_profile=light_culling")
    report.append("capture_path={}".format(capture_path))
    report.append("report_path={}".format(report_path))
    report.append("light_culling_work_events={}".format(summarize_event_ids(light_culling_events)))
    report.append(
        "shader_pass_draw_event={}".format(
            shader_draw.event_id if shader_draw is not None else "<none>"
        )
    )
    report.append(
        "vsm_dispatch_event={}".format(
            vsm_dispatch.event_id if vsm_dispatch is not None else "<none>"
        )
    )
    report.blank()

    if not light_culling_events:
        report.append("status=no_light_culling_dispatch_found")
        report.flush()
        return

    dispatch = light_culling_events[0]
    bindings = collect_event_bindings(controller, resource_names, dispatch.event_id)
    bound_forbidden = forbidden_bindings(bindings["compute_ro"] + bindings["compute_rw"])
    cluster_grid = first_named_binding(bindings["compute_rw"], "LightCullingPass_ClusterGrid")
    light_list = first_named_binding(bindings["compute_rw"], "LightCullingPass_LightIndexList")
    dispatch_ms = gpu_duration_ms(controller, dispatch.event_id)
    raw_action = find_raw_action(controller, dispatch.event_id)
    dims = dispatch_dimensions(raw_action)

    report.append("LightCulling dispatch")
    report.append("event_id={}".format(dispatch.event_id))
    report.append("path={}".format(dispatch.path))
    report.append("flags={}".format(format_flags(dispatch.flags)))
    if dispatch_ms is not None:
        report.append("gpu_duration_ms={:.6f}".format(dispatch_ms))
    if dims is not None:
        report.append("dispatch_groups={} x {} x {}".format(dims[0], dims[1], dims[2]))
    else:
        report.append("dispatch_groups=<unavailable-from-action>")
    append_binding_block(report, "compute_ro", bindings["compute_ro"])
    append_binding_block(report, "compute_rw", bindings["compute_rw"])
    report.append("forbidden_binding_match_count={}".format(len(bound_forbidden)))
    for binding in bound_forbidden:
        report.append("forbidden_binding={}".format(stringify_binding(binding)))
    report.blank()

    if cluster_grid is None or light_list is None:
        report.append("status=missing_light_grid_uavs")
        report.flush()
        return

    cluster_resource = resource_lookup.get(str(cluster_grid["id"]))
    light_list_resource = resource_lookup.get(str(light_list["id"]))
    light_grid_stats = analyze_light_grid(
        controller, cluster_grid["id"], light_list["id"]
    )

    report.append("Light-grid resources")
    report.append(
        "cluster_grid_resource={} length_bytes={}".format(
            cluster_grid["name"],
            resource_length(cluster_resource) or light_grid_stats["cluster_grid_bytes"],
        )
    )
    report.append(
        "light_index_list_resource={} length_bytes={}".format(
            light_list["name"],
            resource_length(light_list_resource) or light_grid_stats["light_index_bytes"],
        )
    )
    report.append("cluster_entry_count={}".format(light_grid_stats["cluster_count"]))
    report.append("light_index_entry_count={}".format(light_grid_stats["light_index_count"]))
    report.append(
        "inferred_max_lights_per_cell={}".format(
            light_grid_stats["inferred_max_lights_per_cell"]
        )
    )
    report.append("non_empty_clusters={}".format(light_grid_stats["non_empty_count"]))
    report.append("total_reported_light_refs={}".format(light_grid_stats["total_reported_refs"]))
    report.append("max_lights_in_cluster={}".format(light_grid_stats["max_count"]))
    report.append("saturated_cluster_count={}".format(light_grid_stats["saturated_count"]))
    report.append("out_of_bounds_cluster_count={}".format(light_grid_stats["out_of_bounds_count"]))
    report.append("unique_referenced_lights={}".format(light_grid_stats["unique_light_count"]))
    report.append("sample_non_empty_clusters={}".format(len(light_grid_stats["samples"])))
    for sample in light_grid_stats["samples"]:
        report.append(
            "cluster_sample index={} offset={} count={} lights={}".format(
                sample["cluster_index"],
                sample["offset"],
                sample["count"],
                ",".join(str(value) for value in sample["sample"]) or "<none>",
            )
        )
    report.blank()

    if shader_draw is not None:
        shader_bindings = collect_event_bindings(
            controller, resource_names, shader_draw.event_id
        )
        shader_grid = matching_bindings(
            shader_bindings["pixel_ro"], "LightCullingPass_ClusterGrid"
        )
        shader_list = matching_bindings(
            shader_bindings["pixel_ro"], "LightCullingPass_LightIndexList"
        )
        report.append("ShaderPass consumer")
        report.append("event_id={}".format(shader_draw.event_id))
        report.append("path={}".format(shader_draw.path))
        report.append(
            "cluster_grid_bound={}".format("yes" if shader_grid else "no")
        )
        report.append(
            "light_index_list_bound={}".format("yes" if shader_list else "no")
        )
        for binding in shader_grid + shader_list:
            report.append("pixel_ro={}".format(stringify_binding(binding)))
        report.blank()

    if vsm_dispatch is not None:
        vsm_bindings = collect_event_bindings(
            controller, resource_names, vsm_dispatch.event_id
        )
        vsm_grid = matching_bindings(
            vsm_bindings["compute_ro"], "LightCullingPass_ClusterGrid"
        )
        vsm_list = matching_bindings(
            vsm_bindings["compute_ro"], "LightCullingPass_LightIndexList"
        )
        report.append("VSM consumer check")
        report.append("event_id={}".format(vsm_dispatch.event_id))
        report.append("path={}".format(vsm_dispatch.path))
        report.append(
            "cluster_grid_bound={}".format("yes" if vsm_grid else "no")
        )
        report.append(
            "light_index_list_bound={}".format("yes" if vsm_list else "no")
        )
        if not vsm_grid and not vsm_list:
            report.append(
                "observation=Light-grid pruning was not active on the captured "
                "VsmPageRequestGeneratorPass dispatch."
            )
        for binding in vsm_bindings["compute_ro"]:
            report.append("compute_ro={}".format(stringify_binding(binding)))
        report.blank()

    report.append("summary")
    report.append(
        "light_culling_uses_depth_or_hzb={}".format(
            "yes" if bound_forbidden else "no"
        )
    )
    report.append(
        "shader_pass_consumes_light_grid={}".format(
            "yes" if shader_draw is not None else "no"
        )
    )
    report.append(
        "vsm_light_grid_consumption_observed={}".format(
            "yes"
            if vsm_dispatch is not None
            and matching_bindings(
                collect_event_bindings(controller, resource_names, vsm_dispatch.event_id)["compute_ro"],
                "LightCullingPass_ClusterGrid",
            )
            else "no"
        )
    )
    report.flush()


RUN_EXIT_CODE = run_ui_script(REPORT_SUFFIX, build_report)
