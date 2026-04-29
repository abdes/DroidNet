"""RenderDoc UI analyzer for VTX-M08 visual SkySphere captures."""

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
    safe_getattr,
    run_ui_script,
)


REPORT_SUFFIX = "_vortex_skybox_report.txt"

ENVIRONMENT_STATIC_DATA_BYTE_SIZE = 672
INVALID_BINDLESS_INDEX = 0xFFFFFFFF
TEXTURES_SHADER_INDEX_BASE = 35816
TEXTURES_CAPACITY = 65536

SKY_ATMOSPHERE_ENABLED_U32 = (224 + 148) // 4
SKY_SPHERE_U32_OFFSET = 496 // 4
SKY_SPHERE_INTENSITY_F32 = SKY_SPHERE_U32_OFFSET + 3
SKY_SPHERE_TINT_F32 = SKY_SPHERE_U32_OFFSET + 4
SKY_SPHERE_ROTATION_F32 = SKY_SPHERE_U32_OFFSET + 7
SKY_SPHERE_SOURCE_U32 = SKY_SPHERE_U32_OFFSET + 8
SKY_SPHERE_ENABLED_U32 = SKY_SPHERE_U32_OFFSET + 9
SKY_SPHERE_CUBEMAP_SLOT_U32 = SKY_SPHERE_U32_OFFSET + 10
SKY_SPHERE_CUBEMAP_MAX_MIP_U32 = SKY_SPHERE_U32_OFFSET + 11

SKY_SPHERE_SOURCE_CUBEMAP = 0


def records_with_name(action_records, name):
    return [record for record in action_records if record.name == name]


def draw_records_under(action_records, scope_name):
    return [
        record
        for record in action_records
        if scope_name in record.path
        and record.name == "ID3D12GraphicsCommandList::DrawInstanced()"
    ]


def last_draw_record_under(action_records, scope_name):
    records = draw_records_under(action_records, scope_name)
    return records[-1] if records else None


def append_count(report, label, actual, expected):
    report.append(f"{label}_actual={actual}")
    report.append(f"{label}_expected={expected}")
    match = actual == expected
    report.append(f"{label}_match={str(match).lower()}")
    if not match:
        raise RuntimeError(f"{label} mismatch: expected {expected}, got {actual}")


def float4_values(pixel_value):
    if pixel_value is None:
        return [0.0, 0.0, 0.0, 0.0]
    if hasattr(pixel_value, "col"):
        return float4_values(pixel_value.col)
    if hasattr(pixel_value, "value"):
        return float4_values(pixel_value.value)
    return [float(v) for v in list(pixel_value.floatValue)[:4]]


def luminance_rgb(values):
    return values[0] * 0.2126 + values[1] * 0.7152 + values[2] * 0.0722


def in_textures_domain(slot):
    return (
        slot != INVALID_BINDLESS_INDEX
        and slot >= TEXTURES_SHADER_INDEX_BASE
        and slot < TEXTURES_SHADER_INDEX_BASE + TEXTURES_CAPACITY
    )


def find_environment_static_descriptor(state, rd):
    readonly = state.GetReadOnlyResources(rd.ShaderStage.Pixel, True)
    for binding in readonly:
        descriptor = binding.descriptor
        byte_size = int(safe_getattr(descriptor, "byteSize", 0) or 0)
        if byte_size == ENVIRONMENT_STATIC_DATA_BYTE_SIZE:
            return descriptor
    return None


def output_texture_size(controller, resource):
    for desc in controller.GetTextures():
        if str(safe_getattr(desc, "resourceId", "")) == str(resource):
            return (
                int(safe_getattr(desc, "width", 0) or 0),
                int(safe_getattr(desc, "height", 0) or 0),
            )
    return (0, 0)


def append_output_samples(controller, report, rd, output_descriptor):
    resource = safe_getattr(output_descriptor, "resource")
    sub = rd.Subresource()
    sub.mip = int(safe_getattr(output_descriptor, "firstMip", 0) or 0)
    sub.slice = int(safe_getattr(output_descriptor, "firstSlice", 0) or 0)
    sub.sample = 0

    min_value, max_value = controller.GetMinMax(resource, sub, rd.CompType.Typeless)
    min_rgba = float4_values(min_value)
    max_rgba = float4_values(max_value)
    report.append(f"scene_color_min={min_rgba}")
    report.append(f"scene_color_max={max_rgba}")
    max_luma = luminance_rgb(max_rgba)
    report.append(f"scene_color_max_luminance={max_luma:.9g}")
    if max_luma <= 0.001:
        raise RuntimeError("SceneColor output remains black after the sky draw")

    width, height = output_texture_size(controller, resource)
    report.append(f"scene_color_size={width}x{height}")
    if width <= 0 or height <= 0:
        raise RuntimeError("Could not resolve SceneColor output dimensions")

    report.append(f"sky_sample_nonblack_count={1 if max_luma > 0.001 else 0}")


