"""Export the rectangles and counters consumed by production histogram dispatches."""

import json
from pathlib import Path
import struct
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "shadows"))
from renderdoc_ui_analysis import (
    collect_action_records, renderdoc_module, resource_id_to_name, run_ui_script,
)


def build_report(controller, report, capture_path, report_path):
    rd = renderdoc_module()
    names = resource_id_to_name(controller)
    textures = {str(t.resourceId): t for t in controller.GetTextures()}
    results = []
    for action in collect_action_records(controller):
        if not action.flags & rd.ActionFlags.Dispatch:
            continue
        controller.SetFrameEvent(action.event_id, True)
        pipeline = controller.GetPipelineState()
        reads = [item.descriptor for item in pipeline.GetReadOnlyResources(rd.ShaderStage.Compute, True)]
        writes = [item.descriptor for item in pipeline.GetReadWriteResources(rd.ShaderStage.Compute, True)]
        histogram = [d for d in writes if names.get(str(d.resource)) == "Vortex.PostProcess.Exposure.Histogram"]
        sources = [d for d in reads if str(d.resource) in textures]
        constants = [d for d in reads if d.elementByteSize == 64 and d.byteSize == 64]
        if not histogram or not sources:
            continue
        if len(histogram) != 1 or len(sources) != 1 or len(constants) != 1:
            raise RuntimeError("This fixture requires one histogram, source and unmasked constant record")
        data = bytes(controller.GetBufferData(constants[0].resource, constants[0].byteOffset, 64))
        left, top, width, height = struct.unpack_from("<4I", data, 16)
        texture = textures[str(sources[0].resource)]
        counters = struct.unpack("<264I", bytes(controller.GetBufferData(
            histogram[0].resource, histogram[0].byteOffset, 1056)))
        if left + width > texture.width or top + height > texture.height or not width or not height:
            raise RuntimeError("Meter rectangle exceeds its own source")
        if counters[260] or counters[256] != min(width, 512) * min(height, 512):
            raise RuntimeError("Histogram did not visit exactly its finite bounded grid")
        result = {"event": action.event_id, "source_width": texture.width,
                  "source_height": texture.height, "rectangle": [left, top, width, height],
                  "finite_samples": counters[256], "weighted_samples": counters[257]}
        results.append(result)
        report.append(str(result))
    if len(results) != 2:
        raise RuntimeError(f"Expected both independent meters, found {len(results)}")
    Path(report_path).with_suffix(".json").write_text(json.dumps(results, indent=2) + "\n", encoding="utf-8")
    report.append("meter_rectangle_verdict=pass")


if __name__ == "__main__":
    run_ui_script("_meter_rectangles.txt", build_report)
