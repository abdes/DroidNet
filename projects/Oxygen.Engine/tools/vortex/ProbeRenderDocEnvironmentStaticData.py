"""Dump EnvironmentStaticData bytes for the Stage 15 fog draw."""

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


REPORT_SUFFIX = "_environment_static_data_probe.txt"
ENVIRONMENT_STATIC_DATA_BYTE_SIZE = 656
ENVIRONMENT_STATIC_DATA_U32_COUNT = ENVIRONMENT_STATIC_DATA_BYTE_SIZE // 4
VOLUMETRIC_FOG_U32_OFFSET = 128 // 4
VOLUMETRIC_FOG_INTEGRATED_LIGHT_SCATTERING_SRV_U32 = (
    VOLUMETRIC_FOG_U32_OFFSET + 12
)
VOLUMETRIC_FOG_FLAGS_U32 = VOLUMETRIC_FOG_U32_OFFSET + 13
VOLUMETRIC_FOG_GRID_WIDTH_U32 = VOLUMETRIC_FOG_U32_OFFSET + 14
VOLUMETRIC_FOG_GRID_HEIGHT_U32 = VOLUMETRIC_FOG_U32_OFFSET + 15
VOLUMETRIC_FOG_GRID_DEPTH_U32 = VOLUMETRIC_FOG_U32_OFFSET + 16
GPU_VOLUMETRIC_FOG_FLAG_ENABLED = 1 << 0
GPU_VOLUMETRIC_FOG_FLAG_INTEGRATED_SCATTERING_VALID = 1 << 1
INVALID_BINDLESS_INDEX = 0xFFFFFFFF


def build_report(controller, report: ReportWriter, capture_path: Path, report_path: Path):
    rd = renderdoc_module()
    actions = collect_action_records(controller)
    resources = resource_id_to_name(controller)

    target = None
    for record in actions:
        if (
            "Vortex.Stage15.Fog" in record.path
            and record.name == "ID3D12GraphicsCommandList::DrawInstanced()"
        ):
            target = record
            break
    if target is None:
        raise RuntimeError("No Stage15 fog draw found")

    controller.SetFrameEvent(target.event_id, True)
    state = controller.GetPipelineState()
    readonly = state.GetReadOnlyResources(rd.ShaderStage.Pixel, True)
    descriptor = None
    for binding in readonly:
        candidate = binding.descriptor
        if (
            int(safe_getattr(candidate, "byteSize", 0) or 0)
            == ENVIRONMENT_STATIC_DATA_BYTE_SIZE
        ):
            descriptor = candidate
            break
    if descriptor is None:
        raise RuntimeError(
            "Could not locate the {}-byte EnvironmentStaticData buffer".format(
                ENVIRONMENT_STATIC_DATA_BYTE_SIZE
            )
        )

    resource = safe_getattr(descriptor, "resource")
    byte_offset = int(safe_getattr(descriptor, "byteOffset", 0) or 0)
    byte_size = int(safe_getattr(descriptor, "byteSize", 0) or 0)
    raw = controller.GetBufferData(resource, byte_offset, byte_size)
    blob = bytes(raw)

    report.append(f"event_id={target.event_id}")
    report.append(f"resource_name={resources.get(str(resource), str(resource))}")
    report.append(f"byte_offset={byte_offset}")
    report.append(f"byte_size={byte_size}")
    report.append(f"u32_count={len(blob) // 4}")

    values_u32 = struct.unpack("<{}I".format(len(blob) // 4), blob[: (len(blob) // 4) * 4])
    values_f32 = struct.unpack("<{}f".format(len(blob) // 4), blob[: (len(blob) // 4) * 4])
    integrated_srv = (
        values_u32[VOLUMETRIC_FOG_INTEGRATED_LIGHT_SCATTERING_SRV_U32]
        if len(values_u32) > VOLUMETRIC_FOG_INTEGRATED_LIGHT_SCATTERING_SRV_U32
        else INVALID_BINDLESS_INDEX
    )
    volumetric_flags = (
        values_u32[VOLUMETRIC_FOG_FLAGS_U32]
        if len(values_u32) > VOLUMETRIC_FOG_FLAGS_U32
        else 0
    )
    grid_width = (
        values_u32[VOLUMETRIC_FOG_GRID_WIDTH_U32]
        if len(values_u32) > VOLUMETRIC_FOG_GRID_WIDTH_U32
        else 0
    )
    grid_height = (
        values_u32[VOLUMETRIC_FOG_GRID_HEIGHT_U32]
        if len(values_u32) > VOLUMETRIC_FOG_GRID_HEIGHT_U32
        else 0
    )
    grid_depth = (
        values_u32[VOLUMETRIC_FOG_GRID_DEPTH_U32]
        if len(values_u32) > VOLUMETRIC_FOG_GRID_DEPTH_U32
        else 0
    )
    integrated_srv_valid = integrated_srv != INVALID_BINDLESS_INDEX
    volumetric_enabled = (volumetric_flags & GPU_VOLUMETRIC_FOG_FLAG_ENABLED) != 0
    integrated_flag_valid = (
        volumetric_flags & GPU_VOLUMETRIC_FOG_FLAG_INTEGRATED_SCATTERING_VALID
    ) != 0
    grid_valid = grid_width > 0 and grid_height > 0 and grid_depth > 0
    report.append(f"volumetric_fog_integrated_light_scattering_srv={integrated_srv}")
    report.append(f"volumetric_fog_flags={volumetric_flags}")
    report.append(f"volumetric_fog_grid={grid_width}x{grid_height}x{grid_depth}")
    report.append(f"volumetric_fog_integrated_light_scattering_srv_valid={str(integrated_srv_valid).lower()}")
    report.append(f"volumetric_fog_enabled_flag={str(volumetric_enabled).lower()}")
    report.append(f"volumetric_fog_integrated_scattering_valid_flag={str(integrated_flag_valid).lower()}")
    report.append(f"volumetric_fog_grid_valid={str(grid_valid).lower()}")
    for index, value in enumerate(values_u32):
        report.append(f"u32_{index}={value}")
    for index, value in enumerate(values_f32):
        report.append(f"f32_{index}={value}")


def main():
    run_ui_script(REPORT_SUFFIX, build_report)


if __name__ == "__main__":
    main()
