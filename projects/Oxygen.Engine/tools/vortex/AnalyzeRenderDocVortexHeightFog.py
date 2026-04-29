"""RenderDoc UI analyzer for focused Vortex height-fog proof."""

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
    renderdoc_module,
    resource_id_to_name,
    run_ui_script,
    safe_getattr,
)


REPORT_SUFFIX = "_vortex_height_fog_report.txt"
DRAW_NAME = "ID3D12GraphicsCommandList::DrawInstanced()"
FOG_SCOPE = "Vortex.Stage15.Fog"
ATMOSPHERE_SCOPE = "Vortex.Stage15.Atmosphere"
SKY_SCOPE = "Vortex.Stage15.Sky"
ENVIRONMENT_STATIC_DATA_BYTE_SIZE = 672
ENVIRONMENT_STATIC_DATA_U32_COUNT = ENVIRONMENT_STATIC_DATA_BYTE_SIZE // 4
INVALID_BINDLESS_INDEX = 0xFFFFFFFF

GPU_FOG_FLAG_ENABLED = 1 << 0
GPU_FOG_FLAG_HEIGHT_FOG_ENABLED = 1 << 1
GPU_FOG_FLAG_RENDER_IN_MAIN_PASS = 1 << 3
GPU_FOG_FLAG_DIRECTIONAL_INSCATTERING = 1 << 7
GPU_FOG_FLAG_CUBEMAP_AUTHORED = 1 << 8
GPU_FOG_FLAG_CUBEMAP_USABLE = 1 << 9


def records_with_name(action_records, name):
    return [record for record in action_records if record.name == name]


def records_under_prefix(action_records, prefix):
    prefix_with_sep = prefix + " > "
    return [record for record in action_records if record.path.startswith(prefix_with_sep)]


def find_last_named_record(records, name):
    matches = [record for record in records if record.name == name]
    return matches[-1] if matches else None


def append_bool(report, key, value):
    report.append("{}={}".format(key, str(bool(value)).lower()))


def format_values(values):
    return ",".join("{:.9g}".format(value) for value in values)


def float4_values(pixel_value):
    return [float(value) for value in list(pixel_value.floatValue)[:4]]


def vec_diff(a_values, b_values):
    return [b - a for a, b in zip(a_values[:4], b_values[:4])]


