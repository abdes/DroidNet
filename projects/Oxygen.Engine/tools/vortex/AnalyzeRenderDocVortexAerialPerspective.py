"""RenderDoc UI analyzer for focused Vortex aerial-perspective proof."""

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
    collect_resource_records_raw,
    renderdoc_module,
    resource_id_to_name,
    run_ui_script,
    safe_getattr,
)


REPORT_SUFFIX = "_vortex_aerial_perspective_report.txt"
DISPATCH_NAME = "ID3D12GraphicsCommandList::Dispatch()"
DRAW_NAME = "ID3D12GraphicsCommandList::DrawInstanced()"
CAMERA_AERIAL_SCOPE = "Vortex.Environment.AtmosphereCameraAerialPerspective"
STAGE15_SKY_SCOPE = "Vortex.Stage15.Sky"
STAGE15_ATMOSPHERE_SCOPE = "Vortex.Stage15.Atmosphere"
CAMERA_AERIAL_TOKEN = "vortex.environment.atmospherecameraaerialperspective"
ENVIRONMENT_STATIC_DATA_BYTE_SIZE = 672
ENVIRONMENT_VIEW_DATA_BYTE_SIZE = 272
INVALID_BINDLESS_INDEX = 0xFFFFFFFF
SKY_ATMOSPHERE_U32_OFFSET = (128 + 96) // 4
SKY_ATMOSPHERE_ENABLED_U32 = SKY_ATMOSPHERE_U32_OFFSET + 37
SKY_ATMOSPHERE_CAMERA_VOLUME_SRV_U32 = SKY_ATMOSPHERE_U32_OFFSET + 42


def records_with_name(action_records, name):
    return [record for record in action_records if record.name == name]


def records_under_prefix(action_records, prefix):
    prefix_with_sep = prefix + " > "
    return [record for record in action_records if record.path.startswith(prefix_with_sep)]


def find_last_named_record(records, name):
    matches = [record for record in records if record.name == name]
    return matches[-1] if matches else None


def collect_event_ids(scope_records, child_records):
    event_ids = {record.event_id for record in child_records}
    event_ids.update(record.event_id for record in scope_records)
    return event_ids


def resource_used_in_events(controller, resource_id, event_ids):
    for usage in controller.GetUsage(resource_id):
        if safe_getattr(usage, "eventId") in event_ids:
            return True
    return False


def find_named_resource_usage(controller, resource_records, event_ids, *tokens):
    matches = []
    for resource in resource_records:
        resource_id = safe_getattr(resource, "resourceId")
        name = safe_getattr(resource, "name", "")
        if resource_id is None or not name:
            continue
        lower_name = str(name).lower()
        if not all(token.lower() in lower_name for token in tokens):
            continue
        if resource_used_in_events(controller, resource_id, event_ids):
            matches.append({"resource_id": resource_id, "name": name})
    return matches


def texture_desc_map(controller):
    descriptions = {}
    for desc in controller.GetTextures():
        resource_id = safe_getattr(desc, "resourceId")
        if resource_id is None:
            continue
        descriptions[str(resource_id)] = desc
    return descriptions


def float4_values(pixel_value):
    return [float(value) for value in list(pixel_value.floatValue)[:4]]


def format_values(values):
    return ",".join("{:.9g}".format(value) for value in values)


def append_bool(report, key, value):
    report.append("{}={}".format(key, str(bool(value)).lower()))


def make_subresource(rd, descriptor):
    sub = rd.Subresource()
    sub.mip = int(safe_getattr(descriptor, "firstMip", 0) or 0)
    sub.slice = int(safe_getattr(descriptor, "firstSlice", 0) or 0)
    sub.sample = 0
    return sub


def resource_dims(texture_desc, descriptor):
    width = int(safe_getattr(texture_desc, "width", 1) or 1)
    height = int(safe_getattr(texture_desc, "height", 1) or 1)
    mip = int(safe_getattr(descriptor, "firstMip", 0) or 0)
    return max(1, width >> mip), max(1, height >> mip)


