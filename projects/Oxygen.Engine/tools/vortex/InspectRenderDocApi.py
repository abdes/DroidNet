"""Temporary Vortex-local RenderDoc API inspector."""

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
    renderdoc_module,
    run_ui_script,
)


REPORT_SUFFIX = "_renderdoc_api_inspect.txt"


def build_report(controller, report: ReportWriter, capture_path: Path, report_path: Path):
    rd = renderdoc_module()
    report.append("capture_path={}".format(capture_path))
    report.append("report_path={}".format(report_path))
    report.append("controller_methods={}".format(",".join(sorted(dir(controller)))))
    for method_name in [
        "GetTextureData",
        "GetMinMax",
        "PickPixel",
        "PixelHistory",
        "SaveTexture",
        "GetUsage",
        "GetTextures",
        "GetPostVSData",
    ]:
        method = getattr(controller, method_name, None)
        if method is not None:
            report.append(
                "controller_doc_{}={}".format(
                    method_name, repr(getattr(method, "__doc__", None))
                )
            )
    report.append("renderdoc_Subresource_doc={}".format(repr(getattr(rd.Subresource, "__doc__", None))))
    report.append("renderdoc_CompType_doc={}".format(repr(getattr(rd.CompType, "__doc__", None))))
    report.append("renderdoc_TextureSave_doc={}".format(repr(getattr(rd.TextureSave, "__doc__", None))))
    report.append("renderdoc_ResourceId_doc={}".format(repr(getattr(rd.ResourceId, "__doc__", None))))
    sub = rd.Subresource()
    report.append("renderdoc_Subresource_methods={}".format(",".join(sorted(dir(sub)))))
    report.append("renderdoc_Subresource_repr={}".format(repr(sub)))

    root_actions = controller.GetRootActions()
    report.append("root_action_count={}".format(len(root_actions)))
    if root_actions:
        controller.SetFrameEvent(root_actions[0].eventId, True)
        state = controller.GetPipelineState()
        report.append("pipeline_state_type={}".format(type(state).__name__))
        report.append("pipeline_state_methods={}".format(",".join(sorted(dir(state)))))
        output_targets = getattr(state, "GetOutputTargets", None)
        if callable(output_targets):
            outputs = output_targets()
            report.append("output_target_count={}".format(len(outputs)))
            if outputs:
                report.append("output_target_type={}".format(type(outputs[0]).__name__))
                report.append("output_target_methods={}".format(",".join(sorted(dir(outputs[0])))))
                report.append(
                    "output_target_repr={}".format(repr(outputs[0]))
                )
                for method_name in ["resource", "firstMip", "firstSlice", "numMip", "numSlice"]:
                    report.append(
                        "output_target_attr_{}={}".format(
                            method_name, repr(getattr(outputs[0], method_name, None))
                        )
                    )
        depth_target = getattr(state, "GetDepthTarget", None)
        if callable(depth_target):
            depth = depth_target()
            report.append("depth_target_type={}".format(type(depth).__name__))
            report.append("depth_target_repr={}".format(repr(depth)))
            report.append(
                "depth_target_methods={}".format(",".join(sorted(dir(depth))))
            )


def main():
    run_ui_script(REPORT_SUFFIX, build_report)


if __name__ == "__main__":
    main()
