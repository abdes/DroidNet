"""Check 8x8 dispatch tiles in a cubemap with 32x32 faces."""

from collections import Counter
import json
from pathlib import Path
import struct
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "shadows"))
from renderdoc_ui_analysis import renderdoc_module, run_ui_script


def build_report(controller, report, capture_path, report_path):
    rd = renderdoc_module()
    actions = []

    def visit(action):
        if action.flags & rd.ActionFlags.Dispatch:
            actions.append(action)
        for child in action.children:
            visit(child)

    for action in controller.GetRootActions():
        visit(action)
    dispatches = []
    spatial = {"IblPrepareCS", "IblNormalizeCS", "IblMipCS", "IblShCS",
               "IblPrefilterCS", "IblNarrowCS"}
    for action in actions:
        controller.SetFrameEvent(action.eventId, True)
        shader = controller.GetPipelineState().GetShaderReflection(rd.ShaderStage.Compute)
        if not shader or not shader.entryPoint.startswith("Ibl"):
            continue
        dimensions = tuple(int(v) for v in action.dispatchDimension)
        if shader.entryPoint in spatial and (dimensions[:2] != (1, 1) or dimensions[2] not in (1, 6)):
            raise RuntimeError(f"Unbounded spatial dispatch: {shader.entryPoint} {dimensions}")
        dispatches.append((shader.entryPoint, action.eventId, dimensions))
    expected = {"IblInitializeCS": 1, "IblPrepareCS": 96, "IblRangeCS": 1,
                "IblNormalizeCS": 96, "IblMipCS": 28, "IblShCS": 96,
                "IblShReduceCS": 1, "IblPrefilterCS": 124, "IblNarrowCS": 248,
                "IblPrecisionRangeCS": 1, "IblPrecisionReduceCS": 1, "IblCompleteCS": 1}
    counts = Counter(name for name, _, _ in dispatches)
    if counts != expected or dispatches[-1][0] != "IblCompleteCS":
        raise RuntimeError(f"Unexpected tiled producer: {dict(counts)}")
    controller.SetFrameEvent(dispatches[-1][1], True)
    metadata = []
    for resource in controller.GetResources():
        if resource.name == "IBL.Metadata":
            values = struct.unpack("<ffIIfIII", bytes(controller.GetBufferData(resource.resourceId, 0, 32)))
            if values[3] == 902:
                metadata.append(resource.resourceId)
    if len(metadata) != 1:
        raise RuntimeError("Expected exactly one metadata buffer for tiled revision 902")
    checkpoints = (dispatches[0], dispatches[-2], dispatches[-1])
    for (name, event, _), flags in zip(checkpoints, (0, 1, 3)):
        controller.SetFrameEvent(event, True)
        values = struct.unpack("<ffIIfIII", bytes(controller.GetBufferData(metadata[0], 0, 32)))
        if values[2] != flags or values[3] != 902:
            raise RuntimeError(f"Incorrect readiness at {name}: {values}")
    result = {"verdict": "pass", "capture": str(capture_path), "revision": 902,
              "spatial_tile_texels": [8, 8], "dispatches": len(dispatches),
              "entry_counts": dict(counts), "metadata_flags": [0, 1, 3],
              "scope": "Replay verifies dispatch subdivision and final readiness; native bitwise comparisons verify product equivalence."}
    Path(report_path).with_suffix(".json").write_text(json.dumps(result, indent=2) + "\n")
    report.append(f"ibl_tiles_verdict=pass dispatches={len(dispatches)} revision=902")


if __name__ == "__main__":
    run_ui_script("_ibl_tiles.txt", build_report)
