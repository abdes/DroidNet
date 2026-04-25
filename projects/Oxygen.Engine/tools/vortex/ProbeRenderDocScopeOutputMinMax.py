"""Probe output targets and min/max for a named RenderDoc scope using safe APIs."""

import builtins
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


REPORT_SUFFIX = "_scope_output_minmax_probe.txt"


def float4_values(pixel_value):
    return [float(v) for v in list(pixel_value.floatValue)[:4]]


def resolve_scope_name():
    configured = os.environ.get("OXYGEN_RENDERDOC_PASS_NAME", "").strip()
    if not configured:
        raise RuntimeError("Set OXYGEN_RENDERDOC_PASS_NAME to the scope name to inspect.")
    return configured


def build_report(controller, report: ReportWriter, capture_path: Path, report_path: Path):
    del capture_path
    del report_path
    rd = renderdoc_module()
    actions = collect_action_records(controller)
    resources = resource_id_to_name(controller)
    scope_name = resolve_scope_name()

    target = None
    for record in actions:
        if scope_name in record.path and record.name in {
            "ID3D12GraphicsCommandList::DrawInstanced()",
            "ID3D12GraphicsCommandList::Dispatch()",
            "ExecuteIndirect()",
        }:
            target = record
    if target is None:
        raise RuntimeError(f"No draw/dispatch found under scope '{scope_name}'")

    controller.SetFrameEvent(target.event_id, True)
    state = controller.GetPipelineState()
    report.append(f"scope_name={scope_name}")
    report.append(f"event_id={target.event_id}")

    outputs = state.GetOutputTargets()
    report.append(f"output_count={len(outputs)}")
    for index, descriptor in enumerate(outputs):
        resource = safe_getattr(descriptor, "resource")
        name = resources.get(str(resource), str(resource))
        report.append(f"output_{index}_resource={name}")
        sub = rd.Subresource()
        sub.mip = int(safe_getattr(descriptor, "firstMip", 0) or 0)
        sub.slice = int(safe_getattr(descriptor, "firstSlice", 0) or 0)
        sub.sample = 0
        min_value, max_value = controller.GetMinMax(resource, sub, rd.CompType.Typeless)
        report.append(f"output_{index}_min={float4_values(min_value)}")
        report.append(f"output_{index}_max={float4_values(max_value)}")


def main():
    run_ui_script(REPORT_SUFFIX, build_report)


if __name__ == "__main__":
    main()
