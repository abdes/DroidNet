"""Verify reserved exposure-state writes and final S/P consumption.

Capture ExposureGpuTest.FrameDomainSolvesReservedStateAndAppliesManualRatio
with OXYGEN_EXPOSURE_CAPTURE, then use Invoke-RenderDocUiAnalysis.ps1.
"""
from pathlib import Path
import math
import struct
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "shadows"))
from renderdoc_ui_analysis import (
    collect_action_records, renderdoc_module, resource_id_to_name, run_ui_script,
)


def build_report(controller, report, capture_path, report_path):
    rd = renderdoc_module()
    names = resource_id_to_name(controller)
    frames = []
    solved = set()
    draws = 0
    for action in collect_action_records(controller):
        if not action.flags & (rd.ActionFlags.Dispatch | rd.ActionFlags.Drawcall):
            continue
        controller.SetFrameEvent(action.event_id, True)
        pipeline = controller.GetPipelineState()
        if action.flags & rd.ActionFlags.Dispatch:
            writes = [x.descriptor for x in pipeline.GetReadWriteResources(rd.ShaderStage.Compute, True)]
            states = [x for x in writes if names.get(str(x.resource)) == "Vortex.PostProcess.Exposure.State"]
            outputs = [x for x in writes if names.get(str(x.resource)) == "Vortex.PostProcess.Exposure.Frame"]
            if outputs:
                if len(outputs) != 1 or len(states) != 1 or len(frames) >= 2:
                    raise RuntimeError("Unexpected pre-scene frame/state allocation")
                frame = outputs[0]
                data = struct.unpack("<2f2I", bytes(controller.GetBufferData(frame.resource, frame.byteOffset, 16)))
                expected_p = (1 / 16, 1 / 256)[len(frames)]
                if data[0:2] != (expected_p, 1 / expected_p) or data[3] != 0:
                    raise RuntimeError("Incorrect pre-scene numerical domain")
                frames.append((frame, states[0], data, expected_p))
                report.append(f"frame={action.event_id} buffer={frame.resource} reserved={states[0].resource} P={data[0]} inverseP={data[1]}")
            elif states:
                matches = [f for f in frames if f[1].resource == states[0].resource]
                if len(matches) != 1:
                    raise RuntimeError("Late solve did not write a reserved state")
                frame, current, data, expected_s = matches[0]
                gains = struct.unpack("<4f", bytes(controller.GetBufferData(current.resource, current.byteOffset, 16)))
                if gains != (expected_s,) * 4:
                    raise RuntimeError("Late solve wrote incorrect gain")
                solved.add(str(current.resource))
                report.append(f"solve={action.event_id} reserved={current.resource} gains={gains}")
            continue
        if "Vortex.PostProcess.Tonemap" not in action.path:
            continue
        if draws >= len(frames):
            raise RuntimeError("Tonemap ran before frame resolve")
        frame, current, expected_frame, expected_s = frames[draws]
        reads = [x.descriptor for x in pipeline.GetReadOnlyResources(rd.ShaderStage.Pixel, True)]
        if str(current.resource) not in solved or not all(any(x.resource == resource for x in reads) for resource in (frame.resource, current.resource)):
            raise RuntimeError("Tonemap did not bind the frame and solved current state")
        retained = struct.unpack("<2f2I", bytes(controller.GetBufferData(frame.resource, frame.byteOffset, 16)))
        if retained != expected_frame:
            raise RuntimeError("Late solve changed the frozen frame record")
        target = pipeline.GetOutputTargets()[0]
        sub = rd.Subresource()
        sub.mip = sub.slice = sub.sample = 0
        pixel = controller.PickPixel(target.resource, 1, 0, sub, rd.CompType.Float).floatValue
        expected = .25 * expected_s
        if any(not math.isclose(float(pixel[i]), expected, rel_tol=2e-5, abs_tol=2**-120) for i in range(3)):
            raise RuntimeError(f"Final S/P pixel is wrong: {list(pixel)} versus {expected}")
        report.append(f"tonemap={action.event_id} pixel={list(pixel)} expected_scene_times_S={expected}")
        draws += 1
    if len(frames) != 2 or draws != 2 or frames[0][0].resource == frames[1][0].resource:
        raise RuntimeError("Expected two independent frame/solve/tonemap sequences")
    report.append("exposure_domain_verdict=pass")


if __name__ == "__main__":
    run_ui_script("_exposure_domain.txt", build_report)
