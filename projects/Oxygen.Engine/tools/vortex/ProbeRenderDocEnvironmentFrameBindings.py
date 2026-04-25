"""Safely inspect Stage 15 sky-pass binding metadata.

This intentionally avoids raw buffer reads and pixel/min-max sampling because
those replay paths have been unstable in RenderDoc for this repository.
"""

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
    safe_getattr,
    run_ui_script,
)


REPORT_SUFFIX = "_environment_frame_bindings_probe.txt"


def describe_resource(resources, descriptor):
    resource = safe_getattr(descriptor, "resource")
    return resources.get(str(resource), str(resource))


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


def build_report(controller, report: ReportWriter, capture_path: Path, report_path: Path):
    del capture_path
    del report_path
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
    report.append(f"event_id={target.event_id}")

    blocks = state.GetConstantBlocks(rd.ShaderStage.Pixel)
    report.append(f"pixel_constant_block_count={len(blocks)}")
    for index, block in enumerate(blocks):
        descriptor = block.descriptor
        report.append(f"cb_{index}_resource={describe_resource(resources, descriptor)}")
        report.append(f"cb_{index}_bind={block.access.index}")
        report.append(f"cb_{index}_array={block.access.arrayElement}")
        append_descriptor_fields(report, f"cb_{index}", descriptor)

    readonly = state.GetReadOnlyResources(rd.ShaderStage.Pixel, True)
    report.append(f"pixel_ro_count={len(readonly)}")
    for index, binding in enumerate(readonly):
        descriptor = binding.descriptor
        report.append(f"ro_{index}_resource={describe_resource(resources, descriptor)}")
        report.append(f"ro_{index}_bind={binding.access.index}")
        report.append(f"ro_{index}_array={binding.access.arrayElement}")
        append_descriptor_fields(report, f"ro_{index}", descriptor)

    outputs = state.GetOutputTargets()
    report.append(f"output_count={len(outputs)}")
    for index, descriptor in enumerate(outputs):
        report.append(f"output_{index}_resource={describe_resource(resources, descriptor)}")
        append_descriptor_fields(report, f"output_{index}", descriptor)


def main():
    run_ui_script(REPORT_SUFFIX, build_report)


if __name__ == "__main__":
    main()
