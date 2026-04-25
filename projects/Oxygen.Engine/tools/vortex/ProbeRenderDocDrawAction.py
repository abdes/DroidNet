"""Temporary Vortex-local draw action probe."""

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

from renderdoc_ui_analysis import ReportWriter, run_ui_script  # noqa: E402


REPORT_SUFFIX = "_draw_action_probe.txt"


def walk_actions(action, structured, path, out):
    name = action.GetName(structured)
    current = path + [name]
    if name == "ID3D12GraphicsCommandList::DrawInstanced()":
        out.append((action, " > ".join(current)))
    for child in action.children:
        walk_actions(child, structured, current, out)


def build_report(controller, report: ReportWriter, capture_path: Path, report_path: Path):
    draws = []
    structured = controller.GetStructuredFile()
    for root in controller.GetRootActions():
        walk_actions(root, structured, [], draws)
    for action, path in draws:
        if "Vortex.Stage3.DepthPrepass" not in path and "Vortex.Stage9.BasePass" not in path:
            continue
        report.append("path={}".format(path))
        report.append("eventId={}".format(action.eventId))
        for attr in [
            "numIndices",
            "numInstances",
            "baseVertex",
            "vertexOffset",
            "indexOffset",
            "instanceOffset",
            "dispatchDimension",
            "flags",
        ]:
            report.append("{}={}".format(attr, repr(getattr(action, attr, None))))
        report.append("action_methods={}".format(",".join(sorted(dir(action)))))
        report.blank()


def main():
    run_ui_script(REPORT_SUFFIX, build_report)


if __name__ == "__main__":
    main()
