"""RenderDoc UI analyzer for Async runtime products."""

import builtins
import os
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
    collect_action_records,
    collect_resource_records_raw,
    is_work_action,
    renderdoc_module,
    resource_id_to_name,
    run_ui_script,
    safe_getattr,
)


REPORT_SUFFIX = "_async_products_report.txt"
DRAW_NAME = "ID3D12GraphicsCommandList::DrawInstanced()"
COLOR_EXPORT_ENV = "OXYGEN_ASYNC_EXPORT_COLOR_PATH"
DEPTH_EXPORT_ENV = "OXYGEN_ASYNC_EXPORT_DEPTH_PATH"

STAGE3_SCOPE_NAME = "Vortex.Stage3.DepthPrepass"
STAGE8_SCOPE_NAME = "Vortex.Stage8.ShadowDepths"
STAGE12_SCOPE_NAME = "Vortex.Stage12.DeferredLighting"
STAGE12_DIRECTIONAL_SCOPE_NAME = "Vortex.Stage12.DirectionalLight"
STAGE12_SPOT_SCOPE_NAME = "Vortex.Stage12.SpotLight"
ATMOSPHERE_SKY_VIEW_SCOPE_NAME = "Vortex.Environment.AtmosphereSkyViewLut"
ATMOSPHERE_CAMERA_AERIAL_SCOPE_NAME = "Vortex.Environment.AtmosphereCameraAerialPerspective"
STAGE15_SKY_SCOPE_NAME = "Vortex.Stage15.Sky"
STAGE15_ATMOSPHERE_SCOPE_NAME = "Vortex.Stage15.Atmosphere"
STAGE15_FOG_SCOPE_NAME = "Vortex.Stage15.Fog"
STAGE22_TONEMAP_SCOPE_NAME = "Vortex.PostProcess.Tonemap"
COMPOSITING_SCOPE_PREFIX = "Vortex.CompositingTask[label=Composite Copy View "
IMGUI_OVERLAY_BLEND_SCOPE_PREFIXES = (
    "Vortex.CompositingTask[label=Composite Blend Texture Vortex.ImGuiOverlay",
    "Vortex.CompositingTask[label=Composite Blend Texture Async.DemoShellOverlay",
)


def records_with_name(action_records, name):
    return [record for record in action_records if record.name == name]


def records_with_name_prefix(action_records, prefix):
    return [
        record for record in action_records if record.name.startswith(prefix)
    ]


def records_with_name_prefixes(action_records, prefixes):
    records = []
    for prefix in prefixes:
        records.extend(records_with_name_prefix(action_records, prefix))
    return records


def records_under_prefix(action_records, prefix):
    prefix_with_sep = prefix + " > "
    return [
        record
        for record in action_records
        if record.path.startswith(prefix_with_sep)
    ]


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


def float4_values(pixel_value):
    return [float(v) for v in list(pixel_value.floatValue)[:4]]


def vec_max_abs(values):
    return max(abs(v) for v in values[:4]) if values else 0.0


def rgb_nonzero(min_values, max_values, epsilon=1.0e-6):
    rgb_delta = [b - a for a, b in zip(min_values[:3], max_values[:3])]
    return (
        max(abs(v) for v in rgb_delta) > epsilon
        or max(abs(v) for v in max_values[:3]) > epsilon
    )


def vec_diff(a_values, b_values):
    return [b - a for a, b in zip(a_values[:4], b_values[:4])]


def is_nonzero_range(min_values, max_values, epsilon=1.0e-6):
    return (
        vec_max_abs(vec_diff(min_values, max_values)) > epsilon
        or vec_max_abs(max_values) > epsilon
    )


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
        y = (
            0
            if y_steps == 1
            else int(round((max_y * row) / float(y_steps - 1)))
        )
        for col in range(x_steps):
            x = (
                0
                if x_steps == 1
                else int(round((max_x * col) / float(x_steps - 1)))
            )
            positions["r{}_c{}".format(row, col)] = (x, y)

    return positions


