"""Save the live Vortex sky-view LUT texture from a capture."""

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


REPORT_SUFFIX = "_save_sky_view_probe.txt"
OUTPUT_PATH_ENV = "OXYGEN_RENDERDOC_OUTPUT_PATH"


def build_report(controller, report: ReportWriter, capture_path: Path, report_path: Path):
    rd = renderdoc_module()
    output_path = os.environ.get(OUTPUT_PATH_ENV, "").strip()
    if not output_path:
        raise RuntimeError("Set OXYGEN_RENDERDOC_OUTPUT_PATH to the destination image path.")

    resources = resource_id_to_name(controller)
    textures = list(controller.GetTextures())
    texture_descs = {
        str(safe_getattr(desc, "resourceId")): desc
        for desc in textures
        if safe_getattr(desc, "resourceId") is not None
    }

    actions = collect_action_records(controller)
    target_event = None
    for record in actions:
        if "Vortex.Environment.AtmosphereSkyViewLut" in record.path and record.name == "ID3D12GraphicsCommandList::Dispatch()":
            target_event = record.event_id
            break
    if target_event is None:
        raise RuntimeError("No AtmosphereSkyViewLut dispatch found.")

    controller.SetFrameEvent(target_event, True)

    target_desc = None
    target_name = None
    for resource_id, name in resources.items():
        if name == "Vortex.Environment.AtmosphereSkyViewLut":
            desc = texture_descs.get(resource_id)
            if desc is None:
                continue
            width = int(safe_getattr(desc, "width", 0) or 0)
            height = int(safe_getattr(desc, "height", 0) or 0)
            if width == 192 and height == 104:
                target_desc = desc
                target_name = name
                break
    if target_desc is None:
        raise RuntimeError("No 192x104 Vortex.Environment.AtmosphereSkyViewLut texture found.")

    save = rd.TextureSave()
    save.resourceId = target_desc.resourceId
    save.destType = rd.FileType.PNG
    save.mip = 0
    save.slice.sliceIndex = 0
    save.sample.sampleIndex = 0
    save.typeCast = rd.CompType.Typeless
    save.alpha = rd.AlphaMapping.Preserve

    output_file = Path(output_path).resolve()
    output_file.parent.mkdir(parents=True, exist_ok=True)
    controller.SaveTexture(save, str(output_file))

    report.append(f"event_id={target_event}")
    report.append(f"resource_name={target_name}")
    report.append(f"resource_id={target_desc.resourceId}")
    report.append(f"output_path={output_file}")
    report.append(f"saved_exists={output_file.exists()}")
    report.append(f"saved_size={(output_file.stat().st_size if output_file.exists() else 0)}")


def main():
    run_ui_script(REPORT_SUFFIX, build_report)


if __name__ == "__main__":
    main()
