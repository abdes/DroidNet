"""RenderDoc UI analyzer for VTX-M08 deferred static SkyLight diffuse captures."""

import builtins
import os
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
    collect_action_records,
    renderdoc_module,
    resource_id_to_name,
    safe_getattr,
    run_ui_script,
)


REPORT_SUFFIX = "_vortex_static_skylight_report.txt"
PROOF_MODE = os.environ.get(
    "VORTEX_STATIC_SKYLIGHT_PROOF_MODE", "ibl-only"
).strip().lower()

ENVIRONMENT_STATIC_DATA_BYTE_SIZE = 672
INVALID_BINDLESS_INDEX = 0xFFFFFFFF
TEXTURES_SHADER_INDEX_BASE = 35816
TEXTURES_CAPACITY = 65536

SKY_LIGHT_U32_OFFSET = 432 // 4
SKY_LIGHT_TINT_F32 = SKY_LIGHT_U32_OFFSET
SKY_LIGHT_RADIANCE_SCALE_F32 = SKY_LIGHT_U32_OFFSET + 3
SKY_LIGHT_DIFFUSE_INTENSITY_F32 = SKY_LIGHT_U32_OFFSET + 4
SKY_LIGHT_SPECULAR_INTENSITY_F32 = SKY_LIGHT_U32_OFFSET + 5
SKY_LIGHT_SOURCE_U32 = SKY_LIGHT_U32_OFFSET + 6
SKY_LIGHT_ENABLED_U32 = SKY_LIGHT_U32_OFFSET + 7
SKY_LIGHT_CUBEMAP_SLOT_U32 = SKY_LIGHT_U32_OFFSET + 8
SKY_LIGHT_IRRADIANCE_MAP_SLOT_U32 = SKY_LIGHT_U32_OFFSET + 10
SKY_LIGHT_PREFILTER_MAP_SLOT_U32 = SKY_LIGHT_U32_OFFSET + 11
SKY_LIGHT_IBL_GENERATION_U32 = SKY_LIGHT_U32_OFFSET + 14
SKY_LIGHT_DIFFUSE_SH_SLOT_U32 = SKY_LIGHT_U32_OFFSET + 15


def records_with_name(action_records, name):
    return [record for record in action_records if record.name == name]


def draw_records_under(action_records, scope_name):
    return [
        record
        for record in action_records
        if scope_name in record.path
        and record.name == "ID3D12GraphicsCommandList::DrawInstanced()"
    ]


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


def make_subresource(rd, descriptor):
    sub = rd.Subresource()
    sub.mip = int(safe_getattr(descriptor, "firstMip", 0) or 0)
    sub.slice = int(safe_getattr(descriptor, "firstSlice", 0) or 0)
    sub.sample = 0
    return sub


def append_output_samples(controller, report, rd, output_descriptor):
    resource = safe_getattr(output_descriptor, "resource")
    sub = make_subresource(rd, output_descriptor)
    min_value, max_value = controller.GetMinMax(resource, sub, rd.CompType.Typeless)
    min_rgba = float4_values(min_value)
    max_rgba = float4_values(max_value)
    max_luma = luminance_rgb(max_rgba)
    report.append(f"scene_color_min={min_rgba}")
    report.append(f"scene_color_max={max_rgba}")
    report.append(f"scene_color_max_luminance={max_luma:.9g}")
    report.append(f"scene_color_nonblack_verdict={str(max_luma > 0.001).lower()}")
    report.append(
        "scene_color_error={}".format(
            "" if max_luma > 0.001 else "IBL-only SceneColor stayed black after static SkyLight draw"
        )
    )