def make_subresource(rd, descriptor):
    sub = rd.Subresource()
    sub.mip = int(safe_getattr(descriptor, "firstMip", 0) or 0)
    sub.slice = int(safe_getattr(descriptor, "firstSlice", 0) or 0)
    sub.sample = 0
    return sub


def append_background_pixel_samples(
    controller, report, rd, sky_draw_event_id, output_descriptor
):
    resource = safe_getattr(output_descriptor, "resource")
    if resource is None:
        raise RuntimeError("Skybox background proof requires a color target")

    width, height = output_texture_size(controller, resource)
    if width <= 0 or height <= 0:
        raise RuntimeError("Could not resolve SceneColor output dimensions")

    color_sub = make_subresource(rd, output_descriptor)
    xs = [width // 10, width // 4, width // 2, (width * 3) // 4, (width * 9) // 10]
    ys = [height // 10, height // 4, height // 2, (height * 3) // 4, (height * 9) // 10]

    controller.SetFrameEvent(sky_draw_event_id, True)
    sampled_count = 0
    sky_history_count = 0
    sky_passed_count = 0
    sky_nonblack_after = 0
    sky_changed = 0
    sky_nonblack_coords = []
    for y in ys:
        for x in xs:
            sampled_count += 1
            history = controller.PixelHistory(
                resource, x, y, color_sub, rd.CompType.Typeless
            )
            sky_mod = None
            for mod in history:
                if int(safe_getattr(mod, "eventId", -1) or -1) == sky_draw_event_id:
                    sky_mod = mod
                    break
            if sky_mod is None:
                report.append(
                    f"sky_background_sample_{x:04d}_{y:04d}=history_count:{len(history)};sky_event:false"
                )
                continue

            sky_history_count += 1
            passed = bool(sky_mod.Passed()) if hasattr(sky_mod, "Passed") else False
            shader_discarded = bool(safe_getattr(sky_mod, "shaderDiscarded", False))
            if passed:
                sky_passed_count += 1
            if not shader_discarded:
                sky_changed += 1
            pixel_value = controller.PickPixel(
                resource, x, y, color_sub, rd.CompType.Typeless
            )
            pixel_rgba = float4_values(pixel_value)
            pixel_luma = luminance_rgb(pixel_rgba)
            if pixel_luma > 0.001:
                sky_nonblack_after += 1
                sky_nonblack_coords.append((x, y))
            report.append(
                "sky_background_sample_{:04d}_{:04d}=sky_event:true;"
                "passed:{};shader_discarded:{};after={};after_luma:{:.9g}".format(
                    x,
                    y,
                    str(passed).lower(),
                    str(shader_discarded).lower(),
                    pixel_rgba,
                    pixel_luma,
                )
            )

    verdict = sky_history_count > 0 and sky_passed_count > 0 and sky_changed > 0
    report.append(f"sky_background_sample_count={sampled_count}")
    report.append(f"sky_background_sky_history_count={sky_history_count}")
    report.append(f"sky_background_sky_passed_count={sky_passed_count}")
    report.append(f"sky_background_sky_not_discarded_count={sky_changed}")
    report.append(f"sky_background_nonblack_after_count={sky_nonblack_after}")
    report.append(f"sky_background_overall_verdict={str(verdict).lower()}")
    return sky_nonblack_coords


def append_tonemap_pixel_samples(
    controller, report, rd, action_records, scene_width, scene_height, sky_coords
):
    tonemap_draw = last_draw_record_under(action_records, "Vortex.PostProcess.Tonemap")
    if tonemap_draw is None:
        report.append("stage22_tonemap_draw_found=false")
        return

    controller.SetFrameEvent(tonemap_draw.event_id, True)
    state = controller.GetPipelineState()
    resources = resource_id_to_name(controller)
    tonemap_constants = None
    for block in state.GetConstantBlocks(rd.ShaderStage.Pixel):
        descriptor = block.descriptor
        name = resources.get(str(descriptor.resource), str(descriptor.resource))
        byte_size = int(safe_getattr(descriptor, "byteSize", 0) or 0)
        report.append(
            "stage22_tonemap_constant_block resource='{}' byte_size={} byte_offset={}".format(
                name,
                byte_size,
                int(safe_getattr(descriptor, "byteOffset", 0) or 0),
            )
        )
        if "Vortex.PostProcess.Tonemap.PassConstants" in name and byte_size >= 32:
            tonemap_constants = descriptor

    if tonemap_constants is not None:
        offset = int(safe_getattr(tonemap_constants, "byteOffset", 0) or 0)
        data = bytes(controller.GetBufferData(
            tonemap_constants.resource, offset, min(32, int(safe_getattr(tonemap_constants, "byteSize", 32) or 32))
        ))
        if len(data) >= 32:
            source_slot, exposure_slot, bloom_slot, tone_mapper = struct.unpack_from("<4I", data, 0)
            exposure, gamma, bloom_intensity, _pad0 = struct.unpack_from("<4f", data, 16)
            report.append(f"stage22_tonemap_source_texture_index={source_slot}")
            report.append(f"stage22_tonemap_exposure_buffer_index={exposure_slot}")
            report.append(f"stage22_tonemap_bloom_texture_index={bloom_slot}")
            report.append(f"stage22_tonemap_tone_mapper={tone_mapper}")
            report.append(f"stage22_tonemap_fixed_exposure={exposure:.9g}")
            report.append(f"stage22_tonemap_gamma={gamma:.9g}")
            report.append(f"stage22_tonemap_bloom_intensity={bloom_intensity:.9g}")

    outputs = state.GetOutputTargets()
    if not outputs:
        report.append("stage22_tonemap_draw_found=true")
        report.append("stage22_tonemap_output_found=false")
        return

    output = outputs[0]
    resource = safe_getattr(output, "resource")
    width, height = output_texture_size(controller, resource)
    if width <= 0 or height <= 0:
        report.append("stage22_tonemap_draw_found=true")
        report.append("stage22_tonemap_output_found=false")
        return

    sub = make_subresource(rd, output)
    min_value, max_value = controller.GetMinMax(resource, sub, rd.CompType.Typeless)
    min_rgba = float4_values(min_value)
    max_rgba = float4_values(max_value)
    max_luma = luminance_rgb(max_rgba)
    report.append("stage22_tonemap_draw_found=true")
    report.append(f"stage22_tonemap_draw_event={tonemap_draw.event_id}")
    report.append("stage22_tonemap_output_found=true")
    report.append(f"stage22_tonemap_output_size={width}x{height}")
    report.append(f"stage22_tonemap_min={min_rgba}")
    report.append(f"stage22_tonemap_max={max_rgba}")
    report.append(f"stage22_tonemap_max_luminance={max_luma:.9g}")

    xs = [scene_width // 10, scene_width // 4, scene_width // 2, (scene_width * 3) // 4, (scene_width * 9) // 10]
    ys = [scene_height // 10, scene_height // 4, scene_height // 2, (scene_height * 3) // 4, (scene_height * 9) // 10]
    nonblack = 0
    sky_nonblack = 0
    for y in ys:
        for x in xs:
            tx = min(width - 1, max(0, int(round((x / max(scene_width - 1, 1)) * (width - 1)))))
            ty = min(height - 1, max(0, int(round((y / max(scene_height - 1, 1)) * (height - 1)))))
            pixel_value = controller.PickPixel(
                resource, tx, ty, sub, rd.CompType.Typeless
            )
            rgba = float4_values(pixel_value)
            luma = luminance_rgb(rgba)
            if luma > 0.001:
                nonblack += 1
            if (x, y) in sky_coords and luma > 0.001:
                sky_nonblack += 1
            report.append(
                "stage22_tonemap_sample_{:04d}_{:04d}=mapped:{}_{};after={};after_luma:{:.9g}".format(
                    x, y, tx, ty, rgba, luma
                )
            )
    report.append(f"stage22_tonemap_nonblack_sample_count={nonblack}")
    report.append(f"stage22_tonemap_sky_sample_count={len(sky_coords)}")
    report.append(f"stage22_tonemap_sky_nonblack_after_count={sky_nonblack}")


def build_report(controller, report: ReportWriter, capture_path: Path, report_path: Path):
    rd = renderdoc_module()
    action_records = collect_action_records(controller)
    resources = resource_id_to_name(controller)

    report.append("analysis_profile=vortex_skybox")
    report.append(f"capture_path={capture_path}")
    report.append(f"report_path={report_path}")
    report.append(f"total_actions={len(action_records)}")

    sky_scope_name = "Vortex.Stage15.Sky"
    atmosphere_scope_name = "Vortex.Stage15.Atmosphere"
    fog_scope_name = "Vortex.Stage15.Fog"

    append_count(
        report,
        "stage15_sky_scope_count",
        len(records_with_name(action_records, sky_scope_name)),
        1,
    )
    append_count(
        report,
        "stage15_sky_draw_count",
        len(draw_records_under(action_records, sky_scope_name)),
        1,
    )
    append_count(
        report,
        "stage15_atmosphere_scope_count",
        len(records_with_name(action_records, atmosphere_scope_name)),
        0,
    )
    report.append(
        f"stage15_fog_scope_count={len(records_with_name(action_records, fog_scope_name))}"
    )

    sky_draw = draw_records_under(action_records, sky_scope_name)[0]
    controller.SetFrameEvent(sky_draw.event_id, True)
    state = controller.GetPipelineState()
    report.append(f"sky_draw_event_id={sky_draw.event_id}")

    env_descriptor = find_environment_static_descriptor(state, rd)
    if env_descriptor is None:
        raise RuntimeError("Stage15 sky draw does not bind EnvironmentStaticData")

    resource = safe_getattr(env_descriptor, "resource")
    byte_offset = int(safe_getattr(env_descriptor, "byteOffset", 0) or 0)
    byte_size = int(safe_getattr(env_descriptor, "byteSize", 0) or 0)
    raw = controller.GetBufferData(resource, byte_offset, byte_size)
    blob = bytes(raw)
    values_u32 = struct.unpack(
        f"<{len(blob) // 4}I", blob[: (len(blob) // 4) * 4]
    )
    values_f32 = struct.unpack(
        f"<{len(blob) // 4}f", blob[: (len(blob) // 4) * 4]
    )

    atmosphere_enabled = values_u32[SKY_ATMOSPHERE_ENABLED_U32]
    sky_sphere_source = values_u32[SKY_SPHERE_SOURCE_U32]
    sky_sphere_enabled = values_u32[SKY_SPHERE_ENABLED_U32]
    sky_sphere_cubemap_slot = values_u32[SKY_SPHERE_CUBEMAP_SLOT_U32]
    sky_sphere_cubemap_max_mip = values_u32[SKY_SPHERE_CUBEMAP_MAX_MIP_U32]
    sky_sphere_intensity = values_f32[SKY_SPHERE_INTENSITY_F32]
    sky_sphere_tint = values_f32[SKY_SPHERE_TINT_F32 : SKY_SPHERE_TINT_F32 + 3]
    sky_sphere_rotation = values_f32[SKY_SPHERE_ROTATION_F32]

    cubemap_slot_valid = in_textures_domain(sky_sphere_cubemap_slot)
    sky_sphere_valid = (
        sky_sphere_enabled == 1
        and sky_sphere_source == SKY_SPHERE_SOURCE_CUBEMAP
        and cubemap_slot_valid
    )
    report.append(
        f"environment_static_resource={resources.get(str(resource), str(resource))}"
    )
    report.append(f"environment_static_byte_offset={byte_offset}")
    report.append(f"environment_static_byte_size={byte_size}")
    report.append(f"atmosphere_enabled={atmosphere_enabled}")
    report.append(f"sky_sphere_source={sky_sphere_source}")
    report.append(f"sky_sphere_enabled={sky_sphere_enabled}")
    report.append(f"sky_sphere_cubemap_slot={sky_sphere_cubemap_slot}")
    report.append(f"sky_sphere_cubemap_slot_valid={str(cubemap_slot_valid).lower()}")
    report.append(
        f"sky_sphere_cubemap_slot_in_textures={str(cubemap_slot_valid).lower()}"
    )
    report.append(f"sky_sphere_cubemap_max_mip={sky_sphere_cubemap_max_mip}")
    report.append(f"sky_sphere_intensity={sky_sphere_intensity:.9g}")
    report.append(
        "sky_sphere_tint={:.9g},{:.9g},{:.9g}".format(
            sky_sphere_tint[0], sky_sphere_tint[1], sky_sphere_tint[2]
        )
    )
    report.append(f"sky_sphere_rotation_radians={sky_sphere_rotation:.9g}")
    report.append(f"sky_sphere_valid_for_cubemap={str(sky_sphere_valid).lower()}")
    if not sky_sphere_valid:
        raise RuntimeError("Stage15 sky draw did not receive a valid cubemap SkySphere")
    if atmosphere_enabled != 0:
        raise RuntimeError("Procedural atmosphere remained enabled for the skybox proof")

    outputs = state.GetOutputTargets()
    report.append(f"sky_draw_output_count={len(outputs)}")
    if len(outputs) != 1:
        raise RuntimeError(f"Expected one SceneColor output, got {len(outputs)}")
    output_resource = safe_getattr(outputs[0], "resource")
    report.append(
        f"sky_draw_output_resource={resources.get(str(output_resource), str(output_resource))}"
    )
    append_output_samples(controller, report, rd, outputs[0])
    scene_width, scene_height = output_texture_size(controller, output_resource)
    sky_coords = append_background_pixel_samples(
        controller, report, rd, sky_draw.event_id, outputs[0]
    )
    append_tonemap_pixel_samples(
        controller, report, rd, action_records, scene_width, scene_height, sky_coords
    )


def main():
    run_ui_script(REPORT_SUFFIX, build_report)


if __name__ == "__main__":
    main()
