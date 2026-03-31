"""Deep-dive auto-exposure inspection for RenderDoc UI captures.

This script inspects Scene and PiP auto-exposure/tone-map resources in the
currently loaded capture and reads the actual exposure-state buffers.
"""

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
        "Unable to locate Examples/RenderScene from the RenderDoc script path."
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
    resource_id_to_name,
    run_ui_script,
)


REPORT_SUFFIX = "_auto_exposure_deep_dive.txt"


def work_events_for(records, view_label, pass_name):
    matches = []
    view_prefix = "View: {} >".format(view_label)
    for record in records:
        if record.pass_name != pass_name:
            continue
        if view_prefix not in record.path:
            continue
        if not is_work_action(record.flags):
            continue
        matches.append(record)
    return matches


def make_subresource(rd):
    try:
        return rd.Subresource(0, 0, 0)
    except Exception:
        sub = rd.Subresource()
        for name, value in (("mip", 0), ("slice", 0), ("sample", 0)):
            if hasattr(sub, name):
                setattr(sub, name, value)
        return sub


def pixel_value_to_string(value):
    if value is None:
        return "<none>"

    for attr in ("floatValue", "value_f", "floatvalue"):
        if hasattr(value, attr):
            try:
                components = getattr(value, attr)
                return ",".join("{:.6f}".format(component) for component in components)
            except Exception:
                pass

    for attr in ("uintValue", "value_u", "intValue", "value_i"):
        if hasattr(value, attr):
            try:
                components = getattr(value, attr)
                return ",".join(str(component) for component in components)
            except Exception:
                pass

    return repr(value)


def resource_dimensions(resources, resource_id):
    resource_id_key = str(resource_id)
    for resource in resources:
        if str(getattr(resource, "resourceId", None)) == resource_id_key:
            return (
                getattr(resource, "width", 0) or 0,
                getattr(resource, "height", 0) or 0,
            )
    return 0, 0


def collect_texture_records_raw(controller):
    return controller.GetTextures() if hasattr(controller, "GetTextures") else []


def buffer_uints(controller, resource_id, byte_length):
    raw = controller.GetBufferData(resource_id, 0, byte_length)
    if raw is None:
        return None
    if len(raw) < byte_length:
        return None
    uint_count = byte_length // 4
    return struct.unpack("<{}I".format(uint_count), raw[:byte_length])


def read_exposure_buffer(controller, resource_id):
    raw = controller.GetBufferData(resource_id, 0, 16)
    if raw is None or len(raw) < 16:
        return None
    avg_lum, exposure, ev = struct.unpack("<3f", raw[:12])
    count = struct.unpack("<I", raw[12:16])[0]
    return {
        "avg_lum": avg_lum,
        "exposure": exposure,
        "ev": ev,
        "count": count,
    }


def named_resource_id(resources, needle):
    lowered = needle.lower()
    for resource in resources:
        name = getattr(resource, "name", "")
        if lowered in name.lower():
            return getattr(resource, "resourceId", None), name
    return None, None


def collect_exposure_state_resources(resources):
    exposure_resources = []
    for resource in resources:
        name = getattr(resource, "name", "")
        if "autoexposurepass_exposurestate_" not in name.lower():
            continue
        exposure_resources.append(
            {
                "id": getattr(resource, "resourceId", None),
                "name": name,
            }
        )

    exposure_resources.sort(key=lambda resource: resource["name"].lower())
    return exposure_resources


def format_exposure_buffer(exposure_data):
    if exposure_data is None:
        return "<unreadable>"
    return "avg_lum={:.6f} exposure={:.6f} ev={:.6f} count={}".format(
        exposure_data["avg_lum"],
        exposure_data["exposure"],
        exposure_data["ev"],
        exposure_data["count"],
    )


def format_histogram_stats(uints):
    if uints is None:
        return "<unreadable>"
    total = sum(uints)
    non_zero = sum(1 for value in uints if value != 0)
    peak = max(uints) if uints else 0
    return "sum={} non_zero_bins={} peak_bin_count={}".format(
        total, non_zero, peak
    )


