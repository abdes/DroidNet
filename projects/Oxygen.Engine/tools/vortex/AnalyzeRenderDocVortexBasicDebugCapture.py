"""RenderDoc UI analyzer for VortexBasic deferred debug visualizations."""

import builtins
import os
import re
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
    renderdoc_module,
    resource_id_to_name,
    run_ui_script,
    safe_getattr,
)


REPORT_SUFFIX = "_vortexbasic_debug_report.txt"
DRAW_NAME = "ID3D12GraphicsCommandList::DrawInstanced()"

MODE_SPECS = {
    "basecolor": {
        "label": "BaseColor",
        "scope_name": "Vortex.DebugVisualization.BaseColor",
        "expect_grayscale": False,
    },
    "worldnormals": {
        "label": "WorldNormals",
        "scope_name": "Vortex.DebugVisualization.WorldNormals",
        "expect_grayscale": False,
    },
    "roughness": {
        "label": "Roughness",
        "scope_name": "Vortex.DebugVisualization.Roughness",
        "expect_grayscale": True,
    },
    "metalness": {
        "label": "Metalness",
        "scope_name": "Vortex.DebugVisualization.Metalness",
        "expect_grayscale": True,
    },
    "scenedepthraw": {
        "label": "SceneDepthRaw",
        "scope_name": "Vortex.DebugVisualization.SceneDepthRaw",
        "expect_grayscale": True,
    },
    "scenedepthlinear": {
        "label": "SceneDepthLinear",
        "scope_name": "Vortex.DebugVisualization.SceneDepthLinear",
        "expect_grayscale": True,
    },
}


def normalize_token(text):
    return re.sub(r"[^a-z0-9]+", "", text.lower())


def resolve_expected_mode(capture_path):
    candidates = [
        os.environ.get("OXYGEN_RENDERDOC_PASS_NAME", ""),
        capture_path.stem,
    ]
    for candidate in candidates:
        normalized = normalize_token(candidate)
        for mode_key, spec in MODE_SPECS.items():
            if mode_key in normalized or normalize_token(spec["label"]) in normalized:
                return mode_key, spec

    raise RuntimeError(
        "Unable to infer the expected debug mode from OXYGEN_RENDERDOC_PASS_NAME "
        "or the capture path."
    )


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
    return max(abs(v) for v in rgb_delta) > epsilon or max(
        abs(v) for v in max_values[:3]
    ) > epsilon


def vec_diff(a_values, b_values):
    return [b - a for a, b in zip(a_values[:4], b_values[:4])]


def is_nonzero_range(min_values, max_values, epsilon=1.0e-6):
    return vec_max_abs(vec_diff(min_values, max_values)) > epsilon or vec_max_abs(
        max_values
    ) > epsilon