def sample_descriptor(
    controller, rd, resource_names, texture_descs, descriptor, type_cast
):
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
    center = controller.PickPixel(
        resource_id, center_x, center_y, sub, type_cast
    )
    center_values = float4_values(center)
    probes = {}
    for probe_name, (probe_x, probe_y) in probe_positions(
        width, height
    ).items():
        probe_value = controller.PickPixel(
            resource_id, probe_x, probe_y, sub, type_cast
        )
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


def find_last_draw_between(action_records, start_event_id, end_event_id):
    matches = [
        record
        for record in action_records
        if record.name == DRAW_NAME
        and start_event_id < record.event_id < end_event_id
    ]
    if not matches:
        return None
    return matches[-1]


def find_last_work_record(records):
    matches = [record for record in records if is_work_action(record.flags)]
    if not matches:
        return None
    return matches[-1]


def resolve_stage22_tonemap_record(
    action_records,
    explicit_record,
    stage15_fog_last_draw,
    compositing_last_action,
):
    if explicit_record is not None:
        return explicit_record
    if stage15_fog_last_draw is None or compositing_last_action is None:
        return None
    return find_last_draw_between(
        action_records,
        stage15_fog_last_draw.event_id,
        compositing_last_action.event_id,
    )


def resolve_stage22_visibility(
    sampled_tonemap_output_nonzero, final_present_nonzero
):
    return {
        "stage22_tonemap_output_nonzero": bool(sampled_tonemap_output_nonzero),
        "final_present_nonzero": bool(final_present_nonzero),
    }


def format_values(values):
    return ",".join("{:.6f}".format(value) for value in values)


def append_texture_sample(report, prefix, sample):
    if sample is None:
        report.append("{}_present=false".format(prefix))
        return
    report.append("{}_present=true".format(prefix))
    report.append("{}_name={}".format(prefix, sample["name"]))
    report.append(
        "{}_dims={}x{}".format(prefix, sample["width"], sample["height"])
    )
    report.append("{}_mip={}".format(prefix, sample["mip"]))
    report.append("{}_slice={}".format(prefix, sample["slice"]))
    report.append("{}_min={}".format(prefix, format_values(sample["min"])))
    report.append("{}_max={}".format(prefix, format_values(sample["max"])))
    report.append(
        "{}_center={}".format(prefix, format_values(sample["center"]))
    )
    for probe_name in sorted(sample["probes"].keys()):
        report.append(
            "{}_probe_{}={}".format(
                prefix, probe_name, format_values(sample["probes"][probe_name])
            )
        )
    report.append(
        "{}_sample_nonzero={}".format(prefix, str(sample["nonzero"]).lower())
    )
    report.append(
        "{}_sample_rgb_nonzero={}".format(
            prefix, str(sample["rgb_nonzero"]).lower()
        )
    )


def find_output_sample(draw_state, name):
    for sample in draw_state["outputs"]:
        if sample["name"] == name:
            return sample
    return None


def sample_rgb_delta(values_a, values_b):
    return max(abs(values_b[index] - values_a[index]) for index in range(3))


def resolve_far_depth_reference(depth_sample, epsilon=1.0e-3):
    if depth_sample is None:
        return 0.0

    probe_depths = [depth_sample["center"][0]]
    probe_depths.extend(values[0] for values in depth_sample["probes"].values())
    zero_hits = sum(1 for value in probe_depths if abs(value - 0.0) <= epsilon)
    one_hits = sum(1 for value in probe_depths if abs(value - 1.0) <= epsilon)
    return 0.0 if zero_hits >= one_hits else 1.0


def probe_luminance(values):
    return values[0] * 0.2126 + values[1] * 0.7152 + values[2] * 0.0722