def texture_desc_map(controller):
    descriptions = {}
    for desc in controller.GetTextures():
        resource_id = safe_getattr(desc, "resourceId")
        if resource_id is None:
            continue
        descriptions[str(resource_id)] = desc
    return descriptions


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
    name = resource_names.get(resource_key, resource_key)
    texture_desc = texture_descs.get(resource_key)
    sub = make_subresource(rd, descriptor)
    min_value, max_value = controller.GetMinMax(resource_id, sub, type_cast)
    min_values = float4_values(min_value)
    max_values = float4_values(max_value)
    width, height = resource_dims(texture_desc, descriptor)
    center_x = max(0, min(width - 1, width // 2))
    center_y = max(0, min(height - 1, height // 2))
    center = controller.PickPixel(resource_id, center_x, center_y, sub, type_cast)
    return {
        "resource_id": resource_id,
        "name": name,
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
    depth_descriptor = state.GetDepthTarget()
    depth = None
    if safe_getattr(depth_descriptor, "resource") is not None:
        depth = sample_descriptor(
            controller,
            rd,
            resource_names,
            texture_descs,
            depth_descriptor,
            rd.CompType.Depth,
        )
    return {"outputs": outputs, "depth": depth}


def find_output_sample(draw_state, name):
    for sample in draw_state["outputs"]:
        if sample["name"] == name:
            return sample
    return draw_state["outputs"][0] if draw_state["outputs"] else None


def dense_grid_delta(controller, rd, event_id_a, sample_a, event_id_b, sample_b):
    if sample_a is None or sample_b is None:
        return 0.0
    if sample_a["resource_id"] != sample_b["resource_id"]:
        return 0.0

    width = min(sample_a["width"], sample_b["width"])
    height = min(sample_a["height"], sample_b["height"])
    if width <= 0 or height <= 0:
        return 0.0

    grid_x = 9
    grid_y = 7
    values_a = []
    sub_a = sample_a["subresource"]
    sub_b = sample_b["subresource"]
    controller.SetFrameEvent(event_id_a, True)
    for row in range(grid_y):
        y = 0 if grid_y == 1 else int(round(((height - 1) * row) / float(grid_y - 1)))
        for col in range(grid_x):
            x = 0 if grid_x == 1 else int(round(((width - 1) * col) / float(grid_x - 1)))
            values_a.append(
                float4_values(
                    controller.PickPixel(sample_a["resource_id"], x, y, sub_a, rd.CompType.Typeless)
                )
            )

    max_delta = 0.0
    controller.SetFrameEvent(event_id_b, True)
    index = 0
    for row in range(grid_y):
        y = 0 if grid_y == 1 else int(round(((height - 1) * row) / float(grid_y - 1)))
        for col in range(grid_x):
            x = 0 if grid_x == 1 else int(round(((width - 1) * col) / float(grid_x - 1)))
            value_b = float4_values(
                controller.PickPixel(sample_b["resource_id"], x, y, sub_b, rd.CompType.Typeless)
            )
            max_delta = max(
                max_delta,
                max(abs(value) for value in vec_diff(values_a[index], value_b)),
            )
            index += 1

    return max_delta


def far_depth_delta(controller, rd, event_id_a, color_a, event_id_b, color_b, depth):
    if color_a is None or color_b is None or depth is None:
        return 0.0, 0
    if color_a["resource_id"] != color_b["resource_id"]:
        return 0.0, 0

    width = min(color_a["width"], color_b["width"], depth["width"])
    height = min(color_a["height"], color_b["height"], depth["height"])
    if width <= 0 or height <= 0:
        return 0.0, 0

    positions = []
    grid_x = 11
    grid_y = 9
    controller.SetFrameEvent(event_id_a, True)
    depth_sub = depth["subresource"]
    for row in range(grid_y):
        y = 0 if grid_y == 1 else int(round(((height - 1) * row) / float(grid_y - 1)))
        for col in range(grid_x):
            x = 0 if grid_x == 1 else int(round(((width - 1) * col) / float(grid_x - 1)))
            depth_value = float4_values(
                controller.PickPixel(depth["resource_id"], x, y, depth_sub, rd.CompType.Depth)
            )[0]
            if depth_value <= 1.0e-4 or depth_value >= 0.9999:
                positions.append((x, y))

    if not positions:
        return 0.0, 0

    color_sub_a = color_a["subresource"]
    color_sub_b = color_b["subresource"]
    values_a = []
    controller.SetFrameEvent(event_id_a, True)
    for x, y in positions:
        values_a.append(
            float4_values(
                controller.PickPixel(color_a["resource_id"], x, y, color_sub_a, rd.CompType.Typeless)
            )
        )

    max_delta = 0.0
    controller.SetFrameEvent(event_id_b, True)
    for index, (x, y) in enumerate(positions):
        value_b = float4_values(
            controller.PickPixel(color_b["resource_id"], x, y, color_sub_b, rd.CompType.Typeless)
        )
        max_delta = max(max_delta, max(abs(value) for value in vec_diff(values_a[index], value_b)))

    return max_delta, len(positions)


def read_stage15_fog_environment_static_data(controller, rd, event_id, resource_names):
    controller.SetFrameEvent(event_id, True)
    state = controller.GetPipelineState()
    for used_descriptor in state.GetReadOnlyResources(rd.ShaderStage.Pixel, True):
        descriptor = used_descriptor.descriptor
        byte_size = int(safe_getattr(descriptor, "byteSize", 0) or 0)
        if byte_size != ENVIRONMENT_STATIC_DATA_BYTE_SIZE:
            continue
        resource_id = safe_getattr(descriptor, "resource")
        byte_offset = int(safe_getattr(descriptor, "byteOffset", 0) or 0)
        if resource_id is None:
            continue
        raw = controller.GetBufferData(resource_id, byte_offset, byte_size)
        blob = bytes(raw)
        u32_count = len(blob) // 4
        if u32_count < ENVIRONMENT_STATIC_DATA_U32_COUNT:
            continue
        values_u32 = struct.unpack("<{}I".format(u32_count), blob[: u32_count * 4])
        values_f32 = struct.unpack("<{}f".format(u32_count), blob[: u32_count * 4])
        return {
            "resource_id": resource_id,
            "resource_name": resource_names.get(str(resource_id), str(resource_id)),
            "byte_offset": byte_offset,
            "byte_size": byte_size,
            "fog_inscattering_luminance_rgb": values_f32[0:3],
            "primary_density": values_f32[3],
            "primary_height_falloff": values_f32[4],
            "primary_height_offset_m": values_f32[5],
            "secondary_density": values_f32[6],
            "secondary_height_falloff": values_f32[7],
            "secondary_height_offset_m": values_f32[8],
            "start_distance_m": values_f32[9],
            "end_distance_m": values_f32[10],
            "cutoff_distance_m": values_f32[11],
            "max_opacity": values_f32[12],
            "min_transmittance": values_f32[13],
            "directional_start_distance_m": values_f32[14],
            "directional_exponent": values_f32[15],
            "directional_inscattering_luminance_rgb": values_f32[16:19],
            "sky_atmosphere_ambient_contribution_color_scale_rgb": values_f32[20:23],
            "cubemap_num_mips": values_f32[28],
            "cubemap_srv": values_u32[29],
            "flags": values_u32[30],
            "model": values_u32[31],
        }
    return None


def build_report(controller, report: ReportWriter, capture_path: Path, report_path: Path):
    rd = renderdoc_module()
    action_records = collect_action_records(controller)
    resource_names = resource_id_to_name(controller)
    texture_descs = texture_desc_map(controller)

    fog_scope = records_with_name(action_records, FOG_SCOPE)
    fog_records = records_under_prefix(action_records, FOG_SCOPE)
    atmosphere_records = records_under_prefix(action_records, ATMOSPHERE_SCOPE)
    sky_records = records_under_prefix(action_records, SKY_SCOPE)
    fog_draw_count = len(records_with_name(fog_records, DRAW_NAME))
    fog_draw = find_last_named_record(fog_records, DRAW_NAME)
    previous_stage_name = ""
    previous_draw = find_last_named_record(atmosphere_records, DRAW_NAME)
    if previous_draw is not None:
        previous_stage_name = ATMOSPHERE_SCOPE
    else:
        previous_draw = find_last_named_record(sky_records, DRAW_NAME)
        if previous_draw is not None:
            previous_stage_name = SKY_SCOPE

    stage15_static_data = None
    scene_color_delta = 0.0
    far_delta = 0.0
    far_sample_count = 0
    previous_scene_color = None
    fog_scene_color = None
    if fog_draw is not None and previous_draw is not None:
        previous_state = analyze_draw_event(
            controller,
            rd,
            resource_names,
            texture_descs,
            previous_draw.event_id,
        )
        fog_state = analyze_draw_event(
            controller,
            rd,
            resource_names,
            texture_descs,
            fog_draw.event_id,
        )
        previous_scene_color = find_output_sample(previous_state, "SceneColor")
        fog_scene_color = find_output_sample(fog_state, "SceneColor")
        scene_color_delta = dense_grid_delta(
            controller,
            rd,
            previous_draw.event_id,
            previous_scene_color,
            fog_draw.event_id,
            fog_scene_color,
        )
        far_delta, far_sample_count = far_depth_delta(
            controller,
            rd,
            previous_draw.event_id,
            previous_scene_color,
            fog_draw.event_id,
            fog_scene_color,
            fog_state["depth"],
        )
        stage15_static_data = read_stage15_fog_environment_static_data(
            controller,
            rd,
            fog_draw.event_id,
            resource_names,
        )

    fog_flags = stage15_static_data["flags"] if stage15_static_data else 0
    cubemap_srv = stage15_static_data["cubemap_srv"] if stage15_static_data else INVALID_BINDLESS_INDEX
    primary_density = stage15_static_data["primary_density"] if stage15_static_data else 0.0
    secondary_density = stage15_static_data["secondary_density"] if stage15_static_data else 0.0
    max_opacity = stage15_static_data["max_opacity"] if stage15_static_data else 0.0
    min_transmittance = stage15_static_data["min_transmittance"] if stage15_static_data else 1.0
    height_fog_changed = scene_color_delta > 1.0e-5
    far_depth_unchanged = far_sample_count == 0 or far_delta <= 1.0e-5
    cubemap_unusable = (
        (fog_flags & GPU_FOG_FLAG_CUBEMAP_USABLE) == 0
        and cubemap_srv == INVALID_BINDLESS_INDEX
    )

    report.append("analysis_result=success")
    report.append("analysis_profile=vortex_height_fog")
    report.append("capture_path={}".format(capture_path))
    report.append("stage15_fog_scope_count={}".format(len(fog_scope)))
    report.append("stage15_fog_draw_count={}".format(fog_draw_count))
    report.append("stage15_previous_stage={}".format(previous_stage_name))
    report.append("stage15_previous_event={}".format(previous_draw.event_id if previous_draw else ""))
    report.append("stage15_fog_event={}".format(fog_draw.event_id if fog_draw else ""))
    report.append("height_fog_scene_color_delta_max={:.9g}".format(scene_color_delta))
    report.append("height_fog_far_depth_delta_max={:.9g}".format(far_delta))
    report.append("height_fog_far_depth_sample_count={}".format(far_sample_count))
    if previous_scene_color is not None:
        report.append("stage15_previous_scene_color_center={}".format(format_values(previous_scene_color["center"])))
    if fog_scene_color is not None:
        report.append("stage15_fog_scene_color_center={}".format(format_values(fog_scene_color["center"])))
    if stage15_static_data is not None:
        report.append("stage15_fog_environment_static_resource={}".format(stage15_static_data["resource_name"]))
        report.append("stage15_fog_environment_static_byte_size={}".format(stage15_static_data["byte_size"]))
        report.append("height_fog_flags={}".format(fog_flags))
        report.append("height_fog_model={}".format(stage15_static_data["model"]))
        report.append("height_fog_primary_density={:.9g}".format(primary_density))
        report.append("height_fog_primary_height_falloff={:.9g}".format(stage15_static_data["primary_height_falloff"]))
        report.append("height_fog_primary_height_offset_m={:.9g}".format(stage15_static_data["primary_height_offset_m"]))
        report.append("height_fog_secondary_density={:.9g}".format(secondary_density))
        report.append("height_fog_secondary_height_falloff={:.9g}".format(stage15_static_data["secondary_height_falloff"]))
        report.append("height_fog_secondary_height_offset_m={:.9g}".format(stage15_static_data["secondary_height_offset_m"]))
        report.append("height_fog_start_distance_m={:.9g}".format(stage15_static_data["start_distance_m"]))
        report.append("height_fog_end_distance_m={:.9g}".format(stage15_static_data["end_distance_m"]))
        report.append("height_fog_cutoff_distance_m={:.9g}".format(stage15_static_data["cutoff_distance_m"]))
        report.append("height_fog_max_opacity={:.9g}".format(max_opacity))
        report.append("height_fog_min_transmittance={:.9g}".format(min_transmittance))
        report.append("height_fog_directional_start_distance_m={:.9g}".format(stage15_static_data["directional_start_distance_m"]))
        report.append("height_fog_directional_exponent={:.9g}".format(stage15_static_data["directional_exponent"]))
        report.append("height_fog_fog_inscattering_luminance={}".format(format_values(stage15_static_data["fog_inscattering_luminance_rgb"])))
        report.append("height_fog_directional_inscattering_luminance={}".format(format_values(stage15_static_data["directional_inscattering_luminance_rgb"])))
        report.append("height_fog_sky_ambient_scale={}".format(format_values(stage15_static_data["sky_atmosphere_ambient_contribution_color_scale_rgb"])))
        report.append("height_fog_cubemap_srv={}".format(cubemap_srv))
        report.append("height_fog_cubemap_num_mips={:.9g}".format(stage15_static_data["cubemap_num_mips"]))

    append_bool(report, "stage15_fog_scope_present", len(fog_scope) == 1)
    append_bool(report, "stage15_fog_draw_valid", fog_draw_count == 1)
    append_bool(report, "stage15_fog_environment_static_data_bound", stage15_static_data is not None)
    append_bool(report, "height_fog_scene_color_changed", height_fog_changed)
    append_bool(report, "height_fog_far_depth_unchanged", far_depth_unchanged)
    append_bool(report, "height_fog_enabled_flag", (fog_flags & GPU_FOG_FLAG_HEIGHT_FOG_ENABLED) != 0)
    append_bool(report, "height_fog_fog_enabled_flag", (fog_flags & GPU_FOG_FLAG_ENABLED) != 0)
    append_bool(report, "height_fog_render_in_main_pass_flag", (fog_flags & GPU_FOG_FLAG_RENDER_IN_MAIN_PASS) != 0)
    append_bool(report, "height_fog_directional_inscattering_flag", (fog_flags & GPU_FOG_FLAG_DIRECTIONAL_INSCATTERING) != 0)
    append_bool(report, "height_fog_primary_density_positive", primary_density > 0.0)
    append_bool(report, "height_fog_any_density_positive", primary_density > 0.0 or secondary_density > 0.0)
    append_bool(report, "height_fog_max_opacity_valid", 0.0 <= max_opacity <= 1.0)
    append_bool(report, "height_fog_min_transmittance_valid", 0.0 <= min_transmittance <= 1.0)
    append_bool(report, "height_fog_cubemap_authored_flag", (fog_flags & GPU_FOG_FLAG_CUBEMAP_AUTHORED) != 0)
    append_bool(report, "height_fog_cubemap_usable_flag", (fog_flags & GPU_FOG_FLAG_CUBEMAP_USABLE) != 0)
    append_bool(report, "height_fog_cubemap_unusable", cubemap_unusable)


def main():
    run_ui_script(REPORT_SUFFIX, build_report)


if __name__ == "__main__":
    main()
