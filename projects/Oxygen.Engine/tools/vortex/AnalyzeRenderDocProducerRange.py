"""Verify the range verdict before a clipped half store and after exposure solve."""

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
    initial_gain = None
    producer_status = None
    producer_event = None
    solved = 0
    finalized = 0
    for action in collect_action_records(controller):
        if not action.flags & rd.ActionFlags.Dispatch:
            continue
        controller.SetFrameEvent(action.event_id, True)
        pipeline = controller.GetPipelineState()
        reads = [x.descriptor for x in pipeline.GetReadOnlyResources(rd.ShaderStage.Compute, True)]
        writes = [x.descriptor for x in pipeline.GetReadWriteResources(rd.ShaderStage.Compute, True)]
        states = [x for x in writes if names.get(str(x.resource)) == "Vortex.PostProcess.Exposure.State"]
        statuses = [x for x in writes if names.get(str(x.resource)) == "Vortex.PostProcess.Exposure.Status"]
        frames = [x for x in writes if names.get(str(x.resource)) == "Vortex.PostProcess.Exposure.Frame"]
        if frames and states and statuses:
            initial_gain = bytes(controller.GetBufferData(states[0].resource, 0, 16))
            if bytes(controller.GetBufferData(statuses[0].resource, 0, 80)) != bytes(80):
                raise RuntimeError("Producer status was not cleared before HDR rendering")

        half_outputs = [x for x in writes if str(x.resource) in textures
                        and textures[str(x.resource)].format.compByteWidth == 2]
        if "Vortex.Stage14.VolumetricFog" in action.path and half_outputs:
            if len(statuses) != 1 or len(half_outputs) != 1 or producer_status is not None:
                raise RuntimeError("Expected one isolated half-fog producer")
            constants = [x for x in reads if x.byteSize == 544 and x.elementByteSize == 544]
            if len(constants) != 1:
                raise RuntimeError("Missing volumetric-fog constants")
            c = constants[0]
            raw_constants = bytes(controller.GetBufferData(c.resource, c.byteOffset, 544))
            status_index, half_flag = struct.unpack_from("<2I", raw_constants, 532)
            if status_index == 0xffffffff or half_flag != 1:
                raise RuntimeError("Pre-store check must use the actual half destination")
            producer_status = statuses[0].resource
            producer_event = action.event_id
            values = struct.unpack("<20I", bytes(controller.GetBufferData(producer_status, 0, 80)))
            if values[12:15] != (18, 10, 2):
                raise RuntimeError(f"Expected a finite pre-store overflow verdict, got {values}")
            texture = textures[str(half_outputs[0].resource)]
            raw = bytes(controller.GetTextureData(half_outputs[0].resource, rd.Subresource()))
            offset = (texture.depth - 1) * texture.width * texture.height * 8
            red = struct.unpack_from("<H", raw, offset)[0]
            if red != 0x7bff:
                raise RuntimeError(f"Expected finite half clipping to 65504, got bits {red:#x}")
            report.append(f"producer={producer_event} status={producer_status} failure_kind=2 half_red=65504")

        if producer_status is not None and states and statuses:
            if statuses[0].resource != producer_status:
                raise RuntimeError("Solve/finalizer did not retain this view's producer status")
            constants = [x for x in reads if x.elementByteSize in (64, 112) and x.byteSize == x.elementByteSize]
            if len(constants) != 1:
                continue
            state = bytes(controller.GetBufferData(states[0].resource, 0, 80))
            values = struct.unpack("<20I", bytes(controller.GetBufferData(producer_status, 0, 80)))
            flags = struct.unpack_from("<I", state, 24)[0]
            if state[:16] != initial_gain or flags & 12 or not flags & 32:
                raise RuntimeError("Producer failure must invalidate the current meter and preserve gain")
            if values[12] & 18 != 18 or values[13] != 10 or not values[14] & 2 or values[15] != 0:
                raise RuntimeError("Producer verdict was overwritten or authorized FP16")
            solved += constants[0].byteSize == 112
            finalized += constants[0].byteSize == 64
    if initial_gain is None or producer_status is None or solved != 1 or finalized != 1:
        raise RuntimeError(f"Incomplete producer/solve/finalizer chain: {solved}, {finalized}")
    report.append("producer_range_verdict=pass")
    report.append("scope=finite/range checks and meter rejection; quantization budgets and format switching remain open")


if __name__ == "__main__":
    run_ui_script("_producer_range.txt", build_report)