def evaluate_stage15_far_background_mask(
    depth_sample,
    stage12_sample,
    sky_sample,
    epsilon=1.0e-3,
    foreground_delta_limit=5.0e-3,
):
    if depth_sample is None or stage12_sample is None or sky_sample is None:
        return {
            "far_depth_reference": 0.0,
            "background_probe_count": 0,
            "foreground_probe_count": 0,
            "foreground_delta_max": 0.0,
            "valid": False,
        }

    far_depth_reference = resolve_far_depth_reference(depth_sample, epsilon)
    background_probe_count = 0
    foreground_probe_count = 0
    foreground_delta_max = 0.0

    def visit(depth_values, before_values, after_values):
        nonlocal background_probe_count
        nonlocal foreground_probe_count
        nonlocal foreground_delta_max

        depth_value = depth_values[0]
        delta = sample_rgb_delta(before_values, after_values)
        if abs(depth_value - far_depth_reference) <= epsilon:
            background_probe_count += 1
        else:
            foreground_probe_count += 1
            foreground_delta_max = max(foreground_delta_max, delta)

    visit(
        depth_sample["center"], stage12_sample["center"], sky_sample["center"]
    )
    for probe_name in stage12_sample["probes"].keys():
        visit(
            depth_sample["probes"][probe_name],
            stage12_sample["probes"][probe_name],
            sky_sample["probes"][probe_name],
        )

    return {
        "far_depth_reference": far_depth_reference,
        "background_probe_count": background_probe_count,
        "foreground_probe_count": foreground_probe_count,
        "foreground_delta_max": foreground_delta_max,
        "valid": foreground_delta_max <= foreground_delta_limit,
    }


def evaluate_stage15_sky_quality(depth_sample, sky_sample, epsilon=1.0e-3):
    if depth_sample is None or sky_sample is None:
        return {
            "background_probe_count": 0,
            "background_luminance_min": 0.0,
            "background_luminance_max": 0.0,
            "background_luminance_range": 0.0,
            "valid": False,
        }

    far_depth_reference = resolve_far_depth_reference(depth_sample, epsilon)
    background_luminances = []

    def maybe_add(depth_values, color_values):
        if abs(depth_values[0] - far_depth_reference) <= epsilon:
            background_luminances.append(probe_luminance(color_values))

    maybe_add(depth_sample["center"], sky_sample["center"])
    for probe_name in sky_sample["probes"].keys():
        maybe_add(
            depth_sample["probes"][probe_name], sky_sample["probes"][probe_name]
        )

    if not background_luminances:
        return {
            "background_probe_count": 0,
            "background_luminance_min": 0.0,
            "background_luminance_max": 0.0,
            "background_luminance_range": 0.0,
            "valid": False,
        }

    luminance_min = min(background_luminances)
    luminance_max = max(background_luminances)
    luminance_range = luminance_max - luminance_min
    return {
        "background_probe_count": len(background_luminances),
        "background_luminance_min": luminance_min,
        "background_luminance_max": luminance_max,
        "background_luminance_range": luminance_range,
        "valid": len(background_luminances) >= 4 and luminance_range >= 5.0e-2,
    }


def max_sample_delta(a_sample, b_sample):
    if a_sample is None or b_sample is None:
        return 0.0

    deltas = []
    for field in ("min", "max", "center"):
        deltas.extend(
            abs(value) for value in vec_diff(a_sample[field], b_sample[field])
        )
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


def dense_grid_delta(
    controller,
    rd,
    event_id_a,
    sample_a,
    event_id_b,
    sample_b,
    grid_x=32,
    grid_y=18,
):
    if sample_a is None or sample_b is None:
        return 0.0

    width = min(int(sample_a["width"]), int(sample_b["width"]))
    height = min(int(sample_a["height"]), int(sample_b["height"]))
    sub_a = make_sample_subresource(rd, sample_a)
    sub_b = make_sample_subresource(rd, sample_b)

    controller.SetFrameEvent(event_id_a, True)
    values_a = []
    for row in range(grid_y):
        y = (
            0
            if grid_y == 1
            else int(round(((height - 1) * row) / float(grid_y - 1)))
        )
        for col in range(grid_x):
            x = (
                0
                if grid_x == 1
                else int(round(((width - 1) * col) / float(grid_x - 1)))
            )
            values_a.append(
                float4_values(
                    controller.PickPixel(
                        sample_a["resource_id"],
                        x,
                        y,
                        sub_a,
                        rd.CompType.Typeless,
                    )
                )
            )

    controller.SetFrameEvent(event_id_b, True)
    max_delta = 0.0
    index = 0
    for row in range(grid_y):
        y = (
            0
            if grid_y == 1
            else int(round(((height - 1) * row) / float(grid_y - 1)))
        )
        for col in range(grid_x):
            x = (
                0
                if grid_x == 1
                else int(round(((width - 1) * col) / float(grid_x - 1)))
            )
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


