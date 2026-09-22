"""Dump EnvironmentFrameBindings bytes for the Stage 15 sky draw."""

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


REPORT_SUFFIX = "_environment_frame_bindings_data_probe.txt"


def build_report(controller, report: ReportWriter, capture_path: Path, report_path: Path):
    rd = renderdoc_module()
    actions = collect_action_records(controller)
    resources = resource_id_to_name(controller)

    target = None
    for record in actions:
        if "Vortex.Stage15.Sky" in record.path and record.name == "ID3D12GraphicsCommandList::DrawInstanced()":
            target = record
            break
    if target is None:
        raise RuntimeError("No Stage15 sky draw found")

    controller.SetFrameEvent(target.event_id, True)
    state = controller.GetPipelineState()
    readonly = state.GetReadOnlyResources(rd.ShaderStage.Pixel, True)

    by_slot = {int(binding.access.arrayElement): binding.descriptor
               for binding in readonly
               if binding.access.index == rd.DescriptorAccess.NoShaderBinding}
    constants = [block.descriptor for block in state.GetConstantBlocks(rd.ShaderStage.Pixel)
                 if block.access.index != rd.DescriptorAccess.NoShaderBinding
                 and block.descriptor.byteSize == 256]
    if len(constants) != 1:
        raise RuntimeError("Could not locate the view root CBV")
    constant = constants[0]
    constant_bytes = bytes(controller.GetBufferData(constant.resource, constant.byteOffset, 256))
    root_slot = struct.unpack_from("<I", constant_bytes, 224)[0]
    root = by_slot.get(root_slot)
    if root is None or root.elementByteSize != 64:
        raise RuntimeError("Could not locate the canonical ViewFrameBindings root")
    root_bytes = bytes(controller.GetBufferData(root.resource, root.byteOffset, 64))
    environment_slot = struct.unpack_from("<I", root_bytes, 8)[0]
    descriptor = by_slot.get(environment_slot)
    if descriptor is None or descriptor.elementByteSize != 112 or descriptor.byteSize != 112:
        raise RuntimeError("Could not locate the routed 112-byte EnvironmentFrameBindings buffer")

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
    for index, value in enumerate(values_u32):
        report.append(f"u32_{index}={value}")
    for index, value in enumerate(values_f32):
        report.append(f"f32_{index}={value}")


def main():
    run_ui_script(REPORT_SUFFIX, build_report)


if __name__ == "__main__":
    main()
