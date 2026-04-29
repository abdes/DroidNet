"""Focused RenderDoc probe for Physics direct-lighting floor failures."""

import builtins
import math
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


REPORT_SUFFIX = "_physics_direct_lighting_probe.txt"
DRAW_NAME = "ID3D12GraphicsCommandList::DrawInstanced()"


def float4_values(pixel_value):
    return [float(v) for v in list(pixel_value.floatValue)[:4]]


def vec3_len(value):
    return math.sqrt(value[0] * value[0] + value[1] * value[1] + value[2] * value[2])


def vec3_dot(a, b):
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]


def vec3_normalize(value):
    length = vec3_len(value)
    if length <= 1.0e-8:
        return [0.0, 0.0, 1.0]
    return [value[0] / length, value[1] / length, value[2] / length]


def octa_decode(encoded_xy):
    f = [encoded_xy[0] * 2.0 - 1.0, encoded_xy[1] * 2.0 - 1.0]
    n = [f[0], f[1], 1.0 - abs(f[0]) - abs(f[1])]
    if n[2] < 0.0:
        x = (1.0 - abs(n[1])) * (1.0 if n[0] >= 0.0 else -1.0)
        y = (1.0 - abs(n[0])) * (1.0 if n[1] >= 0.0 else -1.0)
        n[0] = x
        n[1] = y
    return vec3_normalize(n)


def make_subresource(rd, descriptor):
    sub = rd.Subresource()
    sub.mip = int(safe_getattr(descriptor, "firstMip", 0) or 0)
    sub.slice = int(safe_getattr(descriptor, "firstSlice", 0) or 0)
    sub.sample = 0
    return sub


def texture_desc_map(controller):
    descriptions = {}
    for desc in controller.GetTextures():
        resource_id = safe_getattr(desc, "resourceId")
        if resource_id is not None:
            descriptions[str(resource_id)] = desc
    return descriptions


def descriptor_name(resource_names, descriptor):
    resource_id = safe_getattr(descriptor, "resource")
    if resource_id is None:
        return "<none>"
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


def find_output_descriptor(state, resource_names, wanted_name):
    for descriptor in state.GetOutputTargets():
        if descriptor_name(resource_names, descriptor) == wanted_name:
            return descriptor
    return None


def find_readonly_descriptor(state, rd, resource_names, wanted_name):
    for stage in (rd.ShaderStage.Pixel, rd.ShaderStage.Vertex):
        for used in state.GetReadOnlyResources(stage, True):
            descriptor = used.descriptor
            if descriptor_name(resource_names, descriptor) == wanted_name:
                return descriptor
    return None


def pick(controller, resource, x, y, sub, rd):
    return float4_values(controller.PickPixel(resource, x, y, sub, rd.CompType.Typeless))


def find_action(action_records, path_token):
    selected = None
    for record in action_records:
        if record.name == DRAW_NAME and path_token in record.path:
            selected = record
    return selected


def summarize_resource(controller, rd, report, label, descriptor):
    if descriptor is None:
        report.append(f"{label}_present=false")
        return

    sub = make_subresource(rd, descriptor)
    min_value, max_value = controller.GetMinMax(
        descriptor.resource, sub, rd.CompType.Typeless)
    report.append(f"{label}_present=true")
    report.append(f"{label}_resource={descriptor.resource}")
    report.append(f"{label}_min={float4_values(min_value)}")
    report.append(f"{label}_max={float4_values(max_value)}")


