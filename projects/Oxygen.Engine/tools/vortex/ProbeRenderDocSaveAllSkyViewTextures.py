"""Save all live Vortex sky-view LUT textures from a capture."""

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
    renderdoc_module,
    resource_id_to_name,
    safe_getattr,
    run_ui_script,
)


REPORT_SUFFIX = "_save_all_skyview_probe.txt"
OUTPUT_DIR_ENV = "OXYGEN_RENDERDOC_OUTPUT_DIR"


def build_report(controller, report: ReportWriter, capture_path: Path, report_path: Path):
    rd = renderdoc_module()
    output_dir_value = os.environ.get(OUTPUT_DIR_ENV, "").strip()
    if not output_dir_value:
        raise RuntimeError("Set OXYGEN_RENDERDOC_OUTPUT_DIR to the destination directory.")
    output_dir = Path(output_dir_value).resolve()
    output_dir.mkdir(parents=True, exist_ok=True)

    resources = resource_id_to_name(controller)
    textures = list(controller.GetTextures())

    matches = []
    for desc in textures:
        resource_id = safe_getattr(desc, "resourceId")
        if resource_id is None:
            continue
        name = resources.get(str(resource_id), str(resource_id))
        width = int(safe_getattr(desc, "width", 0) or 0)
        height = int(safe_getattr(desc, "height", 0) or 0)
        if name == "Vortex.Environment.AtmosphereSkyViewLut" and width == 192 and height == 104:
            matches.append((resource_id, desc))

    report.append(f"match_count={len(matches)}")
    for index, (resource_id, desc) in enumerate(matches):
        save = rd.TextureSave()
        save.resourceId = resource_id
        save.destType = rd.FileType.PNG
        save.mip = 0
        save.slice.sliceIndex = 0
        save.sample.sampleIndex = 0
        save.typeCast = rd.CompType.Typeless
        save.alpha = rd.AlphaMapping.Preserve

        output_file = output_dir / f"skyview_{index}.png"
        controller.SaveTexture(save, str(output_file))

        report.append(f"texture_{index}_resource_id={resource_id}")
        report.append(f"texture_{index}_output={output_file}")
        report.append(f"texture_{index}_saved_exists={output_file.exists()}")
        report.append(f"texture_{index}_saved_size={(output_file.stat().st_size if output_file.exists() else 0)}")


def main():
    run_ui_script(REPORT_SUFFIX, build_report)


if __name__ == "__main__":
    main()
