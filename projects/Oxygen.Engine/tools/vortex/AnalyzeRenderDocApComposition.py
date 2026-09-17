"""Check the native AP fixture's real LUT inputs and blended RGBA outputs."""

from pathlib import Path
import struct
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "shadows"))
from renderdoc_ui_analysis import collect_action_records, renderdoc_module, run_ui_script


def build_report(controller, report, capture_path, report_path):
    rd = renderdoc_module()
    textures = {str(t.resourceId): t for t in controller.GetTextures()}
    draws = 0
    formats = set()
    maximum_error = 0.0
    for action in collect_action_records(controller):
        if not action.flags & rd.ActionFlags.Drawcall:
            continue
        controller.SetFrameEvent(action.event_id, True)
        pipeline = controller.GetPipelineState()
        reflection = pipeline.GetShaderReflection(rd.ShaderStage.Pixel)
        if not reflection or reflection.entryPoint != "VortexAtmosphereComposePS":
            continue
        sources = [x.descriptor.resource for x in
                   pipeline.GetReadOnlyResources(rd.ShaderStage.Pixel, True)
                   if str(x.descriptor.resource) in textures
                   and textures[str(x.descriptor.resource)].dimension == 3]
        if len(sources) != 1:
            raise RuntimeError(f"Expected one AP volume, found {sources}")
        texture = textures[str(sources[0])]
        component_bytes = texture.format.compByteWidth
        if (texture.width, texture.height, texture.depth) != (1, 1, 1) or component_bytes not in (2, 4):
            raise RuntimeError("Unexpected AP fixture volume")
        raw = bytes(controller.GetTextureData(sources[0], rd.Subresource()))
        sample = struct.unpack("<4e" if component_bytes == 2 else "<4f", raw)
        target = pipeline.GetOutputTargets()[0].resource
        actual_raw = bytes(controller.GetTextureData(target, rd.Subresource()))
        actual = list(struct.iter_unpack("<4f", actual_raw))
        controller.SetFrameEvent(action.event_id - 1, True)
        before_raw = bytes(controller.GetTextureData(target, rd.Subresource()))
        before = list(struct.iter_unpack("<4f", before_raw))
        if len(actual) != 16 or len(before) != 16:
            raise RuntimeError("Expected a 4x4 accumulated target")
        for old, result in zip(before, actual):
            expected = tuple(sample[c] + old[c] * sample[3] for c in range(3)) + (
                1 - sample[3] + old[3] * sample[3],)
            error = max(abs(a - b) for a, b in zip(result, expected))
            maximum_error = max(maximum_error, error)
            if error > 3e-7:
                raise RuntimeError(f"AP event {action.event_id}: {result} != {expected}")
        report.append(f"event={action.event_id} component_bytes={component_bytes} "
                      f"sample={sample} before={before[0]} after={actual[0]}")
        formats.add(component_bytes)
        draws += 1
    if draws != 72 or formats != {2, 4}:
        raise RuntimeError(f"Incomplete AP matrix: draws={draws}, formats={formats}")
    report.append(f"draws={draws} rgba_pixels={draws * 16} maximum_absolute_error={maximum_error}")
    report.append("ap_composition_verdict=pass")
    report.append("scope=actual deferred AP shader/blend against independent forward transfer equation; not a forward raster fixture")


if __name__ == "__main__":
    run_ui_script("_ap_composition.txt", build_report)
