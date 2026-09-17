"""Verify current-frame FP32 selection after the checked FP16 resolve rejects."""

from pathlib import Path
import struct
import sys

sys.path.insert(0, str(Path(__file__).resolve().parent))
from AnalyzeRenderDocSceneExposure import select_scene_source
from renderdoc_ui_analysis import (
    collect_action_records, renderdoc_module, resource_id_to_name, run_ui_script,
)


def build_report(controller, report, capture_path, report_path):
    rd = renderdoc_module()
    names = resource_id_to_name(controller)
    textures = {str(t.resourceId): t for t in controller.GetTextures()}
    tones = [a for a in collect_action_records(controller)
             if a.flags & rd.ActionFlags.Drawcall and "Vortex.PostProcess.Tonemap" in a.path]
    if len(tones) != 1:
        raise RuntimeError("Expected one checked tonemap draw")
    controller.SetFrameEvent(tones[0].event_id, True)
    pipeline = controller.GetPipelineState()
    reads = [x.descriptor for x in pipeline.GetReadOnlyResources(rd.ShaderStage.Pixel, True)]
    constants = [x for x in reads if x.byteSize == 64 and x.elementByteSize == 64]
    reports = [x for x in reads if names.get(str(x.resource)) == "Vortex.Exposure.Conversion"]
    if len(constants) != 1 or len(reports) != 1:
        raise RuntimeError("Missing checked tonemap constants/report")
    pc = bytes(controller.GetBufferData(constants[0].resource, constants[0].byteOffset, 64))
    fallback, status, reserved0, reserved1 = struct.unpack_from("<4I", pc, 48)
    if fallback == 0xffffffff or status == 0xffffffff or reserved0 or reserved1:
        raise RuntimeError("Checked tonemap constants ABI mismatch")
    values = struct.unpack("<2f10I", bytes(controller.GetBufferData(reports[0].resource, 0, 48)))
    if values != (1.0, 1048576.0, 1024, 2, 11, 0, 0, 0, 1, 16, 1024, 0):
        raise RuntimeError(f"Expected the fixture's single overflow failure: {values}")
    future_reports = []
    for action in collect_action_records(controller):
        if not action.flags & rd.ActionFlags.Dispatch or action.event_id >= tones[0].event_id:
            continue
        controller.SetFrameEvent(action.event_id, True)
        future_reports.extend(x.descriptor for x in controller.GetPipelineState().GetReadOnlyResources(rd.ShaderStage.Compute, True)
                              if names.get(str(x.descriptor.resource)) == "Vortex.Exposure.Suitability")
    if len(future_reports) != 1 or future_reports[0].resource == reports[0].resource:
        raise RuntimeError("Expected a distinct future-candidate report consumed by the finalizer")
    controller.SetFrameEvent(tones[0].event_id, True)
    future = struct.unpack("<2f10I", bytes(controller.GetBufferData(future_reports[0].resource, 0, 48)))
    if future[2] != 1024 or future[3] != 0 or future[10] != 1024:
        raise RuntimeError(f"Expected a qualifying future candidate alongside the rejected current conversion: {future}")
    source = select_scene_source(controller, reads, textures, names, pc)
    if textures[str(source.resource)].format.compByteWidth != 4:
        raise RuntimeError("A rejected resolve must select FP32 accumulation")
    target = pipeline.GetOutputTargets()[0].resource
    pixel = controller.PickPixel(target, 1, 0, rd.Subresource(), rd.CompType.Float).floatValue
    if max(abs(pixel[c] - 1 / 3) for c in range(3)) > 1e-7:
        raise RuntimeError(f"Tonemap sampled rejected half contents instead of FP32: {pixel}")
    report.append(f"tone={tones[0].event_id} source={source.resource} rejection={values[3]} output_rgb={list(pixel[:3])}")
    report.append(f"future_candidate_P={future[0]} future_failure={future[3]} conversion_report_is_separate=true")
    report.append("checked_tonemap_verdict=pass")
    report.append("scope=GPU source selection after rejection; automatic format admission remains separate")


if __name__ == "__main__":
    run_ui_script("_checked_tonemap.txt", build_report)
