"""Inspect the native FP16 product-check reduction and candidate selection."""
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
    records = []
    for action in collect_action_records(controller):
        if not action.flags & rd.ActionFlags.Dispatch:
            continue
        controller.SetFrameEvent(action.event_id, True)
        pipeline = controller.GetPipelineState()
        reads = [x.descriptor for x in pipeline.GetReadOnlyResources(rd.ShaderStage.Compute, True)]
        constants = [x for x in reads if x.byteSize == 80 and x.elementByteSize == 80]
        if not constants:
            continue
        writes = [x.descriptor for x in pipeline.GetReadWriteResources(rd.ShaderStage.Compute, True)]
        outputs = [x for x in writes if names.get(str(x.resource)) == "Vortex.Exposure.Suitability"]
        if len(constants) != 1 or len(outputs) != 1:
            raise RuntimeError("Suitability constants/report are not uniquely bound")
        c = constants[0]
        words = struct.unpack("<20I", bytes(controller.GetBufferData(c.resource, c.byteOffset, 80)))
        output = outputs[0]
        values = struct.unpack("<2f10I", bytes(controller.GetBufferData(output.resource, output.byteOffset, 48)))
        report.append(f"dispatch={action.event_id} report={output.resource} product={words[8]} values={values}")
        if words[9] != 1 or values[10] != 1:
            raise RuntimeError("Expected-product mask does not match the fixture")
        records.append((output.resource, values))
    if len(records) != 4 or any(r[0] != records[0][0] for r in records):
        raise RuntimeError("Expected ordered clear, maximum, candidate and check dispatches")
    clear, maximum, candidate, checked = [r[1] for r in records]
    if clear != (1.0, 0.0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0):
        raise RuntimeError("Reduction record was not reset")
    if maximum[1:4] != (1.0, 1, 0) or candidate[0] != 8192.0:
        raise RuntimeError("GPU maximum/candidate does not match the independent oracle")
    if checked != (8192.0, 1.0, 1, 0, 0, 0, 0, 0, 0, 16, 1, 0):
        raise RuntimeError("Checked product/count/error result is incorrect")
    report.append("hdr_suitability_verdict=pass")
    report.append("scope=product qualification only; no format admission")


if __name__ == "__main__":
    run_ui_script("_hdr_suitability.txt", build_report)
