"""Verify partial IBL submissions and retained complete metadata in replay."""

from collections import Counter
import json
from pathlib import Path
import struct
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "shadows"))
from renderdoc_ui_analysis import renderdoc_module, run_ui_script


def build_report(controller, report, capture_path, report_path):
    rd = renderdoc_module()
    structured = controller.GetStructuredFile()
    batches = []

    def descendants(action):
        result = []
        if action.flags & rd.ActionFlags.Dispatch:
            result.append(action)
        for child in action.children:
            result.extend(descendants(child))
        return result

    def visit(action):
        if action.GetName(structured) == "Vortex.Environment.IBL.Process":
            batches.append(descendants(action))
        else:
            for child in action.children:
                visit(child)

    for action in controller.GetRootActions():
        visit(action)
    if [len(batch) for batch in batches] != [1, 174, 174, 174, 171]:
        raise RuntimeError("Expected initialization plus four partial batches")
    dispatches = [action for batch in batches for action in batch]
    counts = Counter()
    for action in dispatches:
        controller.SetFrameEvent(action.eventId, True)
        shader = controller.GetPipelineState().GetShaderReflection(rd.ShaderStage.Compute)
        if not shader:
            raise RuntimeError(f"Missing shader at event {action.eventId}")
        counts[shader.entryPoint] += 1
    expected = {"IblInitializeCS": 1, "IblPrepareCS": 96, "IblRangeCS": 1,
                "IblNormalizeCS": 96, "IblMipCS": 28, "IblShCS": 96,
                "IblShReduceCS": 1, "IblPrefilterCS": 124, "IblNarrowCS": 248,
                "IblPrecisionRangeCS": 1, "IblPrecisionReduceCS": 1, "IblCompleteCS": 1}
    if counts != expected or shader.entryPoint != "IblCompleteCS":
        raise RuntimeError(f"Unexpected producer sequence: {dict(counts)}")

    def metadata(resource):
        raw = bytes(controller.GetBufferData(resource, 0, 32))
        return raw, struct.unpack("<ffIIfIII", raw)

    owners = {}
    for resource in controller.GetResources():
        if resource.name == "IBL.Metadata":
            _, values = metadata(resource.resourceId)
            revision = values[3]
            if revision in (980, 981):
                if revision in owners:
                    raise RuntimeError(f"Duplicate generation {revision}")
                owners[revision] = resource.resourceId
    if set(owners) != {980, 981}:
        raise RuntimeError("Expected retained revision 980 and candidate 981")
    old_bytes, old_values = metadata(owners[980])
    if old_values[2] != 3:
        raise RuntimeError("Retained generation is incomplete")
    checkpoints = []
    for batch, expected_flags in zip(batches, (0, 1, 1, 1, 3)):
        event = batch[-1].eventId
        controller.SetFrameEvent(event, True)
        retained, _ = metadata(owners[980])
        _, candidate = metadata(owners[981])
        if retained != old_bytes:
            raise RuntimeError(f"Retained metadata changed at event {event}")
        if candidate[2] != expected_flags or candidate[3] != 981:
            raise RuntimeError(f"Incorrect candidate readiness at event {event}: {candidate}")
        checkpoints.append({"event": event, "dispatches": len(batch),
                            "retained_revision": 980, "candidate_flags": candidate[2]})
    controller.SetFrameEvent(dispatches[-2].eventId, True)
    if metadata(owners[981])[1][2] != 1:
        raise RuntimeError("Candidate became complete before its final producer")
    result = {"verdict": "pass", "capture": str(capture_path),
              "dispatches": len(dispatches), "entry_counts": dict(counts),
              "batches": checkpoints,
              "scope": "Diagnostic four-frame job fixture; readbacks serialize GPU work. Scene scheduling, in-flight pressure and timing are separate gates."}
    Path(report_path).with_suffix(".json").write_text(json.dumps(result, indent=2) + "\n")
    report.append("ibl_jobs_verdict=pass batches=5 dispatches=694 retained=980 candidate=981")


if __name__ == "__main__":
    run_ui_script("_ibl_jobs.txt", build_report)
