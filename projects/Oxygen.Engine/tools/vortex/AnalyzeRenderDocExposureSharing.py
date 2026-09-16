"""Audit the native shared-exposure bootstrap/copy/tonemap capture.

Capture ExposureGpuTest.SharedConsumerTransitionIsRejectedAfterGpuCompletion
with OXYGEN_EXPOSURE_CAPTURE set, then use Invoke-RenderDocUiAnalysis.ps1.
Expected gains and pixels are independent literals.
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
    actions = collect_action_records(controller)
    solves = []
    for action in actions:
        if not action.flags & rd.ActionFlags.Dispatch:
            continue
        controller.SetFrameEvent(action.event_id, True)
        pipeline = controller.GetPipelineState()
        reads = [x.descriptor for x in pipeline.GetReadOnlyResources(rd.ShaderStage.Compute, True)]
        constants = [x for x in reads if x.byteSize == 112 and x.elementByteSize == 112]
        if not constants:
            continue
        if len(constants) != 1:
            raise RuntimeError("Unified exposure constants are not unique")
        d = constants[0]
        raw = bytes(controller.GetBufferData(d.resource, d.byteOffset, 112))
        histogram, output_slot = struct.unpack_from("<2I", raw)
        previous, fixed, mode, controls = struct.unpack_from("<If2I", raw, 64)
        borrowed_slot = struct.unpack_from("<I", raw, 100)[0]
        writes = [x.descriptor for x in pipeline.GetReadWriteResources(rd.ShaderStage.Compute, True)]
        outputs = [x for x in writes if names.get(str(x.resource)) == "Vortex.PostProcess.Exposure.State"]
        if len(outputs) != 1:
            raise RuntimeError("Exposure output state is not uniquely bound")
        output = outputs[0]
        state = bytes(controller.GetBufferData(output.resource, output.byteOffset, 80))
        gains = struct.unpack_from("<4f", state)
        flags = struct.unpack_from("<I", state, 24)[0]
        requested, applied = struct.unpack_from("<2Q", state, 40)
        report.append(f"solve={action.event_id} mode={mode} controls={controls} previous={previous} borrowed={borrowed_slot} output={output_slot} gains={gains} flags={flags} requested={requested} applied={applied}")
        if histogram != 0xFFFFFFFF or any(names.get(str(x.resource)) == "Vortex.PostProcess.Exposure.Histogram" for x in writes):
            raise RuntimeError("Source fallback/consumer unexpectedly meters an image")
        if any(not math.isclose(g, 1 / 16, rel_tol=2e-5, abs_tol=2**-120) for g in gains):
            raise RuntimeError("Source-defined gain was not copied exactly")
        roots = [x for x in reads if names.get(str(x.resource)) == "Vortex.PostProcess.Exposure.State"]
        solves.append((controls, borrowed_slot, output.resource, roots, requested, applied, flags))
    if len(solves) != 2:
        raise RuntimeError("Expected source initialization and one consumer copy")
    initial, consumer = solves
    if initial[0] & 2 == 0 or initial[1] != 0xFFFFFFFF or initial[4:6] != (0, 0):
        raise RuntimeError("Source initialization consumed a transition or borrowed another state")
    if consumer[1] == 0xFFFFFFFF or len(consumer[3]) != 1 or consumer[3][0].resource != initial[2] or consumer[2] == initial[2]:
        raise RuntimeError("Consumer did not read a distinct retained source record")
    if consumer[4:6] != (1, 0) or (consumer[6] >> 16) & 15 != 3 or consumer[6] & (4 | 8 | 512):
        raise RuntimeError("Consumer request rejection or local-meter validity is incorrect")

    draws = [a for a in actions if a.flags & rd.ActionFlags.Drawcall and "Vortex.PostProcess.Tonemap" in a.path]
    if len(draws) != 1:
        raise RuntimeError("Expected one production tonemap draw")
    controller.SetFrameEvent(draws[0].event_id, True)
    pipeline = controller.GetPipelineState()
    reads = [x.descriptor for x in pipeline.GetReadOnlyResources(rd.ShaderStage.Pixel, True)]
    if not any(x.resource == consumer[2] for x in reads):
        raise RuntimeError("Tonemap did not consume the consumer's GPU state")
    target = pipeline.GetOutputTargets()[0]
    sub = rd.Subresource()
    sub.mip = sub.slice = sub.sample = 0
    pixel = controller.PickPixel(target.resource, 1, 0, sub, rd.CompType.Float).floatValue
    report.append(f"tonemap={draws[0].event_id} pixel_1_0={list(pixel)} expected={1/64}")
    if any(not math.isclose(float(pixel[i]), 1 / 64, rel_tol=2e-5, abs_tol=2**-120) for i in range(3)):
        raise RuntimeError("Final consumption differs from independent C*S expectation")
    output = Path(report_path).with_suffix(".png")
    save = rd.TextureSave()
    save.resourceId = target.resource
    save.destType = rd.FileType.PNG
    save.mip = 0
    save.slice.sliceIndex = 0
    save.sample.sampleIndex = 0
    save.typeCast = rd.CompType.Float
    save.alpha = rd.AlphaMapping.Preserve
    controller.SaveTexture(save, str(output))
    if not output.is_file() or output.stat().st_size == 0:
        raise RuntimeError("Output image was not exported")
    report.append(f"output_image={output}")
    report.append("sharing_verdict=pass")


if __name__ == "__main__":
    run_ui_script("_exposure_sharing.txt", build_report)
