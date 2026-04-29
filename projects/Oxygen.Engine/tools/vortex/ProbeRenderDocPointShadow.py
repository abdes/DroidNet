"""Probe Vortex point-shadow state from a RenderDoc capture."""

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


REPORT_SUFFIX = "_point_shadow_probe.txt"


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
        "name": resource_names.get(str(resource_id), str(resource_id)),
        "width": width,
        "height": height,
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


def append_descriptor_dump(
    report, controller, rd, resource_names, texture_descs, label, used_descriptor
):
    descriptor = used_descriptor.descriptor
    report.append(
        "{}={}|bind={}|array={}|type={}|byte_size={}|byte_offset={}".format(
            label,
            descriptor_name(resource_names, used_descriptor),
            safe_getattr(used_descriptor.access, "index", ""),
            safe_getattr(used_descriptor.access, "arrayElement", ""),
            safe_getattr(used_descriptor.access, "type", ""),
            int(safe_getattr(descriptor, "byteSize", 0) or 0),
            int(safe_getattr(descriptor, "byteOffset", 0) or 0),
        )
    )
    values_u32, values_f32 = dump_u32_f32(controller, descriptor, 384)
    if values_u32:
        report.append("{}_u32={}".format(label, values_u32))
        report.append(
            "{}_f32={}".format(label, ["{:.9f}".format(v) for v in values_f32])
        )
        return

    texture_sample = sample_descriptor(
        controller, rd, resource_names, texture_descs, descriptor, rd.CompType.Typeless
    )
    if texture_sample is not None:
        report.append(
            "{}_tex={}|slice={}|dims={}x{}|min={}|max={}|center={}".format(
                label,
                texture_sample["name"],
                texture_sample["slice"],
                texture_sample["width"],
                texture_sample["height"],
                texture_sample["min"],
                texture_sample["max"],
                texture_sample["center"],
            )
        )


def build_report(controller, report: ReportWriter, capture_path: Path, report_path: Path):
    rd = renderdoc_module()
    resource_names = resource_id_to_name(controller)
    texture_descs = texture_desc_map(controller)
    action_records = collect_action_records(controller)

    stage8_draws = [
        record
        for record in action_records
        if record.name == "ID3D12GraphicsCommandList::DrawInstanced()"
        and "Vortex.Stage8.ShadowDepths" in record.path
    ]
    point_draws = [
        record
        for record in action_records
        if record.name == "ID3D12GraphicsCommandList::DrawInstanced()"
        and "Vortex.Stage12.PointLight" in record.path
    ]

    report.append("analysis_profile=vortex_point_shadow_probe")
    report.append("capture_path={}".format(capture_path))
    report.append("stage8_draw_events={}".format(",".join(str(r.event_id) for r in stage8_draws)))
    report.append("stage12_point_draw_events={}".format(",".join(str(r.event_id) for r in point_draws)))

    for record in stage8_draws:
        controller.SetFrameEvent(record.event_id, True)
        state = controller.GetPipelineState()
        depth_sample = sample_descriptor(
            controller,
            rd,
            resource_names,
            texture_descs,
            state.GetDepthTarget(),
            rd.CompType.Depth,
        )
        report.append("stage8_draw_{}_num_indices={}".format(
            record.event_id, safe_getattr(record, "numIndices", "")))
        if depth_sample is None:
            report.append("stage8_draw_{}_depth_present=false".format(record.event_id))
            continue
        report.append("stage8_draw_{}_depth_present=true".format(record.event_id))
        report.append(
            "stage8_draw_{}_depth={}|slice={}|dims={}x{}|min={}|max={}|center={}".format(
                record.event_id,
                depth_sample["name"],
                depth_sample["slice"],
                depth_sample["width"],
                depth_sample["height"],
                depth_sample["min"],
                depth_sample["max"],
                depth_sample["center"],
            )
        )

    if not point_draws:
        report.flush()
        return

    point_draw = point_draws[-1]
    controller.SetFrameEvent(point_draw.event_id, True)
    state = controller.GetPipelineState()
    report.append("stage12_point_event={}".format(point_draw.event_id))

    pixel_readonly = state.GetReadOnlyResources(rd.ShaderStage.Pixel, True)
    report.append("stage12_point_pixel_readonly_count={}".format(len(pixel_readonly)))
    for index, used_descriptor in enumerate(pixel_readonly):
        append_descriptor_dump(
            report, controller, rd, resource_names, texture_descs,
            "stage12_point_pixel_ro_{}".format(index), used_descriptor)

    outputs = state.GetOutputTargets()
    report.append("stage12_point_output_count={}".format(len(outputs)))
    for index, descriptor in enumerate(outputs):
        output_sample = sample_descriptor(
            controller, rd, resource_names, texture_descs, descriptor, rd.CompType.Typeless)
        if output_sample is None:
            continue
        report.append(
            "stage12_point_output_{}={}|slice={}|dims={}x{}|min={}|max={}|center={}".format(
                index,
                output_sample["name"],
                output_sample["slice"],
                output_sample["width"],
                output_sample["height"],
                output_sample["min"],
                output_sample["max"],
                output_sample["center"],
            )
        )


def main():
    run_ui_script(REPORT_SUFFIX, build_report)


if __name__ == "__main__":
    main()