def sample_descriptor(controller, rd, resource_names, texture_descs, descriptor, type_cast):
    resource_id = descriptor.resource
    resource_key = str(resource_id)
    texture_desc = texture_descs.get(resource_key)
    sub = make_subresource(rd, descriptor)
    min_value, max_value = controller.GetMinMax(resource_id, sub, type_cast)
    min_values = float4_values(min_value)
    max_values = float4_values(max_value)
    width, height = resource_dims(texture_desc, descriptor)
    center = controller.PickPixel(
        resource_id,
        max(0, min(width - 1, width // 2)),
        max(0, min(height - 1, height // 2)),
        sub,
        type_cast,
    )
    return {
        "resource_id": resource_id,
        "name": resource_names.get(resource_key, resource_key),
        "width": width,
        "height": height,
        "subresource": sub,
        "min": min_values,
        "max": max_values,
        "center": float4_values(center),
    }


def analyze_draw_event(controller, rd, resource_names, texture_descs, event_id):
    controller.SetFrameEvent(event_id, True)
    state = controller.GetPipelineState()
    outputs = [
        sample_descriptor(
            controller,
            rd,
            resource_names,
            texture_descs,
            descriptor,
            rd.CompType.Typeless,
        )
        for descriptor in state.GetOutputTargets()
        if safe_getattr(descriptor, "resource") is not None
    ]
    return {"outputs": outputs}


def find_output_sample(draw_state, name):
    for sample in draw_state["outputs"]:
        if sample["name"] == name:
            return sample
    return draw_state["outputs"][0] if draw_state["outputs"] else None


def vec_diff(a_values, b_values):
    return [b - a for a, b in zip(a_values[:4], b_values[:4])]


def dense_grid_delta(controller, rd, event_id_a, sample_a, event_id_b, sample_b):
    if sample_a is None or sample_b is None:
        return 0.0
    if sample_a["resource_id"] != sample_b["resource_id"]:
        return 0.0

    width = min(sample_a["width"], sample_b["width"])
    height = min(sample_a["height"], sample_b["height"])
    if width <= 0 or height <= 0:
        return 0.0

    values_a = []
    sub_a = sample_a["subresource"]
    sub_b = sample_b["subresource"]
    grid_x = 41
    grid_y = 31
    controller.SetFrameEvent(event_id_a, True)
    for row in range(grid_y):
        y = 0 if grid_y == 1 else int(round(((height - 1) * row) / float(grid_y - 1)))
        for col in range(grid_x):
            x = 0 if grid_x == 1 else int(round(((width - 1) * col) / float(grid_x - 1)))
            values_a.append(
                float4_values(
                    controller.PickPixel(
                        sample_a["resource_id"], x, y, sub_a, rd.CompType.Typeless
                    )
                )
            )

    max_delta = 0.0
    index = 0
    controller.SetFrameEvent(event_id_b, True)
    for row in range(grid_y):
        y = 0 if grid_y == 1 else int(round(((height - 1) * row) / float(grid_y - 1)))
        for col in range(grid_x):
            x = 0 if grid_x == 1 else int(round(((width - 1) * col) / float(grid_x - 1)))
            value_b = float4_values(
                controller.PickPixel(
                    sample_b["resource_id"], x, y, sub_b, rd.CompType.Typeless
                )
            )
            max_delta = max(max_delta, max(abs(value) for value in vec_diff(values_a[index], value_b)))
            index += 1

    return max_delta


def sample_volume(controller, rd, texture_descs, resource_id):
    texture_desc = texture_descs.get(str(resource_id))
    if texture_desc is None:
        return {
            "sampled": False,
            "width": 0,
            "height": 0,
            "depth": 0,
            "min": [0.0, 0.0, 0.0, 1.0],
            "max": [0.0, 0.0, 0.0, 1.0],
            "probe_count": 0,
            "probe_rgb_sum": 0.0,
            "probe_alpha_min": 1.0,
            "probe_alpha_max": 1.0,
            "rgb_nonzero": False,
            "alpha_valid": False,
            "transmittance_varies": False,
        }

    width = max(1, int(safe_getattr(texture_desc, "width", 1) or 1))
    height = max(1, int(safe_getattr(texture_desc, "height", 1) or 1))
    depth = max(
        1,
        int(
            safe_getattr(texture_desc, "depth", 0)
            or safe_getattr(texture_desc, "arraysize", 1)
            or 1
        ),
    )
    all_min = [float("inf")] * 4
    all_max = [float("-inf")] * 4
    probe_sum = [0.0, 0.0, 0.0, 0.0]
    probe_count = 0
    probe_slices = sorted(set([0, depth // 4, depth // 2, (depth * 3) // 4, depth - 1]))
    for slice_index in probe_slices:
        sub = rd.Subresource()
        sub.mip = 0
        sub.slice = int(slice_index)
        sub.sample = 0
        min_value, max_value = controller.GetMinMax(
            resource_id, sub, rd.CompType.Typeless
        )
        min_values = float4_values(min_value)
        max_values = float4_values(max_value)
        all_min = [min(all_min[i], min_values[i]) for i in range(4)]
        all_max = [max(all_max[i], max_values[i]) for i in range(4)]
        for y_factor in (0.0, 0.5, 1.0):
            y = int(round((height - 1) * y_factor))
            for x_factor in (0.0, 0.25, 0.5, 0.75, 1.0):
                x = int(round((width - 1) * x_factor))
                value = float4_values(
                    controller.PickPixel(resource_id, x, y, sub, rd.CompType.Typeless)
                )
                probe_count += 1
                probe_sum = [probe_sum[i] + value[i] for i in range(4)]

    rgb_nonzero = max(abs(value) for value in all_max[:3]) > 1.0e-6 or max(
        abs(all_max[index] - all_min[index]) for index in range(3)
    ) > 1.0e-6
    alpha_valid = all_min[3] >= -1.0e-5 and all_max[3] <= 1.0 + 1.0e-5
    transmittance_varies = (all_max[3] - all_min[3]) > 1.0e-6
    return {
        "sampled": True,
        "width": width,
        "height": height,
        "depth": depth,
        "min": all_min,
        "max": all_max,
        "probe_count": probe_count,
        "probe_rgb_sum": sum(probe_sum[:3]),
        "probe_alpha_min": all_min[3],
        "probe_alpha_max": all_max[3],
        "rgb_nonzero": rgb_nonzero,
        "alpha_valid": alpha_valid,
        "transmittance_varies": transmittance_varies,
    }


def bound_named_resources(controller, rd, resource_names, event_id, shader_stage, read_write, *tokens):
    controller.SetFrameEvent(event_id, True)
    state = controller.GetPipelineState()
    descriptors = (
        state.GetReadWriteResources(shader_stage, True)
        if read_write
        else state.GetReadOnlyResources(shader_stage, True)
    )
    matches = []
    for used_descriptor in descriptors:
        descriptor = used_descriptor.descriptor
        resource_id = safe_getattr(descriptor, "resource")
        if resource_id is None:
            continue
        name = resource_names.get(str(resource_id), str(resource_id))
        lower_name = str(name).lower()
        if all(token.lower() in lower_name for token in tokens):
            matches.append({"resource_id": resource_id, "name": name})
    return matches


def read_stage15_buffer_by_size(controller, rd, event_id, resource_names, byte_size):
    controller.SetFrameEvent(event_id, True)
    state = controller.GetPipelineState()
    for used_descriptor in state.GetReadOnlyResources(rd.ShaderStage.Pixel, True):
        descriptor = used_descriptor.descriptor
        if int(safe_getattr(descriptor, "byteSize", 0) or 0) != byte_size:
            continue
        resource_id = safe_getattr(descriptor, "resource")
        byte_offset = int(safe_getattr(descriptor, "byteOffset", 0) or 0)
        if resource_id is None:
            continue
        raw = controller.GetBufferData(resource_id, byte_offset, byte_size)
        blob = bytes(raw)
        u32_count = len(blob) // 4
        values_u32 = struct.unpack("<{}I".format(u32_count), blob[: u32_count * 4])
        values_f32 = struct.unpack("<{}f".format(u32_count), blob[: u32_count * 4])
        return {
            "resource_id": resource_id,
            "resource_name": resource_names.get(str(resource_id), str(resource_id)),
            "byte_size": byte_size,
            "u32": values_u32,
            "f32": values_f32,
        }
    return None


def build_report(controller, report: ReportWriter, capture_path: Path, report_path: Path):
    rd = renderdoc_module()
    action_records = collect_action_records(controller)
    resource_records = collect_resource_records_raw(controller)
    resource_names = resource_id_to_name(controller)
    texture_descs = texture_desc_map(controller)

    camera_scope = records_with_name(action_records, CAMERA_AERIAL_SCOPE)
    camera_records = records_under_prefix(action_records, CAMERA_AERIAL_SCOPE)
    sky_records = records_under_prefix(action_records, STAGE15_SKY_SCOPE)
    atmosphere_scope = records_with_name(action_records, STAGE15_ATMOSPHERE_SCOPE)
    atmosphere_records = records_under_prefix(action_records, STAGE15_ATMOSPHERE_SCOPE)
    atmosphere_event_ids = collect_event_ids(atmosphere_scope, atmosphere_records)

    camera_dispatches = records_with_name(camera_records, DISPATCH_NAME)
    camera_dispatch = camera_dispatches[0] if camera_dispatches else None
    sky_draw = find_last_named_record(sky_records, DRAW_NAME)
    atmosphere_draw = find_last_named_record(atmosphere_records, DRAW_NAME)

    camera_written = (
        bound_named_resources(
            controller,
            rd,
            resource_names,
            camera_dispatch.event_id,
            rd.ShaderStage.Compute,
            True,
            CAMERA_AERIAL_TOKEN,
        )
        if camera_dispatch is not None
        else []
    )
    if not camera_written:
        camera_written = find_named_resource_usage(
            controller,
            resource_records,
            collect_event_ids(camera_scope, camera_records),
            CAMERA_AERIAL_TOKEN,
        )

    camera_consumed = []
    if atmosphere_draw is not None:
        camera_consumed = bound_named_resources(
            controller,
            rd,
            resource_names,
            atmosphere_draw.event_id,
            rd.ShaderStage.Pixel,
            False,
            CAMERA_AERIAL_TOKEN,
        )
        if not camera_consumed:
            camera_consumed = find_named_resource_usage(
                controller,
                resource_records,
                atmosphere_event_ids,
                CAMERA_AERIAL_TOKEN,
            )

    sky_scene_color = None
    atmosphere_scene_color = None
    scene_color_delta = 0.0
    if sky_draw is not None and atmosphere_draw is not None:
        sky_state = analyze_draw_event(
            controller, rd, resource_names, texture_descs, sky_draw.event_id
        )
        atmosphere_state = analyze_draw_event(
            controller, rd, resource_names, texture_descs, atmosphere_draw.event_id
        )
        sky_scene_color = find_output_sample(sky_state, "SceneColor")
        atmosphere_scene_color = find_output_sample(atmosphere_state, "SceneColor")
        scene_color_delta = dense_grid_delta(
            controller,
            rd,
            sky_draw.event_id,
            sky_scene_color,
            atmosphere_draw.event_id,
            atmosphere_scene_color,
        )

    volume = (
        sample_volume(controller, rd, texture_descs, camera_written[0]["resource_id"])
        if camera_written
        else sample_volume(controller, rd, texture_descs, None)
    )

    view_data = (
        read_stage15_buffer_by_size(
            controller,
            rd,
            atmosphere_draw.event_id,
            resource_names,
            ENVIRONMENT_VIEW_DATA_BYTE_SIZE,
        )
        if atmosphere_draw is not None
        else None
    )
    static_data = (
        read_stage15_buffer_by_size(
            controller,
            rd,
            atmosphere_draw.event_id,
            resource_names,
            ENVIRONMENT_STATIC_DATA_BYTE_SIZE,
        )
        if atmosphere_draw is not None
        else None
    )

    aerial_strength = view_data["f32"][7] if view_data is not None else 0.0
    aerial_distance_scale = view_data["f32"][6] if view_data is not None else 0.0
    aerial_start_depth_km = view_data["f32"][59] if view_data is not None else 0.0
    main_pass_factor = view_data["f32"][63] if view_data is not None else 0.0
    volume_depth_resolution = view_data["f32"][64] if view_data is not None else 0.0
    volume_depth_resolution_inv = view_data["f32"][65] if view_data is not None else 0.0
    volume_depth_slice_length_km = view_data["f32"][66] if view_data is not None else 0.0
    volume_depth_slice_length_inv = view_data["f32"][67] if view_data is not None else 0.0
    static_camera_srv = (
        static_data["u32"][SKY_ATMOSPHERE_CAMERA_VOLUME_SRV_U32]
        if static_data is not None
        else INVALID_BINDLESS_INDEX
    )
    static_atmosphere_enabled = (
        static_data["u32"][SKY_ATMOSPHERE_ENABLED_U32] != 0
        if static_data is not None
        else False
    )

    report.append("analysis_result=success")
    report.append("analysis_profile=vortex_aerial_perspective")
    report.append("capture_path={}".format(capture_path))
    report.append("report_path={}".format(report_path))
    report.append("camera_aerial_scope_count={}".format(len(camera_scope)))
    report.append("camera_aerial_dispatch_count={}".format(len(camera_dispatches)))
    report.append("stage15_atmosphere_scope_count={}".format(len(atmosphere_scope)))
    report.append("stage15_atmosphere_draw_count={}".format(len(records_with_name(atmosphere_records, DRAW_NAME))))
    report.append("stage15_sky_event={}".format(sky_draw.event_id if sky_draw else ""))
    report.append("stage15_atmosphere_event={}".format(atmosphere_draw.event_id if atmosphere_draw else ""))
    report.append("camera_aerial_written_resource={}".format(camera_written[0]["name"] if camera_written else ""))
    report.append("camera_aerial_consumed_resource={}".format(camera_consumed[0]["name"] if camera_consumed else ""))
    report.append("camera_aerial_volume_dims={}x{}x{}".format(volume["width"], volume["height"], volume["depth"]))
    report.append("camera_aerial_volume_min={}".format(format_values(volume["min"])))
    report.append("camera_aerial_volume_max={}".format(format_values(volume["max"])))
    report.append("camera_aerial_probe_count={}".format(volume["probe_count"]))
    report.append("camera_aerial_probe_rgb_sum={:.9g}".format(volume["probe_rgb_sum"]))
    report.append("camera_aerial_probe_alpha_min={:.9g}".format(volume["probe_alpha_min"]))
    report.append("camera_aerial_probe_alpha_max={:.9g}".format(volume["probe_alpha_max"]))
    report.append("stage15_atmosphere_scene_color_delta_max={:.9g}".format(scene_color_delta))
    if sky_scene_color is not None:
        report.append("stage15_sky_scene_color_center={}".format(format_values(sky_scene_color["center"])))
    if atmosphere_scene_color is not None:
        report.append("stage15_atmosphere_scene_color_center={}".format(format_values(atmosphere_scene_color["center"])))
    report.append("stage15_environment_view_resource={}".format(view_data["resource_name"] if view_data else ""))
    report.append("stage15_environment_view_byte_size={}".format(view_data["byte_size"] if view_data else 0))
    report.append("aerial_perspective_distance_scale={:.9g}".format(aerial_distance_scale))
    report.append("aerial_scattering_strength={:.9g}".format(aerial_strength))
    report.append("aerial_perspective_start_depth_km={:.9g}".format(aerial_start_depth_km))
    report.append("aerial_perspective_main_pass_factor={:.9g}".format(main_pass_factor))
    report.append("camera_aerial_depth_resolution={:.9g}".format(volume_depth_resolution))
    report.append("camera_aerial_depth_resolution_inv={:.9g}".format(volume_depth_resolution_inv))
    report.append("camera_aerial_depth_slice_length_km={:.9g}".format(volume_depth_slice_length_km))
    report.append("camera_aerial_depth_slice_length_inv={:.9g}".format(volume_depth_slice_length_inv))
    report.append("stage15_environment_static_resource={}".format(static_data["resource_name"] if static_data else ""))
    report.append("stage15_environment_static_byte_size={}".format(static_data["byte_size"] if static_data else 0))
    report.append("static_atmosphere_camera_volume_srv={}".format(static_camera_srv))

    append_bool(report, "camera_aerial_scope_present", len(camera_scope) == 1)
    append_bool(report, "camera_aerial_dispatch_valid", len(camera_dispatches) == 1)
    append_bool(report, "camera_aerial_before_stage15_atmosphere", camera_dispatch is not None and atmosphere_draw is not None and camera_dispatch.event_id < atmosphere_draw.event_id)
    append_bool(report, "camera_aerial_written", len(camera_written) > 0)
    append_bool(report, "camera_aerial_consumed_by_atmosphere", len(camera_consumed) > 0)
    append_bool(report, "camera_aerial_volume_sampled", volume["sampled"])
    append_bool(report, "camera_aerial_volume_rgb_nonzero", volume["rgb_nonzero"])
    append_bool(report, "camera_aerial_volume_alpha_valid", volume["alpha_valid"])
    append_bool(report, "camera_aerial_volume_transmittance_varies", volume["transmittance_varies"])
    append_bool(report, "stage15_atmosphere_scope_present", len(atmosphere_scope) == 1)
    append_bool(report, "stage15_atmosphere_draw_valid", len(records_with_name(atmosphere_records, DRAW_NAME)) == 1)
    append_bool(report, "stage15_environment_view_data_bound", view_data is not None)
    append_bool(report, "stage15_environment_static_data_bound", static_data is not None)
    append_bool(report, "static_atmosphere_enabled", static_atmosphere_enabled)
    append_bool(report, "static_atmosphere_camera_volume_srv_valid", static_camera_srv != INVALID_BINDLESS_INDEX)
    append_bool(report, "aerial_perspective_main_pass_enabled", main_pass_factor > 0.5)
    append_bool(report, "aerial_perspective_scene_color_changed", scene_color_delta > 1.0e-5)


def main():
    run_ui_script(REPORT_SUFFIX, build_report)


if __name__ == "__main__":
    main()
