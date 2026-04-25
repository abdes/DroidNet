"""Safely inspect a named RenderDoc scope using action-tree metadata only.

This intentionally avoids replay-state inspection because those paths have been
unstable in qrenderdoc for this repository.
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
    resource_id_to_name,
    safe_getattr,
    run_ui_script,
)


REPORT_SUFFIX = "_named_scope_output_probe.txt"


def resolve_scope_name(capture_path: Path):
    del capture_path
    configured = os.environ.get("OXYGEN_RENDERDOC_PASS_NAME", "").strip()
    if not configured:
        raise RuntimeError("Set OXYGEN_RENDERDOC_PASS_NAME to the scope name to inspect.")
    return configured


def find_first_draw(scope_name, action_records):
    for record in action_records:
        if scope_name in record.path and record.name == "ID3D12GraphicsCommandList::DrawInstanced()":
            return record
    return None


def append_descriptor_fields(report, prefix, descriptor):
    for field in (
        "byteOffset",
        "byteSize",
        "firstMip",
        "firstSlice",
        "type",
    ):
        report.append(f"{prefix}_{field}={safe_getattr(descriptor, field, None)}")


def build_report(controller, report: ReportWriter, capture_path: Path, report_path: Path):
    del report_path
    scope_name = resolve_scope_name(capture_path)
    action_records = collect_action_records(controller)
    resource_names = resource_id_to_name(controller)

    draw = find_first_draw(scope_name, action_records)
    report.append(f"scope_name={scope_name}")
    report.append(f"capture_path={capture_path}")
    report.append(f"draw_event={'' if draw is None else draw.event_id}")

    if draw is None:
        raise RuntimeError(f"No draw event found under scope '{scope_name}'")

    report.append(f"draw_name={draw.name}")
    report.append(f"draw_path={draw.path}")
    report.append(f"draw_flags={draw.flags}")


def main():
    run_ui_script(REPORT_SUFFIX, build_report)


if __name__ == "__main__":
    main()