def append_pixel_history_samples(
    controller, report, rd, static_event_id, output_descriptor, directional_event_ids
):
    resource = safe_getattr(output_descriptor, "resource")
    width, height = output_texture_size(controller, resource)
    if width <= 0 or height <= 0:
        raise RuntimeError("Could not resolve SceneColor output dimensions")

    sub = make_subresource(rd, output_descriptor)
    scan_columns = 33
    scan_rows = 19
    max_history_samples = 16
    sample_points = [
        (
            min(width - 1, max(0, int((width - 1) * x / (scan_columns - 1)))),
            min(height - 1, max(0, int((height - 1) * y / (scan_rows - 1)))),
        )
        for y in range(scan_rows)
        for x in range(scan_columns)
    ]

    sampled_count = len(sample_points)
    nonblack_candidates = []
    for x, y in sample_points:
        pixel_value = controller.PickPixel(
            resource, x, y, sub, rd.CompType.Typeless
        )
        pixel_rgba = float4_values(pixel_value)
        pixel_luma = luminance_rgb(pixel_rgba)
        if pixel_luma > 0.001:
            nonblack_candidates.append((x, y, pixel_rgba, pixel_luma))

    history_count = 0
    passed_count = 0
    directional_history_count = 0
    directional_passed_count = 0
    both_history_count = 0
    for x, y, pixel_rgba, pixel_luma in nonblack_candidates[:max_history_samples]:
        history = controller.PixelHistory(
            resource, x, y, sub, rd.CompType.Typeless
        )
        static_mod = None
        directional_mod = None
        for mod in history:
            event_id = int(safe_getattr(mod, "eventId", -1) or -1)
            if event_id == static_event_id:
                static_mod = mod
            elif event_id in directional_event_ids:
                directional_mod = mod
        if static_mod is None:
            report.append(
                "static_skylight_sample_{:04d}_{:04d}=history:false;"
                "after={};after_luma:{:.9g}".format(
                    x, y, pixel_rgba, pixel_luma
                )
            )
            continue

        history_count += 1
        passed = bool(static_mod.Passed()) if hasattr(static_mod, "Passed") else False
        if passed:
            passed_count += 1
        directional_passed = False
        if directional_mod is not None:
            directional_history_count += 1
            directional_passed = (
                bool(directional_mod.Passed())
                if hasattr(directional_mod, "Passed")
                else False
            )
            if directional_passed:
                directional_passed_count += 1
            both_history_count += 1
        report.append(
            "static_skylight_sample_{:04d}_{:04d}=history:true;"
            "passed:{};directional_history:{};directional_passed:{};"
            "after={};after_luma:{:.9g}".format(
                x,
                y,
                str(passed).lower(),
                str(directional_mod is not None).lower(),
                str(directional_passed).lower(),
                pixel_rgba,
                pixel_luma,
            )
        )

    report.append(f"static_skylight_sample_count={sampled_count}")
    report.append(f"static_skylight_history_sample_limit={max_history_samples}")
    report.append(f"static_skylight_history_count={history_count}")
    report.append(f"static_skylight_passed_count={passed_count}")
    report.append(f"directional_history_count={directional_history_count}")
    report.append(f"directional_passed_count={directional_passed_count}")
    report.append(f"direct_plus_ibl_both_history_count={both_history_count}")
    nonblack_after_count = len(nonblack_candidates)
    report.append(f"static_skylight_nonblack_after_count={nonblack_after_count}")
    verdict = history_count > 0 and passed_count > 0 and nonblack_after_count > 0
    if directional_event_ids:
        verdict = (
            verdict
            and directional_history_count > 0
            and directional_passed_count > 0
            and both_history_count > 0
        )
    report.append(f"static_skylight_pixel_history_verdict={str(verdict).lower()}")
    report.append(
        "static_skylight_pixel_history_error={}".format(
            "" if verdict else "Static SkyLight draw did not leave a non-black sampled pixel"
        )
    )


