"""RenderDoc UI analyzer for VortexBasic stage products."""

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

    raise RuntimeError(
        "Unable to locate tools/shadows from the RenderDoc script path."
    )


if "pyrenderdoc" in globals():
    builtins.pyrenderdoc = pyrenderdoc


SCRIPT_DIR = resolve_script_dir()
if str(SCRIPT_DIR) not in sys.path:
    sys.path.insert(0, str(SCRIPT_DIR))

from renderdoc_ui_analysis import (  # noqa: E402
    ReportWriter,
    collect_resource_records_raw,
    collect_action_records,
    renderdoc_module,
    resource_id_to_name,
    run_ui_script,
    safe_getattr,
)


REPORT_SUFFIX = "_vortexbasic_products_report.txt"
DRAW_NAME = "ID3D12GraphicsCommandList::DrawInstanced()"
COPY_NAME = "ID3D12GraphicsCommandList::CopyTextureRegion()"
LOCAL_FOG_EXECUTE_INDIRECT_NAME = "ExecuteIndirect()"


def records_with_name(action_records, name):
    return [record for record in action_records if record.name == name]


def records_under_prefix(action_records, prefix):
    prefix_with_sep = prefix + " > "
    return [
        record
        for record in action_records
        if record.path.startswith(prefix_with_sep)
    ]


def float4_values(pixel_value):
    return [float(v) for v in list(pixel_value.floatValue)[:4]]


def vec_max_abs(values):
    return max(abs(v) for v in values[:4]) if values else 0.0


def rgb_nonzero(min_values, max_values, epsilon=1.0e-6):
    rgb_delta = [b - a for a, b in zip(min_values[:3], max_values[:3])]
    return max(abs(v) for v in rgb_delta) > epsilon or max(abs(v) for v in max_values[:3]) > epsilon


def vec_diff(a_values, b_values):
    return [b - a for a, b in zip(a_values[:4], b_values[:4])]


def is_nonzero_range(min_values, max_values, epsilon=1.0e-6):
    return vec_max_abs(vec_diff(min_values, max_values)) > epsilon or vec_max_abs(max_values) > epsilon


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
        if resource_id is None:
            continue
        descriptions[str(resource_id)] = desc
    return descriptions


def round_up_pow2(value):
    value = max(int(value), 1)
    return 1 << (value - 1).bit_length()


def expected_screen_hzb_extent(value):
    return max(round_up_pow2(value) >> 1, 1)


def expected_screen_hzb_mip_count(width, height):
    return max(max(int(width), int(height)).bit_length() - 1, 1)


def resource_dims(texture_desc, descriptor):
    width = int(safe_getattr(texture_desc, "width", 1) or 1)
    height = int(safe_getattr(texture_desc, "height", 1) or 1)
    mip = int(safe_getattr(descriptor, "firstMip", 0) or 0)
    width = max(1, width >> mip)
    height = max(1, height >> mip)
    return width, height


