"""RenderDoc UI proof script for DP-6 ShaderPass depth-equal validation.

Run only inside RenderDoc UI:

PowerShell:
    $env:OXYGEN_RENDERDOC_REPORT_PATH = 'H:/path/shader_pass_depth_equal.txt'
    & 'C:/Program Files/RenderDoc/qrenderdoc.exe' --ui-python `
        'H:/projects/DroidNet/projects/Oxygen.Engine/Examples/RenderScene/AnalyzeRenderDocShaderPassDepthEqual.py' `
        'H:/path/capture.rdc'

Environment variables
---------------------
- `OXYGEN_RENDERDOC_REPORT_PATH`: optional explicit report path.
- `OXYGEN_RENDERDOC_CAPTURE_PATH`: optional explicit capture path.
- `OXYGEN_RENDERDOC_EVENT_ID`: optional explicit ShaderPass work event id.
- `OXYGEN_RENDERDOC_PICK_X`: optional x coordinate for depth/output pixel picks.
- `OXYGEN_RENDERDOC_PICK_Y`: optional y coordinate for depth/output pixel picks.
"""

import builtins
import inspect
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

    capture_context = globals().get("pyrenderdoc")
    get_capture_filename = getattr(capture_context, "GetCaptureFilename", None)
    if callable(get_capture_filename):
        try:
            capture_path = Path(get_capture_filename()).resolve()
            candidates.append(capture_path.parent)
            candidates.extend(capture_path.parents)
        except Exception:
            pass

    search_root = Path.cwd().resolve()
    candidates.append(search_root)
    candidates.extend(search_root.parents)

    seen = set()
    for candidate_root in candidates:
        normalized = str(candidate_root).lower()
        if normalized in seen:
            continue
        seen.add(normalized)

        if (candidate_root / "renderdoc_ui_analysis.py").exists():
            return candidate_root

        candidate = candidate_root / "Examples" / "RenderScene"
        if (candidate / "renderdoc_ui_analysis.py").exists():
            return candidate

    raise RuntimeError(
        "Unable to locate Examples/RenderScene from the RenderDoc script path. "
        "Launch qrenderdoc with the repository script path."
    )


if "pyrenderdoc" in globals():
    builtins.pyrenderdoc = pyrenderdoc


SCRIPT_DIR = resolve_script_dir()
if str(SCRIPT_DIR) not in sys.path:
    sys.path.insert(0, str(SCRIPT_DIR))

from renderdoc_ui_analysis import (  # noqa: E402
    EVENT_ID_ENV,
    ReportWriter,
    collect_action_records,
    describe_descriptor,
    find_event_record,
    find_pass_work_records,
    format_flags,
    parse_optional_int_env,
    renderdoc_module,
    resource_id_to_name,
    run_ui_script,
    safe_getattr,
)


REPORT_SUFFIX = "_shader_pass_depth_equal_report.txt"
PICK_X_ENV = "OXYGEN_RENDERDOC_PICK_X"
PICK_Y_ENV = "OXYGEN_RENDERDOC_PICK_Y"
SCAN_GRID_X_ENV = "OXYGEN_RENDERDOC_SCAN_GRID_X"
SCAN_GRID_Y_ENV = "OXYGEN_RENDERDOC_SCAN_GRID_Y"
DEPTH_WORK_PASS_NAME = "DepthPrePass.SceneDepthWork"
SHADER_PASS_NAME = "ShaderPass"

_SUMMARY_KEYS = (
    "depth",
    "stencil",
    "compare",
    "func",
    "test",
    "passed",
    "event",
    "frag",
    "primitive",
    "pre",
    "post",
    "shader",
    "write",
    "read",
    "bound",
    "target",
    "output",
    "resource",
    "state",
    "name",
)


def object_summary(value):
    if value is None:
        return "None"

    primitive_types = (str, int, float, bool)
    if isinstance(value, primitive_types):
        return repr(value)

    if not isinstance(value, (str, bytes, bytearray)):
        try:
            length = len(value)
        except Exception:
            length = None
        if length is not None and length > 8:
            return "{}(len={})".format(type(value).__name__, length)

    try:
        return "{}({})".format(type(value).__name__, str(value))
    except Exception:
        return "<{}>".format(type(value).__name__)


