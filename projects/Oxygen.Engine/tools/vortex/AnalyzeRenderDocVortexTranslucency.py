"""RenderDoc UI analyzer for the VortexBasic translucency proof scene."""

import builtins
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


REPORT_SUFFIX = "_vortex_translucency_report.txt"
DRAW_NAME = "ID3D12GraphicsCommandList::DrawInstanced()"
PIXEL_GRID_X = 97
PIXEL_GRID_Y = 73
MIN_COLORED_PIXEL_COUNT = 16
MIN_STAGE18_RGB_DELTA = 0.08


def records_with_name(action_records, name):
    return [record for record in action_records if record.name == name]


def records_under_prefix(action_records, prefix):
    prefix_with_sep = prefix + " > "
    return [record for record in action_records if record.path.startswith(prefix_with_sep)]


def count_named_records(action_records, name):
    return sum(1 for record in action_records if record.name == name)


def first_event(records):
    if not records:
        return 0
    return min(record.event_id for record in records)


def last_event(records):
    if not records:
        return 0
    return max(record.event_id for record in records)


def last_draw_before(action_records, event_id):
    candidates = [
        record.event_id
        for record in action_records
        if record.name == DRAW_NAME and record.event_id < event_id
    ]
    return max(candidates) if candidates else 0


def append_exact_count_check(report, label, actual, expected):
    report.append("{}_actual={}".format(label, actual))
    report.append("{}_expected={}".format(label, expected))
    report.append("{}_match={}".format(label, "true" if actual == expected else "false"))
    if actual != expected:
        raise RuntimeError("{} mismatch: expected {}, got {}".format(label, expected, actual))


def append_bool_check(report, label, value, details=""):
    report.append("{}={}".format(label, "true" if value else "false"))
    if not value:
        suffix = ": {}".format(details) if details else ""
        raise RuntimeError("{} check failed{}".format(label, suffix))


def float4_values(pixel_value):
    return [float(v) for v in list(pixel_value.floatValue)[:4]]


def format_values(values):
    return ",".join("{:.9g}".format(value) for value in values)


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


def output_dimensions(texture_desc, descriptor):
    width = int(safe_getattr(texture_desc, "width", 1) or 1)
    height = int(safe_getattr(texture_desc, "height", 1) or 1)
    mip = int(safe_getattr(descriptor, "firstMip", 0) or 0)
    return max(1, width >> mip), max(1, height >> mip)


def resolve_scene_color_output(controller, rd, event_id, resource_names, texture_descs):
    controller.SetFrameEvent(event_id, True)
    state = controller.GetPipelineState()
    outputs = state.GetOutputTargets()
    if not outputs:
        raise RuntimeError("No color output bound at event {}".format(event_id))

    fallback = outputs[0]
    for descriptor in outputs:
        resource_id = safe_getattr(descriptor, "resource")
        name = resource_names.get(str(resource_id), str(resource_id))
        lower_name = str(name).lower()
        if "scenecolor" in lower_name or "scene color" in lower_name:
            fallback = descriptor
            break

    resource_id = safe_getattr(fallback, "resource")
    texture_desc = texture_descs.get(str(resource_id))
    if resource_id is None or texture_desc is None:
        raise RuntimeError("Unable to resolve output texture at event {}".format(event_id))

    sub = make_subresource(rd, fallback)
    width, height = output_dimensions(texture_desc, fallback)
    min_value, max_value = controller.GetMinMax(resource_id, sub, rd.CompType.Typeless)
    return {
        "resource_id": resource_id,
        "name": resource_names.get(str(resource_id), str(resource_id)),
        "subresource": sub,
        "width": width,
        "height": height,
        "min": float4_values(min_value),
        "max": float4_values(max_value),
    }


def classify_translucent_color(values):
    r, g, b = values[:3]
    maximum = max(r, g, b)
    if maximum < 0.18:
        return "dark"
    if g >= 0.24 and b >= 0.24 and r <= min(g, b) * 0.72:
        return "cyan"
    if r >= 0.24 and b >= 0.20 and g <= min(r, b) * 0.72:
        return "magenta"
    return "other"


