"""Qualify canonical point/spot bindings in the MultiView forward preview."""

import math
import os
from pathlib import Path
import struct
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "shadows"))
from renderdoc_ui_analysis import (
    collect_action_records, renderdoc_module, resource_id_to_name, run_ui_script,
)


def build_report(controller, report, capture_path, report_path):
    cases = {
        "ForwardLocalPoint": [0],
        "ForwardLocalSpot": [1],
        "ForwardLocalBoth": [0, 1],
        "ForwardLocalNone": [],
    }
    case = os.environ.get("OXYGEN_RENDERDOC_PASS_NAME")
    if case not in cases:
        raise RuntimeError("PassName must identify a ForwardLocalPoint/Spot/Both/None case")
    expected = cases[case]
    rd = renderdoc_module()
    actions = collect_action_records(controller)
    draws = [a for a in actions if a.flags & rd.ActionFlags.Drawcall
             and "Vortex.Stage9.BasePass.Forward" in a.path]
    if not draws:
        raise RuntimeError("No real forward scene draws")
    names = resource_id_to_name(controller)
    textures = {str(t.resourceId): t for t in controller.GetTextures()}
    found = False
    for draw in draws:
        controller.SetFrameEvent(draw.event_id, True)
        pipeline = controller.GetPipelineState()
        reads = [x.descriptor for x in pipeline.GetReadOnlyResources(rd.ShaderStage.Pixel, True)]
        bindings = [x for x in reads if x.elementByteSize == 208]
        if not bindings:
            continue
        if len(bindings) != 1:
            raise RuntimeError("Ambiguous lighting bindings")
        binding = bindings[0]
        words = struct.unpack("<52I", bytes(controller.GetBufferData(
            binding.resource, binding.byteOffset, 208)))
        if words[16] != len(expected) or words[49] != 0xFFFFFFFF:
            raise RuntimeError(f"Wrong canonical count or revived legacy slot: {words}")
        lights = [x for x in reads if x.elementByteSize == 96]
        if expected:
            if len(lights) != 1 or words[0] == 0xFFFFFFFF:
                raise RuntimeError("Forward pixel shader did not consume the canonical record")
            light = lights[0]
            if light.byteSize != 96 * len(expected):
                raise RuntimeError("Incorrect six-float4 upload size")
            data = bytes(controller.GetBufferData(light.resource, light.byteOffset, light.byteSize))
            kinds = []
            for index in range(len(expected)):
                values = struct.unpack_from("<24f", data, index * 96)
                if not all(math.isfinite(v) for v in values):
                    raise RuntimeError("Nonfinite canonical light record")
                kind, flags, radius = values[20:23]
                if kind != int(kind) or flags != int(flags) or int(flags) & ~1:
                    raise RuntimeError("Invalid canonical kind/flag encoding")
                if values[7] <= 0 or radius <= 0 or abs(values[3] * radius - 1) > 1e-5:
                    raise RuntimeError("Incorrect color/intensity or radius fields")
                kinds.append(int(kind))
                report.append(f"light={index} kind={int(kind)} flags={int(flags)} intensity={values[7]} radius={radius}")
            if sorted(kinds) != expected:
                raise RuntimeError(f"Wrong light kinds: {kinds} vs {expected}")
            ranges = [x for x in reads if x.elementByteSize == 8
                      and x.byteSize >= words[13] * 8]
            if len(ranges) != 1:
                raise RuntimeError("Missing or ambiguous canonical cluster ranges")
            ranges_data = bytes(controller.GetBufferData(
                ranges[0].resource, ranges[0].byteOffset, words[13] * 8))
            if len(ranges_data) != words[13] * 8:
                raise RuntimeError("Incomplete cluster range readback")
            complete_cells = compact_cells = 0
            for offset, count in struct.iter_unpack("<2I", ranges_data):
                if offset == 0xFFFFFFFF:
                    if count != len(expected):
                        raise RuntimeError("Complete-list range lost local records")
                    complete_cells += 1
                elif count:
                    if count > len(expected):
                        raise RuntimeError("Compact range exceeds selected light count")
                    compact_cells += 1
                elif offset:
                    raise RuntimeError("Empty cluster has a nonzero offset")
            if compact_cells and words[1] == 0xFFFFFFFF:
                raise RuntimeError("Compact cells require a valid index descriptor")
            if not any(x.elementByteSize == 64 for x in reads):
                raise RuntimeError("Forward shader did not consume canonical grid metadata")
            report.append(f"complete_cells={complete_cells} compact_cells={compact_cells} indices_srv={words[1]}")
        elif lights:
            raise RuntimeError("Unexpected local-light read with both lights disabled")
        report.append(f"bindings_event={draw.event_id} canonical_count={words[16]} legacy_slot=invalid")
        found = True
        break
    if not found:
        raise RuntimeError("No forward fragment consumed its lighting bindings")

    controller.SetFrameEvent(draws[-1].event_id, True)
    target = controller.GetPipelineState().GetOutputTargets()[0].resource
    texture = textures[str(target)]
    sub = rd.Subresource()
    sub.mip = sub.slice = sub.sample = 0
    lit = 0
    maximum = 0.0
    for y in range(1, 10):
        for x in range(1, 10):
            pixel = list(controller.PickPixel(target, texture.width * x // 10,
                texture.height * y // 10, sub, rd.CompType.Float).floatValue)
            if not all(math.isfinite(v) for v in pixel):
                raise RuntimeError("Nonfinite forward scene radiance")
            maximum = max(maximum, max(pixel[:3]))
            lit += int(max(pixel[:3]) > 1e-6)
    if bool(lit) != bool(expected):
        raise RuntimeError(f"Unexpected scene signal: expected kinds={expected}, lit probes={lit}")
    report.append(f"forward_target={names.get(str(target))} lit_probes={lit} max_scene_rgb={maximum}")
    tone = next((a for a in actions if a.event_id > draws[-1].event_id
                 and a.flags & rd.ActionFlags.Drawcall
                 and "Vortex.PostProcess.Tonemap" in a.path), None)
    if tone is None:
        raise RuntimeError("Missing forward-view tonemap")
    controller.SetFrameEvent(tone.event_id, True)
    reads = [x.descriptor for x in controller.GetPipelineState().GetReadOnlyResources(rd.ShaderStage.Pixel, True)]
    states = [x for x in reads if names.get(str(x.resource)) == "Vortex.PostProcess.Exposure.State"]
    if len(states) != 1:
        raise RuntimeError("Missing forward-view GPU exposure state")
    state = bytes(controller.GetBufferData(states[0].resource, states[0].byteOffset, 80))
    gain = struct.unpack_from("<f", state)[0]
    luminance, raw_ev, flags = struct.unpack_from("<2fI", state, 16)
    synthetic_dark = bool(flags & 16)
    if synthetic_dark == bool(expected) or (expected and not flags & 4):
        raise RuntimeError(f"Wrong ordinary/synthetic-dark meter disposition: flags={flags}")
    if not all(math.isfinite(x) for x in (gain, luminance, raw_ev)) or gain <= 0:
        raise RuntimeError("Invalid forward-view exposure state")
    report.append(f"meter_event={tone.event_id} S={gain} rawL={luminance} rawEV={raw_ev} synthetic_dark={synthetic_dark}")
    report.append("forward_local_binding_verdict=pass")
    report.append("scope=canonical binding and visible contribution; physical-unit and shadow parity are separate gates")


if __name__ == "__main__":
    run_ui_script("_forward_local_lights.txt", build_report)
