"""Verify canonical shadow header/arrays consumed by production lighting draws."""

import json
import math
from pathlib import Path
import struct
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "shadows"))
from renderdoc_ui_analysis import collect_action_records, renderdoc_module, run_ui_script


def build_report(controller, report, capture_path, report_path):
    rd = renderdoc_module()
    actions = collect_action_records(controller)
    observed = set()
    results = []
    for draw in actions:
        if not draw.flags & rd.ActionFlags.Drawcall:
            continue
        kind = next((kind for kind in ("Directional", "Point", "Spot")
                     if f"Vortex.Stage12.{kind}Light" in draw.path), None)
        if kind is None:
            continue
        controller.SetFrameEvent(draw.event_id, True)
        used = controller.GetPipelineState().GetReadOnlyResources(rd.ShaderStage.Pixel, True)
        by_slot = {int(x.access.arrayElement): x.descriptor for x in used
                   if x.access.index == rd.DescriptorAccess.NoShaderBinding}
        headers = [x.descriptor for x in used
                   if x.descriptor.elementByteSize == x.descriptor.byteSize == 112]
        if len(headers) != 1:
            raise RuntimeError(f"Expected one shadow header at {draw.event_id}, got {len(headers)}")
        header = headers[0]
        data = bytes(controller.GetBufferData(header.resource, header.byteOffset, 112))
        words = struct.unpack("<28I", data)
        lighting = next(x.descriptor for x in used if x.descriptor.elementByteSize == 96)
        light_words = struct.unpack("<24I", bytes(controller.GetBufferData(lighting.resource, lighting.byteOffset, 96)))
        if words[16:24] != light_words[12:20] or words[9] != light_words[10]:
            raise RuntimeError("Shadow and lighting publication identities/status differ")
        if words[10] != 0 or words[11] != 1 or any(words[24:28]) or words[8] != 0xFFFFFFFF:
            raise RuntimeError("Unexpected contact/sampling/reserved header values")
        if not all(math.isfinite(x) for x in struct.unpack_from("<4f", data, 48)):
            raise RuntimeError("Nonfinite content rectangle")

        def read_records(descriptor_slot, count, stride):
            if count == 0 or descriptor_slot not in by_slot:
                raise RuntimeError("Missing consumed shadow record array")
            descriptor = by_slot[descriptor_slot]
            if descriptor.elementByteSize != stride or descriptor.byteSize != count * stride:
                raise RuntimeError("Wrong shadow array stride/count")
            return bytes(controller.GetBufferData(descriptor.resource, descriptor.byteOffset, count * stride))

        if kind == "Directional":
            families = read_records(words[0], words[1], 16)
            cascades = read_records(words[6], words[7], 128)
            for selection, first, count, reserved in struct.iter_unpack("<4I", families):
                if selection >= light_words[4] or count == 0 or first + count > words[7] or reserved:
                    raise RuntimeError("Invalid directional family")
                for index in range(first, first + count):
                    record = cascades[index * 128:(index + 1) * 128]
                    srv, layer, r0, r1 = struct.unpack_from("<4I", record, 80)
                    if srv not in by_slot or layer == 0xFFFFFFFF or r0 or r1 or any(struct.unpack_from("<2I", record, 120)):
                        raise RuntimeError("Invalid cascade surface/layer/reserved fields")
                    if not all(math.isfinite(v) for v in struct.unpack_from("<20f", record)):
                        raise RuntimeError("Nonfinite cascade")
        else:
            descriptor, count = words[4:6] if kind == "Point" else words[2:4]
            stride, tail_offset, expected_kind = (448, 384, 0) if kind == "Point" else (128, 64, 1)
            records = read_records(descriptor, count, stride)
            local = by_slot[light_words[1]]
            for index in range(count):
                record = records[index * stride:(index + 1) * stride]
                srv, layer, selection, reserved = struct.unpack_from("<4I", record, tail_offset + 32)
                if selection >= light_words[5] or srv not in by_slot or layer == 0xFFFFFFFF or reserved:
                    raise RuntimeError("Invalid local surface/layer/selection")
                if any(struct.unpack_from("<2I", record, tail_offset + 56)):
                    raise RuntimeError("Nonzero local reserved fields")
                light = bytes(controller.GetBufferData(local.resource, local.byteOffset + selection * 80, 80))
                light_kind, flags, light_selection = struct.unpack_from("<3I", light, 56)
                if light_kind != expected_kind or light_selection != selection or struct.unpack_from("<3f", light) != struct.unpack_from("<3f", record, tail_offset):
                    raise RuntimeError("Local shadow record belongs to a different selected light")
        observed.add(kind)
        result = {"kind": kind, "event": draw.event_id,
                  "counts": {"directional": words[1], "projected": words[3], "cube": words[5], "cascades": words[7]},
                  "scene_generation": words[16:18], "view_generation": words[22:24]}
        results.append(result)
        report.append(json.dumps(result))
    if observed != {"Directional", "Point", "Spot"}:
        raise RuntimeError(f"Missing consumers: {observed}")
    Path(report_path).with_suffix(".json").write_text(json.dumps(results, indent=2))
    report.append("shadow_record_publication_verdict=pass")
    report.append("scope=canonical header/array ABI, generations and consumer routing; coverage and resource lifetime gates remain open")


if __name__ == "__main__":
    run_ui_script("_shadow_records.txt", build_report)