def try_signature(callable_value):
    try:
        return str(inspect.signature(callable_value))
    except Exception:
        return "<signature-unavailable>"


def try_zero_arg_call(callable_value):
    try:
        signature = inspect.signature(callable_value)
    except Exception:
        signature = None

    if signature is not None:
        for parameter in signature.parameters.values():
            if parameter.kind in (
                inspect.Parameter.POSITIONAL_ONLY,
                inspect.Parameter.POSITIONAL_OR_KEYWORD,
            ) and parameter.default is inspect._empty:
                return False, "requires-args"

    try:
        return True, callable_value()
    except TypeError as exc:
        return False, "type-error: {}".format(exc)
    except Exception as exc:
        return False, "call-error: {}".format(exc)


def interesting_names(value):
    names = []
    for name in dir(value):
        lowered = name.lower()
        if any(token in lowered for token in _SUMMARY_KEYS):
            names.append(name)
    return sorted(set(names))


def summarize_object(report, label, value, max_depth=1, depth=0, seen=None):
    if seen is None:
        seen = set()

    if value is None:
        report.append("{}=<none>".format(label))
        return

    object_id = id(value)
    if object_id in seen:
        report.append("{}=<already-visited {}>".format(label, type(value).__name__))
        return
    seen.add(object_id)

    report.append("{}_type={}".format(label, type(value).__name__))
    report.append("{}_summary={}".format(label, object_summary(value)))

    for name in interesting_names(value):
        member = safe_getattr(value, name)
        if callable(member):
            report.append(
                "{}.{} callable signature={}".format(
                    label, name, try_signature(member)
                )
            )
            called, result = try_zero_arg_call(member)
            if called:
                report.append(
                    "{}.{}() -> {}".format(label, name, object_summary(result))
                )
                if depth < max_depth and not isinstance(
                    result, (str, int, float, bool, bytes, bytearray, tuple, list)
                ):
                    summarize_object(
                        report,
                        "{}.{}()".format(label, name),
                        result,
                        max_depth=max_depth,
                        depth=depth + 1,
                        seen=seen,
                    )
            else:
                report.append("{}.{}() skipped={}".format(label, name, result))
        else:
            report.append(
                "{}.{}={}".format(label, name, object_summary(member))
            )
            if depth < max_depth and member is not None and not isinstance(
                member, (str, int, float, bool, bytes, bytearray, tuple, list)
            ):
                summarize_object(
                    report,
                    "{}.{}".format(label, name),
                    member,
                    max_depth=max_depth,
                    depth=depth + 1,
                    seen=seen,
                )


def choose_event(action_records):
    explicit_event_id = parse_optional_int_env(EVENT_ID_ENV)
    if explicit_event_id is not None:
        record = find_event_record(action_records, explicit_event_id)
        if record is None:
            raise RuntimeError(
                "Requested ShaderPass event {} was not found".format(
                    explicit_event_id
                )
            )
        return record

    shader_events = find_pass_work_records(action_records, SHADER_PASS_NAME)
    if not shader_events:
        raise RuntimeError("No ShaderPass work events found in capture")
    return shader_events[0]


def action_record_lookup(action_records):
    return {record.event_id: record for record in action_records}


def first_draw_for_pass(action_records, pass_name):
    for record in find_pass_work_records(action_records, pass_name):
        if "Draw" in format_flags(record.flags):
            return record
    return None


def output_dimensions(controller, output_descriptor):
    texture = safe_getattr(output_descriptor, "resource")
    if texture is None:
        return None, None

    for resource in controller.GetTextures():
        if safe_getattr(resource, "resourceId") == texture:
            return (
                safe_getattr(resource, "width", 0) or 0,
                safe_getattr(resource, "height", 0) or 0,
            )

    return None, None


def make_subresource(rd):
    sub = rd.Subresource()
    sub.mip = 0
    sub.slice = 0
    sub.sample = 0
    return sub


def pick_coordinates(width, height):
    configured_x = parse_optional_int_env(PICK_X_ENV)
    configured_y = parse_optional_int_env(PICK_Y_ENV)
    if configured_x is not None and configured_y is not None:
        return configured_x, configured_y
    if not width or not height:
        return None, None
    return width // 2, height // 2


def resource_key(resource_id):
    if resource_id is None:
        return None
    return str(resource_id)


