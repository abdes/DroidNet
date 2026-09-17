"""Inspect the native two-frame GPU pre-exposure resolve capture.

Capture FrameResolvePinsManualGainAndDistinctInFlightRecords with
OXYGEN_EXPOSURE_CAPTURE, then use Invoke-RenderDocUiAnalysis.ps1.
"""
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
        constants = [x for x in reads if x.byteSize == 48 and x.elementByteSize == 48]
        if not constants:
            continue
        if len(constants) != 1:
            raise RuntimeError("Frame constants are not uniquely bound")
        descriptor = constants[0]
        raw = bytes(controller.GetBufferData(descriptor.resource, descriptor.byteOffset, 48))
        fields = struct.unpack("<4I3f5I", raw)
        writes = [x.descriptor for x in pipeline.GetReadWriteResources(rd.ShaderStage.Compute, True)]
        frames = [x for x in writes if names.get(str(x.resource)) == "Vortex.PostProcess.Exposure.Frame"]
        states = [x for x in writes if names.get(str(x.resource)) == "Vortex.PostProcess.Exposure.State"]
        if len(frames) != 1 or len(states) != 1:
            raise RuntimeError("Frame and current-state UAVs are not uniquely bound")
        statuses = [x for x in writes if names.get(str(x.resource)) == "Vortex.PostProcess.Exposure.Status"]
        if len(statuses) != 1 or bytes(controller.GetBufferData(statuses[0].resource, 0, 256)) != bytes(256):
            raise RuntimeError("Frame preparation must clear the current producer status")
        frame = frames[0]
        values = struct.unpack("<2f2I", bytes(controller.GetBufferData(frame.resource, frame.byteOffset, 16)))
        gain = struct.unpack("<f", bytes(controller.GetBufferData(states[0].resource, states[0].byteOffset, 4)))[0]
        if len(records) >= 2:
            raise RuntimeError("Repeated resolve recorded a third dispatch")
        expected = (2**-14, 2**-4)[len(records)]
        if values != (expected, 1 / expected, fields[10], 0) or gain != expected:
            raise RuntimeError(f"Frame ABI/gain mismatch: {values}, current={gain}")
        if fields[2:4] != (0xFFFFFFFF, 0xFFFFFFFF) or fields[7:10] != (0, 0, 0) or fields[11] == 0xFFFFFFFF:
            raise RuntimeError("Unexpected history, candidate, mode, controls or missing status UAV")
        report.append(f"resolve={action.event_id} frame={frame.resource} current={states[0].resource} values={values} current_gain={gain}")
        records.append((frame.resource, states[0].resource, frame.byteOffset, values))
    if len(records) != 2 or records[0][0] == records[1][0] or records[0][1] == records[1][1]:
        raise RuntimeError("Expected two distinct retained frame/current-state allocations")
    # At the second dispatch, the first record must still contain its old domain.
    first = records[0]
    retained = struct.unpack("<2f2I", bytes(controller.GetBufferData(first[0], first[2], 16)))
    if retained != first[3]:
        raise RuntimeError("A later resolve overwrote the first frame's P")
    report.append("frame_exposure_verdict=pass")


if __name__ == "__main__":
    run_ui_script("_frame_exposure.txt", build_report)