def probe_positions(width, height):
    max_x = max(0, width - 1)
    max_y = max(0, height - 1)
    x_steps = 7
    y_steps = 5
    positions = {}

    for row in range(y_steps):
        y = 0 if y_steps == 1 else int(round((max_y * row) / float(y_steps - 1)))
        for col in range(x_steps):
            x = 0 if x_steps == 1 else int(round((max_x * col) / float(x_steps - 1)))
            positions["r{}_c{}".format(row, col)] = (x, y)

    return positions


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
    center_values = float4_values(center)
    probes = {}
    for probe_name, (probe_x, probe_y) in probe_positions(width, height).items():
        probe_value = controller.PickPixel(resource_id, probe_x, probe_y, sub, type_cast)
        probes[probe_name] = float4_values(probe_value)
    return {
        "resource_id": resource_id,
        "name": name,
        "width": width,
        "height": height,
        "mip": sub.mip,
        "slice": sub.slice,
        "min": min_values,
        "max": max_values,
        "center": center_values,
        "probes": probes,
        "nonzero": is_nonzero_range(min_values, max_values),
        "rgb_nonzero": rgb_nonzero(min_values, max_values),
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


def find_last_named_record(records, name):
    matches = [record for record in records if record.name == name]
    if not matches:
        return None
    return matches[-1]


def find_last_named_record_any(records, names):
    matches = [record for record in records if record.name in names]
    if not matches:
        return None
    return matches[-1]


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
        lower_name = name.lower()
        if not all(token.lower() in lower_name for token in tokens):
            continue
        if resource_used_in_events(controller, resource_id, event_ids):
            matches.append({"resource_id": resource_id, "name": name})
    return matches


def read_u32_buffer(controller, resource_id, max_words):
    raw = controller.GetBufferData(resource_id, 0, max_words * 4)
    blob = bytes(raw)
    if not blob:
        return []
    word_count = len(blob) // 4
    return list(struct.unpack("<{}I".format(word_count), blob[: word_count * 4]))


def find_screen_hzb_resource(
    controller,
    resource_records,
    resource_names,
    texture_descs,
    stage5_event_ids,
    stage14_event_ids,
    expected_width,
    expected_height,
    expected_mips,
):
    named_matches = find_named_resource_usage(
        controller,
        resource_records,
        stage14_event_ids,
        "vortex.stage5.screenhzbbuild.view",
        "furthest.history",
    )
    if named_matches:
        return named_matches[0]

    fallback_matches = []
    for resource_key, texture_desc in texture_descs.items():
        resource_id = safe_getattr(texture_desc, "resourceId")
        if resource_id is None:
            continue
        width = int(safe_getattr(texture_desc, "width", 0) or 0)
        height = int(safe_getattr(texture_desc, "height", 0) or 0)
        mips = int(safe_getattr(texture_desc, "mips", 0) or 0)
        if (
            width != expected_width
            or height != expected_height
            or mips != expected_mips
        ):
            continue
        if not resource_used_in_events(controller, resource_id, stage14_event_ids):
            continue
        if not resource_used_in_events(controller, resource_id, stage5_event_ids):
            continue
        name = resource_names.get(resource_key, str(resource_id))
        fallback_matches.append({"resource_id": resource_id, "name": name})

    if not fallback_matches:
        return None

    preferred_matches = [
        match
        for match in fallback_matches
        if "furthest" in match["name"].lower()
        or "screenhzb" in match["name"].lower()
    ]
    if preferred_matches:
        return preferred_matches[0]
    return fallback_matches[0]


def find_texture_with_usage(controller, resource_names, texture_descs, event_id, exclude_ids):
    candidates = []
    for resource_key, desc in texture_descs.items():
        resource_id = safe_getattr(desc, "resourceId")
        if resource_id is None or resource_id in exclude_ids:
            continue
        usages = controller.GetUsage(resource_id)
        for usage in usages:
            usage_event = safe_getattr(usage, "eventId")
            if usage_event == event_id:
                candidates.append(
                    {
                        "resource_id": resource_id,
                        "name": resource_names.get(resource_key, resource_key),
                    }
                )
                break

    preferred = [
        candidate
        for candidate in candidates
        if "backbuffer" in candidate["name"].lower()
        or "swapchain" in candidate["name"].lower()
    ]
    if preferred:
        return preferred[0]
    if candidates:
        return candidates[0]
    return None


def format_values(values):
    return ",".join("{:.6f}".format(value) for value in values)


def append_texture_sample(report, prefix, sample):
    if sample is None:
        report.append("{}_present=false".format(prefix))
        return
    report.append("{}_present=true".format(prefix))
    report.append("{}_name={}".format(prefix, sample["name"]))
    report.append("{}_dims={}x{}".format(prefix, sample["width"], sample["height"]))
    report.append("{}_mip={}".format(prefix, sample["mip"]))
    report.append("{}_slice={}".format(prefix, sample["slice"]))
    report.append("{}_min={}".format(prefix, format_values(sample["min"])))
    report.append("{}_max={}".format(prefix, format_values(sample["max"])))
    report.append("{}_center={}".format(prefix, format_values(sample["center"])))
    for probe_name in sorted(sample["probes"].keys()):
        report.append(
            "{}_probe_{}={}".format(
                prefix, probe_name, format_values(sample["probes"][probe_name])
            )
        )
    report.append("{}_nonzero={}".format(prefix, str(sample["nonzero"]).lower()))
    report.append("{}_rgb_nonzero={}".format(prefix, str(sample["rgb_nonzero"]).lower()))


def find_output_sample(draw_state, name):
    for sample in draw_state["outputs"]:
        if sample["name"] == name:
            return sample
    return None


def choose_latest_stage_sample(candidates):
    available = [candidate for candidate in candidates if candidate[0] is not None]
    if not available:
        return None, None, ""
    last_draw, draw_state, label = max(available, key=lambda candidate: candidate[0].event_id)
    return last_draw, draw_state, label


def max_sample_delta(a_sample, b_sample):
    if a_sample is None or b_sample is None:
        return 0.0

    deltas = []
    for field in ("min", "max", "center"):
        deltas.extend(abs(value) for value in vec_diff(a_sample[field], b_sample[field]))
    for probe_name in a_sample["probes"].keys():
        deltas.extend(
            abs(value)
            for value in vec_diff(
                a_sample["probes"][probe_name], b_sample["probes"][probe_name]
            )
        )
    return max(deltas) if deltas else 0.0


def make_sample_subresource(rd, sample):
    sub = rd.Subresource()
    sub.mip = int(sample["mip"])
    sub.slice = int(sample["slice"])
    sub.sample = 0
    return sub


def dense_grid_delta(controller, rd, event_id_a, sample_a, event_id_b, sample_b, grid_x=32, grid_y=18):
    if sample_a is None or sample_b is None:
        return 0.0

    width = min(int(sample_a["width"]), int(sample_b["width"]))
    height = min(int(sample_a["height"]), int(sample_b["height"]))
    sub_a = make_sample_subresource(rd, sample_a)
    sub_b = make_sample_subresource(rd, sample_b)

    controller.SetFrameEvent(event_id_a, True)
    values_a = []
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

    controller.SetFrameEvent(event_id_b, True)
    max_delta = 0.0
    index = 0
    for row in range(grid_y):
        y = 0 if grid_y == 1 else int(round(((height - 1) * row) / float(grid_y - 1)))
        for col in range(grid_x):
            x = 0 if grid_x == 1 else int(round(((width - 1) * col) / float(grid_x - 1)))
            value_b = float4_values(
                controller.PickPixel(
                    sample_b["resource_id"], x, y, sub_b, rd.CompType.Typeless
                )
            )
            max_delta = max(
                max_delta,
                max(abs(value) for value in vec_diff(values_a[index], value_b)),
            )
            index += 1

    return max_delta


def build_report(controller, report: ReportWriter, capture_path: Path, report_path: Path):
    rd = renderdoc_module()
    action_records = collect_action_records(controller)
    resource_records = collect_resource_records_raw(controller)
    resource_names = resource_id_to_name(controller)
    texture_descs = texture_desc_map(controller)

    stage3_name = "Vortex.Stage3.DepthPrepass"
    stage5_name = "Vortex.Stage5.ScreenHzbBuild"
    stage9_name = "Vortex.Stage9.BasePass"
    stage9_main_pass_name = "Vortex.Stage9.BasePass.MainPass"
    stage12_name = "Vortex.Stage12.DeferredLighting"
    stage12_spot_name = "Vortex.Stage12.SpotLight"
    stage12_point_name = "Vortex.Stage12.PointLight"
    stage12_directional_name = "Vortex.Stage12.DirectionalLight"
    atmosphere_transmittance_lut_name = "Vortex.Environment.AtmosphereTransmittanceLut"
    atmosphere_multi_scattering_lut_name = "Vortex.Environment.AtmosphereMultiScatteringLut"
    atmosphere_sky_view_lut_name = "Vortex.Environment.AtmosphereSkyViewLut"
    atmosphere_camera_aerial_name = "Vortex.Environment.AtmosphereCameraAerialPerspective"
    distant_sky_light_lut_name = "Vortex.Environment.DistantSkyLightLut"
    stage14_local_fog_name = "Vortex.Stage14.LocalFogTiledCulling"
    stage14_volumetric_fog_name = "Vortex.Stage14.VolumetricFog"
    stage15_sky_name = "Vortex.Stage15.Sky"
    stage15_atmosphere_name = "Vortex.Stage15.Atmosphere"
    stage15_fog_name = "Vortex.Stage15.Fog"
    stage15_local_fog_name = "Vortex.Stage15.LocalFog"
    compositing_name = "Vortex.CompositingTask[label=Composite Copy View 1]"

    stage3_records = records_under_prefix(action_records, stage3_name)
    stage5_scope = records_with_name(action_records, stage5_name)
    stage5_records = records_under_prefix(action_records, stage5_name)
    stage9_main_pass_records = records_under_prefix(
        action_records, stage9_name + " > " + stage9_main_pass_name
    )
    stage9_records = (
        stage9_main_pass_records
        if stage9_main_pass_records
        else records_under_prefix(action_records, stage9_name)
    )
    stage12_records = records_under_prefix(action_records, stage12_name)
    spot_records = records_under_prefix(action_records, stage12_name + " > " + stage12_spot_name)
    point_records = records_under_prefix(action_records, stage12_name + " > " + stage12_point_name)
    directional_records = records_under_prefix(action_records, stage12_name + " > " + stage12_directional_name)
    atmosphere_transmittance_lut_scope = records_with_name(action_records, atmosphere_transmittance_lut_name)
    atmosphere_transmittance_lut_records = records_under_prefix(action_records, atmosphere_transmittance_lut_name)
    atmosphere_multi_scattering_lut_scope = records_with_name(action_records, atmosphere_multi_scattering_lut_name)
    atmosphere_multi_scattering_lut_records = records_under_prefix(action_records, atmosphere_multi_scattering_lut_name)
    atmosphere_sky_view_lut_scope = records_with_name(action_records, atmosphere_sky_view_lut_name)
    atmosphere_sky_view_lut_records = records_under_prefix(action_records, atmosphere_sky_view_lut_name)
    atmosphere_camera_aerial_scope = records_with_name(action_records, atmosphere_camera_aerial_name)
    atmosphere_camera_aerial_records = records_under_prefix(action_records, atmosphere_camera_aerial_name)
    distant_sky_light_lut_scope = records_with_name(action_records, distant_sky_light_lut_name)
    distant_sky_light_lut_records = records_under_prefix(action_records, distant_sky_light_lut_name)
    stage14_local_fog_records = records_under_prefix(action_records, stage14_local_fog_name)
    stage14_volumetric_fog_scope = records_with_name(
        action_records, stage14_volumetric_fog_name
    )
    stage14_volumetric_fog_records = records_under_prefix(
        action_records, stage14_volumetric_fog_name
    )
    stage15_sky_records = records_under_prefix(action_records, stage15_sky_name)
    stage15_atmosphere_records = records_under_prefix(action_records, stage15_atmosphere_name)
    stage15_fog_records = records_under_prefix(action_records, stage15_fog_name)
    stage15_local_fog_records = records_under_prefix(action_records, stage15_local_fog_name)
    compositing_records = records_under_prefix(action_records, compositing_name)
    stage5_event_ids = collect_event_ids(stage5_scope, stage5_records)
    stage14_event_ids = collect_event_ids(
        records_with_name(action_records, stage14_local_fog_name), stage14_local_fog_records
    )
    stage14_volumetric_fog_event_ids = collect_event_ids(
        stage14_volumetric_fog_scope, stage14_volumetric_fog_records
    )
    stage15_atmosphere_event_ids = collect_event_ids(
        records_with_name(action_records, stage15_atmosphere_name), stage15_atmosphere_records
    )
    stage15_fog_event_ids = collect_event_ids(
        records_with_name(action_records, stage15_fog_name), stage15_fog_records
    )
    stage15_local_fog_event_ids = collect_event_ids(
        records_with_name(action_records, stage15_local_fog_name), stage15_local_fog_records
    )

    stage3_last_draw = find_last_named_record(stage3_records, DRAW_NAME)
    stage9_last_draw = find_last_named_record(stage9_records, DRAW_NAME)
    stage12_spot_last_draw = find_last_named_record(spot_records, DRAW_NAME)
    stage12_point_last_draw = find_last_named_record(point_records, DRAW_NAME)
    stage12_directional_last_draw = find_last_named_record(directional_records, DRAW_NAME)
    stage15_sky_last_draw = find_last_named_record(stage15_sky_records, DRAW_NAME)
    stage15_atmosphere_last_draw = find_last_named_record(stage15_atmosphere_records, DRAW_NAME)
    stage15_fog_last_draw = find_last_named_record(stage15_fog_records, DRAW_NAME)
    stage15_local_fog_last_draw = find_last_named_record_any(
        stage15_local_fog_records, {DRAW_NAME, LOCAL_FOG_EXECUTE_INDIRECT_NAME}
    )
    compositing_draw = find_last_named_record_any(
        compositing_records, {DRAW_NAME, COPY_NAME}
    )

    if (
        stage3_last_draw is None
        or stage9_last_draw is None
        or stage12_directional_last_draw is None
        or stage12_point_last_draw is None
        or stage15_sky_last_draw is None
        or stage15_atmosphere_last_draw is None
        or stage15_fog_last_draw is None
        or stage15_local_fog_last_draw is None
        or compositing_draw is None
    ):
        raise RuntimeError("Required VortexBasic stage events were not found in the capture.")

    stage3 = analyze_draw_event(controller, rd, resource_names, texture_descs, stage3_last_draw.event_id)
    stage9 = analyze_draw_event(controller, rd, resource_names, texture_descs, stage9_last_draw.event_id)
    stage12_directional = analyze_draw_event(controller, rd, resource_names, texture_descs, stage12_directional_last_draw.event_id)
    stage12_spot = analyze_draw_event(controller, rd, resource_names, texture_descs, stage12_spot_last_draw.event_id) if stage12_spot_last_draw is not None else {"outputs": [], "depth": None}
    stage12_point = analyze_draw_event(controller, rd, resource_names, texture_descs, stage12_point_last_draw.event_id) if stage12_point_last_draw is not None else {"outputs": [], "depth": None}
    stage15_sky = analyze_draw_event(controller, rd, resource_names, texture_descs, stage15_sky_last_draw.event_id)
    stage15_atmosphere = analyze_draw_event(
        controller, rd, resource_names, texture_descs, stage15_atmosphere_last_draw.event_id
    )
    stage15_fog = analyze_draw_event(controller, rd, resource_names, texture_descs, stage15_fog_last_draw.event_id)
    stage15_local_fog = analyze_draw_event(
        controller, rd, resource_names, texture_descs, stage15_local_fog_last_draw.event_id
    )

    compositing = analyze_draw_event(controller, rd, resource_names, texture_descs, compositing_draw.event_id)
    final_present = compositing["outputs"][0] if compositing["outputs"] else None

    stage3_depth_ok = stage3["depth"] is not None and stage3["depth"]["name"] == "SceneDepth" and stage3["depth"]["nonzero"]
    stage9_names = [sample["name"] for sample in stage9["outputs"]]
    stage9_has_expected_targets = stage9_names == [
        "GBufferNormal",
        "GBufferMaterial",
        "GBufferBaseColor",
        "GBufferCustomData",
        "SceneColor",
        "Velocity",
    ]
    stage9_gbuffer_nonzero = any(
        sample["name"] == "GBufferBaseColor" and sample["rgb_nonzero"]
        for sample in stage9["outputs"]
    )
    stage9_velocity_nonzero = any(
        sample["name"] == "Velocity" and sample["nonzero"]
        for sample in stage9["outputs"]
    )
    stage12_spot_scene_color_nonzero = any(
        sample["name"] == "SceneColor" and sample["rgb_nonzero"]
        for sample in stage12_spot["outputs"]
    )
    stage12_point_scene_color_nonzero = any(
        sample["name"] == "SceneColor" and sample["rgb_nonzero"]
        for sample in stage12_point["outputs"]
    )
    stage12_directional_scene_color_nonzero = any(
        sample["name"] == "SceneColor" and sample["rgb_nonzero"]
        for sample in stage12_directional["outputs"]
    )
    stage12_final_last_draw, stage12_final_draw_state, stage12_final_label = choose_latest_stage_sample(
        [
            (stage12_directional_last_draw, stage12_directional, "directional"),
            (stage12_point_last_draw, stage12_point, "point"),
            (stage12_spot_last_draw, stage12_spot, "spot"),
        ]
    )
    stage12_final_scene_color = find_output_sample(stage12_final_draw_state, "SceneColor")
    if stage12_final_last_draw is None or stage12_final_scene_color is None:
        raise RuntimeError("Required VortexBasic final Stage 12 SceneColor output was not found.")
    stage15_sky_scene_color = find_output_sample(stage15_sky, "SceneColor")
    stage15_atmosphere_scene_color = find_output_sample(stage15_atmosphere, "SceneColor")
    stage15_fog_scene_color = find_output_sample(stage15_fog, "SceneColor")
    stage15_local_fog_scene_color = find_output_sample(stage15_local_fog, "SceneColor")
    stage15_sky_scene_color_delta = max_sample_delta(
        stage12_final_scene_color, stage15_sky_scene_color
    )
    stage15_atmosphere_scene_color_delta = max_sample_delta(
        stage15_sky_scene_color, stage15_atmosphere_scene_color
    )
    stage15_fog_scene_color_delta = max_sample_delta(
        stage15_atmosphere_scene_color, stage15_fog_scene_color
    )
    stage15_local_fog_scene_color_delta = max_sample_delta(
        stage15_fog_scene_color, stage15_local_fog_scene_color
    )
    stage15_sky_scene_color_dense_delta = dense_grid_delta(
        controller,
        rd,
        stage12_final_last_draw.event_id,
        stage12_final_scene_color,
        stage15_sky_last_draw.event_id,
        stage15_sky_scene_color,
    )
    stage15_atmosphere_scene_color_dense_delta = dense_grid_delta(
        controller,
        rd,
        stage15_sky_last_draw.event_id,
        stage15_sky_scene_color,
        stage15_atmosphere_last_draw.event_id,
        stage15_atmosphere_scene_color,
    )
    stage15_fog_scene_color_dense_delta = dense_grid_delta(
        controller,
        rd,
        stage15_atmosphere_last_draw.event_id,
        stage15_atmosphere_scene_color,
        stage15_fog_last_draw.event_id,
        stage15_fog_scene_color,
    )
    stage15_local_fog_scene_color_dense_delta = dense_grid_delta(
        controller,
        rd,
        stage15_fog_last_draw.event_id,
        stage15_fog_scene_color,
        stage15_local_fog_last_draw.event_id,
        stage15_local_fog_scene_color,
    )
    stage15_sky_scene_color_delta = max(
        stage15_sky_scene_color_delta, stage15_sky_scene_color_dense_delta
    )
    stage15_atmosphere_scene_color_delta = max(
        stage15_atmosphere_scene_color_delta,
        stage15_atmosphere_scene_color_dense_delta,
    )
    stage15_fog_scene_color_delta = max(
        stage15_fog_scene_color_delta, stage15_fog_scene_color_dense_delta
    )
    stage15_local_fog_scene_color_delta = max(
        stage15_local_fog_scene_color_delta,
        stage15_local_fog_scene_color_dense_delta,
    )
    stage15_sky_scene_color_changed = stage15_sky_scene_color_delta > 1.0e-5
    stage15_atmosphere_scene_color_changed = (
        stage15_atmosphere_scene_color_delta > 1.0e-5
    )
    stage15_fog_scene_color_changed = stage15_fog_scene_color_delta > 1.0e-5
    stage15_local_fog_scene_color_changed = stage15_local_fog_scene_color_delta > 1.0e-5
    final_present_nonzero = final_present is not None and final_present["rgb_nonzero"]
    stage3_depth_width = stage3["depth"]["width"] if stage3["depth"] is not None else 0
    stage3_depth_height = stage3["depth"]["height"] if stage3["depth"] is not None else 0
    expected_screen_hzb_width = (
        expected_screen_hzb_extent(stage3_depth_width) if stage3_depth_width > 0 else 0
    )
    expected_screen_hzb_height = (
        expected_screen_hzb_extent(stage3_depth_height) if stage3_depth_height > 0 else 0
    )
    expected_screen_hzb_mips = (
        expected_screen_hzb_mip_count(expected_screen_hzb_width, expected_screen_hzb_height)
        if expected_screen_hzb_width > 0 and expected_screen_hzb_height > 0
        else 0
    )

    local_fog_hzb_resource = find_screen_hzb_resource(
        controller,
        resource_records,
        resource_names,
        texture_descs,
        stage5_event_ids,
        stage14_event_ids,
        expected_screen_hzb_width,
        expected_screen_hzb_height,
        expected_screen_hzb_mips,
    )
    actual_screen_hzb_width = 0
    actual_screen_hzb_height = 0
    actual_screen_hzb_mips = 0
    screen_hzb_written_in_stage5 = False
    if local_fog_hzb_resource is not None:
        texture_desc = texture_descs.get(str(local_fog_hzb_resource["resource_id"]))
        if texture_desc is not None:
            actual_screen_hzb_width = int(safe_getattr(texture_desc, "width", 0) or 0)
            actual_screen_hzb_height = int(safe_getattr(texture_desc, "height", 0) or 0)
            actual_screen_hzb_mips = int(safe_getattr(texture_desc, "mips", 0) or 0)
        screen_hzb_written_in_stage5 = resource_used_in_events(
            controller, local_fog_hzb_resource["resource_id"], stage5_event_ids
        )

    screen_hzb_published = (
        len(stage5_scope) == 1
        and local_fog_hzb_resource is not None
        and screen_hzb_written_in_stage5
        and actual_screen_hzb_width == expected_screen_hzb_width
        and actual_screen_hzb_height == expected_screen_hzb_height
        and actual_screen_hzb_mips == expected_screen_hzb_mips
    )
    local_fog_hzb_consumed = local_fog_hzb_resource is not None and screen_hzb_published

    indirect_args_usage = find_named_resource_usage(
        controller,
        resource_records,
        stage15_local_fog_event_ids,
        "vortex.environment.localfogtiledrawargs",
    )
    local_fog_indirect_draw_args_values = []
    if indirect_args_usage:
        local_fog_indirect_draw_args_values = read_u32_buffer(
            controller, indirect_args_usage[0]["resource_id"], 4
        )
    local_fog_indirect_draw_instance_count = (
        local_fog_indirect_draw_args_values[1]
        if len(local_fog_indirect_draw_args_values) > 1
        else 0
    )
    local_fog_indirect_draw_valid = (
        len(indirect_args_usage) > 0
        and len(local_fog_indirect_draw_args_values) >= 4
        and local_fog_indirect_draw_args_values[0] == 6
        and local_fog_indirect_draw_instance_count > 0
    )

    atmosphere_transmittance_lut_scope_count_match = len(atmosphere_transmittance_lut_scope) == 1
    atmosphere_transmittance_lut_dispatch_count_match = (
        len(records_with_name(atmosphere_transmittance_lut_records, "ID3D12GraphicsCommandList::Dispatch()")) == 1
    )
    atmosphere_multi_scattering_lut_scope_count_match = len(atmosphere_multi_scattering_lut_scope) == 1
    atmosphere_multi_scattering_lut_dispatch_count_match = (
        len(records_with_name(atmosphere_multi_scattering_lut_records, "ID3D12GraphicsCommandList::Dispatch()")) == 1
    )
    atmosphere_sky_view_lut_scope_count_match = len(atmosphere_sky_view_lut_scope) == 1
    atmosphere_sky_view_lut_dispatch_count_match = (
        len(records_with_name(atmosphere_sky_view_lut_records, "ID3D12GraphicsCommandList::Dispatch()")) == 1
    )
    atmosphere_camera_aerial_scope_count_match = len(atmosphere_camera_aerial_scope) == 1
    atmosphere_camera_aerial_dispatch_count_match = (
        len(records_with_name(atmosphere_camera_aerial_records, "ID3D12GraphicsCommandList::Dispatch()")) == 1
    )
    atmosphere_camera_aerial_usage = find_named_resource_usage(
        controller,
        resource_records,
        stage15_atmosphere_event_ids,
        "vortex.environment.atmospherecameraaerialperspective",
    )
    atmosphere_camera_aerial_consumed = len(atmosphere_camera_aerial_usage) > 0
    distant_sky_light_lut_scope_count_match = len(distant_sky_light_lut_scope) == 1
    distant_sky_light_lut_dispatch_count_match = (
        len(records_with_name(distant_sky_light_lut_records, "ID3D12GraphicsCommandList::Dispatch()")) == 1
    )
    stage14_volumetric_fog_scope_count_match = len(stage14_volumetric_fog_scope) == 1
    stage14_volumetric_fog_dispatch_count_match = (
        len(records_with_name(stage14_volumetric_fog_records, "ID3D12GraphicsCommandList::Dispatch()")) == 1
    )
    integrated_light_scattering_written = find_named_resource_usage(
        controller,
        resource_records,
        stage14_volumetric_fog_event_ids,
        "vortex.environment.integratedlightscattering",
    )
    integrated_light_scattering_consumed = find_named_resource_usage(
        controller,
        resource_records,
        stage15_fog_event_ids,
        "vortex.environment.integratedlightscattering",
    )
    integrated_light_scattering_published = (
        stage14_volumetric_fog_scope_count_match
        and stage14_volumetric_fog_dispatch_count_match
        and len(integrated_light_scattering_written) > 0
    )
    integrated_light_scattering_consumed_by_fog = (
        len(integrated_light_scattering_consumed) > 0
    )

    report.append("analysis_profile=vortexbasic_stage_products")
    report.append("capture_path={}".format(capture_path))
    report.append("report_path={}".format(report_path))
    report.append("stage3_last_draw_event={}".format(stage3_last_draw.event_id))
    report.append("stage9_last_draw_event={}".format(stage9_last_draw.event_id))
    report.append("stage12_spot_last_draw_event={}".format(stage12_spot_last_draw.event_id if stage12_spot_last_draw else ""))
    report.append("stage12_point_last_draw_event={}".format(stage12_point_last_draw.event_id if stage12_point_last_draw else ""))
    report.append("stage12_directional_last_draw_event={}".format(stage12_directional_last_draw.event_id))
    report.append("stage12_final_last_draw_event={}".format(stage12_final_last_draw.event_id))
    report.append("stage12_final_last_draw_label={}".format(stage12_final_label))
    report.append("stage15_sky_last_draw_event={}".format(stage15_sky_last_draw.event_id))
    report.append(
        "stage15_atmosphere_last_draw_event={}".format(stage15_atmosphere_last_draw.event_id)
    )
    report.append("stage15_fog_last_draw_event={}".format(stage15_fog_last_draw.event_id))
    report.append(
        "stage15_local_fog_last_draw_event={}".format(stage15_local_fog_last_draw.event_id)
    )
    report.append("compositing_draw_event={}".format(compositing_draw.event_id))
    report.append("stage3_depth_ok={}".format(str(stage3_depth_ok).lower()))
    report.append(
        "atmosphere_transmittance_lut_scope_count_match={}".format(
            str(atmosphere_transmittance_lut_scope_count_match).lower()
        )
    )
    report.append(
        "atmosphere_transmittance_lut_dispatch_count_match={}".format(
            str(atmosphere_transmittance_lut_dispatch_count_match).lower()
        )
    )
    report.append(
        "atmosphere_multi_scattering_lut_scope_count_match={}".format(
            str(atmosphere_multi_scattering_lut_scope_count_match).lower()
        )
    )
    report.append(
        "atmosphere_multi_scattering_lut_dispatch_count_match={}".format(
            str(atmosphere_multi_scattering_lut_dispatch_count_match).lower()
        )
    )
    report.append(
        "atmosphere_sky_view_lut_scope_count_match={}".format(
            str(atmosphere_sky_view_lut_scope_count_match).lower()
        )
    )
    report.append(
        "atmosphere_sky_view_lut_dispatch_count_match={}".format(
            str(atmosphere_sky_view_lut_dispatch_count_match).lower()
        )
    )
    report.append(
        "atmosphere_camera_aerial_scope_count_match={}".format(
            str(atmosphere_camera_aerial_scope_count_match).lower()
        )
    )
    report.append(
        "atmosphere_camera_aerial_dispatch_count_match={}".format(
            str(atmosphere_camera_aerial_dispatch_count_match).lower()
        )
    )
    report.append(
        "atmosphere_camera_aerial_consumed={}".format(
            str(atmosphere_camera_aerial_consumed).lower()
        )
    )
    report.append(
        "atmosphere_camera_aerial_resource_name={}".format(
            atmosphere_camera_aerial_usage[0]["name"] if atmosphere_camera_aerial_usage else ""
        )
    )
    report.append(
        "distant_sky_light_lut_scope_count_match={}".format(
            str(distant_sky_light_lut_scope_count_match).lower()
        )
    )
    report.append(
        "distant_sky_light_lut_dispatch_count_match={}".format(
            str(distant_sky_light_lut_dispatch_count_match).lower()
        )
    )
    report.append(
        "stage14_volumetric_fog_scope_count_match={}".format(
            str(stage14_volumetric_fog_scope_count_match).lower()
        )
    )
    report.append(
        "stage14_volumetric_fog_dispatch_count_match={}".format(
            str(stage14_volumetric_fog_dispatch_count_match).lower()
        )
    )
    report.append(
        "integrated_light_scattering_published={}".format(
            str(integrated_light_scattering_published).lower()
        )
    )
    report.append(
        "integrated_light_scattering_consumed_by_fog={}".format(
            str(integrated_light_scattering_consumed_by_fog).lower()
        )
    )
    report.append(
        "integrated_light_scattering_written_resource_name={}".format(
            integrated_light_scattering_written[0]["name"]
            if integrated_light_scattering_written
            else ""
        )
    )
    report.append(
        "integrated_light_scattering_consumed_resource_name={}".format(
            integrated_light_scattering_consumed[0]["name"]
            if integrated_light_scattering_consumed
            else ""
        )
    )
    report.append("stage9_has_expected_targets={}".format(str(stage9_has_expected_targets).lower()))
    report.append("stage9_gbuffer_base_color_nonzero={}".format(str(stage9_gbuffer_nonzero).lower()))
    report.append("stage9_velocity_nonzero={}".format(str(stage9_velocity_nonzero).lower()))
    report.append("stage12_spot_scene_color_nonzero={}".format(str(stage12_spot_scene_color_nonzero).lower()))
    report.append("stage12_point_scene_color_nonzero={}".format(str(stage12_point_scene_color_nonzero).lower()))
    report.append("stage12_directional_scene_color_nonzero={}".format(str(stage12_directional_scene_color_nonzero).lower()))
    report.append(
        "stage15_sky_scene_color_delta_max={:.6f}".format(stage15_sky_scene_color_delta)
    )
    report.append(
        "stage15_sky_scene_color_dense_delta_max={:.6f}".format(
            stage15_sky_scene_color_dense_delta
        )
    )
    report.append(
        "stage15_atmosphere_scene_color_delta_max={:.6f}".format(
            stage15_atmosphere_scene_color_delta
        )
    )
    report.append(
        "stage15_atmosphere_scene_color_dense_delta_max={:.6f}".format(
            stage15_atmosphere_scene_color_dense_delta
        )
    )
    report.append(
        "stage15_fog_scene_color_delta_max={:.6f}".format(stage15_fog_scene_color_delta)
    )
    report.append(
        "stage15_fog_scene_color_dense_delta_max={:.6f}".format(
            stage15_fog_scene_color_dense_delta
        )
    )
    report.append(
        "stage15_local_fog_scene_color_delta_max={:.6f}".format(
            stage15_local_fog_scene_color_delta
        )
    )
    report.append(
        "stage15_local_fog_scene_color_dense_delta_max={:.6f}".format(
            stage15_local_fog_scene_color_dense_delta
        )
    )
    report.append(
        "stage15_sky_scene_color_changed={}".format(
            str(stage15_sky_scene_color_changed).lower()
        )
    )
    report.append(
        "stage15_atmosphere_scene_color_changed={}".format(
            str(stage15_atmosphere_scene_color_changed).lower()
        )
    )
    report.append(
        "stage15_fog_scene_color_changed={}".format(
            str(stage15_fog_scene_color_changed).lower()
        )
    )
    report.append(
        "stage15_local_fog_scene_color_changed={}".format(
            str(stage15_local_fog_scene_color_changed).lower()
        )
    )
    report.append("screen_hzb_expected_width={}".format(expected_screen_hzb_width))
    report.append("screen_hzb_expected_height={}".format(expected_screen_hzb_height))
    report.append("screen_hzb_expected_mips={}".format(expected_screen_hzb_mips))
    report.append("screen_hzb_actual_width={}".format(actual_screen_hzb_width))
    report.append("screen_hzb_actual_height={}".format(actual_screen_hzb_height))
    report.append("screen_hzb_actual_mips={}".format(actual_screen_hzb_mips))
    report.append(
        "screen_hzb_resource_name={}".format(
            local_fog_hzb_resource["name"] if local_fog_hzb_resource is not None else ""
        )
    )
    report.append("screen_hzb_published={}".format(str(screen_hzb_published).lower()))
    report.append("local_fog_hzb_consumed={}".format(str(local_fog_hzb_consumed).lower()))
    report.append(
        "local_fog_indirect_draw_valid={}".format(
            str(local_fog_indirect_draw_valid).lower()
        )
    )
    report.append(
        "local_fog_indirect_draw_args={}".format(
            ",".join(str(value) for value in local_fog_indirect_draw_args_values)
        )
    )
    report.append(
        "local_fog_indirect_draw_instance_count={}".format(
            local_fog_indirect_draw_instance_count
        )
    )
    report.append("final_present_nonzero={}".format(str(final_present_nonzero).lower()))
    overall = (
        stage3_depth_ok
        and atmosphere_transmittance_lut_scope_count_match
        and atmosphere_transmittance_lut_dispatch_count_match
        and atmosphere_multi_scattering_lut_scope_count_match
        and atmosphere_multi_scattering_lut_dispatch_count_match
        and atmosphere_sky_view_lut_scope_count_match
        and atmosphere_sky_view_lut_dispatch_count_match
        and atmosphere_camera_aerial_scope_count_match
        and atmosphere_camera_aerial_dispatch_count_match
        and atmosphere_camera_aerial_consumed
        and distant_sky_light_lut_scope_count_match
        and distant_sky_light_lut_dispatch_count_match
        and stage14_volumetric_fog_scope_count_match
        and stage14_volumetric_fog_dispatch_count_match
        and integrated_light_scattering_published
        and stage9_has_expected_targets
        and stage9_gbuffer_nonzero
        and stage9_velocity_nonzero
        and stage12_spot_scene_color_nonzero
        and stage12_point_scene_color_nonzero
        and stage12_directional_scene_color_nonzero
        and stage15_sky_scene_color_changed
        and stage15_atmosphere_scene_color_changed
        and stage15_fog_scene_color_changed
        and stage15_local_fog_scene_color_changed
        and screen_hzb_published
        and local_fog_hzb_consumed
        and local_fog_indirect_draw_valid
        and final_present_nonzero
    )
    report.append("overall_verdict={}".format("pass" if overall else "fail"))
    report.blank()

    append_texture_sample(report, "stage3_depth", stage3["depth"])
    for index, sample in enumerate(stage3["outputs"]):
        append_texture_sample(report, "stage3_output_{}".format(index), sample)
    for index, sample in enumerate(stage9["outputs"]):
        append_texture_sample(report, "stage9_output_{}".format(index), sample)
    for index, sample in enumerate(stage12_spot["outputs"]):
        append_texture_sample(report, "stage12_spot_output_{}".format(index), sample)
    for index, sample in enumerate(stage12_point["outputs"]):
        append_texture_sample(report, "stage12_point_output_{}".format(index), sample)
    for index, sample in enumerate(stage12_directional["outputs"]):
        append_texture_sample(report, "stage12_directional_output_{}".format(index), sample)
    for index, sample in enumerate(stage15_sky["outputs"]):
        append_texture_sample(report, "stage15_sky_output_{}".format(index), sample)
    for index, sample in enumerate(stage15_atmosphere["outputs"]):
        append_texture_sample(report, "stage15_atmosphere_output_{}".format(index), sample)
    for index, sample in enumerate(stage15_fog["outputs"]):
        append_texture_sample(report, "stage15_fog_output_{}".format(index), sample)
    for index, sample in enumerate(stage15_local_fog["outputs"]):
        append_texture_sample(report, "stage15_local_fog_output_{}".format(index), sample)
    append_texture_sample(report, "final_present", final_present)


def main():
    run_ui_script(REPORT_SUFFIX, build_report)


if __name__ == "__main__":
    main()
