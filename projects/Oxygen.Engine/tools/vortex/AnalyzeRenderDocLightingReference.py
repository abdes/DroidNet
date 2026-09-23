"""Inspect the 33-light forward frame from the native serial-image fixture."""

from collections import Counter
import hashlib
import json
import math
from pathlib import Path
import struct
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "shadows"))
from renderdoc_ui_analysis import collect_action_records, renderdoc_module, run_ui_script


def build_report(controller, report, capture_path, report_path):
    rd = renderdoc_module()
    checked = 0
    for draw in collect_action_records(controller):
        if not draw.flags & rd.ActionFlags.Drawcall or "Vortex.Stage9.BasePass.Forward" not in draw.path:
            continue
        controller.SetFrameEvent(draw.event_id, True)
        pipeline = controller.GetPipelineState()
        used = pipeline.GetReadOnlyResources(rd.ShaderStage.Pixel, True)
        by_slot = {
            int(item.access.arrayElement): item.descriptor for item in used
            if item.access.index == rd.DescriptorAccess.NoShaderBinding
        }
        headers = []
        for item in used:
            descriptor = item.descriptor
            if descriptor.elementByteSize == descriptor.byteSize == 96:
                words = struct.unpack("<24I", bytes(controller.GetBufferData(
                    descriptor.resource, descriptor.byteOffset, 96)))
                if words[4] == 0 and words[5] == 33:
                    headers.append(words)
        if len(headers) != 1:
            raise RuntimeError("Expected one canonical 33-local-light publication")
        words = headers[0]
        manifest_path = Path(__file__).resolve().parents[2] / "src/Oxygen/Vortex/Lighting/Data/GgxModel1.json"
        model = json.loads(manifest_path.read_text())
        if words[23] != model["model_revision"]:
            raise RuntimeError("Lighting publication has the wrong BRDF model")
        model_payload = bytearray()
        for slot, width in ((words[21], model["view_nodes"]), (words[22], 1)):
            descriptor = by_slot[slot]
            texture = next(item for item in controller.GetTextures() if item.resourceId == descriptor.resource)
            if (texture.width, texture.height) != (width, model["roughness_nodes"]):
                raise RuntimeError("BRDF moment texture extent mismatch")
            data = bytes(controller.GetTextureData(descriptor.resource, rd.Subresource()))
            if len(data) != width * model["roughness_nodes"] * 8:
                raise RuntimeError("BRDF moment texture format mismatch")
            model_payload.extend(data)
        if hashlib.sha256(model_payload).hexdigest() != model["payload_sha256"]:
            raise RuntimeError("Production GPU moment contents differ from the model payload")
        report.append(f"brdf_model_revision={words[23]} brdf_payload_sha256={model['payload_sha256']} gpu_payload_bytes={len(model_payload)}")
        local = by_slot[words[1]]
        if local.elementByteSize != 80 or local.byteSize != 33 * 80:
            raise RuntimeError("Local-light stride or count mismatch")
        records = bytes(controller.GetBufferData(local.resource, local.byteOffset, local.byteSize))
        kinds = Counter(struct.unpack_from("<I", records, index * 80 + 56)[0] for index in range(33))
        selections = [struct.unpack_from("<I", records, index * 80 + 64)[0] for index in range(33)]
        if kinds != {0: 17, 1: 16} or sorted(selections) != list(range(33)):
            raise RuntimeError("Missing, duplicated or mistyped light record")
        corrected_spots = 0
        for index in range(33):
            kind = struct.unpack_from("<I", records, index * 80 + 56)[0]
            high = struct.unpack_from("<2f", records, index * 80 + 48)
            corrections = struct.unpack_from("<2f", records, index * 80 + 68)
            reserved = struct.unpack_from("<I", records, index * 80 + 76)[0]
            if reserved or not all(math.isfinite(v) and abs(v) <= 2**-24 for v in corrections):
                raise RuntimeError("Invalid cone precision or reserved lane")
            if kind == 0:
                if any(high) or any(corrections):
                    raise RuntimeError("Point record contains cone parameters")
                continue
            for encoded, correction, angle in zip(high, corrections, (0.2, 1.2)):
                authored = struct.unpack("<f", struct.pack("<f", angle))[0]
                expected = math.sin(authored / 2)**2
                if abs(encoded * (1 + correction) - expected) > expected * 1e-14:
                    raise RuntimeError("Spot record lost authored cone precision")
            corrected_spots += 1
        ranges = by_slot[words[2]]
        if ranges.elementByteSize != 8 or ranges.byteSize != words[6] * 8:
            raise RuntimeError("Cluster range buffer is incomplete")
        data = bytes(controller.GetBufferData(ranges.resource, ranges.byteOffset, ranges.byteSize))
        if any(pair != (0xFFFFFFFF, 33) for pair in struct.iter_unpack("<2I", data)):
            raise RuntimeError("Fixture capture is not using complete unculled lists")
        target = pipeline.GetOutputTargets()[0].resource
        texture = next(item for item in controller.GetTextures() if item.resourceId == target)
        if (texture.width, texture.height) != (96, 64):
            raise RuntimeError("Unexpected fixture image extent")
        pixels = bytes(controller.GetTextureData(target, rd.Subresource()))
        if len(pixels) != 96 * 64 * 16:
            raise RuntimeError("Expected an RGBA32 float scene image")
        values = list(struct.iter_unpack("<4f", pixels))
        if not all(math.isfinite(value) for pixel in values for value in pixel):
            raise RuntimeError("Nonfinite scene image")
        peak = max(max(pixel[:3]) for pixel in values)
        if peak <= 0:
            raise RuntimeError("Empty lighting image")
        output = Path(report_path).with_suffix(".rgba32f")
        output.write_bytes(pixels)
        report.append(f"event={draw.event_id} points=17 spots=16 corrected_spots={corrected_spots} complete_cells={words[6]} peak={peak}")
        report.append(f"scene_color_rgba32f={output}")
        checked += 1
    if checked != 1:
        raise RuntimeError(f"Expected one forward fixture draw, got {checked}")
    report.append("lighting_reference_capture_verdict=pass")
    report.append("scope=complete-list wiring, exact moment upload and finite lit output; furnace/reciprocity and full lighting qualification remain separate")


if __name__ == "__main__":
    run_ui_script("_lighting_reference.txt", build_report)