def build_report(controller, report: ReportWriter, capture_path: Path, report_path: Path):
    rd = renderdoc_module()
    resource_names = resource_id_to_name(controller)
    textures = texture_desc_map(controller)
    actions = collect_action_records(controller)

    stage9_draw = find_action(actions, "Vortex.Stage9.BasePass")
    stage12_draw = find_action(actions, "Vortex.Stage12.DirectionalLight")
    if stage9_draw is None:
        raise RuntimeError("No Stage9 base-pass draw found")
    if stage12_draw is None:
        raise RuntimeError("No Stage12 directional-light draw found")

    report.append("analysis_profile=physics_direct_lighting")
    report.append(f"capture_path={capture_path}")
    report.append(f"stage9_event={stage9_draw.event_id}")
    report.append(f"stage12_directional_event={stage12_draw.event_id}")

    controller.SetFrameEvent(stage12_draw.event_id, True)
    state = controller.GetPipelineState()

    scene_color = find_output_descriptor(state, resource_names, "SceneColor")
    gbuffer_base = find_readonly_descriptor(state, rd, resource_names, "GBufferBaseColor")
    gbuffer_normal = find_readonly_descriptor(state, rd, resource_names, "GBufferNormal")
    scene_depth = find_readonly_descriptor(state, rd, resource_names, "SceneDepth")

    summarize_resource(controller, rd, report, "scene_color_after_directional", scene_color)
    summarize_resource(controller, rd, report, "gbuffer_base", gbuffer_base)
    summarize_resource(controller, rd, report, "gbuffer_normal", gbuffer_normal)
    summarize_resource(controller, rd, report, "scene_depth", scene_depth)

    if scene_color is None or gbuffer_base is None or gbuffer_normal is None:
        raise RuntimeError("Required SceneColor/GBuffer descriptors are missing")

    scene_color_sub = make_subresource(rd, scene_color)
    base_sub = make_subresource(rd, gbuffer_base)
    normal_sub = make_subresource(rd, gbuffer_normal)
    depth_sub = make_subresource(rd, scene_depth) if scene_depth is not None else None

    texture_desc = textures.get(str(scene_color.resource))
    width = int(safe_getattr(texture_desc, "width", 1) or 1)
    height = int(safe_getattr(texture_desc, "height", 1) or 1)
    report.append(f"framebuffer_dims={width}x{height}")

    # Includes the reported black center plus a coarse scan around the visible
    # floor region. The probe labels floor candidates by authored base color
    # and upward normal, then records whether the direct-light result is zero.
    sample_points = [
        (width // 2, height // 2),
        (width // 2, int(height * 0.58)),
        (width // 2, int(height * 0.65)),
        (width // 2, int(height * 0.72)),
        (int(width * 0.40), int(height * 0.58)),
        (int(width * 0.60), int(height * 0.58)),
        (int(width * 0.35), int(height * 0.70)),
        (int(width * 0.65), int(height * 0.70)),
        (int(width * 0.50), int(height * 0.82)),
    ]

    floor_candidate_count = 0
    zero_direct_floor_count = 0
    nonzero_direct_floor_count = 0
    floor_samples = []
    for x, y in sample_points:
        base = pick(controller, gbuffer_base.resource, x, y, base_sub, rd)
        normal_encoded = pick(controller, gbuffer_normal.resource, x, y, normal_sub, rd)
        normal = octa_decode(normal_encoded[:2])
        direct = pick(controller, scene_color.resource, x, y, scene_color_sub, rd)
        depth = pick(controller, scene_depth.resource, x, y, depth_sub, rd) if scene_depth is not None else [0.0] * 4
        base_delta = max(
            abs(base[0] - 0.44),
            abs(base[1] - 0.47),
            abs(base[2] - 0.50),
        )
        is_floor = base_delta < 0.08 and normal[2] > 0.75
        direct_max = max(abs(direct[0]), abs(direct[1]), abs(direct[2]))
        if is_floor:
            floor_candidate_count += 1
            if direct_max <= 1.0e-5:
                zero_direct_floor_count += 1
            else:
                nonzero_direct_floor_count += 1
        floor_samples.append((x, y, is_floor, base, normal, direct, depth))

    report.append(f"floor_candidate_count={floor_candidate_count}")
    report.append(f"zero_direct_floor_count={zero_direct_floor_count}")
    report.append(f"nonzero_direct_floor_count={nonzero_direct_floor_count}")
    for index, (x, y, is_floor, base, normal, direct, depth) in enumerate(floor_samples):
        report.append(f"sample_{index}_xy={x},{y}")
        report.append(f"sample_{index}_is_floor={str(is_floor).lower()}")
        report.append(f"sample_{index}_base={base}")
        report.append(f"sample_{index}_normal={normal}")
        report.append(f"sample_{index}_direct={direct}")
        report.append(f"sample_{index}_depth={depth}")

    # Dump the directional light constants CBVs. The second 32-bit root constant
    # points at a bindless constant buffer, so RenderDoc exposes both the view
    # constants block and the pass constants descriptor here.
    blocks = state.GetConstantBlocks(rd.ShaderStage.Pixel)
    report.append(f"pixel_constant_block_count={len(blocks)}")
    for index, block in enumerate(blocks):
        descriptor = block.descriptor
        report.append(
            f"pixel_cb_{index}_resource={descriptor_name(resource_names, descriptor)}")
        report.append(
            "pixel_cb_{}_bind={}|byte_size={}|byte_offset={}".format(
                index,
                safe_getattr(block.access, "index", ""),
                int(safe_getattr(descriptor, "byteSize", 0) or 0),
                int(safe_getattr(descriptor, "byteOffset", 0) or 0),
            ))
        values_u32, values_f32 = dump_u32_f32(controller, descriptor, 48)
        if values_f32:
            report.append(
                "pixel_cb_{}_f32={}".format(
                    index, ["{:.9f}".format(v) for v in values_f32]))
        if values_u32:
            report.append(f"pixel_cb_{index}_u32={values_u32}")
        if index == 2 and len(values_f32) >= 40:
            light_color = values_f32[4:8]
            light_direction = vec3_normalize(values_f32[8:11])
            atmosphere = values_f32[36:40]
            report.append(f"directional_light_color_intensity={light_color}")
            report.append(f"directional_light_direction={light_direction}")
            report.append(f"directional_light_atmosphere={atmosphere}")
            for sample_index, (_, _, is_floor, _base, normal, _direct, _depth) in enumerate(floor_samples):
                if is_floor:
                    report.append(
                        "sample_{}_nol={:.9f}".format(
                            sample_index,
                            max(vec3_dot(normal, light_direction), 0.0)))


def main():
    run_ui_script(REPORT_SUFFIX, build_report)


if __name__ == "__main__":
    main()
