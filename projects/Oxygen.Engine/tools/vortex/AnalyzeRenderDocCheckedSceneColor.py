"""Inspect the native whole-image check followed by gated FP16 conversion."""

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
    actions = [a for a in collect_action_records(controller)
               if a.flags & rd.ActionFlags.Dispatch]
    checks = []
    converted = []
    for action in actions:
        controller.SetFrameEvent(action.event_id, True)
        pipeline = controller.GetPipelineState()
        reads = [x.descriptor for x in pipeline.GetReadOnlyResources(rd.ShaderStage.Compute, True)]
        writes = [x.descriptor for x in pipeline.GetReadWriteResources(rd.ShaderStage.Compute, True)]
        constants = [r for r in reads if r.byteSize == 32 and r.elementByteSize == 32]
        check_constants = [r for r in reads if r.byteSize == 80 and r.elementByteSize == 80]
        if check_constants:
            c = check_constants[0]
            words = struct.unpack("<20I", bytes(controller.GetBufferData(c.resource, c.byteOffset, 80)))
            if not words[7] & 16 or words[9] != 1024:
                raise RuntimeError("The conversion check must use current P and product 11")
            checks.append(action.event_id)
        if not constants:
            continue
        c = constants[0]
        words = struct.unpack("<8I", bytes(controller.GetBufferData(c.resource, c.byteOffset, 32)))
        if words[3:] != (9, 3, 0, 0, 0):
            raise RuntimeError("Conversion constants extent/reserved ABI mismatch")
        reports = [r for r in reads if names.get(str(r.resource)) == "Vortex.Exposure.Suitability"]
        destinations = [r for r in writes if names.get(str(r.resource)) == "CheckedSceneColorDestination"]
        if len(reports) != 1 or len(destinations) != 1:
            raise RuntimeError("Missing or ambiguous conversion resources")
        state = struct.unpack("<2f10I", bytes(controller.GetBufferData(reports[0].resource, 0, 48)))
        if state != (1.0, .5, 1024, 0, 0, 0, 0, 0, 0, 27, 1024, 0):
            raise RuntimeError(f"Current-frame qualification mismatch: {state}")
        raw = bytes(controller.GetTextureData(destinations[0].resource, rd.Subresource()))
        expected = struct.pack("<4H", 0x3000, 0x3555, 0x3800, 0x3800) * 27
        if raw != expected:
            raise RuntimeError("FP16 destination differs from the independent bit-pattern oracle")
        converted.append(action.event_id)
        report.append(f"conversion_event={action.event_id} state={state} texels=27 bytes={len(raw)}")
    if len(checks) != 4 or len(converted) != 1 or max(checks) >= converted[0]:
        raise RuntimeError("Expected four ordered qualification dispatches before one conversion")
    report.append("checked_scene_color_verdict=pass")
    report.append("scope=checked conversion primitive; runtime admission remains separate")


if __name__ == "__main__":
    run_ui_script("_checked_scene_color.txt", build_report)
