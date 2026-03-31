"""RenderDoc UI API surface dump for local deep-dive tooling.

Run only inside RenderDoc UI:

PowerShell:
    $env:OXYGEN_RENDERDOC_REPORT_PATH = 'H:/path/api_surface.txt'
    & 'C:/Program Files/RenderDoc/qrenderdoc.exe' --ui-python `
        'H:/projects/DroidNet/projects/Oxygen.Engine/Examples/RenderScene/AnalyzeRenderDocApiSurface.py' `
        'H:/path/capture.rdc'
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

    capture_context = globals().get("pyrenderdoc")
    get_capture_filename = getattr(capture_context, "GetCaptureFilename", None)
    if callable(get_capture_filename):
        try:
            capture_path = Path(get_capture_filename()).resolve()
            candidates.append(capture_path.parent)
            candidates.extend(capture_path.parents)
        except Exception:
            pass

    search_root = Path.cwd().resolve()
    candidates.append(search_root)
    candidates.extend(search_root.parents)

    seen = set()
    for candidate_root in candidates:
        normalized = str(candidate_root).lower()
        if normalized in seen:
            continue
        seen.add(normalized)

        if (candidate_root / "renderdoc_ui_analysis.py").exists():
            return candidate_root

        candidate = candidate_root / "Examples" / "RenderScene"
        if (candidate / "renderdoc_ui_analysis.py").exists():
            return candidate

    raise RuntimeError(
        "Unable to locate Examples/RenderScene from the RenderDoc script path."
    )


if "pyrenderdoc" in globals():
    builtins.pyrenderdoc = pyrenderdoc


SCRIPT_DIR = resolve_script_dir()
if str(SCRIPT_DIR) not in sys.path:
    sys.path.insert(0, str(SCRIPT_DIR))

from renderdoc_ui_analysis import ReportWriter, run_ui_script  # noqa: E402


REPORT_SUFFIX = "_api_surface.txt"


def matching_names(names, needles):
    lowered = tuple(needle.lower() for needle in needles)
    return [
        name
        for name in sorted(names)
        if any(needle in name.lower() for needle in lowered)
    ]


def build_report(controller, report: ReportWriter, capture_path, report_path):
    state = controller.GetPipelineState()
    report.append("analysis_profile=api_surface")
    report.append("capture_path={}".format(capture_path))
    report.append("report_path={}".format(report_path))
    report.blank()

    controller_names = dir(controller)
    state_names = dir(state)

    report.append("controller_matching_methods")
    report.extend(
        matching_names(
            controller_names,
            (
                "buffer",
                "texture",
                "pixel",
                "hist",
                "minmax",
                "save",
                "data",
                "custom",
            ),
        )
        or ["<none>"]
    )
    report.blank()

    report.append("pipeline_state_matching_methods")
    report.extend(
        matching_names(
            state_names,
            (
                "buffer",
                "texture",
                "pixel",
                "hist",
                "minmax",
                "save",
                "data",
                "output",
                "read",
                "write",
            ),
        )
        or ["<none>"]
    )
    report.blank()

    for method_name in (
        "GetBufferData",
        "GetTextureData",
        "GetMinMax",
        "GetHistogram",
        "PickPixel",
        "SaveTexture",
    ):
        method = getattr(controller, method_name, None)
        report.append("{}_callable={}".format(method_name, callable(method)))
        if callable(method):
            doc = getattr(method, "__doc__", None)
            report.append("{}_doc={}".format(method_name, doc or "<none>"))
    report.flush()


RUN_EXIT_CODE = run_ui_script(REPORT_SUFFIX, build_report)
