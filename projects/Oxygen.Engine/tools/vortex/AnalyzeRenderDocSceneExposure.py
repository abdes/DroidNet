"""Audit the VortexBasic scene exposure domain and export mapped output."""
from pathlib import Path
import math
import struct
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "shadows"))
from renderdoc_ui_analysis import (
    collect_action_records, renderdoc_module, resource_id_to_name, run_ui_script,
)


def map_color(color, mapper, gamma):
    if mapper == 1:
        rows = ((.59719, .35458, .04823), (.076, .90834, .01566), (.0284, .13383, .83777))
        color = [sum(a * b for a, b in zip(row, color)) for row in rows]
        color = [(x * (x + .0245786) - .000090537) / (x * (.983729 * x + .4329510) + .238081) for x in color]
        rows = ((1.60475, -.53108, -.07367), (-.10208, 1.10813, -.00605), (-.00327, -.07276, 1.07602))
        color = [min(1, max(0, sum(a * b for a, b in zip(row, color)))) for row in rows]
    elif mapper == 2:
        def film(x):
            return (x * (.15 * x + .05) + .004) / (x * (.15 * x + .5) + .06) - .02 / .3
        color = [film(2 * x) / film(11.2) for x in color]
    elif mapper == 3:
        color = [x / (x + 1) for x in color]
    else:
        color = [min(1, max(0, x)) for x in color]
    return [max(0, x) ** (1 / gamma) for x in color]


def select_scene_source(controller, reads, textures, names, constants):
    sources = [x for x in reads if str(x.resource) in textures]
    report_slot = struct.unpack_from("<I", constants, 52)[0]
    if report_slot != 0xffffffff:
        reports = [x for x in reads if names.get(str(x.resource)) == "Vortex.Exposure.Suitability"]
        if len(reports) != 1:
            raise RuntimeError("Checked tonemap requires its GPU conversion report")
        raw = bytes(controller.GetBufferData(reports[0].resource, reports[0].byteOffset, 48))
        checked, failed = struct.unpack_from("<2I", raw, 8)
        expected = struct.unpack_from("<I", raw, 40)[0]
        byte_width = 2 if failed == 0 and checked == expected == 1024 else 4
        sources = [x for x in sources if textures[str(x.resource)].format.compByteWidth == byte_width]
    if len(sources) != 1:
        raise RuntimeError("Missing/ambiguous selected tonemap scene source")
    return sources[0]


