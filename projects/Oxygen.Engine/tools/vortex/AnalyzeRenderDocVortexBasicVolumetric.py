"""RenderDoc UI analyzer for the focused VortexBasic volumetric-fog proof."""

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
    collect_resource_records_raw,
    renderdoc_module,
    resource_id_to_name,
    run_ui_script,
    safe_getattr,
)


REPORT_SUFFIX = "_vortexbasic_volumetric_report.txt"
DISPATCH_NAME = "ID3D12GraphicsCommandList::Dispatch()"
DRAW_NAME = "ID3D12GraphicsCommandList::DrawInstanced()"
VOLUMETRIC_SCOPE = "Vortex.Stage14.VolumetricFog"
FOG_SCOPE = "Vortex.Stage15.Fog"
INTEGRATED_LIGHT_SCATTERING_TOKEN = "vortex.environment.integratedlightscattering"
ENVIRONMENT_STATIC_DATA_BYTE_SIZE = 672
ENVIRONMENT_STATIC_DATA_U32_COUNT = ENVIRONMENT_STATIC_DATA_BYTE_SIZE // 4
VOLUMETRIC_FOG_U32_OFFSET = 128 // 4
VOLUMETRIC_FOG_INTEGRATED_LIGHT_SCATTERING_SRV_U32 = (
    VOLUMETRIC_FOG_U32_OFFSET + 12
)
VOLUMETRIC_FOG_FLAGS_U32 = VOLUMETRIC_FOG_U32_OFFSET + 13
VOLUMETRIC_FOG_GRID_WIDTH_U32 = VOLUMETRIC_FOG_U32_OFFSET + 14
VOLUMETRIC_FOG_GRID_HEIGHT_U32 = VOLUMETRIC_FOG_U32_OFFSET + 15
VOLUMETRIC_FOG_GRID_DEPTH_U32 = VOLUMETRIC_FOG_U32_OFFSET + 16
VOLUMETRIC_FOG_GRID_Z_PARAMS_F32 = VOLUMETRIC_FOG_U32_OFFSET + 20
GPU_VOLUMETRIC_FOG_FLAG_ENABLED = 1 << 0
GPU_VOLUMETRIC_FOG_FLAG_INTEGRATED_SCATTERING_VALID = 1 << 1
INVALID_BINDLESS_INDEX = 0xFFFFFFFF


def records_with_name(action_records, name):
    return [record for record in action_records if record.name == name]


def records_under_prefix(action_records, prefix):
    prefix_with_sep = prefix + " > "
    return [record for record in action_records if record.path.startswith(prefix_with_sep)]


def float4_values(pixel_value):
    return [float(value) for value in list(pixel_value.floatValue)[:4]]


def format_values(values):
    return ",".join("{:.9g}".format(value) for value in values)


def texture_desc_map(controller):
    descriptions = {}
    for desc in controller.GetTextures():
        resource_id = safe_getattr(desc, "resourceId")
        if resource_id is None:
            continue
        descriptions[str(resource_id)] = desc
    return descriptions