def passed_status(modification):
    passed_method = getattr(modification, "Passed", None)
    if not callable(passed_method):
        return None
    called, result = try_zero_arg_call(passed_method)
    return result if called else None


def append_modification_details(
    report, label, modification, action_lookup
):
    event_id = safe_getattr(modification, "eventId")
    action = action_lookup.get(event_id)
    report.append(
        "{} event_id={} passed={} path={}".format(
            label,
            event_id,
            passed_status(modification),
            action.path if action is not None else "<unknown>",
        )
    )
    report.append(
        "{} depthTestFailed={} stencilTestFailed={} shaderDiscarded={} unboundPS={}".format(
            label,
            safe_getattr(modification, "depthTestFailed"),
            safe_getattr(modification, "stencilTestFailed"),
            safe_getattr(modification, "shaderDiscarded"),
            safe_getattr(modification, "unboundPS"),
        )
    )

    pre_mod = safe_getattr(modification, "preMod")
    post_mod = safe_getattr(modification, "postMod")
    shader_out = safe_getattr(modification, "shaderOut")
    report.append(
        "{} pre_depth={} shader_depth={} post_depth={}".format(
            label,
            safe_getattr(pre_mod, "depth"),
            safe_getattr(shader_out, "depth"),
            safe_getattr(post_mod, "depth"),
        )
    )


def summarize_matching_resource_states(
    report, label, resource_states, target_resource_id
):
    target_key = resource_key(target_resource_id)
    if resource_states is None or target_key is None:
        report.append("{}=<skipped>".format(label))
        return

    matches = []
    for index, entry in enumerate(resource_states):
        candidate_id = safe_getattr(
            entry,
            "resourceId",
            safe_getattr(entry, "id", safe_getattr(entry, "resource")),
        )
        if resource_key(candidate_id) == target_key:
            matches.append((index, entry))

    report.append("{}_match_count={}".format(label, len(matches)))
    for index, entry in matches[:4]:
        summarize_object(report, "{}[{}]".format(label, index), entry, max_depth=1)
        states = safe_getattr(entry, "states")
        if states:
            for state_index, state in enumerate(states[:4]):
                summarize_object(
                    report,
                    "{}[{}].states[{}]".format(label, index, state_index),
                    state,
                    max_depth=1,
                )


def emit_d3d12_state_details(
    report, label, d3d12_state, resource_names, depth_target, primary_output
):
    if d3d12_state is None:
        report.append("{}=<none>".format(label))
        return

    output_merger = safe_getattr(d3d12_state, "outputMerger")
    summarize_object(report, "{}.outputMerger".format(label), output_merger, max_depth=1)

    depth_stencil_state = safe_getattr(output_merger, "depthStencilState")
    summarize_object(
        report,
        "{}.outputMerger.depthStencilState".format(label),
        depth_stencil_state,
        max_depth=1,
    )

    om_depth_target = safe_getattr(output_merger, "depthTarget")
    report.append(
        "{}.outputMerger.depthTarget={}".format(
            label,
            describe_descriptor(resource_names, om_depth_target) or "<none>",
        )
    )

    render_targets = safe_getattr(output_merger, "renderTargets")
    if render_targets:
        for index, descriptor in enumerate(render_targets):
            rendered = describe_descriptor(resource_names, descriptor)
            if rendered is not None:
                report.append(
                    "{}.outputMerger.renderTargets[{}]={}".format(
                        label, index, rendered
                    )
                )

    resource_states = safe_getattr(d3d12_state, "resourceStates")
    summarize_matching_resource_states(
        report,
        "{}.resourceStates.depth".format(label),
        resource_states,
        safe_getattr(depth_target, "resource"),
    )
    summarize_matching_resource_states(
        report,
        "{}.resourceStates.output".format(label),
        resource_states,
        safe_getattr(primary_output, "resource") if primary_output is not None else None,
    )


def safe_pixel_value(report, controller, rd, label, resource_id, x, y):
    if resource_id is None or x is None or y is None:
        report.append("{}=<skipped>".format(label))
        return

    try:
        picked = controller.PickPixel(
            resource_id,
            x,
            y,
            make_subresource(rd),
            rd.CompType.Typeless,
        )
        report.append("{}={}".format(label, object_summary(picked)))
    except Exception as exc:
        report.append("{}_error={}".format(label, exc))


