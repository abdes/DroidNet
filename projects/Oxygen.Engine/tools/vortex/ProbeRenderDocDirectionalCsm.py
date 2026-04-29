"""Probe Vortex directional CSM products from a RenderDoc capture."""

import builtins
import struct
import sys
from pathlib import Path
import math


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
    safe_getattr,
    run_ui_script,
)


REPORT_SUFFIX = "_directional_csm_probe.txt"


def float4_values(pixel_value):
    return [float(v) for v in list(pixel_value.floatValue)[:4]]


def texture_desc_map(controller):
    descriptions = {}
    for desc in controller.GetTextures():
        resource_id = safe_getattr(desc, "resourceId")
        if resource_id is not None:
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


def sample_descriptor(controller, rd, resource_names, texture_descs, descriptor, comp_type):
    resource_id = safe_getattr(descriptor, "resource")
    if resource_id is None:
        return None
    sub = make_subresource(rd, descriptor)
    min_value, max_value = controller.GetMinMax(resource_id, sub, comp_type)
    texture_desc = texture_descs.get(str(resource_id))
    width, height = resource_dims(texture_desc, descriptor)
    center = controller.PickPixel(
        resource_id,
        max(0, min(width - 1, width // 2)),
        max(0, min(height - 1, height // 2)),
        sub,
        comp_type,
    )
    return {
        "resource": resource_id,
        "name": resource_names.get(str(resource_id), str(resource_id)),
        "width": width,
        "height": height,
        "mip": sub.mip,
        "slice": sub.slice,
        "min": float4_values(min_value),
        "max": float4_values(max_value),
        "center": float4_values(center),
    }


def descriptor_name(resource_names, used_descriptor):
    descriptor = used_descriptor.descriptor
    resource_id = safe_getattr(descriptor, "resource")
    return resource_names.get(str(resource_id), str(resource_id))


def dump_u32_f32(controller, descriptor, max_dwords):
    resource = safe_getattr(descriptor, "resource")
    byte_offset = int(safe_getattr(descriptor, "byteOffset", 0) or 0)
    byte_size = int(safe_getattr(descriptor, "byteSize", 0) or 0)
    byte_size = min(byte_size, max_dwords * 4)
    if resource is None or byte_size <= 0:
        return [], []
    blob = bytes(controller.GetBufferData(resource, byte_offset, byte_size))
    dword_count = len(blob) // 4
    if dword_count <= 0:
        return [], []
    payload = blob[: dword_count * 4]
    return (
        list(struct.unpack("<{}I".format(dword_count), payload)),
        list(struct.unpack("<{}f".format(dword_count), payload)),
    )


def mat4_mul_vec4_column_major(values, vector):
    result = []
    for row in range(4):
        result.append(
            values[row + 0] * vector[0]
            + values[row + 4] * vector[1]
            + values[row + 8] * vector[2]
            + values[row + 12] * vector[3]
        )
    return result


def vec3_normalize(values):
    length_sq = sum(v * v for v in values)
    if length_sq <= 1.0e-8:
        return [0.0, 0.0, 1.0]
    inv_len = 1.0 / math.sqrt(length_sq)
    return [v * inv_len for v in values]


def sample_shadow_point(
    controller, rd, shadow_resource, shadow_frame_u32, shadow_frame_f32, point
):
    cascade_count = max(1, min(4, int(shadow_frame_u32[1])))
    cascade_base = 8
    splits = []
    for cascade_index in range(cascade_count):
        offset = cascade_base + cascade_index * 28
        splits.append(shadow_frame_f32[offset + 17])

    # VortexBasic proof camera: (0, 8, 4) looking at (0, 0, 1.5).
    eye = [0.0, 8.0, 4.0]
    target = [0.0, 0.0, 1.5]
    forward = vec3_normalize([target[i] - eye[i] for i in range(3)])
    view_depth = max(0.0, sum((point[i] - eye[i]) * forward[i] for i in range(3)))
    cascade_index = cascade_count - 1
    for index, split_far in enumerate(splits):
        if view_depth <= split_far:
            cascade_index = index
            break

    offset = cascade_base + cascade_index * 28
    matrix = shadow_frame_f32[offset : offset + 16]
    meta0 = shadow_frame_f32[offset + 18 : offset + 22]
    meta1 = shadow_frame_f32[offset + 22 : offset + 26]
    world_texel_size = max(meta0[3], 0.0)
    normal_bias = max(meta1[3], 0.0) + world_texel_size * 0.55
    constant_bias = max(meta1[2], 0.0) + world_texel_size * 0.03
    light_dir = vec3_normalize(shadow_frame_f32[4:7])
    normal = [0.0, 0.0, 1.0]
    biased = [
        point[i] + normal[i] * normal_bias + light_dir[i] * constant_bias
        for i in range(3)
    ]
    clip = mat4_mul_vec4_column_major(matrix, [biased[0], biased[1], biased[2], 1.0])
    if abs(clip[3]) <= 1.0e-6:
        return {
            "point": point,
            "cascade": cascade_index,
            "view_depth": view_depth,
            "valid": False,
            "reason": "zero_w",
        }
    ndc = [clip[i] / clip[3] for i in range(3)]
    uv = [ndc[0] * 0.5 + 0.5, ndc[1] * -0.5 + 0.5]
    valid = 0.0 <= uv[0] <= 1.0 and 0.0 <= uv[1] <= 1.0 and 0.0 <= ndc[2] <= 1.0
    stored = None
    visibility = None
    if valid and shadow_resource is not None:
        inverse_resolution = max(meta0[1], 1.0e-6)
        texture_size = int(round(1.0 / inverse_resolution))
        x = max(0, min(texture_size - 1, int(uv[0] * texture_size)))
        y = max(0, min(texture_size - 1, int(uv[1] * texture_size)))
        sub = rd.Subresource()
        sub.mip = 0
        sub.slice = cascade_index
        sub.sample = 0
        pixel = controller.PickPixel(
            shadow_resource, x, y, sub, rd.CompType.Depth)
        stored = float4_values(pixel)[0]
        visibility = 1.0 if ndc[2] >= stored - 0.0008 else 0.0

    return {
        "point": point,
        "cascade": cascade_index,
        "view_depth": view_depth,
        "valid": valid,
        "uv": uv,
        "receiver_depth": ndc[2],
        "stored_depth": stored,
        "visibility": visibility,
    }


def build_report(controller, report: ReportWriter, capture_path: Path, report_path: Path):
    rd = renderdoc_module()
    resource_names = resource_id_to_name(controller)
    texture_descs = texture_desc_map(controller)
    action_records = collect_action_records(controller)

    stage8_records = [
        record for record in action_records if "Vortex.Stage8.ShadowDepths" in record.path
    ]
    stage8_draws = [
        record
        for record in stage8_records
        if record.name == "ID3D12GraphicsCommandList::DrawInstanced()"
    ]
    stage8_clears = [
        record
        for record in stage8_records
        if "ClearDepthStencilView" in record.name
    ]
    report.append("analysis_profile=vortex_directional_csm_probe")
    report.append("capture_path={}".format(capture_path))
    report.append("stage8_record_count={}".format(len(stage8_records)))
    report.append("stage8_clear_count={}".format(len(stage8_clears)))
    report.append("stage8_draw_count={}".format(len(stage8_draws)))
    report.append("stage8_draw_events={}".format(",".join(str(r.event_id) for r in stage8_draws)))

    by_slice = {}
    for record in stage8_draws:
        controller.SetFrameEvent(record.event_id, True)
        state = controller.GetPipelineState()
        vertex_readonly = state.GetReadOnlyResources(rd.ShaderStage.Vertex, True)
        vertex_blocks = state.GetConstantBlocks(rd.ShaderStage.Vertex)
        report.append("stage8_draw_{}_num_indices={}".format(
            record.event_id, safe_getattr(record, "numIndices", "")))
        report.append("stage8_draw_{}_num_instances={}".format(
            record.event_id, safe_getattr(record, "numInstances", "")))
        report.append("stage8_draw_{}_vertex_readonly_count={}".format(
            record.event_id, len(vertex_readonly)))
        for index, used_descriptor in enumerate(vertex_readonly):
            descriptor = used_descriptor.descriptor
            report.append(
                "stage8_draw_{}_vertex_ro_{}={}|byte_size={}|byte_offset={}".format(
                    record.event_id,
                    index,
                    descriptor_name(resource_names, used_descriptor),
                    # keep these next two fields parseable for old reports
                    int(safe_getattr(descriptor, "byteSize", 0) or 0),
                    int(safe_getattr(descriptor, "byteOffset", 0) or 0),
                )
            )
            report.append(
                "stage8_draw_{}_vertex_ro_{}_access=bind:{} array:{} type:{}".format(
                    record.event_id,
                    index,
                    safe_getattr(used_descriptor.access, "index", ""),
                    safe_getattr(used_descriptor.access, "arrayElement", ""),
                    safe_getattr(used_descriptor.access, "type", ""),
                )
            )
            if record.event_id == stage8_draws[0].event_id and index in (1, 2, 5):
                values_u32, values_f32 = dump_u32_f32(
                    controller, descriptor, 32)
                report.append(
                    "stage8_first_vertex_ro_{}_u32={}".format(
                        index, values_u32))
                report.append(
                    "stage8_first_vertex_ro_{}_f32={}".format(
                        index, ["{:.9f}".format(v) for v in values_f32]))
        report.append("stage8_draw_{}_vertex_constant_block_count={}".format(
            record.event_id, len(vertex_blocks)))
        for index, block in enumerate(vertex_blocks):
            descriptor = block.descriptor
            report.append(
                "stage8_draw_{}_vertex_cb_{}={}|bind={}|byte_size={}|byte_offset={}".format(
                    record.event_id,
                    index,
                    resource_names.get(
                        str(safe_getattr(descriptor, "resource")),
                        str(safe_getattr(descriptor, "resource")),
                    ),
                    safe_getattr(block.access, "index", ""),
                    int(safe_getattr(descriptor, "byteSize", 0) or 0),
                    int(safe_getattr(descriptor, "byteOffset", 0) or 0),
                )
            )
        depth_descriptor = state.GetDepthTarget()
        depth_sample = sample_descriptor(
            controller, rd, resource_names, texture_descs, depth_descriptor, rd.CompType.Depth
        )
        if depth_sample is None:
            report.append("stage8_draw_{}_depth_present=false".format(record.event_id))
            continue
        slice_index = depth_sample["slice"]
        by_slice[slice_index] = depth_sample
        report.append("stage8_draw_{}_depth_present=true".format(record.event_id))
        report.append(
            "stage8_draw_{}_depth={}|slice={}|dims={}x{}|min={}|max={}|center={}".format(
                record.event_id,
                depth_sample["name"],
                slice_index,
                depth_sample["width"],
                depth_sample["height"],
                depth_sample["min"],
                depth_sample["max"],
                depth_sample["center"],
            )
        )

    report.append("stage8_depth_slice_count={}".format(len(by_slice)))
    for slice_index in sorted(by_slice.keys()):
        sample = by_slice[slice_index]
        depth_range = sample["max"][0] - sample["min"][0]
        wrote_non_clear = sample["min"][0] < 0.999 and depth_range > 1.0e-6
        report.append("cascade_{}_resource={}".format(slice_index, sample["name"]))
        report.append("cascade_{}_min={:.9f}".format(slice_index, sample["min"][0]))
        report.append("cascade_{}_max={:.9f}".format(slice_index, sample["max"][0]))
        report.append("cascade_{}_center={:.9f}".format(slice_index, sample["center"][0]))
        report.append("cascade_{}_wrote_non_clear={}".format(slice_index, str(wrote_non_clear).lower()))

    directional_draw = None
    for record in action_records:
        if (
            record.name == "ID3D12GraphicsCommandList::DrawInstanced()"
            and "Vortex.Stage12.DirectionalLight" in record.path
        ):
            directional_draw = record
    report.append("stage12_directional_event={}".format(directional_draw.event_id if directional_draw else ""))
    if directional_draw is not None:
        controller.SetFrameEvent(directional_draw.event_id, True)
        state = controller.GetPipelineState()
        pixel_blocks = state.GetConstantBlocks(rd.ShaderStage.Pixel)
        report.append("stage12_pixel_constant_block_count={}".format(len(pixel_blocks)))
        for index, block in enumerate(pixel_blocks):
            descriptor = block.descriptor
            report.append(
                "stage12_pixel_cb_{}={}|bind={}|byte_size={}|byte_offset={}".format(
                    index,
                    resource_names.get(
                        str(safe_getattr(descriptor, "resource")),
                        str(safe_getattr(descriptor, "resource")),
                    ),
                    safe_getattr(block.access, "index", ""),
                    int(safe_getattr(descriptor, "byteSize", 0) or 0),
                    int(safe_getattr(descriptor, "byteOffset", 0) or 0),
                )
            )
            values_u32, values_f32 = dump_u32_f32(controller, descriptor, 48)
            report.append("stage12_pixel_cb_{}_u32={}".format(index, values_u32))
            report.append(
                "stage12_pixel_cb_{}_f32={}".format(
                    index, ["{:.9f}".format(v) for v in values_f32]))
        pixel_readonly = state.GetReadOnlyResources(rd.ShaderStage.Pixel, True)
        report.append("stage12_pixel_readonly_count={}".format(len(pixel_readonly)))
        shadow_frame_u32 = None
        shadow_frame_f32 = None
        shadow_surface_resource = None
        for index, used_descriptor in enumerate(pixel_readonly):
            descriptor = used_descriptor.descriptor
            readonly_name = descriptor_name(resource_names, used_descriptor)
            if readonly_name == "Vortex.DirectionalShadowSurface":
                shadow_surface_resource = safe_getattr(descriptor, "resource")
            report.append(
                "stage12_pixel_ro_{}={}".format(
                    index, readonly_name
                )
            )
            report.append(
                "stage12_pixel_ro_{}_access=bind:{} array:{} type:{} byte_size:{} byte_offset:{}".format(
                    index,
                    safe_getattr(used_descriptor.access, "index", ""),
                    safe_getattr(used_descriptor.access, "arrayElement", ""),
                    safe_getattr(used_descriptor.access, "type", ""),
                    int(safe_getattr(descriptor, "byteSize", 0) or 0),
                    int(safe_getattr(descriptor, "byteOffset", 0) or 0),
                )
            )
            max_dwords = 128 if index == 3 else 48
            if index < 8:
                values_u32, values_f32 = dump_u32_f32(controller, descriptor, max_dwords)
                report.append("stage12_pixel_ro_{}_u32={}".format(index, values_u32))
                report.append(
                    "stage12_pixel_ro_{}_f32={}".format(
                        index, ["{:.9f}".format(v) for v in values_f32]))
                if index == 3:
                    shadow_frame_u32 = values_u32
                    shadow_frame_f32 = values_f32
        if shadow_frame_u32 is not None and shadow_frame_f32 is not None:
            sample_points = [
                (0.0, 0.5, 0.0),
                (0.0, 1.0, 0.0),
                (0.0, 1.5, 0.0),
                (0.0, 2.0, 0.0),
                (0.0, 2.5, 0.0),
                (0.0, 3.0, 0.0),
                (-0.5, 1.5, 0.0),
                (0.5, 1.5, 0.0),
            ]
            for point in sample_points:
                sample = sample_shadow_point(
                    controller,
                    rd,
                    shadow_surface_resource,
                    shadow_frame_u32,
                    shadow_frame_f32,
                    point,
                )
                label = "stage12_shadow_sample_{:.1f}_{:.1f}_{:.1f}".format(*point)
                report.append("{}={}".format(label, sample))


def main():
    run_ui_script(REPORT_SUFFIX, build_report)


if __name__ == "__main__":
    main()
