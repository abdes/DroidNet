"""Inspect GPU-only producer bounds and the fog history descriptor lifetime."""

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
    producers = []
    current = None
    previous = None
    final_tail = None
    consumers = 0
    qualifications = 0
    for action in collect_action_records(controller):
        if not action.flags & rd.ActionFlags.Dispatch:
            continue
        controller.SetFrameEvent(action.event_id, True)
        pipeline = controller.GetPipelineState()
        reads = [x.descriptor for x in pipeline.GetReadOnlyResources(rd.ShaderStage.Compute, True)]
        writes = [x.descriptor for x in pipeline.GetReadWriteResources(rd.ShaderStage.Compute, True)]
        shader = pipeline.GetShaderReflection(rd.ShaderStage.Compute)
        if shader and shader.entryPoint == "CheckSuitabilityProduct":
            constants = [x for x in reads if x.byteSize == x.elementByteSize == 96]
            if len(constants) != 1:
                raise RuntimeError("Missing local qualification constants")
            c = constants[0]
            words = struct.unpack("<24I", bytes(controller.GetBufferData(c.resource, c.byteOffset, 96)))
            if words[8] == 10:
                bound_reads = [x for x in reads if names.get(str(x.resource)) == "Vortex.PostProcess.Exposure.Status"]
                reports = [x for x in writes if names.get(str(x.resource)) == "Vortex.Exposure.Suitability"]
                if len(bound_reads) != 1 or len(reports) != 1 or bound_reads[0].resource != current:
                    raise RuntimeError("Qualification did not consume this frame's producer certificate")
                values = struct.unpack("<2f10I", bytes(controller.GetBufferData(reports[0].resource, 0, 48)))
                if not values[3] & 4 or values[4] != 10:
                    raise RuntimeError("Retained fog error failed to block qualification")
                qualifications += 1
        statuses = [x for x in writes if names.get(str(x.resource)) == "Vortex.PostProcess.Exposure.Status"]
        frames = [x for x in writes if names.get(str(x.resource)) == "Vortex.PostProcess.Exposure.Frame"]
        if frames and statuses:
            current = statuses[0].resource
            if bytes(controller.GetBufferData(current, 0, 256)) != bytes(256):
                raise RuntimeError("The full GPU status allocation was not cleared")

        if "Vortex.Stage14.VolumetricFog" in action.path and statuses:
            constants = [x for x in reads if x.byteSize == 544 and x.elementByteSize == 544]
            histories = [x for x in reads if names.get(str(x.resource)) == "Vortex.PostProcess.Exposure.Status"]
            if len(constants) != 1 or len(histories) != 1 or statuses[0].resource != current:
                raise RuntimeError("Missing current/prior bound resources")
            if histories[0].resource == current:
                raise RuntimeError("Fog must not read its current writable certificate as history")
            if previous is not None and histories[0].resource != previous:
                raise RuntimeError("The fixture's two producers must use the same prior view record")
            previous = histories[0].resource
            c = constants[0]
            raw = bytes(controller.GetBufferData(c.resource, c.byteOffset, 544))
            half_flag, prior_srv = struct.unpack_from("<2I", raw, 536)
            if prior_srv == 0xffffffff or half_flag not in (0, 1):
                raise RuntimeError("Fog bound constants ABI mismatch")
            old = struct.unpack("<4f", bytes(controller.GetBufferData(previous, 112, 16)))
            final_tail = bytes(controller.GetBufferData(current, 80, 48))
            now = struct.unpack_from("<4f", final_tail, 32)
            if not all(math.isfinite(x) and x >= 0 for x in old + now):
                raise RuntimeError("Expected finite nonnegative affine bounds in this fixture")
            if old[0] + old[1] <= 0 or now[0] + now[1] <= 0:
                raise RuntimeError("Inherited quantization error was silently cleared")
            producers.append(half_flag)
            report.append(f"producer={action.event_id} half={half_flag} previous={previous} current={current} bounds={now}")

        states = [x for x in writes if names.get(str(x.resource)) == "Vortex.PostProcess.Exposure.State"]
        if final_tail is not None and states and statuses:
            if statuses[0].resource != current or bytes(controller.GetBufferData(current, 80, 48)) != final_tail:
                raise RuntimeError("Exposure solve/finalization overwrote the GPU-only error tail")
            consumers += 1
    if sorted(producers) != [0, 1] or consumers != 2 or qualifications != 1:
        raise RuntimeError(f"Incomplete half/float producer and solve/finalizer chain: {producers}, {consumers}")
    report.append("hdr_error_history_verdict=pass")
    report.append("local_history_qualification=pass")
    report.append("scope=GPU error transport/history and local qualification; final composition and meter admission remain open")


if __name__ == "__main__":
    run_ui_script("_hdr_error_history.txt", build_report)
