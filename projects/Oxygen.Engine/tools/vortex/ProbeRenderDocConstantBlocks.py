"""Temporary Vortex-local constant block probe."""

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
)


REPORT_SUFFIX = "_constant_blocks_probe.txt"


def build_report(controller, report: ReportWriter, capture_path: Path, report_path: Path):
    rd = renderdoc_module()
    actions = collect_action_records(controller)
    resources = resource_id_to_name(controller)
    draw = None
    for record in actions:
        if record.event_id == 59:
            draw = record
            break
    if draw is None:
        raise RuntimeError("No stage9 draw event 59")
    controller.SetFrameEvent(draw.event_id, True)
    state = controller.GetPipelineState()
    for stage_name, stage in [("vs", rd.ShaderStage.Vertex), ("ps", rd.ShaderStage.Pixel)]:
        blocks = state.GetConstantBlocks(stage)
        report.append("{}_block_count={}".format(stage_name, len(blocks)))
        for index, block in enumerate(blocks):
            report.append("{}_block_{}_type={}".format(stage_name, index, type(block).__name__))
            report.append("{}_block_{}_repr={}".format(stage_name, index, repr(block)))
            report.append("{}_block_{}_methods={}".format(stage_name, index, ",".join(sorted(dir(block)))))
            descriptor = block.descriptor
            report.append(
                "{}_block_{}_resource={}".format(
                    stage_name, index, resources.get(str(descriptor.resource), str(descriptor.resource))
                )
            )
            report.append(
                "{}_block_{}_bind={}".format(stage_name, index, block.access.index)
            )
            report.append(
                "{}_block_{}_array={}".format(stage_name, index, block.access.arrayElement)
            )


def main():
    run_ui_script(REPORT_SUFFIX, build_report)


if __name__ == "__main__":
    main()