def sample_output_grid(controller, rd, event_id, sample):
    controller.SetFrameEvent(event_id, True)
    width = sample["width"]
    height = sample["height"]
    resource_id = sample["resource_id"]
    sub = sample["subresource"]
    counts = {"cyan": 0, "magenta": 0, "dark": 0, "other": 0}
    rgb_sum = [0.0, 0.0, 0.0]
    total = 0
    for row in range(PIXEL_GRID_Y):
        y = 0 if PIXEL_GRID_Y == 1 else int(round(((height - 1) * row) / float(PIXEL_GRID_Y - 1)))
        for col in range(PIXEL_GRID_X):
            x = 0 if PIXEL_GRID_X == 1 else int(round(((width - 1) * col) / float(PIXEL_GRID_X - 1)))
            values = float4_values(controller.PickPixel(resource_id, x, y, sub, rd.CompType.Typeless))
            counts[classify_translucent_color(values)] += 1
            rgb_sum[0] += values[0]
            rgb_sum[1] += values[1]
            rgb_sum[2] += values[2]
            total += 1
    return {"counts": counts, "rgb_sum": rgb_sum, "total": total}


def sample_grid_delta(controller, rd, before_event, before_sample, after_event, after_sample):
    if before_sample["resource_id"] != after_sample["resource_id"]:
        return 0.0

    width = min(before_sample["width"], after_sample["width"])
    height = min(before_sample["height"], after_sample["height"])
    resource_id = before_sample["resource_id"]
    before_sub = before_sample["subresource"]
    after_sub = after_sample["subresource"]

    before_values = []
    controller.SetFrameEvent(before_event, True)
    for row in range(PIXEL_GRID_Y):
        y = 0 if PIXEL_GRID_Y == 1 else int(round(((height - 1) * row) / float(PIXEL_GRID_Y - 1)))
        for col in range(PIXEL_GRID_X):
            x = 0 if PIXEL_GRID_X == 1 else int(round(((width - 1) * col) / float(PIXEL_GRID_X - 1)))
            before_values.append(
                float4_values(controller.PickPixel(resource_id, x, y, before_sub, rd.CompType.Typeless))
            )

    max_delta = 0.0
    controller.SetFrameEvent(after_event, True)
    for index, row in enumerate(range(PIXEL_GRID_Y)):
        y = 0 if PIXEL_GRID_Y == 1 else int(round(((height - 1) * row) / float(PIXEL_GRID_Y - 1)))
        for col in range(PIXEL_GRID_X):
            x = 0 if PIXEL_GRID_X == 1 else int(round(((width - 1) * col) / float(PIXEL_GRID_X - 1)))
            after_values = float4_values(
                controller.PickPixel(resource_id, x, y, after_sub, rd.CompType.Typeless)
            )
            before = before_values[index * PIXEL_GRID_X + col]
            max_delta = max(max_delta, max(abs(after_values[i] - before[i]) for i in range(3)))
    return max_delta


