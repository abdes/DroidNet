"""Dump AtmosphereSkyViewLut compute-dispatch bindings and raw buffer data."""

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


REPORT_SUFFIX = "_sky_view_dispatch_state_probe.txt"


def append_descriptor_fields(report, prefix, descriptor):
    for field in (
        "byteOffset",
        "byteSize",
        "firstElement",
        "numElements",
        "elementByteSize",
        "firstMip",
        "firstSlice",
        "type",
    ):
        report.append(f"{prefix}_{field}={safe_getattr(descriptor, field, None)}")


def dump_buffer(report, controller, resources, prefix, descriptor):
    resource = safe_getattr(descriptor, "resource")
    byte_offset = int(safe_getattr(descriptor, "byteOffset", 0) or 0)
    byte_size = int(safe_getattr(descriptor, "byteSize", 0) or 0)
    if resource is None or byte_size <= 0:
        return

    raw = controller.GetBufferData(resource, byte_offset, byte_size)
    blob = bytes(raw)
    u32_count = len(blob) // 4
    report.append(f"{prefix}_resource_name={resources.get(str(resource), str(resource))}")
    report.append(f"{prefix}_byte_offset={byte_offset}")
    report.append(f"{prefix}_byte_size={byte_size}")
    report.append(f"{prefix}_u32_count={u32_count}")

    if u32_count == 0:
        return

    values_u32 = struct.unpack("<{}I".format(u32_count), blob[: u32_count * 4])
    values_f32 = struct.unpack("<{}f".format(u32_count), blob[: u32_count * 4])
    for index, value in enumerate(values_u32):
        report.append(f"{prefix}_u32_{index}={value}")
    for index, value in enumerate(values_f32):
        report.append(f"{prefix}_f32_{index}={value}")


def build_report(controller, report: ReportWriter, capture_path: Path, report_path: Path):
    del report_path
    rd = renderdoc_module()
    actions = collect_action_records(controller)
    resources = resource_id_to_name(controller)

    target = None
    for record in actions:
        if "Vortex.Environment.AtmosphereSkyViewLut" in record.path and record.name == "ID3D12GraphicsCommandList::Dispatch()":
            target = record
            break
    if target is None:
        raise RuntimeError("No AtmosphereSkyViewLut dispatch found")

    controller.SetFrameEvent(target.event_id, True)
    state = controller.GetPipelineState()
    report.append(f"capture_path={capture_path}")
    report.append(f"event_id={target.event_id}")
    report.append(f"path={target.path}")

    blocks = state.GetConstantBlocks(rd.ShaderStage.Compute)
    report.append(f"compute_constant_block_count={len(blocks)}")
    for index, block in enumerate(blocks):
        descriptor = block.descriptor
        report.append(f"cb_{index}_resource={resources.get(str(safe_getattr(descriptor, 'resource')), str(safe_getattr(descriptor, 'resource')))}")
        report.append(f"cb_{index}_bind={block.access.index}")
        report.append(f"cb_{index}_array={block.access.arrayElement}")
        append_descriptor_fields(report, f"cb_{index}", descriptor)
        dump_buffer(report, controller, resources, f"cb_{index}", descriptor)

    readonly = state.GetReadOnlyResources(rd.ShaderStage.Compute, True)
    report.append(f"compute_ro_count={len(readonly)}")
    for index, binding in enumerate(readonly):
        descriptor = binding.descriptor
        report.append(f"ro_{index}_resource={resources.get(str(safe_getattr(descriptor, 'resource')), str(safe_getattr(descriptor, 'resource')))}")
        report.append(f"ro_{index}_bind={binding.access.index}")
        report.append(f"ro_{index}_array={binding.access.arrayElement}")
        append_descriptor_fields(report, f"ro_{index}", descriptor)
        dump_buffer(report, controller, resources, f"ro_{index}", descriptor)


def main():
    run_ui_script(REPORT_SUFFIX, build_report)


if __name__ == "__main__":
    main()
