"""Temporary Vortex-local Stage 9 state probe."""

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
    run_ui_script,
    safe_getattr,
)


REPORT_SUFFIX = "_stage9_state_probe.txt"


def binding_dict(resource_names, descriptor_access):
    descriptor = descriptor_access.descriptor
    resource_id = descriptor.resource
    return {
        "id": resource_id,
        "name": resource_names.get(str(resource_id), str(resource_id)),
        "bind": descriptor_access.access.index,
        "array": descriptor_access.access.arrayElement,
        "type": descriptor_access.access.type,
    }


def dump_descriptor(report, prefix, resource_names, descriptor):
    report.append("{}_resource={}".format(prefix, resource_names.get(str(descriptor.resource), str(descriptor.resource))))
    report.append("{}_firstMip={}".format(prefix, safe_getattr(descriptor, "firstMip", None)))
    report.append("{}_firstSlice={}".format(prefix, safe_getattr(descriptor, "firstSlice", None)))
    report.append("{}_numMips={}".format(prefix, safe_getattr(descriptor, "numMips", None)))
    report.append("{}_numSlices={}".format(prefix, safe_getattr(descriptor, "numSlices", None)))
    report.append("{}_format={}".format(prefix, repr(safe_getattr(descriptor, "format", None))))
    report.append("{}_type={}".format(prefix, repr(safe_getattr(descriptor, "type", None))))
    report.append("{}_view={}".format(prefix, repr(safe_getattr(descriptor, "view", None))))


def dump_draw_state(controller, report, event_id, label, resource_names):
    rd = renderdoc_module()
    controller.SetFrameEvent(event_id, True)
    state = controller.GetPipelineState()
    viewport = state.GetViewport(0)
    scissor = state.GetScissor(0)
    report.append("{}_event={}".format(label, event_id))
    for attr in ["enabled", "x", "y", "width", "height", "minDepth", "maxDepth"]:
        report.append("{}_viewport_{}={}".format(label, attr, repr(safe_getattr(viewport, attr, None))))
    for attr in ["enabled", "x", "y", "width", "height", "left", "top", "right", "bottom"]:
        report.append("{}_scissor_{}={}".format(label, attr, repr(safe_getattr(scissor, attr, None))))
    report.append("{}_topology={}".format(label, repr(state.GetPrimitiveTopology())))
    report.append("{}_vs={}".format(label, repr(state.GetShader(rd.ShaderStage.Vertex))))
    report.append("{}_ps={}".format(label, repr(state.GetShader(rd.ShaderStage.Pixel))))
    report.append("{}_vs_entry={}".format(label, repr(state.GetShaderEntryPoint(rd.ShaderStage.Vertex))))
    report.append("{}_ps_entry={}".format(label, repr(state.GetShaderEntryPoint(rd.ShaderStage.Pixel))))
    depth_target = state.GetDepthTarget()
    dump_descriptor(report, "{}_depth_target".format(label), resource_names, depth_target)
    outputs = state.GetOutputTargets()
    report.append("{}_output_count={}".format(label, len(outputs)))
    for index, output in enumerate(outputs):
        dump_descriptor(report, "{}_output_{}".format(label, index), resource_names, output)


def build_report(controller, report: ReportWriter, capture_path: Path, report_path: Path):
    actions = collect_action_records(controller)
    resource_names = resource_id_to_name(controller)
    stage3 = []
    stage9 = []
    for record in actions:
        if record.name == "ID3D12GraphicsCommandList::DrawInstanced()" and "Vortex.Stage3.DepthPrepass" in record.path:
            stage3.append(record.event_id)
        if record.name == "ID3D12GraphicsCommandList::DrawInstanced()" and "Vortex.Stage9.BasePass" in record.path:
            stage9.append(record.event_id)
    if not stage3 or not stage9:
        raise RuntimeError("Missing stage draw events")
    dump_draw_state(controller, report, stage3[-1], "stage3", resource_names)
    dump_draw_state(controller, report, stage9[-1], "stage9", resource_names)


def main():
    run_ui_script(REPORT_SUFFIX, build_report)


if __name__ == "__main__":
    main()