def dense_grid_clip_fraction(
    controller, rd, event_id, sample, grid_x=32, grid_y=18, clip_threshold=0.995
):
    if sample is None:
        return 1.0

    width = int(sample["width"])
    height = int(sample["height"])
    sub = make_sample_subresource(rd, sample)
    controller.SetFrameEvent(event_id, True)

    clipped_count = 0
    total_count = 0
    for row in range(grid_y):
        y = (
            0
            if grid_y == 1
            else int(round(((height - 1) * row) / float(grid_y - 1)))
        )
        for col in range(grid_x):
            x = (
                0
                if grid_x == 1
                else int(round(((width - 1) * col) / float(grid_x - 1)))
            )
            values = float4_values(
                controller.PickPixel(
                    sample["resource_id"], x, y, sub, rd.CompType.Typeless
                )
            )
            if max(values[:3]) >= clip_threshold:
                clipped_count += 1
            total_count += 1

    if total_count == 0:
        return 1.0
    return clipped_count / float(total_count)


def save_texture_sample(controller, rd, sample, path_value, type_cast):
    if sample is None or not path_value:
        return False

    output_path = Path(path_value).resolve()
    output_path.parent.mkdir(parents=True, exist_ok=True)

    save = rd.TextureSave()
    save.resourceId = sample["resource_id"]
    save.destType = rd.FileType.PNG
    save.mip = int(sample["mip"])
    save.typeCast = type_cast
    save.alpha = rd.AlphaMapping.Preserve
    save.slice.sliceIndex = int(sample["slice"])
    save.sample.sampleIndex = 0

    controller.SaveTexture(save, str(output_path))
    return output_path.exists() and output_path.stat().st_size > 0


