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

    descriptor = None
    for binding in readonly:
        candidate = binding.descriptor
        if int(safe_getattr(candidate, "byteSize", 0) or 0) == 176:
            descriptor = candidate
            break
    if descriptor is None:
        raise RuntimeError("Could not locate the 176-byte EnvironmentFrameBindings buffer")

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