def is_grayscale(values, epsilon=1.0e-4):
    return (
        abs(values[0] - values[1]) <= epsilon
        and abs(values[1] - values[2]) <= epsilon
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
        "nonzero": is_nonzero_range(min_values, max_values),
        "rgb_nonzero": rgb_nonzero(min_values, max_values),
        "center_grayscale": is_grayscale(center_values),
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


def find_last_named_record(records, name):
    matches = [record for record in records if record.name == name]
    return matches[-1] if matches else None


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
    report.append("{}_nonzero={}".format(prefix, str(sample["nonzero"]).lower()))
    report.append(
        "{}_rgb_nonzero={}".format(prefix, str(sample["rgb_nonzero"]).lower())
    )
    report.append(
        "{}_center_grayscale={}".format(
            prefix, str(sample["center_grayscale"]).lower()
        )
    )


def build_report(controller, report: ReportWriter, capture_path: Path, report_path: Path):
    rd = renderdoc_module()
    action_records = collect_action_records(controller)
    resource_names = resource_id_to_name(controller)
    texture_descs = texture_desc_map(controller)
    mode_key, expected_mode = resolve_expected_mode(capture_path)

    stage9_name = "Vortex.Stage9.BasePass"
    stage9_main_pass_name = "Vortex.Stage9.BasePass.MainPass"
    stage12_name = "Vortex.Stage12.DeferredLighting"
    compositing_name = "Vortex.CompositingTask[label=Composite Copy View 1]"

    stage9_scope = records_with_name(action_records, stage9_name)
    debug_scope = records_with_name(action_records, expected_mode["scope_name"])
    stage12_scope = records_with_name(action_records, stage12_name)
    compositing_scope = records_with_name(action_records, compositing_name)

    if len(stage9_scope) != 1:
        raise RuntimeError("Expected one Stage 9 scope, found {}".format(len(stage9_scope)))
    if len(debug_scope) != 1:
        raise RuntimeError(
            "Expected one debug scope '{}' , found {}".format(
                expected_mode["scope_name"], len(debug_scope)
            )
        )
    if len(stage12_scope) != 0:
        raise RuntimeError(
            "Deferred lighting should be bypassed for debug mode '{}'; found {} Stage 12 scope(s)".format(
                expected_mode["label"], len(stage12_scope)
            )
        )
    if len(compositing_scope) != 1:
        raise RuntimeError(
            "Expected one compositing scope, found {}".format(len(compositing_scope))
        )

    stage9_main_pass_records = records_under_prefix(
        action_records, stage9_name + " > " + stage9_main_pass_name
    )
    stage9_records = (
        stage9_main_pass_records
        if stage9_main_pass_records
        else records_under_prefix(action_records, stage9_name)
    )
    debug_records = records_under_prefix(action_records, expected_mode["scope_name"])
    compositing_records = records_under_prefix(action_records, compositing_name)

    debug_last_draw = find_last_named_record(debug_records, DRAW_NAME)
    compositing_last_draw = find_last_named_record(compositing_records, DRAW_NAME)
    if debug_last_draw is None or compositing_last_draw is None:
        raise RuntimeError("Required debug/compositing draw events were not found.")

    debug_draw = analyze_draw_event(
        controller, rd, resource_names, texture_descs, debug_last_draw.event_id
    )
    compositing_draw = analyze_draw_event(
        controller, rd, resource_names, texture_descs, compositing_last_draw.event_id
    )
    debug_output = debug_draw["outputs"][0] if debug_draw["outputs"] else None
    final_present = compositing_draw["outputs"][0] if compositing_draw["outputs"] else None

    debug_scope_count_match = len(debug_scope) == 1
    debug_draw_count_match = (
        len([record for record in debug_records if record.name == DRAW_NAME]) == 1
    )
    stage12_scope_count_match = len(stage12_scope) == 0
    compositing_draw_count_match = (
        len([record for record in compositing_records if record.name == DRAW_NAME]) == 1
    )
    phase_order_match = (
        stage9_scope[0].event_id < debug_scope[0].event_id < compositing_scope[0].event_id
    )
    debug_output_name_match = debug_output is not None and debug_output["name"] == "SceneColor"
    debug_output_nonzero = debug_output is not None and debug_output["rgb_nonzero"]
    debug_output_grayscale_match = (
        debug_output is not None
        and debug_output["center_grayscale"] == expected_mode["expect_grayscale"]
    )
    final_present_nonzero = final_present is not None and final_present["rgb_nonzero"]

    overall = (
        debug_scope_count_match
        and debug_draw_count_match
        and stage12_scope_count_match
        and compositing_draw_count_match
        and phase_order_match
        and debug_output_name_match
        and debug_output_nonzero
        and debug_output_grayscale_match
        and final_present_nonzero
    )

    report.append("analysis_profile=vortexbasic_debug_visualization")
    report.append("capture_path={}".format(capture_path))
    report.append("report_path={}".format(report_path))
    report.append("expected_mode={}".format(expected_mode["label"]))
    report.append("debug_scope_name={}".format(expected_mode["scope_name"]))
    report.append("stage9_scope_count_match={}".format(str(len(stage9_scope) == 1).lower()))
    report.append("debug_scope_count_match={}".format(str(debug_scope_count_match).lower()))
    report.append("debug_draw_count_match={}".format(str(debug_draw_count_match).lower()))
    report.append("stage12_scope_count_match={}".format(str(stage12_scope_count_match).lower()))
    report.append(
        "compositing_draw_count_match={}".format(
            str(compositing_draw_count_match).lower()
        )
    )
    report.append("phase_order_match={}".format(str(phase_order_match).lower()))
    report.append("debug_output_name_match={}".format(str(debug_output_name_match).lower()))
    report.append("debug_output_nonzero={}".format(str(debug_output_nonzero).lower()))
    report.append(
        "debug_output_grayscale_match={}".format(
            str(debug_output_grayscale_match).lower()
        )
    )
    report.append("final_present_nonzero={}".format(str(final_present_nonzero).lower()))
    report.append("overall_verdict={}".format("pass" if overall else "fail"))
    report.blank()

    append_texture_sample(report, "debug_output", debug_output)
    append_texture_sample(report, "final_present", final_present)


def main():
    run_ui_script(REPORT_SUFFIX, build_report)


if __name__ == "__main__":
    main()