def safe_pixel_history(
    report, controller, rd, label, resource_id, x, y, event_id, action_lookup
):
    method = getattr(controller, "PixelHistory", None)
    report.append(
        "{}_callable={}".format(label, callable(method))
    )
    if not callable(method):
        return

    report.append("{}_signature={}".format(label, try_signature(method)))
    doc = getattr(method, "__doc__", None)
    if doc:
        report.append(
            "{}_doc={}".format(
                label,
                " ".join(line.strip() for line in doc.strip().splitlines()),
            )
        )

    if resource_id is None or x is None or y is None:
        report.append("{}=<skipped-no-coordinates>".format(label))
        return

    subresource = make_subresource(rd)
    history_attempts = [
        ("resource_xy_sub", (resource_id, x, y, subresource)),
        ("resource_xy_sub_type", (resource_id, x, y, subresource, rd.CompType.Typeless)),
        ("resource_event_xy_sub", (resource_id, event_id, x, y, subresource)),
        ("resource_event_xy_sub_type", (resource_id, event_id, x, y, subresource, rd.CompType.Typeless)),
    ]

    for attempt_name, args in history_attempts:
        try:
            history = method(*args)
            report.append(
                "{}_attempt={} result_type={}".format(
                    label, attempt_name, type(history).__name__
                )
            )
            length = len(history) if history is not None else 0
            report.append(
                "{}_attempt={}: count={}".format(label, attempt_name, length)
            )
            if history:
                for index, modification in enumerate(history[:8]):
                    report.append(
                        "{}_attempt={}: [{}] {}".format(
                            label, attempt_name, index, object_summary(modification)
                        )
                    )
                    append_modification_details(
                        report,
                        "{}_mod{}".format(label, index),
                        modification,
                        action_lookup,
                    )
                    summarize_object(
                        report,
                        "{}_mod{}".format(label, index),
                        modification,
                        max_depth=1,
                    )
            return
        except Exception as exc:
            report.append(
                "{}_attempt={}_error={}".format(label, attempt_name, exc)
            )


