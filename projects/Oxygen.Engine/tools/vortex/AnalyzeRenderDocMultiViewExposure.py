"""Inspect each mapped MultiView image before composition and export the composite.

This checks captured S/P consumption, not temporal or standalone/family parity.
"""

import math
import json
from pathlib import Path
import struct
import sys

sys.path.insert(0, str(Path(__file__).resolve().parent))
from AnalyzeRenderDocSceneExposure import map_color
from renderdoc_ui_analysis import (
    collect_action_records, renderdoc_module, resource_id_to_name, run_ui_script,
)


def save_image(controller, rd, resource, path):
    save = rd.TextureSave()
    save.resourceId = resource
    save.destType = rd.FileType.PNG
    save.mip = 0
    save.slice.sliceIndex = save.sample.sampleIndex = 0
    save.typeCast = rd.CompType.Float
    save.alpha = rd.AlphaMapping.Preserve
    if not controller.SaveTexture(save, str(path)):
        raise RuntimeError(f"Could not export {path}")


def build_report(controller, report, capture_path, report_path):
    rd = renderdoc_module()
    names = resource_id_to_name(controller)
    textures = {str(t.resourceId): t for t in controller.GetTextures()}
    actions = collect_action_records(controller)
    # Export the complete replay before seeking backwards through individual
    # passes, so presentation and per-view outputs are separate observations.
    final_draw = next(a for a in reversed(actions) if a.flags & rd.ActionFlags.Drawcall)
    controller.SetFrameEvent(final_draw.event_id, True)
    initial_final = controller.GetPipelineState().GetOutputTargets()[0].resource
    composite_image = Path(report_path).with_suffix(".png")
    save_image(controller, rd, initial_final, composite_image)
    tones = [a for a in actions if a.flags & rd.ActionFlags.Drawcall
             and "Vortex.PostProcess.Tonemap" in a.path]
    if not tones:
        raise RuntimeError("No mapped scene views")
    report.append(f"capture={capture_path}")
    report.append(f"mapped_views={len(tones)}")
    frame_resources = set()
    view_results = []
    sub = rd.Subresource()
    sub.mip = sub.slice = sub.sample = 0
    bayer = (0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5)
    for index, draw in enumerate(tones):
        controller.SetFrameEvent(draw.event_id, True)
        pipeline = controller.GetPipelineState()
        reads = [x.descriptor for x in pipeline.GetReadOnlyResources(rd.ShaderStage.Pixel, True)]
        constants = [x for x in reads if x.byteSize == 48 and x.elementByteSize == 48]
        frames = [x for x in reads if names.get(str(x.resource)) == "Vortex.PostProcess.Exposure.Frame"]
        states = [x for x in reads if names.get(str(x.resource)) == "Vortex.PostProcess.Exposure.State"]
        sources = [x for x in reads if str(x.resource) in textures]
        if any(len(items) != 1 for items in (constants, frames, states, sources)):
            raise RuntimeError(f"Ambiguous/missing tonemap inputs at event {draw.event_id}")
        pc = bytes(controller.GetBufferData(constants[0].resource, constants[0].byteOffset, 48))
        mapper = struct.unpack_from("<I", pc, 12)[0]
        gamma, bloom = struct.unpack_from("<2f", pc, 20)
        background_enabled = struct.unpack_from("<I", pc, 44)[0]
        if bloom != 0:
            raise RuntimeError("The independent pixel oracle requires bloom disabled")
        p, inverse_p, state_slot, flags = struct.unpack("<2f2I", bytes(
            controller.GetBufferData(frames[0].resource, frames[0].byteOffset, 16)))
        state = bytes(controller.GetBufferData(states[0].resource, states[0].byteOffset, 80))
        gain, target_gain = struct.unpack_from("<2f", state)
        raw_luminance, raw_ev, state_flags, fallback = struct.unpack_from("<2f2I", state, 16)
        requested, applied, sequence = struct.unpack_from("<3Q", state, 40)
        if not all(math.isfinite(x) for x in (p, inverse_p, gain)) or p <= 0 or abs(p * inverse_p - 1) > 1e-6:
            raise RuntimeError(f"Invalid exposure domain at event {draw.event_id}")
        frame_key = (str(frames[0].resource), frames[0].byteOffset)
        if frame_key in frame_resources:
            raise RuntimeError("Different mapped views alias one frame exposure record")
        frame_resources.add(frame_key)
        source = sources[0]
        texture = textures[str(source.resource)]
        target = pipeline.GetOutputTargets()[0].resource
        target_desc = textures[str(target)]
        if (texture.width, texture.height) != (target_desc.width, target_desc.height):
            raise RuntimeError("Oracle requires equal source and mapped-output extents")
        checked = 0
        nonzero_scene_probes = 0
        max_error = 0.0
        probes = []
        for gy in range(1, 10):
            for gx in range(1, 10):
                x, y = texture.width * gx // 10, texture.height * gy // 10
                pixel = list(controller.PickPixel(source.resource, x, y, sub, rd.CompType.Float).floatValue)
                if not all(math.isfinite(c) for c in pixel):
                    raise RuntimeError(f"Nonfinite source at event {draw.event_id}, pixel {x},{y}")
                if background_enabled and pixel[3] < 1:
                    continue
                nonzero_scene_probes += int(max(pixel[:3]) > 1e-6)
                expected = map_color([c * gain / p for c in pixel[:3]], mapper, gamma)
                dither = (bayer[(x & 3) | ((y & 3) << 2)] / 16 - .5) / 255
                expected = [min(1, max(0, c + dither)) for c in expected]
                actual = list(controller.PickPixel(target, x, y, sub, rd.CompType.Float).floatValue)
                error = max(abs(c - e) for c, e in zip(actual[:3], expected))
                if not all(math.isfinite(c) for c in actual) or error > 1 / 255:
                    raise RuntimeError(f"S/P mismatch at event {draw.event_id}, pixel {x},{y}: {actual} vs {expected}")
                max_error = max(max_error, error)
                probes.append({"x": x, "y": y, "scene": pixel, "mapped": actual})
                checked += 1
        if not checked:
            raise RuntimeError(f"No opaque probes for mapped view {index}")
        output = Path(report_path).with_name(f"{Path(report_path).stem}-view-{index}.png")
        save_image(controller, rd, target, output)
        next_tone = tones[index + 1].event_id if index + 1 < len(tones) else actions[-1].event_id + 1
        writes = [u.eventId for u in controller.GetUsage(target)
                  if draw.event_id <= u.eventId < next_tone
                  and str(u.usage).endswith(("ColorTarget", "CopyDst", "ResolveDst"))]
        precomposition = Path(report_path).with_name(f"{Path(report_path).stem}-precomposition-{index}.png")
        controller.SetFrameEvent(max(writes, default=draw.event_id), True)
        save_image(controller, rd, target, precomposition)
        view_results.append({
            "index": index, "event": draw.event_id,
            "width": texture.width, "height": texture.height,
            "frame": sequence, "gain": gain, "target_gain": target_gain,
            "pre_exposure": p, "raw_luminance": raw_luminance, "raw_ev": raw_ev,
            "state_flags": state_flags, "frame_flags": flags,
            "fallback_reason": fallback,
            "requested_generation": requested, "applied_generation": applied,
            "image": str(output), "precomposition_image": str(precomposition),
            "probes": probes,
        })
        report.append(f"view={index} event={draw.event_id} extent={texture.width}x{texture.height} P={p} S={gain} targetS={target_gain} rawL={raw_luminance} rawEV={raw_ev} state_flags={state_flags} fallback={fallback} flags={flags} state_slot={state_slot} target={target} opaque_probes={checked} nonzero_scene_probes={nonzero_scene_probes} max_error={max_error} image={output}")
    composites = [a for a in actions if a.flags & rd.ActionFlags.Drawcall
                  and "Vortex.CompositingTask" in a.path]
    if not composites:
        raise RuntimeError("No final composition draw")
    report.append(f"composite_image={composite_image}")
    Path(report_path).with_suffix(".json").write_text(json.dumps({
        "capture": str(capture_path), "composite_image": str(composite_image),
        "views": view_results,
    }, indent=2) + "\n", encoding="utf-8")
    report.append("per_view_tonemap_verdict=pass")
    report.append("scope=captured per-view S/P only; temporal, standalone/family equivalence and visual inspection are separate gates")


if __name__ == "__main__":
    run_ui_script("_multiview_exposure.txt", build_report)