def describe_used_resource(resource_names, used_descriptor):
    descriptor = used_descriptor.descriptor
    resource_id = descriptor.resource
    return {
        "id": resource_id,
        "name": resource_names.get(str(resource_id), str(resource_id)),
        "stage": used_descriptor.access.stage,
        "bind": used_descriptor.access.index,
        "array": used_descriptor.access.arrayElement,
        "type": used_descriptor.access.type,
    }


def collect_event_snapshot(controller, rd, resource_names, event_id):
    controller.SetFrameEvent(event_id, True)
    state = controller.GetPipelineState()

    pixel_ro = [
        describe_used_resource(resource_names, descriptor)
        for descriptor in state.GetReadOnlyResources(rd.ShaderStage.Pixel, True)
    ]
    compute_ro = [
        describe_used_resource(resource_names, descriptor)
        for descriptor in state.GetReadOnlyResources(rd.ShaderStage.Compute, True)
    ]
    compute_rw = [
        describe_used_resource(resource_names, descriptor)
        for descriptor in state.GetReadWriteResources(rd.ShaderStage.Compute, True)
    ]
    return {
        "pixel_ro": pixel_ro,
        "compute_ro": compute_ro,
        "compute_rw": compute_rw,
    }


def first_resource_with_name(resources, needle):
    for resource in resources:
        if needle.lower() in resource["name"].lower():
            return resource
    return None


def first_output_resource(resource_names, state):
    try:
        outputs = state.GetOutputTargets()
    except Exception:
        return None

    for descriptor in outputs:
        resource_id = getattr(descriptor, "resource", None)
        if resource_id is None:
            continue
        return {
            "id": resource_id,
            "name": resource_names.get(str(resource_id), str(resource_id)),
        }
    return None


def write_resource_list(report, header, resources):
    report.append(header)
    if not resources:
        report.append("  <none>")
        return
    for resource in resources:
        report.append(
            "  {} | bind={} array={} stage={} type={} id={}".format(
                resource["name"],
                resource["bind"],
                resource["array"],
                resource["stage"],
                resource["type"],
                resource["id"],
            )
        )