def build_report(controller, report: ReportWriter, capture_path: Path, report_path: Path):
    rd = renderdoc_module()
    action_records = collect_action_records(controller)
    resource_names = resource_id_to_name(controller)
    texture_descs = texture_desc_map(controller)

    stage9_scope_name = "Vortex.Stage9.BasePass"
    stage9_main_scope_name = "Vortex.Stage9.BasePass.MainPass"
    stage12_scope_name = "Vortex.Stage12.DeferredLighting"
    stage15_scope_name = "Vortex.Stage15.SkyAtmosphereFog"
    stage18_scope_name = "Vortex.Stage18.Translucency"
    stage20_ground_grid_scope_name = "Vortex.Stage20.GroundGrid"
    stage21_scope_name = "Vortex.Stage21.ResolveSceneColor"

    stage9_scope = records_with_name(action_records, stage9_scope_name)
    stage12_scope = records_with_name(action_records, stage12_scope_name)
    stage15_scope = records_with_name(action_records, stage15_scope_name)
    stage18_scope = records_with_name(action_records, stage18_scope_name)
    stage20_ground_grid_scope = records_with_name(action_records, stage20_ground_grid_scope_name)
    stage21_scope = records_with_name(action_records, stage21_scope_name)

    stage9_main_records = records_under_prefix(
        action_records, stage9_scope_name + " > " + stage9_main_scope_name
    )
    stage9_records = (
        stage9_main_records if stage9_main_records else records_under_prefix(action_records, stage9_scope_name)
    )
    stage18_records = records_under_prefix(action_records, stage18_scope_name)

    stage9_draw_count = count_named_records(stage9_records, DRAW_NAME)
    stage18_draw_count = count_named_records(stage18_records, DRAW_NAME)

    stage12_event = last_event(stage12_scope)
    stage15_event = last_event(stage15_scope)
    stage18_first_event = first_event(stage18_records)
    stage18_last_event = last_event(stage18_records)
    stage18_scope_event = first_event(stage18_scope)
    stage21_event = first_event(stage21_scope)
    stage18_after_post_opaque = stage18_first_event > max(stage12_event, stage15_event)
    stage18_before_resolve = stage21_event == 0 or stage18_last_event < stage21_event
    before_stage18_event = last_draw_before(action_records, stage18_scope_event)

    report.append("analysis_profile=vortex_translucency_runtime_capture")
    report.append("capture_path={}".format(capture_path))
    report.append("report_path={}".format(report_path))
    report.append("total_actions={}".format(len(action_records)))
    report.append("stage12_event={}".format(stage12_event))
    report.append("stage15_event={}".format(stage15_event))
    report.append("stage18_scope_event={}".format(stage18_scope_event))
    report.append("stage18_first_event={}".format(stage18_first_event))
    report.append("stage18_last_event={}".format(stage18_last_event))
    report.append("stage21_event={}".format(stage21_event))

    append_exact_count_check(report, "stage9_scope_count", len(stage9_scope), 1)
    append_exact_count_check(report, "stage18_scope_count", len(stage18_scope), 1)
    append_exact_count_check(report, "stage18_draw_count", stage18_draw_count, 2)
    append_exact_count_check(report, "stage9_draw_count", stage9_draw_count, 2)
    append_exact_count_check(report, "stage20_ground_grid_scope_count", len(stage20_ground_grid_scope), 0)
    append_bool_check(report, "stage18_after_post_opaque", stage18_after_post_opaque)
    append_bool_check(report, "stage18_before_resolve", stage18_before_resolve)
    append_bool_check(report, "stage18_before_event_resolved", before_stage18_event > 0)

    before_sample = resolve_scene_color_output(
        controller, rd, before_stage18_event, resource_names, texture_descs
    )
    after_sample = resolve_scene_color_output(
        controller, rd, stage18_last_event, resource_names, texture_descs
    )
    before_grid = sample_output_grid(controller, rd, before_stage18_event, before_sample)
    after_grid = sample_output_grid(controller, rd, stage18_last_event, after_sample)
    max_rgb_delta = sample_grid_delta(
        controller, rd, before_stage18_event, before_sample, stage18_last_event, after_sample
    )

    report.append("stage18_output_resource={}".format(after_sample["name"]))
    report.append("stage18_output_dims={}x{}".format(after_sample["width"], after_sample["height"]))
    report.append("stage18_output_min={}".format(format_values(after_sample["min"])))
    report.append("stage18_output_max={}".format(format_values(after_sample["max"])))
    report.append("stage18_pixel_grid={}x{}".format(PIXEL_GRID_X, PIXEL_GRID_Y))
    report.append("stage18_before_cyan_pixels={}".format(before_grid["counts"]["cyan"]))
    report.append("stage18_before_magenta_pixels={}".format(before_grid["counts"]["magenta"]))
    report.append("stage18_after_cyan_pixels={}".format(after_grid["counts"]["cyan"]))
    report.append("stage18_after_magenta_pixels={}".format(after_grid["counts"]["magenta"]))
    report.append("stage18_max_rgb_delta={:.9g}".format(max_rgb_delta))

    append_bool_check(
        report,
        "stage18_cyan_visible",
        after_grid["counts"]["cyan"] >= MIN_COLORED_PIXEL_COUNT,
        "before={} after={} required={}".format(
            before_grid["counts"]["cyan"],
            after_grid["counts"]["cyan"],
            MIN_COLORED_PIXEL_COUNT,
        )
        + " before_event={} stage18_scope_event={} stage18_first_event={} after_event={}".format(
            before_stage18_event,
            stage18_scope_event,
            stage18_first_event,
            stage18_last_event,
        ),
    )
    append_bool_check(
        report,
        "stage18_magenta_visible",
        after_grid["counts"]["magenta"] >= MIN_COLORED_PIXEL_COUNT,
        "before={} after={} required={}".format(
            before_grid["counts"]["magenta"],
            after_grid["counts"]["magenta"],
            MIN_COLORED_PIXEL_COUNT,
        )
        + " after_counts={}".format(after_grid["counts"]),
    )
    append_bool_check(
        report,
        "stage18_scene_color_changed",
        max_rgb_delta >= MIN_STAGE18_RGB_DELTA,
        "delta={:.9g} required={:.9g}".format(max_rgb_delta, MIN_STAGE18_RGB_DELTA),
    )


def main():
    run_ui_script(REPORT_SUFFIX, build_report)


main()
