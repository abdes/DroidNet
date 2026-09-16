"""Inspect the frame-exposure ABI consumed by VortexBasic's default scene.

The default native scene authors Manual EV13, key 12.5. Inspect actual shader
resource reads, including both P and its reciprocal, through the shared replay
runner. This qualifies publication, not the later all-producer HDR migration.
"""

import math
from pathlib import Path
import struct
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "shadows"))
from renderdoc_ui_analysis import (  # noqa: E402
    collect_action_records,
    renderdoc_module,
    run_ui_script,
)


def build_report(controller, report, capture_path, report_path):
    del report_path
    rd = renderdoc_module()
    expected_p = 1 / 8192
    report.append("capture_path={}".format(capture_path))
    observations = []
    for action in collect_action_records(controller):
        if not (action.flags & rd.ActionFlags.Drawcall and "Vortex.Stage15.Sky" in action.path):
            continue
        controller.SetFrameEvent(action.event_id, True)
        pipeline = controller.GetPipelineState()
        for used in pipeline.GetReadOnlyResources(rd.ShaderStage.Pixel, True):
            descriptor = used.descriptor
            if descriptor.byteSize != 16 or descriptor.elementByteSize != 16:
                continue
            raw = bytes(controller.GetBufferData(descriptor.resource, descriptor.byteOffset, 16))
            p, inverse_p, state_slot, flags = struct.unpack("<2f2I", raw)
            if state_slot != 0xFFFFFFFF or flags != 0:
                continue
            if p == expected_p and inverse_p == 8192 and math.isfinite(p * inverse_p):
                observations.append((action.event_id, str(descriptor.resource), descriptor.byteOffset))
                report.append("frame_exposure event={} P={} inverse_P={} state_slot={} flags={}".format(
                    action.event_id, p, inverse_p, state_slot, flags))
    if not observations:
        raise RuntimeError("No actual sky shader read of the expected 16-byte P/inverse-P record")
    report.append("frame_exposure_abi_verdict=pass")


if __name__ == "__main__":
    run_ui_script("_frame_exposure.txt", build_report)