def sample_integrated_volume(controller, rd, texture_descs, resource_id):
    texture_desc = texture_descs.get(str(resource_id))
    if texture_desc is None:
        return {
            "sampled": False,
            "width": 0,
            "height": 0,
            "depth": 0,
            "slices": [],
            "min": [0.0, 0.0, 0.0, 0.0],
            "max": [0.0, 0.0, 0.0, 0.0],
            "rgb_nonzero": False,
            "alpha_valid": False,
        }

    width = int(safe_getattr(texture_desc, "width", 1) or 1)
    height = int(safe_getattr(texture_desc, "height", 1) or 1)
    depth = int(
        safe_getattr(texture_desc, "depth", 0)
        or safe_getattr(texture_desc, "arraysize", 1)
        or 1
    )
    width = max(width, 1)
    height = max(height, 1)
    depth = max(depth, 1)
    candidate_slices = sorted(set([0, depth // 2, depth - 1]))
    all_min = [float("inf")] * 4
    all_max = [float("-inf")] * 4
    slice_reports = []
    for slice_index in candidate_slices:
        sub = rd.Subresource()
        sub.mip = 0
        sub.slice = int(slice_index)
        sub.sample = 0
        min_value, max_value = controller.GetMinMax(
            resource_id, sub, rd.CompType.Typeless
        )
        min_values = float4_values(min_value)
        max_values = float4_values(max_value)
        center = controller.PickPixel(
            resource_id,
            max(0, min(width - 1, width // 2)),
            max(0, min(height - 1, height // 2)),
            sub,
            rd.CompType.Typeless,
        )
        center_values = float4_values(center)
        all_min = [min(all_min[i], min_values[i]) for i in range(4)]
        all_max = [max(all_max[i], max_values[i]) for i in range(4)]
        slice_reports.append(
            {
                "slice": slice_index,
                "min": min_values,
                "max": max_values,
                "center": center_values,
            }
        )

    rgb_nonzero = max(abs(value) for value in all_max[:3]) > 1.0e-6 or max(
        abs(all_max[index] - all_min[index]) for index in range(3)
    ) > 1.0e-6
    alpha_valid = all_min[3] >= -1.0e-5 and all_max[3] <= 1.0 + 1.0e-5
    return {
        "sampled": len(slice_reports) > 0,
        "width": width,
        "height": height,
        "depth": depth,
        "slices": slice_reports,
        "min": all_min,
        "max": all_max,
        "rgb_nonzero": rgb_nonzero,
        "alpha_valid": alpha_valid,
    }


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


def bound_named_resources(controller, rd, resource_names, event_id, shader_stage, read_write, *tokens):
    controller.SetFrameEvent(event_id, True)
    state = controller.GetPipelineState()
    if read_write:
        descriptors = state.GetReadWriteResources(shader_stage, True)
    else:
        descriptors = state.GetReadOnlyResources(shader_stage, True)

    matches = []
    for used_descriptor in descriptors:
        descriptor = used_descriptor.descriptor
        resource_id = safe_getattr(descriptor, "resource")
        if resource_id is None:
            continue
        name = resource_names.get(str(resource_id), str(resource_id))
        lower_name = str(name).lower()
        if all(token.lower() in lower_name for token in tokens):
            matches.append({"resource_id": resource_id, "name": name})
    return matches


def append_bool(report, key, value):
    report.append("{}={}".format(key, str(bool(value)).lower()))


def read_stage15_fog_environment_static_data(controller, rd, event_id, resource_names):
    controller.SetFrameEvent(event_id, True)
    state = controller.GetPipelineState()
    for used_descriptor in state.GetReadOnlyResources(rd.ShaderStage.Pixel, True):
        descriptor = used_descriptor.descriptor
        byte_size = int(safe_getattr(descriptor, "byteSize", 0) or 0)
        if byte_size != ENVIRONMENT_STATIC_DATA_BYTE_SIZE:
            continue
        resource_id = safe_getattr(descriptor, "resource")
        byte_offset = int(safe_getattr(descriptor, "byteOffset", 0) or 0)
        if resource_id is None:
            continue
        raw = controller.GetBufferData(resource_id, byte_offset, byte_size)
        blob = bytes(raw)
        u32_count = len(blob) // 4
        if u32_count < ENVIRONMENT_STATIC_DATA_U32_COUNT:
            continue
        values_u32 = struct.unpack(
            "<{}I".format(u32_count),
            blob[: u32_count * 4],
        )
        values_f32 = struct.unpack(
            "<{}f".format(u32_count),
            blob[: u32_count * 4],
        )
        return {
            "resource_id": resource_id,
            "resource_name": resource_names.get(str(resource_id), str(resource_id)),
            "byte_offset": byte_offset,
            "byte_size": byte_size,
            "integrated_light_scattering_srv": values_u32[
                VOLUMETRIC_FOG_INTEGRATED_LIGHT_SCATTERING_SRV_U32
            ],
            "flags": values_u32[VOLUMETRIC_FOG_FLAGS_U32],
            "grid_width": values_u32[VOLUMETRIC_FOG_GRID_WIDTH_U32],
            "grid_height": values_u32[VOLUMETRIC_FOG_GRID_HEIGHT_U32],
            "grid_depth": values_u32[VOLUMETRIC_FOG_GRID_DEPTH_U32],
            "grid_z_params": values_f32[
                VOLUMETRIC_FOG_GRID_Z_PARAMS_F32 : VOLUMETRIC_FOG_GRID_Z_PARAMS_F32 + 3
            ],
        }
    return None


def build_report(controller, report: ReportWriter, capture_path: Path, report_path: Path):
    rd = renderdoc_module()
    action_records = collect_action_records(controller)
    resource_records = collect_resource_records_raw(controller)
    resource_names = resource_id_to_name(controller)
    texture_descs = texture_desc_map(controller)

    volumetric_scope = records_with_name(action_records, VOLUMETRIC_SCOPE)
    volumetric_records = records_under_prefix(action_records, VOLUMETRIC_SCOPE)
    fog_scope = records_with_name(action_records, FOG_SCOPE)
    fog_records = records_under_prefix(action_records, FOG_SCOPE)

    volumetric_event_ids = collect_event_ids(volumetric_scope, volumetric_records)
    fog_event_ids = collect_event_ids(fog_scope, fog_records)

    volumetric_dispatch_count = len(records_with_name(volumetric_records, DISPATCH_NAME))
    fog_draw_count = len(records_with_name(fog_records, DRAW_NAME))
    volumetric_dispatch = (
        records_with_name(volumetric_records, DISPATCH_NAME)[0]
        if volumetric_dispatch_count > 0
        else None
    )
    fog_draw = records_with_name(fog_records, DRAW_NAME)[0] if fog_draw_count > 0 else None
    volumetric_scope_present = len(volumetric_scope) == 1
    volumetric_dispatch_valid = volumetric_dispatch_count == 1
    fog_scope_present = len(fog_scope) == 1
    fog_draw_valid = fog_draw_count == 1
    volumetric_before_fog = (
        volumetric_scope_present
        and fog_scope_present
        and volumetric_scope[0].event_id < fog_scope[0].event_id
    )

    written = (
        bound_named_resources(
            controller,
            rd,
            resource_names,
            volumetric_dispatch.event_id,
            rd.ShaderStage.Compute,
            True,
            INTEGRATED_LIGHT_SCATTERING_TOKEN,
        )
        if volumetric_dispatch is not None
        else []
    )
    consumed = (
        bound_named_resources(
            controller,
            rd,
            resource_names,
            fog_draw.event_id,
            rd.ShaderStage.Pixel,
            False,
            INTEGRATED_LIGHT_SCATTERING_TOKEN,
        )
        if fog_draw is not None
        else []
    )
    if not written:
        written = find_named_resource_usage(
            controller,
            resource_records,
            volumetric_event_ids,
            INTEGRATED_LIGHT_SCATTERING_TOKEN,
        )
    if not consumed:
        consumed = find_named_resource_usage(
            controller,
            resource_records,
            fog_event_ids,
            INTEGRATED_LIGHT_SCATTERING_TOKEN,
        )
    integrated_light_scattering_written = len(written) > 0
    integrated_light_scattering_consumed_by_fog = len(consumed) > 0
    integrated_volume = sample_integrated_volume(
        controller,
        rd,
        texture_descs,
        written[0]["resource_id"] if written else None,
    ) if written else {
        "sampled": False,
        "width": 0,
        "height": 0,
        "depth": 0,
        "slices": [],
        "min": [0.0, 0.0, 0.0, 0.0],
        "max": [0.0, 0.0, 0.0, 0.0],
        "rgb_nonzero": False,
        "alpha_valid": False,
    }
    stage15_static_data = (
        read_stage15_fog_environment_static_data(
            controller,
            rd,
            fog_draw.event_id,
            resource_names,
        )
        if fog_draw is not None
        else None
    )
    stage15_static_data_bound = stage15_static_data is not None
    stage15_integrated_srv = (
        stage15_static_data["integrated_light_scattering_srv"]
        if stage15_static_data_bound
        else INVALID_BINDLESS_INDEX
    )
    stage15_volumetric_flags = (
        stage15_static_data["flags"] if stage15_static_data_bound else 0
    )
    stage15_grid_width = (
        stage15_static_data["grid_width"] if stage15_static_data_bound else 0
    )
    stage15_grid_height = (
        stage15_static_data["grid_height"] if stage15_static_data_bound else 0
    )
    stage15_grid_depth = (
        stage15_static_data["grid_depth"] if stage15_static_data_bound else 0
    )
    stage15_integrated_srv_valid = stage15_integrated_srv != INVALID_BINDLESS_INDEX
    stage15_volumetric_enabled = (
        stage15_volumetric_flags & GPU_VOLUMETRIC_FOG_FLAG_ENABLED
    ) != 0
    stage15_integrated_flag_valid = (
        stage15_volumetric_flags & GPU_VOLUMETRIC_FOG_FLAG_INTEGRATED_SCATTERING_VALID
    ) != 0
    stage15_grid_valid = (
        stage15_grid_width > 0 and stage15_grid_height > 0 and stage15_grid_depth > 0
    )
    stage15_grid_z_params = (
        stage15_static_data["grid_z_params"] if stage15_static_data_bound else (0.0, 0.0, 0.0)
    )
    stage15_grid_z_valid = (
        stage15_grid_z_params[0] > 0.0
        and stage15_grid_z_params[1] < 1.0
        and abs(stage15_grid_z_params[2] - 32.0) < 0.001
    )

    report.append("analysis_profile=vortexbasic_volumetric_fog")
    report.append("capture_path={}".format(capture_path))
    report.append("report_path={}".format(report_path))
    report.append("total_actions={}".format(len(action_records)))
    report.append("stage14_volumetric_fog_scope_count={}".format(len(volumetric_scope)))
    report.append("stage14_volumetric_fog_dispatch_count={}".format(volumetric_dispatch_count))
    report.append("stage15_fog_scope_count={}".format(len(fog_scope)))
    report.append("stage15_fog_draw_count={}".format(fog_draw_count))
    report.append(
        "stage14_volumetric_fog_event={}".format(
            volumetric_scope[0].event_id if volumetric_scope else ""
        )
    )
    report.append(
        "stage15_fog_event={}".format(fog_scope[0].event_id if fog_scope else "")
    )
    report.append(
        "integrated_light_scattering_written_resource={}".format(
            written[0]["name"] if written else ""
        )
    )
    report.append(
        "integrated_light_scattering_consumed_resource={}".format(
            consumed[0]["name"] if consumed else ""
        )
    )
    report.append(
        "integrated_light_scattering_volume_dims={}x{}x{}".format(
            integrated_volume["width"],
            integrated_volume["height"],
            integrated_volume["depth"],
        )
    )
    report.append(
        "integrated_light_scattering_volume_min={}".format(
            format_values(integrated_volume["min"])
        )
    )
    report.append(
        "integrated_light_scattering_volume_max={}".format(
            format_values(integrated_volume["max"])
        )
    )
    for index, slice_report in enumerate(integrated_volume["slices"]):
        report.append(
            "integrated_light_scattering_slice_{}_index={}".format(
                index,
                slice_report["slice"],
            )
        )
        report.append(
            "integrated_light_scattering_slice_{}_min={}".format(
                index,
                format_values(slice_report["min"]),
            )
        )
        report.append(
            "integrated_light_scattering_slice_{}_max={}".format(
                index,
                format_values(slice_report["max"]),
            )
        )
        report.append(
            "integrated_light_scattering_slice_{}_center={}".format(
                index,
                format_values(slice_report["center"]),
            )
        )
    report.append(
        "stage15_fog_environment_static_resource={}".format(
            stage15_static_data["resource_name"] if stage15_static_data_bound else ""
        )
    )
    report.append(
        "stage15_fog_environment_static_byte_size={}".format(
            stage15_static_data["byte_size"] if stage15_static_data_bound else 0
        )
    )
    report.append(
        "stage15_fog_integrated_light_scattering_srv={}".format(
            stage15_integrated_srv if stage15_static_data_bound else ""
        )
    )
    report.append(
        "stage15_fog_volumetric_flags={}".format(
            stage15_volumetric_flags if stage15_static_data_bound else ""
        )
    )
    report.append(
        "stage15_fog_volumetric_grid={}x{}x{}".format(
            stage15_grid_width,
            stage15_grid_height,
            stage15_grid_depth,
        )
    )
    report.append(
        "stage15_fog_volumetric_grid_z_params={:.9g},{:.9g},{:.9g}".format(
            stage15_grid_z_params[0],
            stage15_grid_z_params[1],
            stage15_grid_z_params[2],
        )
    )

    append_bool(report, "stage14_volumetric_fog_scope_present", volumetric_scope_present)
    append_bool(report, "stage14_volumetric_fog_dispatch_valid", volumetric_dispatch_valid)
    append_bool(report, "stage15_fog_scope_present", fog_scope_present)
    append_bool(report, "stage15_fog_draw_valid", fog_draw_valid)
    append_bool(report, "volumetric_fog_before_stage15_fog", volumetric_before_fog)
    append_bool(report, "integrated_light_scattering_written", integrated_light_scattering_written)
    append_bool(
        report,
        "integrated_light_scattering_volume_sampled",
        integrated_volume["sampled"],
    )
    append_bool(
        report,
        "integrated_light_scattering_volume_rgb_nonzero",
        integrated_volume["rgb_nonzero"],
    )
    append_bool(
        report,
        "integrated_light_scattering_volume_alpha_valid",
        integrated_volume["alpha_valid"],
    )
    append_bool(
        report,
        "integrated_light_scattering_consumed_by_fog",
        integrated_light_scattering_consumed_by_fog,
    )
    append_bool(
        report,
        "stage15_fog_environment_static_data_bound",
        stage15_static_data_bound,
    )
    append_bool(
        report,
        "stage15_fog_integrated_light_scattering_srv_valid",
        stage15_integrated_srv_valid,
    )
    append_bool(
        report,
        "stage15_fog_volumetric_enabled_flag",
        stage15_volumetric_enabled,
    )
    append_bool(
        report,
        "stage15_fog_integrated_scattering_valid_flag",
        stage15_integrated_flag_valid,
    )
    append_bool(report, "stage15_fog_volumetric_grid_valid", stage15_grid_valid)
    append_bool(report, "stage15_fog_volumetric_grid_z_valid", stage15_grid_z_valid)

    overall = (
        volumetric_scope_present
        and volumetric_dispatch_valid
        and fog_scope_present
        and fog_draw_valid
        and volumetric_before_fog
        and integrated_light_scattering_written
        and integrated_volume["sampled"]
        and integrated_volume["rgb_nonzero"]
        and integrated_volume["alpha_valid"]
        and stage15_static_data_bound
        and stage15_integrated_srv_valid
        and stage15_volumetric_enabled
        and stage15_integrated_flag_valid
        and stage15_grid_valid
        and stage15_grid_z_valid
    )
    report.append("overall_verdict={}".format("pass" if overall else "fail"))

    interesting_tokens = ("volumetric", "stage15.fog", "integratedlightscattering")
    for index, record in enumerate(
        [
            record
            for record in action_records
            if any(token in "{} {}".format(record.name, record.path).lower() for token in interesting_tokens)
        ][:80],
        start=1,
    ):
        report.append(
            "action_{:02d}=event:{} name:{} path:{}".format(
                index,
                record.event_id,
                record.name,
                record.path,
            )
        )

    for resource_key, name in sorted(resource_names.items(), key=lambda item: str(item[1])):
        if INTEGRATED_LIGHT_SCATTERING_TOKEN in str(name).lower():
            report.append("resource_{}={}".format(resource_key, name))


def main():
    run_ui_script(REPORT_SUFFIX, build_report)


if __name__ == "__main__":
    main()
