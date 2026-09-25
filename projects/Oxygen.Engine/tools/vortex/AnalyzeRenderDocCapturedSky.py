"""Check native global-sky capture, frozen LUT copies and complete HDR products."""

import json
import math
from pathlib import Path
import struct
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "shadows"))
from renderdoc_ui_analysis import collect_action_records, renderdoc_module, run_ui_script


def build_report(controller, report, capture_path, report_path):
    rd = renderdoc_module()
    textures = {str(t.resourceId): t for t in controller.GetTextures()}
    resources = {r.name: r.resourceId for r in controller.GetResources()}
    actions = []
    for action in collect_action_records(controller):
        if not action.flags & rd.ActionFlags.Dispatch:
            continue
        controller.SetFrameEvent(action.event_id, True)
        shader = controller.GetPipelineState().GetShaderReflection(rd.ShaderStage.Compute)
        if shader:
            actions.append((shader.entryPoint, action.event_id))
    sky_events = [e for n, e in actions if n == "VortexAtmosphereCaptureSkyViewLutCS"]
    ibl = [(n, e) for n, e in actions if n.startswith("Ibl")]
    paired = "IBL.ProcessedHalfCube" in resources and "IBL.SpecularHalfCube" in resources
    expected_order = (["IblInitializeCS", "IblCapturePrepareCS", "IblRangeCS", "IblNormalizeCS"]
                      + ["IblMipCS"] * 7 + ["IblShCS", "IblShReduceCS"]
                      + ["IblPrefilterCS"] * 8)
    if paired:
        expected_order += (["IblNarrowCS"] * 16
                           + ["IblPrecisionRangeCS", "IblPrecisionReduceCS"])
    expected_order += ["IblCompleteCS"]
    if len(sky_events) != 1 or [n for n, _ in ibl] != expected_order:
        raise RuntimeError(f"Unexpected capture pipeline: {actions}")

    def descriptors(event, write=False):
        controller.SetFrameEvent(event, True)
        pipeline = controller.GetPipelineState()
        return [x.descriptor for x in (pipeline.GetReadWriteResources(rd.ShaderStage.Compute, True)
                if write else pipeline.GetReadOnlyResources(rd.ShaderStage.Compute, True))]

    def texture_output(event, byte_width=None):
        found = [x.resource for x in descriptors(event, True) if str(x.resource) in textures
                 and (byte_width is None or textures[str(x.resource)].format.compByteWidth == byte_width)]
        if len(found) != 1:
            raise RuntimeError(f"Expected one texture output at {event}")
        return found[0]

    def pixels(resource, face=0, mip=0):
        sub = rd.Subresource()
        sub.slice, sub.mip = face, mip
        kind = "f" if textures[str(resource)].format.compByteWidth == 4 else "e"
        return list(struct.iter_unpack("<4" + kind, bytes(controller.GetTextureData(resource, sub))))

    sky = texture_output(sky_events[0])
    shader = controller.GetPipelineState().GetShaderReflection(rd.ShaderStage.Compute)
    constant_blocks = [b.name for b in shader.constantBlocks]
    if any("ViewConstants" in name for name in constant_blocks):
        raise RuntimeError("Capture shader still consumes camera/view constants")
    source_sky = bytes(controller.GetTextureData(sky, rd.Subresource()))
    source_distant_event = next(e for n, e in actions if n == "VortexDistantSkyLightLutCS")
    distant = descriptors(source_distant_event, True)[0]
    source_distant = bytes(controller.GetBufferData(distant.resource, 0, 16))
    prepare = next(e for n, e in ibl if n == "IblCapturePrepareCS")
    reads = descriptors(prepare)
    frozen_sky = next(x.resource for x in reads if str(x.resource) in textures)
    copies = []
    def collect_copies(action):
        if action.flags & rd.ActionFlags.Copy and action.eventId < prepare:
            copies.append(action)
        for child in action.children:
            collect_copies(child)
    for action in controller.GetRootActions():
        collect_copies(action)
    distant_copies = [a for a in copies if str(a.copySource) == str(distant.resource)]
    if len(distant_copies) != 1:
        raise RuntimeError("Missing unique distant-light snapshot copy")
    frozen_distant = distant_copies[0].copyDestination
    if str(frozen_sky) == str(sky) or str(frozen_distant) == str(distant.resource):
        raise RuntimeError("Candidate aliases a mutable source LUT")
    if bytes(controller.GetTextureData(frozen_sky, rd.Subresource())) != source_sky:
        raise RuntimeError("Frozen sky LUT differs from the submitted source")
    if bytes(controller.GetBufferData(frozen_distant, 0, 16)) != source_distant:
        raise RuntimeError("Frozen distant light differs from the submitted source")
    scratch = texture_output(prepare)
    processed = resources["IBL.ProcessedCube"]
    specular = texture_output(next(e for n, e in ibl if n == "IblPrefilterCS"))
    metadata = resources["IBL.Metadata"]
    publication = []
    for name, event in ibl:
        controller.SetFrameEvent(event, True)
        scale, brightness, flags, revision = struct.unpack("<ffII", bytes(controller.GetBufferData(metadata, 0, 16)))
        expected = 0 if name in expected_order[:2] else 3 if name == "IblCompleteCS" else 1
        if flags != expected or revision != 3 or not math.isfinite(scale) or scale < 1:
            raise RuntimeError(f"Invalid publication at {event}")
        publication.append({"event": event, "producer": name, "flags": flags})
    if not math.isfinite(brightness) or brightness <= 0:
        raise RuntimeError("Captured sky has no finite lighting energy")
    checked = 0
    products = (processed, specular)
    half_products = (resources["IBL.ProcessedHalfCube"], resources["IBL.SpecularHalfCube"]) if paired else ()
    for resource in products + half_products:
        desc = textures[str(resource)]
        if (desc.width, desc.height, desc.arraysize, desc.mips, desc.format.compByteWidth) != (128, 128, 6, 8, 4 if paired and resource in products else 2):
            raise RuntimeError("Wrong captured product shape/format")
        for face in range(6):
            previous = None
            for mip in range(8):
                actual = pixels(resource, face, mip)
                size = 128 >> mip
                if len(actual) != size * size:
                    raise RuntimeError("Incomplete product subresource")
                reference = pixels(scratch, face) if mip == 0 and resource == processed else None
                for i, pixel in enumerate(actual):
                    if any(not math.isfinite(v) or v < 0 for v in pixel) or pixel[3] != 1:
                        raise RuntimeError("Invalid captured product texel")
                    if resource == processed:
                        if mip == 0:
                            expected_rgb = [v / scale for v in reference[i][:3]]
                        else:
                            x, y = i % size, i // size
                            children = [(2*y+dy)*(2*size)+2*x+dx for dy in (0, 1) for dx in (0, 1)]
                            expected_rgb = [sum(previous[j][c] for j in children) / 4 for c in range(3)]
                        for a, e in zip(pixel[:3], expected_rgb):
                            exponent, mantissa = (-149, 23) if paired else (-24, 10)
                            ulp = 2 ** max(exponent, math.floor(math.log2(e)) - mantissa) if e > 0 else 0
                            if paired:
                                ulp *= 4
                            if abs(a-e) > ulp:
                                raise RuntimeError("Captured source normalization/mip mismatch")
                    if resource in half_products:
                        canonical = products[half_products.index(resource)]
                        if i == 0:
                            canonical_pixels = pixels(canonical, face, mip)
                        expected_half = struct.unpack("<4e", struct.pack("<4e", *canonical_pixels[i]))
                        if pixel != expected_half:
                            raise RuntimeError("Half product is not nearest-even canonical narrowing")
                    checked += 4
                previous = actual
    result = {"verdict": "pass", "capture": str(capture_path), "unit_sky_event": sky_events[0],
              "capture_constant_blocks": constant_blocks, "frozen_lut_bytes": len(source_sky) + 16,
              "source_radiance_scale": scale, "average_brightness": brightness,
              "publication": publication, "product_scalars": checked,
              "scope": "Frozen LUT identity, HDR normalization, mip chain and publication; SH/GGX numerical qualification uses native tests"}
    Path(report_path).with_suffix(".json").write_text(json.dumps(result, indent=2) + "\n")
    report.append(f"captured_sky_verdict=pass frozen_lut_bytes={len(source_sky)+16} product_scalars={checked}")


if __name__ == "__main__":
    run_ui_script("_captured_sky.txt", build_report)
