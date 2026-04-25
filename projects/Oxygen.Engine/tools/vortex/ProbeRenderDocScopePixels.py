"""Sample output ranges from SceneColor for a named scope draw.

This probe intentionally avoids PickPixel() because that API has been unstable
for this repository's large HDR captures inside RenderDoc UI replay.
"""

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


REPORT_SUFFIX = "_scope_pixels_probe.txt"


def float4_values(pixel_value):
    return [float(v) for v in list(pixel_value.floatValue)[:4]]


def build_report(controller, report: ReportWriter, capture_path: Path, report_path: Path):
    rd = renderdoc_module()
    scope_name = os.environ.get("OXYGEN_RENDERDOC_PASS_NAME", "").strip()
    if not scope_name:
        raise RuntimeError("Set OXYGEN_RENDERDOC_PASS_NAME")

    actions = collect_action_records(controller)
    target = None
    for record in actions:
        if scope_name in record.path and record.name == "ID3D12GraphicsCommandList::DrawInstanced()":
            target = record
            break
    if target is None:
        raise RuntimeError(f"No draw found for {scope_name}")

    controller.SetFrameEvent(target.event_id, True)
    state = controller.GetPipelineState()
    resources = resource_id_to_name(controller)
    outputs = state.GetOutputTargets()
    scene_color = None
    tex_desc = None
    for descriptor in outputs:
        if resources.get(str(descriptor.resource), str(descriptor.resource)) == "SceneColor":
            scene_color = descriptor.resource
            for desc in controller.GetTextures():
                if safe_getattr(desc, "resourceId") == scene_color:
                    tex_desc = desc
                    break
            break
    if scene_color is None or tex_desc is None:
        raise RuntimeError("SceneColor output not found")

    width = int(safe_getattr(tex_desc, "width", 1) or 1)
    height = int(safe_getattr(tex_desc, "height", 1) or 1)
    sub = rd.Subresource()
    sub.mip = 0
    sub.slice = 0
    sub.sample = 0

    report.append(f"scope_name={scope_name}")
    report.append(f"event_id={target.event_id}")
    report.append(f"scene_color_dims={width}x{height}")
    min_value, max_value = controller.GetMinMax(scene_color, sub, rd.CompType.Typeless)
    report.append(f"scene_color_min={float4_values(min_value)}")
    report.append(f"scene_color_max={float4_values(max_value)}")


def main():
    run_ui_script(REPORT_SUFFIX, build_report)


if __name__ == "__main__":
    main()