def build_report(controller, report, loaded_capture_path, report_path):
    del loaded_capture_path
    del report_path

    rd = renderdoc_module()
    resources = resource_id_to_name(controller)
    action_records = collect_action_records(controller)

    static_scope_name = "Vortex.Stage12.StaticSkyLight"
    directional_scope_name = "Vortex.Stage12.DirectionalLight"
    point_scope_name = "Vortex.Stage12.PointLight"
    spot_scope_name = "Vortex.Stage12.SpotLight"
    sky_scope_name = "Vortex.Stage15.Sky"

    static_scopes = records_with_name(action_records, static_scope_name)
    static_draws = draw_records_under(action_records, static_scope_name)
    directional_scopes = records_with_name(action_records, directional_scope_name)
    directional_draws = draw_records_under(action_records, directional_scope_name)
    point_scopes = records_with_name(action_records, point_scope_name)
    spot_scopes = records_with_name(action_records, spot_scope_name)
    sky_scopes = records_with_name(action_records, sky_scope_name)
    report.append(f"static_skylight_proof_mode={PROOF_MODE}")
    report.append(f"stage12_static_skylight_scope_count={len(static_scopes)}")
    report.append(f"stage12_static_skylight_draw_count={len(static_draws)}")
    report.append(f"stage12_directional_scope_count={len(directional_scopes)}")
    report.append(f"stage12_directional_draw_count={len(directional_draws)}")
    report.append(f"stage12_point_scope_count={len(point_scopes)}")
    report.append(f"stage12_spot_scope_count={len(spot_scopes)}")
    report.append(f"stage15_sky_scope_count={len(sky_scopes)}")

    if len(static_scopes) != 1 or len(static_draws) != 1:
        raise RuntimeError("Expected exactly one static SkyLight Stage 12 draw")
    if PROOF_MODE not in ("ibl-only", "direct-plus-ibl"):
        raise RuntimeError(f"Unsupported static SkyLight proof mode: {PROOF_MODE}")
    if PROOF_MODE == "ibl-only" and directional_scopes:
        raise RuntimeError("IBL-only proof unexpectedly executed directional lighting")
    if PROOF_MODE == "direct-plus-ibl" and (
        not directional_scopes or not directional_draws
    ):
        raise RuntimeError("Direct-plus-IBL proof did not execute directional lighting")
    if point_scopes or spot_scopes:
        raise RuntimeError("Static SkyLight proof unexpectedly executed local lighting")
    if sky_scopes:
        raise RuntimeError("Static SkyLight proof unexpectedly executed visual sky background")

    static_draw = static_draws[0]
    controller.SetFrameEvent(static_draw.event_id, True)
    state = controller.GetPipelineState()
    report.append(f"static_skylight_draw_event_id={static_draw.event_id}")

    env_descriptor = find_environment_static_descriptor(state, rd)
    if env_descriptor is None:
        raise RuntimeError("Static SkyLight draw does not bind EnvironmentStaticData")

    env_resource = safe_getattr(env_descriptor, "resource")
    byte_offset = int(safe_getattr(env_descriptor, "byteOffset", 0) or 0)
    byte_size = int(safe_getattr(env_descriptor, "byteSize", 0) or 0)
    raw = controller.GetBufferData(env_resource, byte_offset, byte_size)
    blob = bytes(raw)
    values_u32 = struct.unpack(
        f"<{len(blob) // 4}I", blob[: (len(blob) // 4) * 4]
    )
    values_f32 = struct.unpack(
        f"<{len(blob) // 4}f", blob[: (len(blob) // 4) * 4]
    )

    sky_light_enabled = values_u32[SKY_LIGHT_ENABLED_U32]
    sky_light_source = values_u32[SKY_LIGHT_SOURCE_U32]
    cubemap_slot = values_u32[SKY_LIGHT_CUBEMAP_SLOT_U32]
    diffuse_sh_slot = values_u32[SKY_LIGHT_DIFFUSE_SH_SLOT_U32]
    irradiance_map_slot = values_u32[SKY_LIGHT_IRRADIANCE_MAP_SLOT_U32]
    prefilter_map_slot = values_u32[SKY_LIGHT_PREFILTER_MAP_SLOT_U32]
    ibl_generation = values_u32[SKY_LIGHT_IBL_GENERATION_U32]
    radiance_scale = values_f32[SKY_LIGHT_RADIANCE_SCALE_F32]
    diffuse_intensity = values_f32[SKY_LIGHT_DIFFUSE_INTENSITY_F32]
    specular_intensity = values_f32[SKY_LIGHT_SPECULAR_INTENSITY_F32]
    tint = values_f32[SKY_LIGHT_TINT_F32 : SKY_LIGHT_TINT_F32 + 3]

    report.append(
        f"environment_static_resource={resources.get(str(env_resource), str(env_resource))}"
    )
    report.append(f"environment_static_byte_offset={byte_offset}")
    report.append(f"environment_static_byte_size={byte_size}")
    report.append(f"sky_light_enabled={sky_light_enabled}")
    report.append(f"sky_light_source={sky_light_source}")
    report.append(f"sky_light_cubemap_slot={cubemap_slot}")
    report.append(
        f"sky_light_cubemap_slot_in_textures={str(in_textures_domain(cubemap_slot)).lower()}"
    )
    report.append(f"sky_light_diffuse_sh_slot={diffuse_sh_slot}")
    report.append(f"sky_light_diffuse_sh_slot_valid={str(diffuse_sh_slot != INVALID_BINDLESS_INDEX).lower()}")
    report.append(f"sky_light_irradiance_map_slot={irradiance_map_slot}")
    report.append(f"sky_light_prefilter_map_slot={prefilter_map_slot}")
    report.append(f"sky_light_ibl_generation={ibl_generation}")
    report.append(f"sky_light_radiance_scale={radiance_scale:.9g}")
    report.append(f"sky_light_diffuse_intensity={diffuse_intensity:.9g}")
    report.append(f"sky_light_specular_intensity={specular_intensity:.9g}")
    report.append("sky_light_tint={:.9g},{:.9g},{:.9g}".format(tint[0], tint[1], tint[2]))

    if sky_light_enabled != 1:
        raise RuntimeError("Static SkyLight draw did not receive enabled SkyLight")
    if diffuse_sh_slot == INVALID_BINDLESS_INDEX:
        raise RuntimeError("Static SkyLight draw did not receive diffuse SH slot")
    if radiance_scale <= 0.0 or diffuse_intensity <= 0.0:
        raise RuntimeError("Static SkyLight radiance/diffuse intensity is not positive")
    if prefilter_map_slot != INVALID_BINDLESS_INDEX:
        raise RuntimeError("M08 static diffuse proof unexpectedly has specular prefilter product")

    outputs = state.GetOutputTargets()
    report.append(f"static_skylight_output_count={len(outputs)}")
    if len(outputs) != 1:
        raise RuntimeError(f"Expected one SceneColor output, got {len(outputs)}")
    output_resource = safe_getattr(outputs[0], "resource")
    report.append(
        f"static_skylight_output_resource={resources.get(str(output_resource), str(output_resource))}"
    )
    append_output_samples(controller, report, rd, outputs[0])
    append_pixel_history_samples(
        controller,
        report,
        rd,
        static_draw.event_id,
        outputs[0],
        {draw.event_id for draw in directional_draws},
    )


def main():
    run_ui_script(REPORT_SUFFIX, build_report)


if __name__ == "__main__":
    main()