def build_report(
    controller, report: ReportWriter, capture_path: Path, report_path: Path
):
    rd = renderdoc_module()
    action_records = collect_action_records(controller)
    resource_records = collect_resource_records_raw(controller)
    resource_names = resource_id_to_name(controller)
    texture_descs = texture_desc_map(controller)

    atmosphere_sky_view_scopes = records_with_name(action_records, ATMOSPHERE_SKY_VIEW_SCOPE_NAME)
    atmosphere_sky_view_records = records_under_prefix(action_records, ATMOSPHERE_SKY_VIEW_SCOPE_NAME)
    atmosphere_camera_aerial_scopes = records_with_name(action_records, ATMOSPHERE_CAMERA_AERIAL_SCOPE_NAME)
    atmosphere_camera_aerial_records = records_under_prefix(action_records, ATMOSPHERE_CAMERA_AERIAL_SCOPE_NAME)

    stage3_records = records_under_prefix(action_records, STAGE3_SCOPE_NAME)
    stage8_records = records_under_prefix(action_records, STAGE8_SCOPE_NAME)
    stage12_directional_records = records_under_prefix(
        action_records,
        STAGE12_SCOPE_NAME + " > " + STAGE12_DIRECTIONAL_SCOPE_NAME,
    )
    stage12_spot_records = records_under_prefix(
        action_records, STAGE12_SCOPE_NAME + " > " + STAGE12_SPOT_SCOPE_NAME
    )
    stage15_sky_records = records_under_prefix(
        action_records, STAGE15_SKY_SCOPE_NAME
    )
    stage15_atmosphere_records = records_under_prefix(
        action_records, STAGE15_ATMOSPHERE_SCOPE_NAME
    )
    stage15_atmosphere_event_ids = collect_event_ids(
        records_with_name(action_records, STAGE15_ATMOSPHERE_SCOPE_NAME),
        stage15_atmosphere_records,
    )
    stage15_fog_records = records_under_prefix(
        action_records, STAGE15_FOG_SCOPE_NAME
    )
    stage22_tonemap_records = records_under_prefix(
        action_records, STAGE22_TONEMAP_SCOPE_NAME
    )
    compositing_scopes = records_with_name_prefix(
        action_records, COMPOSITING_SCOPE_PREFIX
    )
    imgui_overlay_blend_scopes = records_with_name_prefixes(
        action_records, IMGUI_OVERLAY_BLEND_SCOPE_PREFIXES
    )
    compositing_records = []
    for scope in compositing_scopes:
        compositing_records.extend(
            records_under_prefix(action_records, scope.name)
        )
    imgui_overlay_blend_records = []
    for scope in imgui_overlay_blend_scopes:
        imgui_overlay_blend_records.extend(
            records_under_prefix(action_records, scope.name)
        )

    stage3_last_draw = find_last_named_record(stage3_records, DRAW_NAME)
    stage8_last_draw = find_last_named_record(stage8_records, DRAW_NAME)
    stage12_directional_last_draw = find_last_named_record(
        stage12_directional_records, DRAW_NAME
    )
    stage12_spot_last_draw = find_last_named_record(
        stage12_spot_records, DRAW_NAME
    )
    stage15_sky_last_draw = find_last_named_record(
        stage15_sky_records, DRAW_NAME
    )
    stage15_atmosphere_last_draw = find_last_named_record(
        stage15_atmosphere_records, DRAW_NAME
    )
    stage15_fog_last_draw = find_last_named_record(
        stage15_fog_records, DRAW_NAME
    )
    stage22_tonemap_last_draw = find_last_named_record(
        stage22_tonemap_records, DRAW_NAME
    )
    compositing_last_action = find_last_work_record(compositing_records)
    final_present_action = (
        find_last_named_record(imgui_overlay_blend_records, DRAW_NAME)
        or compositing_last_action
    )

    required = [
        ("stage3", stage3_last_draw),
        ("stage8", stage8_last_draw),
        ("stage12_directional", stage12_directional_last_draw),
        ("stage12_spot", stage12_spot_last_draw),
        ("stage15_sky", stage15_sky_last_draw),
        ("stage15_atmosphere", stage15_atmosphere_last_draw),
        ("stage15_fog", stage15_fog_last_draw),
        ("compositing", compositing_last_action),
        ("final_present", final_present_action),
    ]
    missing = [name for name, record in required if record is None]
    if missing:
        raise RuntimeError(
            "Required Async events were not found: {}".format(
                ", ".join(missing)
            )
        )

    stage22_tonemap_last_draw = resolve_stage22_tonemap_record(
        action_records,
        stage22_tonemap_last_draw,
        stage15_fog_last_draw,
        compositing_last_action,
    )
    if stage22_tonemap_last_draw is None:
        raise RuntimeError(
            "Required Async events were not found: stage22_tonemap"
        )

    stage3 = analyze_draw_event(
        controller, rd, resource_names, texture_descs, stage3_last_draw.event_id
    )
    stage8 = analyze_draw_event(
        controller, rd, resource_names, texture_descs, stage8_last_draw.event_id
    )
    stage12_directional = analyze_draw_event(
        controller,
        rd,
        resource_names,
        texture_descs,
        stage12_directional_last_draw.event_id,
    )
    stage12_spot = analyze_draw_event(
        controller,
        rd,
        resource_names,
        texture_descs,
        stage12_spot_last_draw.event_id,
    )
    stage15_sky = analyze_draw_event(
        controller,
        rd,
        resource_names,
        texture_descs,
        stage15_sky_last_draw.event_id,
    )
    stage15_atmosphere = analyze_draw_event(
        controller,
        rd,
        resource_names,
        texture_descs,
        stage15_atmosphere_last_draw.event_id,
    )
    stage15_fog = analyze_draw_event(
        controller,
        rd,
        resource_names,
        texture_descs,
        stage15_fog_last_draw.event_id,
    )
    stage22_tonemap = analyze_draw_event(
        controller,
        rd,
        resource_names,
        texture_descs,
        stage22_tonemap_last_draw.event_id,
    )
    compositing = analyze_draw_event(
        controller,
        rd,
        resource_names,
        texture_descs,
        compositing_last_action.event_id,
    )
    final_present_state = analyze_draw_event(
        controller,
        rd,
        resource_names,
        texture_descs,
        final_present_action.event_id,
    )

    stage3_depth_ok = stage3["depth"] is not None and stage3["depth"]["nonzero"]
    stage8_shadow_depth_ok = (
        stage8["depth"] is not None and stage8["depth"]["nonzero"]
    )
    stage12_directional_scene_color = find_output_sample(
        stage12_directional, "SceneColor"
    )
    stage12_spot_scene_color = find_output_sample(stage12_spot, "SceneColor")
    stage12_directional_scene_color_nonzero = (
        stage12_directional_scene_color is not None
        and stage12_directional_scene_color["rgb_nonzero"]
    )
    stage12_spot_scene_color_nonzero = (
        stage12_spot_scene_color is not None
        and stage12_spot_scene_color["rgb_nonzero"]
    )

    stage15_sky_scene_color = find_output_sample(stage15_sky, "SceneColor")
    stage15_atmosphere_scene_color = find_output_sample(
        stage15_atmosphere, "SceneColor"
    )
    stage15_fog_scene_color = find_output_sample(stage15_fog, "SceneColor")
    stage12_final_scene_color = (
        stage12_spot_scene_color or stage12_directional_scene_color
    )

    stage15_sky_scene_color_delta = max(
        max_sample_delta(stage12_final_scene_color, stage15_sky_scene_color),
        dense_grid_delta(
            controller,
            rd,
            stage12_spot_last_draw.event_id,
            stage12_final_scene_color,
            stage15_sky_last_draw.event_id,
            stage15_sky_scene_color,
        ),
    )
    stage15_atmosphere_scene_color_delta = max(
        max_sample_delta(
            stage15_sky_scene_color, stage15_atmosphere_scene_color
        ),
        dense_grid_delta(
            controller,
            rd,
            stage15_sky_last_draw.event_id,
            stage15_sky_scene_color,
            stage15_atmosphere_last_draw.event_id,
            stage15_atmosphere_scene_color,
        ),
    )
    stage15_fog_scene_color_delta = max(
        max_sample_delta(
            stage15_atmosphere_scene_color, stage15_fog_scene_color
        ),
        dense_grid_delta(
            controller,
            rd,
            stage15_atmosphere_last_draw.event_id,
            stage15_atmosphere_scene_color,
            stage15_fog_last_draw.event_id,
            stage15_fog_scene_color,
        ),
    )

    stage15_sky_scene_color_changed = stage15_sky_scene_color_delta > 1.0e-5
    stage15_atmosphere_scene_color_changed = (
        stage15_atmosphere_scene_color_delta > 1.0e-5
    )
    stage15_fog_scene_color_changed = stage15_fog_scene_color_delta > 1.0e-5
    atmosphere_sky_view_scope_count_match = len(atmosphere_sky_view_scopes) == 1
    atmosphere_sky_view_dispatch_count_match = (
        len(records_with_name(atmosphere_sky_view_records, "ID3D12GraphicsCommandList::Dispatch()")) == 1
    )
    atmosphere_camera_aerial_scope_count_match = len(atmosphere_camera_aerial_scopes) == 1
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
    stage15_async_scene_color_delta = max(
        max_sample_delta(stage12_final_scene_color, stage15_fog_scene_color),
        dense_grid_delta(
            controller,
            rd,
            stage12_spot_last_draw.event_id,
            stage12_final_scene_color,
            stage15_fog_last_draw.event_id,
            stage15_fog_scene_color,
        ),
    )
    stage15_async_scene_color_changed = stage15_async_scene_color_delta > 1.0e-5
    stage15_far_background_mask = evaluate_stage15_far_background_mask(
        stage3["depth"], stage12_final_scene_color, stage15_sky_scene_color
    )
    stage15_sky_quality = evaluate_stage15_sky_quality(
        stage3["depth"], stage15_sky_scene_color
    )

    final_present = (
        final_present_state["outputs"][0]
        if final_present_state["outputs"]
        else None
    )
    final_present_nonzero = (
        final_present is not None and final_present["rgb_nonzero"]
    )
    tonemap_output = (
        stage22_tonemap["outputs"][0] if stage22_tonemap["outputs"] else None
    )
    sampled_tonemap_output_nonzero = (
        tonemap_output is not None and tonemap_output["rgb_nonzero"]
    )
    stage22_visibility = resolve_stage22_visibility(
        sampled_tonemap_output_nonzero, final_present_nonzero
    )
    tonemap_output_nonzero = stage22_visibility[
        "stage22_tonemap_output_nonzero"
    ]
    tonemap_clipping_ratio = dense_grid_clip_fraction(
        controller, rd, stage22_tonemap_last_draw.event_id, tonemap_output
    )
    tonemap_clipping_ratio_ok = tonemap_clipping_ratio <= 0.25
    overlay_or_composition_delta = max(
        max_sample_delta(tonemap_output, final_present),
        dense_grid_delta(
            controller,
            rd,
            stage22_tonemap_last_draw.event_id,
            tonemap_output,
            final_present_action.event_id,
            final_present,
        ),
    )
    overlay_or_composition_changed = overlay_or_composition_delta > 1.0e-5
    imgui_overlay_composited_on_scene = (
        len(imgui_overlay_blend_scopes) > 0 and overlay_or_composition_changed
    )

    color_export_path = os.environ.get(COLOR_EXPORT_ENV, "").strip()
    depth_export_path = os.environ.get(DEPTH_EXPORT_ENV, "").strip()
    color_exported = save_texture_sample(
        controller, rd, final_present, color_export_path, rd.CompType.Typeless
    )
    depth_exported = save_texture_sample(
        controller, rd, stage3["depth"], depth_export_path, rd.CompType.Depth
    )

    report.append("analysis_profile=async_runtime_products")
    report.append("capture_path={}".format(capture_path))
    report.append("report_path={}".format(report_path))
    report.append("stage3_last_draw_event={}".format(stage3_last_draw.event_id))
    report.append("stage8_last_draw_event={}".format(stage8_last_draw.event_id))
    report.append(
        "stage12_directional_last_draw_event={}".format(
            stage12_directional_last_draw.event_id
        )
    )
    report.append(
        "stage12_spot_last_draw_event={}".format(
            stage12_spot_last_draw.event_id
        )
    )
    report.append(
        "stage15_sky_last_draw_event={}".format(stage15_sky_last_draw.event_id)
    )
    report.append(
        "stage15_atmosphere_last_draw_event={}".format(
            stage15_atmosphere_last_draw.event_id
        )
    )
    report.append(
        "stage15_fog_last_draw_event={}".format(stage15_fog_last_draw.event_id)
    )
    report.append(
        "stage22_tonemap_last_draw_event={}".format(
            stage22_tonemap_last_draw.event_id
        )
    )
    report.append(
        "compositing_last_event={}".format(compositing_last_action.event_id)
    )
    report.append(
        "final_present_event={}".format(final_present_action.event_id)
    )
    report.append(
        "stage3_scene_depth_nonzero={}".format(str(stage3_depth_ok).lower())
    )
    report.append(
        "stage8_shadow_depth_nonzero={}".format(
            str(stage8_shadow_depth_ok).lower()
        )
    )
    report.append(
        "stage12_directional_scene_color_nonzero={}".format(
            str(stage12_directional_scene_color_nonzero).lower()
        )
    )
    report.append(
        "stage12_spot_scene_color_nonzero={}".format(
            str(stage12_spot_scene_color_nonzero).lower()
        )
    )
    report.append(
        "stage15_sky_scene_color_delta_max={:.6f}".format(
            stage15_sky_scene_color_delta
        )
    )
    report.append(
        "stage15_atmosphere_scene_color_delta_max={:.6f}".format(
            stage15_atmosphere_scene_color_delta
        )
    )
    report.append(
        "stage15_fog_scene_color_delta_max={:.6f}".format(
            stage15_fog_scene_color_delta
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
        "atmosphere_sky_view_lut_scope_count_match={}".format(
            str(atmosphere_sky_view_scope_count_match).lower()
        )
    )
    report.append(
        "atmosphere_sky_view_lut_dispatch_count_match={}".format(
            str(atmosphere_sky_view_dispatch_count_match).lower()
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
        "stage15_async_scene_color_delta_max={:.6f}".format(
            stage15_async_scene_color_delta
        )
    )
    report.append(
        "stage15_async_scene_color_changed={}".format(
            str(stage15_async_scene_color_changed).lower()
        )
    )
    report.append(
        "stage15_far_depth_reference={:.6f}".format(
            stage15_far_background_mask["far_depth_reference"]
        )
    )
    report.append(
        "stage15_background_probe_count={}".format(
            stage15_sky_quality["background_probe_count"]
        )
    )
    report.append(
        "stage15_foreground_probe_count={}".format(
            stage15_far_background_mask["foreground_probe_count"]
        )
    )
    report.append(
        "stage15_foreground_delta_max={:.6f}".format(
            stage15_far_background_mask["foreground_delta_max"]
        )
    )
    report.append(
        "stage15_far_background_mask_valid={}".format(
            str(stage15_far_background_mask["valid"]).lower()
        )
    )
    report.append(
        "stage15_background_luminance_min={:.6f}".format(
            stage15_sky_quality["background_luminance_min"]
        )
    )
    report.append(
        "stage15_background_luminance_max={:.6f}".format(
            stage15_sky_quality["background_luminance_max"]
        )
    )
    report.append(
        "stage15_background_luminance_range={:.6f}".format(
            stage15_sky_quality["background_luminance_range"]
        )
    )
    report.append(
        "stage15_sky_quality_ok={}".format(
            str(stage15_sky_quality["valid"]).lower()
        )
    )
    report.append(
        "stage22_tonemap_output_nonzero={}".format(
            str(tonemap_output_nonzero).lower()
        )
    )
    report.append(
        "stage22_sampled_tonemap_output_nonzero={}".format(
            str(sampled_tonemap_output_nonzero).lower()
        )
    )
    report.append(
        "final_present_nonzero={}".format(str(final_present_nonzero).lower())
    )
    report.append(
        "stage22_exposure_clipping_ratio={:.6f}".format(tonemap_clipping_ratio)
    )
    report.append(
        "stage22_exposure_clipping_ratio_ok={}".format(
            str(tonemap_clipping_ratio_ok).lower()
        )
    )
    report.append(
        "final_present_vs_tonemap_delta_max={:.6f}".format(
            overlay_or_composition_delta
        )
    )
    report.append(
        "final_present_vs_tonemap_changed={}".format(
            str(overlay_or_composition_changed).lower()
        )
    )
    report.append(
        "imgui_overlay_composited_on_scene={}".format(
            str(imgui_overlay_composited_on_scene).lower()
        )
    )
    report.append("exported_color_path={}".format(color_export_path))
    report.append(
        "exported_color_exists={}".format(str(color_exported).lower())
    )
    report.append("exported_depth_path={}".format(depth_export_path))
    report.append(
        "exported_depth_exists={}".format(str(depth_exported).lower())
    )

    overall = (
        stage3_depth_ok
        and stage8_shadow_depth_ok
        and stage12_directional_scene_color_nonzero
        and stage12_spot_scene_color_nonzero
        and stage15_sky_scene_color_changed
        and stage15_atmosphere_scene_color_changed
        and stage15_fog_scene_color_changed
        and atmosphere_sky_view_scope_count_match
        and atmosphere_sky_view_dispatch_count_match
        and atmosphere_camera_aerial_scope_count_match
        and atmosphere_camera_aerial_dispatch_count_match
        and atmosphere_camera_aerial_consumed
        and stage15_async_scene_color_changed
        and stage15_far_background_mask["valid"]
        and stage15_sky_quality["valid"]
        and tonemap_output_nonzero
        and tonemap_clipping_ratio_ok
        and final_present_nonzero
        and imgui_overlay_composited_on_scene
        and color_exported
        and depth_exported
    )
    report.append("overall_verdict={}".format("pass" if overall else "fail"))
    report.blank()

    append_texture_sample(report, "stage3_depth", stage3["depth"])
    append_texture_sample(report, "stage8_depth", stage8["depth"])
    append_texture_sample(
        report,
        "stage12_directional_scene_color",
        stage12_directional_scene_color,
    )
    append_texture_sample(
        report, "stage12_spot_scene_color", stage12_spot_scene_color
    )
    append_texture_sample(
        report, "stage15_sky_scene_color", stage15_sky_scene_color
    )
    append_texture_sample(
        report, "stage15_atmosphere_scene_color", stage15_atmosphere_scene_color
    )
    append_texture_sample(
        report, "stage15_fog_scene_color", stage15_fog_scene_color
    )
    append_texture_sample(report, "stage22_tonemap_output", tonemap_output)
    append_texture_sample(report, "final_present", final_present)


def main():
    run_ui_script(REPORT_SUFFIX, build_report)


if __name__ == "__main__":
    main()