def find_shader_depth_failure(
    report, controller, rd, resource_id, width, height, action_lookup
):
    if resource_id is None or not width or not height:
        report.append("shader_depth_failure_scan=<skipped>")
        return

    grid_x = parse_optional_int_env(SCAN_GRID_X_ENV) or 12
    grid_y = parse_optional_int_env(SCAN_GRID_Y_ENV) or 8
    step_x = max(1, width // grid_x)
    step_y = max(1, height // grid_y)
    origin_x = min(width - 1, max(0, step_x // 2))
    origin_y = min(height - 1, max(0, step_y // 2))

    report.append(
        "shader_depth_failure_scan_grid={}x{} step={}x{}".format(
            grid_x, grid_y, step_x, step_y
        )
    )

    for y in range(origin_y, height, step_y):
        for x in range(origin_x, width, step_x):
            try:
                history = controller.PixelHistory(
                    resource_id,
                    x,
                    y,
                    make_subresource(rd),
                    rd.CompType.Typeless,
                )
            except Exception as exc:
                report.append(
                    "shader_depth_failure_scan_error x={} y={} error={}".format(
                        x, y, exc
                    )
                )
                return

            for modification in history or []:
                action = action_lookup.get(safe_getattr(modification, "eventId"))
                if action is None or action.pass_name != SHADER_PASS_NAME:
                    continue
                if not safe_getattr(modification, "depthTestFailed"):
                    continue

                report.append(
                    "shader_depth_failure_found=True x={} y={} event_id={} path={}".format(
                        x,
                        y,
                        safe_getattr(modification, "eventId"),
                        action.path,
                    )
                )
                append_modification_details(
                    report,
                    "shader_depth_failure_mod",
                    modification,
                    action_lookup,
                )
                summarize_object(
                    report,
                    "shader_depth_failure_mod",
                    modification,
                    max_depth=1,
                )
                return

    report.append("shader_depth_failure_found=False")


def build_report(controller, report: ReportWriter, capture_path: Path, report_path: Path):
    rd = renderdoc_module()
    action_records = collect_action_records(controller)
    action_lookup = action_record_lookup(action_records)
    resource_names = resource_id_to_name(controller)

    shader_event = choose_event(action_records)
    depth_event = first_draw_for_pass(action_records, DEPTH_WORK_PASS_NAME)

    report.append("analysis_profile=shader_pass_depth_equal")
    report.append("capture_path={}".format(capture_path))
    report.append("report_path={}".format(report_path))
    report.append(
        "shader_event={} flags={} path={}".format(
            shader_event.event_id,
            format_flags(shader_event.flags),
            shader_event.path,
        )
    )
    if depth_event is not None:
        report.append(
            "depth_prepass_event={} flags={} path={}".format(
                depth_event.event_id,
                format_flags(depth_event.flags),
                depth_event.path,
            )
        )
    else:
        report.append("depth_prepass_event=<none>")
    report.blank()

    if depth_event is not None:
        controller.SetFrameEvent(depth_event.event_id, True)
        depth_state = controller.GetPipelineState()
        depth_target = depth_state.GetDepthTarget()
        report.append(
            "depth_prepass_depth_target={}".format(
                describe_descriptor(resource_names, depth_target) or "<none>"
            )
        )
        summarize_object(report, "depth_prepass_state", depth_state, max_depth=1)
        report.blank()

    controller.SetFrameEvent(shader_event.event_id, True)
    shader_state = controller.GetPipelineState()
    shader_depth_target = shader_state.GetDepthTarget()
    shader_outputs = shader_state.GetOutputTargets()
    primary_output = shader_outputs[0] if shader_outputs else None

    report.append(
        "shader_depth_target={}".format(
            describe_descriptor(resource_names, shader_depth_target) or "<none>"
        )
    )
    for index, output in enumerate(shader_outputs):
        descriptor = describe_descriptor(resource_names, output)
        if descriptor is not None:
            report.append("shader_output[{}]={}".format(index, descriptor))

    summarize_object(report, "shader_state", shader_state, max_depth=1)

    pipeline_state_getters = [
        name
        for name in dir(controller)
        if "pipeline" in name.lower() or "d3d12" in name.lower()
    ]
    report.blank()
    report.append("controller_pipeline_state_methods={}".format(",".join(sorted(pipeline_state_getters)) or "<none>"))
    for name in sorted(pipeline_state_getters):
        member = safe_getattr(controller, name)
        if callable(member):
            report.append(
                "controller.{} signature={}".format(name, try_signature(member))
            )
            called, result = try_zero_arg_call(member)
            if called:
                report.append(
                    "controller.{}() -> {}".format(name, object_summary(result))
                )
                if name == "GetD3D12PipelineState" and result is not None:
                    emit_d3d12_state_details(
                        report,
                        "controller.GetD3D12PipelineState()",
                        result,
                        resource_names,
                        shader_depth_target,
                        primary_output,
                    )
                elif result is not None:
                    summarize_object(
                        report,
                        "controller.{}()".format(name),
                        result,
                        max_depth=1,
                    )
            else:
                report.append("controller.{}() skipped={}".format(name, result))

    width, height = output_dimensions(controller, primary_output)
    pick_x, pick_y = pick_coordinates(width, height)
    report.blank()
    report.append("pick_width={}".format(width))
    report.append("pick_height={}".format(height))
    report.append("pick_x={}".format(pick_x))
    report.append("pick_y={}".format(pick_y))

    if primary_output is not None:
        safe_pixel_value(
            report,
            controller,
            rd,
            "shader_output_pick",
            safe_getattr(primary_output, "resource"),
            pick_x,
            pick_y,
        )
        safe_pixel_history(
            report,
            controller,
            rd,
            "shader_output_history",
            safe_getattr(primary_output, "resource"),
            pick_x,
            pick_y,
            shader_event.event_id,
            action_lookup,
        )

    safe_pixel_value(
        report,
        controller,
        rd,
        "shader_depth_pick",
        safe_getattr(shader_depth_target, "resource"),
        pick_x,
        pick_y,
    )
    safe_pixel_history(
        report,
        controller,
        rd,
        "shader_depth_history",
        safe_getattr(shader_depth_target, "resource"),
        pick_x,
        pick_y,
        shader_event.event_id,
        action_lookup,
    )
    report.blank()
    find_shader_depth_failure(
        report,
        controller,
        rd,
        safe_getattr(primary_output, "resource") if primary_output is not None else None,
        width,
        height,
        action_lookup,
    )

    report.flush()


RUN_EXIT_CODE = run_ui_script(REPORT_SUFFIX, build_report)
