"""Export AP proof radiance and verify real deferred/forward AP consumers."""

import json
import os
from pathlib import Path
import struct
import sys

sys.path.insert(0, str(Path(__file__).resolve().parent))
from AnalyzeRenderDocSceneExposure import select_scene_source
from renderdoc_ui_analysis import collect_action_records, renderdoc_module, resource_id_to_name, run_ui_script


def build_report(controller, report, capture_path, report_path):
    case = os.environ.get("OXYGEN_RENDERDOC_PASS_NAME")
    if case not in ("ApDeferred", "ApForward", "ApMixed", "ApOpaqueReference", "ApOpaqueForward"):
        raise RuntimeError("Unknown AP proof phase")
    expected_strength = 8.0 if case.startswith("ApOpaque") else .01
    rd = renderdoc_module()
    actions = collect_action_records(controller)
    names = resource_id_to_name(controller)
    textures = {str(t.resourceId): t for t in controller.GetTextures()}
    previous_tone = 0
    views = []
    for tone in (a for a in actions if a.flags & rd.ActionFlags.Drawcall
                 and "Vortex.PostProcess.Tonemap" in a.path):
        draws = [a for a in actions if previous_tone < a.event_id < tone.event_id
                 and a.flags & rd.ActionFlags.Drawcall]
        ap = [a for a in draws if "Vortex.Stage15.Atmosphere" in a.path]
        translucent = [a for a in draws if "Vortex.Stage18.Translucency" in a.path]
        opaque_forward = [a for a in draws if "Vortex.Stage9.BasePass.Forward" in a.path]
        forward = translucent + opaque_forward
        if (len(ap) != 1 or bool(translucent) != (case in ("ApForward", "ApMixed"))
                or bool(opaque_forward) != (case == "ApOpaqueForward")):
            raise RuntimeError(f"Incorrect consumer draws: AP={len(ap)} translucent={len(translucent)} opaque-forward={len(opaque_forward)}")
        inline_opaque_ap = 0
        ap_before = max(a.event_id for a in draws if a.event_id < ap[0].event_id)
        controller.SetFrameEvent(ap[0].event_id, True)
        ap_target = controller.GetPipelineState().GetOutputTargets()[0].resource
        after_path = Path(report_path).with_name(f"{Path(report_path).stem}-view-{len(views)}-after-ap.rgba32f")
        after_path.write_bytes(bytes(controller.GetTextureData(ap_target, rd.Subresource())))
        controller.SetFrameEvent(ap_before, True)
        before_path = after_path.with_name(after_path.name.replace("-after-ap", "-before-ap"))
        before_path.write_bytes(bytes(controller.GetTextureData(ap_target, rd.Subresource())))
        for draw in ap + forward:
            controller.SetFrameEvent(draw.event_id, True)
            reads = [x.descriptor for x in controller.GetPipelineState().GetReadOnlyResources(rd.ShaderStage.Pixel, True)]
            volumes = [x for x in reads if "AtmosphereCameraAerialPerspective" in names.get(str(x.resource), "")]
            view_data = [x for x in reads if x.elementByteSize == 272]
            if draw in opaque_forward:
                inline_opaque_ap += len(volumes)
            if draw not in opaque_forward and (len(volumes) != 1 or len(view_data) != 1):
                raise RuntimeError(f"AP not consumed at event {draw.event_id}")
            if volumes:
                if len(volumes) != 1 or len(view_data) != 1:
                    raise RuntimeError("Ambiguous AP consumer bindings")
                data = view_data[0]
                strength = struct.unpack("<f", bytes(controller.GetBufferData(data.resource, data.byteOffset + 28, 4)))[0]
                if strength != struct.unpack("<f", struct.pack("<f", expected_strength))[0]:
                    raise RuntimeError(f"Unexpected AP strength: {strength}")
                raw = bytes(controller.GetTextureData(volumes[0].resource, rd.Subresource()))
                values = memoryview(raw).cast("f")
                maximum_rgb = max(max(values[i:i+3]) for i in range(0, len(values), 4))
                if maximum_rgb <= 0:
                    raise RuntimeError("AP volume has no scattering signal")
                report.append(f"consumer={draw.event_id} path={draw.path} strength={strength} max_ap_rgb={maximum_rgb}")
            if draw in forward:
                lighting = [x for x in reads if x.elementByteSize == 208]
                environment = [x for x in reads if x.elementByteSize == 672]
                materials = [x for x in reads if names.get(str(x.resource)) == "MaterialShadingConstantsAtlas"]
                if any(len(x) != 1 for x in (lighting, environment, materials)):
                    raise RuntimeError("Missing forward isolation bindings")
                light = lighting[0]
                data = bytes(controller.GetBufferData(light.resource, light.byteOffset, 208))
                local_count = struct.unpack_from("<I", data, 64)[0]
                directional_count = struct.unpack_from("<I", data, 60)[0]
                sun_direction = struct.unpack_from("<3f", data, 112)
                sun_lux = struct.unpack_from("<f", data, 140)[0]
                if local_count != 0 or directional_count != 1 or sun_lux != 1000 or sun_direction[1] <= 0 or sun_direction[2] <= 0:
                    raise RuntimeError(f"Surface lights are not isolated: locals={local_count}, directionals={directional_count}, lux={sun_lux}, sun={sun_direction}")
                env = environment[0]
                sky_enabled = struct.unpack("<I", bytes(controller.GetBufferData(env.resource, env.byteOffset + 460, 4)))[0]
                if sky_enabled:
                    raise RuntimeError("Unexpected IBL contribution in AP-only comparison")
                material = materials[0]
                data = bytes(controller.GetBufferData(material.resource, material.byteOffset, material.byteSize))
                metadata = [x for x in reads if x.elementByteSize == 64 and x.byteSize == 5 * 64]
                if len(metadata) != 1:
                    raise RuntimeError("Expected metadata for the five proof cards")
                meta = metadata[0]
                records = bytes(controller.GetBufferData(meta.resource, meta.byteOffset, meta.byteSize))
                material_indices = {struct.unpack_from("<I", records, i + 32)[0] for i in range(0, len(records), 64)}
                for index in material_indices:
                    offset = index * 112
                    if offset + 112 > len(data):
                        raise RuntimeError("Card material handle exceeds its bound atlas")
                    base = struct.unpack_from("<3f", data, offset)
                    flags = struct.unpack_from("<I", data, offset + 28)[0]
                    if base != (0, 0, 0) or flags != 1:
                        raise RuntimeError(f"Card material {index} must be lit/scalar-only/zero base RGB: {base}, flags={flags}")
                vertex_reads = [x.descriptor for x in controller.GetPipelineState().GetReadOnlyResources(rd.ShaderStage.Vertex, True)]
                vertices = [x for x in vertex_reads if x.elementByteSize == 72]
                if len(vertices) != 1:
                    raise RuntimeError("Missing card vertex data")
                vertex = vertices[0]
                data = bytes(controller.GetBufferData(vertex.resource, vertex.byteOffset, vertex.byteSize))
                if any(struct.unpack_from("<3f",data,i+12) != (0,-1,0) for i in range(0,len(data),72)):
                    raise RuntimeError("Card normals must face away from the sun")
                report.append(f"isolation_event={draw.event_id} local_lights=0 ibl=0 card_normal=(0,-1,0) sun_direction={sun_direction}")
        controller.SetFrameEvent(tone.event_id, True)
        reads = [x.descriptor for x in controller.GetPipelineState().GetReadOnlyResources(rd.ShaderStage.Pixel, True)]
        constants = [x for x in reads if x.byteSize == x.elementByteSize == 64]
        states = [x for x in reads if names.get(str(x.resource)) == "Vortex.PostProcess.Exposure.State"]
        frames = [x for x in reads if names.get(str(x.resource)) == "Vortex.PostProcess.Exposure.Frame"]
        if any(len(x) != 1 for x in (constants, states, frames)):
            raise RuntimeError("Missing exposure bindings")
        c = constants[0]
        source = select_scene_source(controller, reads, textures, names,
            bytes(controller.GetBufferData(c.resource, c.byteOffset, 64)))
        frame = frames[0]
        p, inverse_p = struct.unpack("<2f", bytes(controller.GetBufferData(frame.resource, frame.byteOffset, 8)))
        gain = struct.unpack("<f", bytes(controller.GetBufferData(states[0].resource, states[0].byteOffset, 4)))[0]
        if (p, inverse_p, gain) != (1, 1, .25):
            raise RuntimeError(f"Proof exposure changed: P={p}, inverseP={inverse_p}, S={gain}")
        texture = textures[str(source.resource)]
        if texture.format.compByteWidth != 4 or texture.format.compCount != 4:
            raise RuntimeError("Expected FP32 RGBA source")
        output = Path(report_path).with_name(f"{Path(report_path).stem}-view-{len(views)}.rgba32f")
        output.write_bytes(bytes(controller.GetTextureData(source.resource, rd.Subresource())))
        views.append({"width":texture.width,"height":texture.height,"radiance":str(output),
                      "gain":gain,"pre_exposure":p,"ap_draws":len(ap),"forward_draws":len(forward),
                      "opaque_forward_draws":len(opaque_forward),"inline_opaque_ap_draws":inline_opaque_ap,
                      "before_ap":str(before_path),"after_ap":str(after_path)})
        previous_tone = tone.event_id
    if len(views) != 2:
        raise RuntimeError(f"Expected two views, found {len(views)}")
    Path(report_path).with_suffix(".json").write_text(json.dumps({"case":case,"views":views},indent=2)+"\n")
    report.append("ap_consumers_verdict=pass")
    report.append("scope=bindings and exported radiance; run cross-phase image comparison separately")


if __name__ == "__main__":
    run_ui_script("_multiview_atmosphere.txt", build_report)