def build_report(controller, report, capture_path, report_path):
    rd = renderdoc_module()
    names = resource_id_to_name(controller)
    textures = {str(t.resourceId): t for t in controller.GetTextures()}
    actions = collect_action_records(controller)
    tone = [a for a in actions if a.flags & rd.ActionFlags.Drawcall and "Vortex.PostProcess.Tonemap" in a.path]
    if len(tone) != 1:
        raise RuntimeError("Expected one mapped scene view")
    controller.SetFrameEvent(tone[0].event_id, True)
    pipeline = controller.GetPipelineState()
    reads = [x.descriptor for x in pipeline.GetReadOnlyResources(rd.ShaderStage.Pixel, True)]
    constants = [x for x in reads if x.byteSize == 64 and x.elementByteSize == 64]
    frames = [x for x in reads if names.get(str(x.resource)) == "Vortex.PostProcess.Exposure.Frame"]
    states = [x for x in reads if names.get(str(x.resource)) == "Vortex.PostProcess.Exposure.State"]
    if len(constants) != 1 or len(frames) != 1 or len(states) != 1:
        raise RuntimeError("Missing/ambiguous scene, frame, state or pass constants")
    pc = bytes(controller.GetBufferData(constants[0].resource, constants[0].byteOffset, 64))
    source = select_scene_source(controller, reads, textures, names, pc)
    mapper = struct.unpack_from("<I", pc, 12)[0]
    gamma = struct.unpack_from("<f", pc, 20)[0]
    bloom = struct.unpack_from("<f", pc, 24)[0]
    background = struct.unpack_from("<3fI", pc, 32)
    if bloom != 0:
        raise RuntimeError("This fixture oracle requires bloom disabled")
    p, inverse_p, state_slot, flags = struct.unpack("<2f2I", bytes(controller.GetBufferData(frames[0].resource, frames[0].byteOffset, 16)))
    gain = struct.unpack("<f", bytes(controller.GetBufferData(states[0].resource, states[0].byteOffset, 4)))[0]
    if p != 1 or inverse_p != 1 or flags & 1 == 0:
        raise RuntimeError("Unqualified scene did not stay in the FP32 bootstrap domain")
    texture = textures[str(source.resource)]
    if texture.format.compCount != 4 or texture.format.compByteWidth != 4:
        raise RuntimeError("SceneColor was narrowed before metering/tonemapping")
    x, y = texture.width // 2, texture.height // 2
    sub = rd.Subresource()
    sub.mip = sub.slice = sub.sample = 0
    scene = list(controller.PickPixel(source.resource, x, y, sub, rd.CompType.Float).floatValue)
    coverage = min(1, max(0, scene[3])) if background[3] else 1
    if coverage != 1:
        raise RuntimeError("Center probe must be opaque for this independent oracle")
    expected = map_color([c * gain / p for c in scene[:3]], mapper, gamma)
    bayer = (0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5)
    dither = (bayer[(x & 3) | ((y & 3) << 2)] / 16 - .5) / 255
    expected = [min(1, max(0, c + dither)) for c in expected]
    target = pipeline.GetOutputTargets()[0]
    actual = list(controller.PickPixel(target.resource, x, y, sub, rd.CompType.Float).floatValue)
    if any(not math.isfinite(c) or abs(c - e) > 1 / 255 for c, e in zip(actual[:3], expected)):
        raise RuntimeError(f"Final scene S/P mismatch: {actual} versus {expected}")
    report.append(f"tonemap={tone[0].event_id} P={p} inverseP={inverse_p} S={gain} flags={flags} current_slot={state_slot}")
    report.append(f"probe=({x},{y}) source={scene} expected={expected} actual={actual}")
    required = ("AtmosphereSkyViewLut", "AtmosphereCameraAerialPerspective", "IntegratedLightScattering")
    for product in required:
        found = [t for key, t in textures.items() if product in names.get(key, "")]
        if not found or any(t.format.compCount != 4 or t.format.compByteWidth != 4 for t in found):
            raise RuntimeError(f"Missing/narrowed FP32 radiance product: {product}")
        report.append(f"product={product} allocations={len(found)} format=RGBA32F")
    output = Path(report_path).with_suffix(".png")
    save = rd.TextureSave()
    save.resourceId = target.resource
    save.destType = rd.FileType.PNG
    save.mip = 0
    save.slice.sliceIndex = save.sample.sampleIndex = 0
    save.typeCast = rd.CompType.Float
    save.alpha = rd.AlphaMapping.Preserve
    controller.SaveTexture(save, str(output))
    report.append(f"output_image={output}")
    for scope, product in (("Vortex.Stage15.Fog", "IntegratedLightScattering"),
                           ("Vortex.Stage15.Atmosphere", "AtmosphereCameraAerialPerspective")):
        consumers = [a for a in actions if a.flags & rd.ActionFlags.Drawcall and scope in a.path]
        if len(consumers) != 1:
            raise RuntimeError(f"Expected one consumer draw for {product}")
        controller.SetFrameEvent(consumers[0].event_id, True)
        used = [x.descriptor for x in controller.GetPipelineState().GetReadOnlyResources(rd.ShaderStage.Pixel, True)]
        matches = [x for x in used if product in names.get(str(x.resource), "")]
        if not matches:
            raise RuntimeError(f"The pixel shader did not consume {product}")
        report.append(f"consumer={scope} event={consumers[0].event_id} product={product} resources={[str(x.resource) for x in matches]}")
    report.append("scene_exposure_verdict=pass")


if __name__ == "__main__":
    run_ui_script("_scene_exposure.txt", build_report)