def inspect_view(
    controller, rd, report, records, resources, resource_names, view_label
):
    textures = collect_texture_records_raw(controller)
    tone_map_events = work_events_for(records, view_label, "ToneMapPass")
    auto_events = work_events_for(records, view_label, "AutoExposurePass")
    tone_map_event = tone_map_events[0] if tone_map_events else None
    auto_event = auto_events[-1] if auto_events else None

    report.append("[{}]".format(view_label))
    report.append(
        "tone_map_event={}".format(tone_map_event.event_id if tone_map_event else "<none>")
    )
    report.append(
        "auto_exposure_event={}".format(auto_event.event_id if auto_event else "<none>")
    )
    report.append(
        "tone_map_work_events={}".format(
            ",".join(str(record.event_id) for record in tone_map_events) or "<none>"
        )
    )
    report.append(
        "auto_exposure_work_events={}".format(
            ",".join(str(record.event_id) for record in auto_events) or "<none>"
        )
    )

    exposure_state_resources = collect_exposure_state_resources(resources)
    histogram_id, histogram_name = named_resource_id(
        resources, "AutoExposure_Histogram"
    )

    if tone_map_event is not None:
        controller.SetFrameEvent(tone_map_event.event_id, True)
        state = controller.GetPipelineState()
        snapshot = {
            "pixel_ro": [
                describe_used_resource(resource_names, descriptor)
                for descriptor in state.GetReadOnlyResources(rd.ShaderStage.Pixel, True)
            ]
        }
        write_resource_list(report, "tone_map_pixel_ro", snapshot["pixel_ro"])
        exposure_resource = first_resource_with_name(
            snapshot["pixel_ro"], "AutoExposurePass_ExposureState"
        )
        source_resource = first_resource_with_name(
            snapshot["pixel_ro"], "Forward_HDR_Intermediate"
        )
        output_resource = first_output_resource(resource_names, state)

        if exposure_resource is not None:
            exposure_data = read_exposure_buffer(controller, exposure_resource["id"])
            if exposure_data is not None:
                report.append("tone_map_exposure_buffer={} {}".format(
                    exposure_resource["name"], format_exposure_buffer(exposure_data)
                ))
            else:
                report.append(
                    "tone_map_exposure_buffer={} unreadable".format(
                        exposure_resource["name"]
                    )
                )
        else:
            report.append("tone_map_exposure_buffer=<none>")

        if source_resource is not None:
            report.append(
                "tone_map_source_texture={} id={}".format(
                    source_resource["name"],
                    source_resource["id"],
                )
            )
            source_width, source_height = resource_dimensions(
                textures, source_resource["id"]
            )
            report.append(
                "tone_map_source_dimensions={}x{}".format(
                    source_width, source_height
                )
            )
            if source_width > 0 and source_height > 0:
                try:
                    min_value, max_value = controller.GetMinMax(
                        source_resource["id"],
                        make_subresource(rd),
                        rd.CompType.Typeless,
                    )
                    report.append(
                        "tone_map_source_min={} max={}".format(
                            pixel_value_to_string(min_value),
                            pixel_value_to_string(max_value),
                        )
                    )
                except Exception as exc:
                    report.append("tone_map_source_minmax_error={}".format(exc))

                try:
                    picked = controller.PickPixel(
                        source_resource["id"],
                        source_width // 2,
                        source_height // 2,
                        make_subresource(rd),
                        rd.CompType.Typeless,
                    )
                    report.append(
                        "tone_map_source_center_pixel={} at={},{}".format(
                            pixel_value_to_string(picked),
                            source_width // 2,
                            source_height // 2,
                        )
                    )
                except Exception as exc:
                    report.append("tone_map_source_pick_error={}".format(exc))

                try:
                    histogram = controller.GetHistogram(
                        source_resource["id"],
                        make_subresource(rd),
                        rd.CompType.Typeless,
                        0.0,
                        32.0,
                        (True, True, True, False),
                    )
                    histogram_total = sum(histogram) if histogram is not None else 0
                    histogram_peak = max(histogram) if histogram else 0
                    histogram_non_zero = (
                        sum(1 for value in histogram if value != 0)
                        if histogram is not None
                        else 0
                    )
                    report.append(
                        "tone_map_source_histogram[0,32]_total={} non_zero_bins={} peak_bin={}".format(
                            histogram_total,
                            histogram_non_zero,
                            histogram_peak,
                        )
                    )
                except Exception as exc:
                    report.append("tone_map_source_histogram_error={}".format(exc))
        else:
            report.append("tone_map_source_texture=<none>")

        if output_resource is not None:
            report.append(
                "tone_map_output_texture={} id={}".format(
                    output_resource["name"],
                    output_resource["id"],
                )
            )
            output_width, output_height = resource_dimensions(
                textures, output_resource["id"]
            )
            report.append(
                "tone_map_output_dimensions={}x{}".format(
                    output_width, output_height
                )
            )
            if output_width > 0 and output_height > 0:
                try:
                    min_value, max_value = controller.GetMinMax(
                        output_resource["id"],
                        make_subresource(rd),
                        rd.CompType.Typeless,
                    )
                    report.append(
                        "tone_map_output_min={} max={}".format(
                            pixel_value_to_string(min_value),
                            pixel_value_to_string(max_value),
                        )
                    )
                except Exception as exc:
                    report.append("tone_map_output_minmax_error={}".format(exc))

                try:
                    picked = controller.PickPixel(
                        output_resource["id"],
                        output_width // 2,
                        output_height // 2,
                        make_subresource(rd),
                        rd.CompType.Typeless,
                    )
                    report.append(
                        "tone_map_output_center_pixel={} at={},{}".format(
                            pixel_value_to_string(picked),
                            output_width // 2,
                            output_height // 2,
                        )
                    )
                except Exception as exc:
                    report.append("tone_map_output_pick_error={}".format(exc))

                try:
                    histogram = controller.GetHistogram(
                        output_resource["id"],
                        make_subresource(rd),
                        rd.CompType.Typeless,
                        0.0,
                        1.0,
                        (True, True, True, False),
                    )
                    histogram_total = sum(histogram) if histogram is not None else 0
                    histogram_peak = max(histogram) if histogram else 0
                    histogram_non_zero = (
                        sum(1 for value in histogram if value != 0)
                        if histogram is not None
                        else 0
                    )
                    report.append(
                        "tone_map_output_histogram[0,1]_total={} non_zero_bins={} peak_bin={}".format(
                            histogram_total,
                            histogram_non_zero,
                            histogram_peak,
                        )
                    )
                except Exception as exc:
                    report.append("tone_map_output_histogram_error={}".format(exc))
        else:
            report.append("tone_map_output_texture=<none>")

    if auto_event is not None:
        snapshot = collect_event_snapshot(
            controller, rd, resource_names, auto_event.event_id
        )
        write_resource_list(report, "auto_exposure_compute_ro", snapshot["compute_ro"])
        write_resource_list(report, "auto_exposure_compute_rw", snapshot["compute_rw"])
        exposure_resource = first_resource_with_name(
            snapshot["compute_rw"], "AutoExposurePass_ExposureState"
        )
        if exposure_resource is not None:
            exposure_data = read_exposure_buffer(controller, exposure_resource["id"])
            if exposure_data is not None:
                report.append("auto_exposure_buffer={} {}".format(
                    exposure_resource["name"], format_exposure_buffer(exposure_data)
                ))
            else:
                report.append(
                    "auto_exposure_buffer={} unreadable".format(
                        exposure_resource["name"]
                    )
                )
        else:
            report.append("auto_exposure_buffer=<none>")

    if auto_events:
        report.append("auto_exposure_event_buffer_timeline")
        for event_record in auto_events:
            controller.SetFrameEvent(event_record.event_id, True)
            histogram_uints = (
                buffer_uints(controller, histogram_id, 256 * 4)
                if histogram_id is not None
                else None
            )
            timeline_parts = []
            for exposure_resource in exposure_state_resources:
                exposure_data = read_exposure_buffer(
                    controller, exposure_resource["id"]
                )
                timeline_parts.append(
                    "{}={}".format(
                        exposure_resource["name"],
                        format_exposure_buffer(exposure_data),
                    )
                )
            timeline_parts.append(
                "{}={}".format(
                    histogram_name or "Histogram",
                    format_histogram_stats(histogram_uints),
                )
            )
            report.append(
                "  event {} {} | {}".format(
                    event_record.event_id,
                    event_record.path,
                    " | ".join(timeline_parts),
                )
            )

    report.blank()


def build_report(controller, report: ReportWriter, capture_path, report_path):
    rd = sys.modules.get("renderdoc") or sys.modules.get("_renderdoc")
    if rd is None:
        raise RuntimeError("RenderDoc python module is unavailable")

    records = [
        record
        for record in collect_action_records(controller)
        if is_work_action(record.flags)
    ]
    resources = collect_resource_records_raw(controller)
    resource_names = resource_id_to_name(controller)

    report.append("analysis_profile=auto_exposure_deep_dive")
    report.append("capture_path={}".format(capture_path))
    report.append("report_path={}".format(report_path))
    report.blank()

    inspect_view(controller, rd, report, records, resources, resource_names, "Scene")
    inspect_view(controller, rd, report, records, resources, resource_names, "PiP")
    report.flush()


RUN_EXIT_CODE = run_ui_script(REPORT_SUFFIX, build_report)
