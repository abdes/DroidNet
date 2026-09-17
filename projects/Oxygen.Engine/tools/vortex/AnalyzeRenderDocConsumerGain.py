"""Audit consumer gain transport and the native amplification rejection case."""

from pathlib import Path
import math
import os
import struct
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "shadows"))
from renderdoc_ui_analysis import collect_action_records, renderdoc_module, resource_id_to_name, run_ui_script


def build_report(controller, report, capture_path, report_path):
    rd = renderdoc_module()
    names = resource_id_to_name(controller)
    scene = os.environ.get("OXYGEN_RENDERDOC_PASS_NAME") == "ConsumerGainScene"
    actions = collect_action_records(controller)
    volume_gains = {}
    if scene:
        for action in actions:
            if not action.flags & rd.ActionFlags.Drawcall or "Vortex.Stage15.Atmosphere" not in action.path:
                continue
            controller.SetFrameEvent(action.event_id, True)
            reads = [x.descriptor for x in controller.GetPipelineState().GetReadOnlyResources(rd.ShaderStage.Pixel, True)]
            volume = [x for x in reads if "AtmosphereCameraAerialPerspective" in names.get(str(x.resource), "")]
            data = [x for x in reads if x.elementByteSize == 272]
            if len(volume) != 1 or len(data) != 1:
                raise RuntimeError("Missing actual AP consumer bindings")
            d = data[0]
            volume_gains[str(volume[0].resource)] = struct.unpack("<f", bytes(
                controller.GetBufferData(d.resource, d.byteOffset + 28, 4)))[0]
    checked = []
    for action in actions:
        if not action.flags & rd.ActionFlags.Dispatch:
            continue
        controller.SetFrameEvent(action.event_id, True)
        pipeline = controller.GetPipelineState()
        shader = pipeline.GetShaderReflection(rd.ShaderStage.Compute)
        if not shader or shader.entryPoint != "CheckSuitabilityProduct":
            continue
        reads = [x.descriptor for x in pipeline.GetReadOnlyResources(rd.ShaderStage.Compute, True)]
        constants = [x for x in reads if x.byteSize == x.elementByteSize == 96]
        if len(constants) != 1:
            raise RuntimeError("Expected the 96-byte qualification record")
        c = constants[0]
        raw = bytes(controller.GetBufferData(c.resource, c.byteOffset, 96))
        words = struct.unpack("<24I", raw)
        if words[8] != 6:
            continue
        gain = struct.unpack_from("<f", raw, 80)[0]
        if words[21:] != (0, 0, 0):
            raise RuntimeError("Nonzero qualification padding")
        if scene:
            sources = [x for x in reads if str(x.resource) in volume_gains]
            if len(sources) != 1 or gain != volume_gains[str(sources[0].resource)]:
                raise RuntimeError("Qualification gain differs from the actual view consumer")
        else:
            outputs = [x.descriptor for x in pipeline.GetReadWriteResources(rd.ShaderStage.Compute, True)
                       if names.get(str(x.descriptor.resource)) == "Vortex.Exposure.Suitability"]
            if len(outputs) != 1:
                raise RuntimeError("Missing suitability report")
            values = struct.unpack("<2f10I", bytes(controller.GetBufferData(outputs[0].resource, 0, 48)))
            expected = 1 if not math.isfinite(gain) or gain < 0 else 4 if gain == 1e6 else 0
            if values[0] != 1 or values[3] != expected or values[9] != 3:
                raise RuntimeError(f"Incorrect amplified qualification: gain={gain}, report={values}")
        checked.append(gain)
        report.append(f"event={action.event_id} product=6 consumer_gain={gain}")
    if scene:
        if len(volume_gains) != 2 or len(checked) != 2 or any(g != struct.unpack('<f',struct.pack('<f',.01))[0] for g in checked):
            raise RuntimeError("Expected both actual AP views at the authored 0.01 gain")
    elif len(checked) != 7:
        raise RuntimeError(f"Expected seven gain cases, found {checked}")
    report.append("consumer_gain_verdict=pass")
    report.append("scope=local amplification gate and parameter transport; cumulative composition admission remains open")


if __name__ == "__main__":
    run_ui_script("_consumer_gain.txt", build_report)
