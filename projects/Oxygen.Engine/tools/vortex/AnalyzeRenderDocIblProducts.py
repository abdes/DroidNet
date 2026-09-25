"""Check the controlled constant-cube IBL producer capture and publication order."""

import json
import math
import os
from pathlib import Path
import struct
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "shadows"))
from renderdoc_ui_analysis import collect_action_records, renderdoc_module, run_ui_script


def build_report(controller, report, capture_path, report_path):
    rd = renderdoc_module()
    surface = os.environ.get("OXYGEN_RENDERDOC_PASS_NAME") == "IblSurface"
    fog_capture = os.environ.get("OXYGEN_RENDERDOC_PASS_NAME") == "IblFogCapture"
    size, mips, revision = (16, 5, 42) if surface else (128, 8, 31)
    expected_rgb = (2.0, 0.5, 1.0) if surface else (1.0, 0.5, 0.125)
    source_scale = 1.0
    if fog_capture:
        size, mips, revision = 128, 8, 201
        source_scale = struct.unpack("<f", struct.pack("<f", 7.5e7 / 65504))[0]
        expected_rgb = tuple(v / source_scale for v in (7.5e7, 1.5e6, 6))
    resources = {resource.name: resource.resourceId for resource in controller.GetResources()}
    textures = {str(texture.resourceId): texture for texture in controller.GetTextures()}
    required = ("IBL.ProcessedCube", "IBL.SpecularCube", "IBL.DiffuseSH", "IBL.Metadata")
    if any(name not in resources for name in required):
        raise RuntimeError("Missing named IBL products")
    paired = "IBL.ProcessedHalfCube" in resources and "IBL.SpecularHalfCube" in resources
    metadata_id = resources["IBL.Metadata"]
    dispatches = []
    surface_event = None
    for action in collect_action_records(controller):
        if not action.flags & rd.ActionFlags.Dispatch:
            continue
        controller.SetFrameEvent(action.event_id, True)
        shader = controller.GetPipelineState().GetShaderReflection(rd.ShaderStage.Compute)
        if surface and shader and shader.entryPoint == "CS":
            surface_event = action.event_id
        if shader and shader.entryPoint.startswith("Ibl"):
            dispatches.append((shader.entryPoint, action.event_id))
    prepare = "IblCapturePrepareCS" if fog_capture else "IblPrepareCS"
    expected_order = (["IblInitializeCS", prepare, "IblRangeCS", "IblNormalizeCS"]
                      + ["IblMipCS"] * (mips - 1) + ["IblShCS", "IblShReduceCS"]
                      + ["IblPrefilterCS"] * mips)
    if paired:
        expected_order += (["IblNarrowCS"] * (2 * mips)
                           + ["IblPrecisionRangeCS", "IblPrecisionReduceCS"])
    expected_order += ["IblCompleteCS"]
    if [name for name, _ in dispatches] != expected_order:
        raise RuntimeError("Incorrect producer order: " + repr(dispatches))

    publication = []
    precision = None
    for name, event in dispatches:
        controller.SetFrameEvent(event, True)
        scale, brightness, flags, bound_revision = struct.unpack(
            "<ffII", bytes(controller.GetBufferData(metadata_id, 0, 16)))
        expected_flags = (0 if name in ("IblInitializeCS", prepare)
                          else 3 if name == "IblCompleteCS" else 1)
        expected_scale = 1.0 if expected_flags == 0 else source_scale
        if flags != expected_flags or bound_revision != revision or scale != expected_scale:
            raise RuntimeError("Premature/incoherent metadata at {}: {}".format(event, (scale, flags, bound_revision)))
        if paired:
            gain, precision_flags, processed_half, specular_half = struct.unpack(
                "<fIII", bytes(controller.GetBufferData(metadata_id, 16, 16)))
            if precision_flags != (1 if name == "IblCompleteCS" else 0):
                raise RuntimeError("Premature/missing precision qualification")
            if name == "IblCompleteCS":
                if not math.isfinite(gain) or gain < 0 or any(slot == 0xffffffff for slot in (processed_half, specular_half)):
                    raise RuntimeError("Invalid half certificate")
                if not fog_capture and gain < 2 ** 32:
                    raise RuntimeError("Constant cube is not admitted to half sampling")
                precision = {"maximum_half_gain": gain, "flags": precision_flags,
                             "processed_half_srv": processed_half, "specular_half_srv": specular_half}
        publication.append({"event": event, "producer": name, "flags": flags, "revision": revision})

    controller.SetFrameEvent(dispatches[-1][1], True)
    maximum_error = 0.0
    scalar_checks = 0
    cubes = ("IBL.ProcessedCube", "IBL.SpecularCube")
    if paired:
        cubes += ("IBL.ProcessedHalfCube", "IBL.SpecularHalfCube")
    for name in cubes:
        resource = resources[name]
        texture = textures[str(resource)]
        if (texture.width, texture.height, texture.arraysize, texture.mips) != (size, size, 6, mips):
            raise RuntimeError("Incorrect dimensions for " + name)
        expected_width = 4 if paired and "Half" not in name else 2
        if texture.format.compByteWidth != expected_width or texture.format.compCount != 4:
            raise RuntimeError("Incorrect storage format for " + name)
        for face in range(6):
            for mip in range(mips):
                sub = rd.Subresource()
                sub.slice = face
                sub.mip = mip
                raw = bytes(controller.GetTextureData(resource, sub))
                values = list(struct.iter_unpack("<4f" if expected_width == 4 else "<4e", raw))
                if len(values) != (size >> mip) ** 2:
                    raise RuntimeError("Incorrect subresource length")
                for pixel in values:
                    for actual, expected in zip(pixel, expected_rgb + (1.0,)):
                        error = abs(actual - expected)
                        tolerance = (2 * 2 ** max(-24, math.floor(math.log2(expected)) - 10)
                                     if fog_capture and expected > 0 else 0.001)
                        if not math.isfinite(actual) or error > tolerance:
                            raise RuntimeError("Incorrect {} face {} mip {}: {}".format(name, face, mip, pixel))
                        maximum_error = max(maximum_error, error)
                        scalar_checks += 1
    sh = struct.unpack("<32f", bytes(controller.GetBufferData(resources["IBL.DiffuseSH"], 0, 128)))
    if any(not math.isfinite(value) for value in sh):
        raise RuntimeError("Non-finite SH")
    for channel, expected in enumerate(expected_rgb):
        # N=(0,0,1): linear.z + constant + quadratic.z.
        value = sh[channel * 4 + 2] + sh[channel * 4 + 3] + sh[(channel + 3) * 4 + 2]
        if abs(value - expected) > 0.001 * (max(1, expected) if fog_capture else 1):
            raise RuntimeError("Incorrect diffuse normalization")
    surface_checks = 0
    if surface:
        if surface_event is None:
            raise RuntimeError("Missing native surface evaluation")
        controller.SetFrameEvent(surface_event, True)
        lut_id = resources["IBL.BrdfLookup"]
        lut_desc = textures[str(lut_id)]
        if (lut_desc.width, lut_desc.height, lut_desc.mips, lut_desc.format.compByteWidth) != (128, 32, 1, 2):
            raise RuntimeError("Incorrect BRDF lookup format")
        lut = list(struct.iter_unpack("<2H", bytes(controller.GetTextureData(lut_id, rd.Subresource()))))
        records = bytes(controller.GetBufferData(resources["Lighting ABI records"], 0, 0))
        output = bytes(controller.GetBufferData(resources["Lighting ABI decoded"], 0, 0))
        if len(records) != 36 * 144 or len(output) != 36 * 24:
            raise RuntimeError("Incomplete material/view/roughness cases")
        for index in range(36):
            offset = index * 144
            tint = struct.unpack_from("<3f", records, offset)
            scale, diffuse_gain, specular_gain = struct.unpack_from("<3f", records, offset + 12)
            roughness = struct.unpack_from("<f", records, offset + 92)[0]
            nv, metallic = struct.unpack_from("<2f", records, offset + 104)
            base = struct.unpack_from("<3f", records, offset + 112)
            occlusion = struct.unpack_from("<f", records, offset + 124)[0]
            f0 = struct.unpack_from("<3f", records, offset + 128)
            a, b = [v / 65535.0 for v in lut[int(roughness * 32) * 128 + int(nv * 128)]]
            gain = [expected_rgb[c] * tint[c] * scale for c in range(3)]
            expected = [gain[c] * base[c] * (1-metallic) * occlusion * diffuse_gain for c in range(3)]
            expected += [gain[c] * specular_gain * (f0[c]*a + min(1,50*f0[1])*b) for c in range(3)]
            actual = struct.unpack_from("<6f", output, index * 24)
            for value, reference in zip(actual, expected):
                if not math.isfinite(value) or abs(value-reference) > 0.001 * max(0.01, reference):
                    raise RuntimeError("Incorrect surface result: {} vs {}".format(actual, expected))
                surface_checks += 1
    result = {"verdict": "pass", "capture": str(capture_path),
              "dispatches": publication, "scalar_checks": scalar_checks,
              "surface_scalar_checks": surface_checks, "surface_event": surface_event,
              "maximum_error": maximum_error,
              "source_radiance_scale": source_scale, "paired_precision": precision,
              "complete_products_event": dispatches[-1][1]}
    Path(report_path).with_suffix(".json").write_text(json.dumps(result, indent=2) + "\n")
    report.append("ibl_product_verdict=pass")
    report.append("ibl_dispatches={}".format(len(dispatches)))
    report.append("ibl_scalar_checks={}".format(scalar_checks))


if __name__ == "__main__":
    run_ui_script("_ibl_products.txt", build_report)
