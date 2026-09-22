"""Export original lit-mesh inputs and output for independent readability checks."""

import json
import math
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
    actions = collect_action_records(controller)
    views = []
    previous_tone = 0
    for tone in (a for a in actions if a.flags & rd.ActionFlags.Drawcall
                 and "Vortex.PostProcess.Tonemap" in a.path):
        lighting = [a for a in actions if previous_tone < a.event_id < tone.event_id
                    and "Vortex.Stage12.DeferredLighting" in a.path
                    and a.flags & (rd.ActionFlags.Drawcall | rd.ActionFlags.Dispatch)]
        if not lighting:
            candidates = [(a.event_id, a.path) for a in actions
                          if previous_tone < a.event_id < tone.event_id
                          and a.flags & (rd.ActionFlags.Drawcall | rd.ActionFlags.Dispatch)]
            raise RuntimeError(f"Missing deferred lighting draws: {candidates}")
        for light in lighting:
            controller.SetFrameEvent(light.event_id, True)
            pipeline = controller.GetPipelineState()
            stage = rd.ShaderStage.Compute if light.flags & rd.ActionFlags.Dispatch else rd.ShaderStage.Pixel
            reads = [x.descriptor for x in pipeline.GetReadOnlyResources(stage, True)]
            blocks = pipeline.GetConstantBlocks(stage)
            bindings = [x.descriptor for x in blocks
                        if x.access.index == rd.DescriptorAccess.NoShaderBinding
                        and x.descriptor.byteSize == 256]
            used = pipeline.GetReadOnlyResources(stage, True)
            by_slot = {int(x.access.arrayElement): x.descriptor for x in used
                       if x.access.index == rd.DescriptorAccess.NoShaderBinding}
            headers = [x for x in reads if x.elementByteSize == x.byteSize == 96]
            bases = [x for x in reads if "GBufferBaseColor" in names.get(str(x.resource), "")]
            if len(bindings) == 1 and len(bases) == 1 and len(headers) == 1:
                break
        else:
            raise RuntimeError("No deferred lighting draw has both light constants and base color")
        binding = bindings[0]
        if binding.byteOffset % 256:
            raise RuntimeError("Deferred light constants are not CBV aligned")
        raw = bytes(controller.GetBufferData(binding.resource, binding.byteOffset, 80))
        kind, selection = struct.unpack_from("<2I", raw, 64)
        if kind != 0:
            raise RuntimeError("The lit-material fixture must begin with a directional draw")
        header = headers[0]
        words = struct.unpack("<24I", bytes(controller.GetBufferData(header.resource, header.byteOffset, 96)))
        if selection >= words[4] or words[0] not in by_slot:
            raise RuntimeError("Missing selected directional record")
        descriptor = by_slot[words[0]]
        if descriptor.elementByteSize != 64 or descriptor.byteSize < (selection + 1) * 64:
            raise RuntimeError("Invalid directional array layout")
        record = bytes(controller.GetBufferData(descriptor.resource, descriptor.byteOffset + selection * 64, 64))
        if struct.unpack_from("<I", record, 48)[0] != selection:
            raise RuntimeError("Draw and directional source identities differ")
        view = {
            "index": len(views), "lighting_event": light.event_id,
            "lighting_draws": [a.path for a in lighting],
            "first_light_rgb_lux": struct.unpack_from("<3f", record, 16),
            "first_light_type": kind,
            "first_light_selection_index": selection,
            "first_light_atmosphere_slot": struct.unpack_from("<I", record, 12)[0],
            "first_light_atmosphere_transmittance": struct.unpack_from("<3f", record, 32),
        }
        base_path = Path(report_path).with_name(f"{Path(report_path).stem}-view-{len(views)}-base.rgba8")
        base_path.write_bytes(bytes(controller.GetTextureData(bases[0].resource, rd.Subresource())))
        view["base"] = str(base_path)
        accumulation = pipeline.GetOutputTargets()[0].resource
        after_path = base_path.with_name(base_path.name.replace("-base.rgba8", "-first-light.rgba32f"))
        after_path.write_bytes(bytes(controller.GetTextureData(accumulation, rd.Subresource())))
        view["first_light"] = str(after_path)
        base_draw = max(a.event_id for a in actions if previous_tone < a.event_id < light.event_id
                        and "Vortex.Stage9.BasePass" in a.path and a.flags & rd.ActionFlags.Drawcall)
        controller.SetFrameEvent(base_draw, True)
        before_path = base_path.with_name(base_path.name.replace("-base.rgba8", "-before-light.rgba32f"))
        before_path.write_bytes(bytes(controller.GetTextureData(accumulation, rd.Subresource())))
        view["before_light"] = str(before_path)
        controller.SetFrameEvent(tone.event_id, True)
        pipeline = controller.GetPipelineState()
        reads = [x.descriptor for x in pipeline.GetReadOnlyResources(rd.ShaderStage.Pixel, True)]
        constants = [x for x in reads if x.byteSize == x.elementByteSize == 64]
        frames = [x for x in reads if names.get(str(x.resource)) == "Vortex.PostProcess.Exposure.Frame"]
        states = [x for x in reads if names.get(str(x.resource)) == "Vortex.PostProcess.Exposure.State"]
        if any(len(x) != 1 for x in (constants, frames, states)):
            raise RuntimeError("Missing final exposure bindings")
        c = constants[0]
        raw = bytes(controller.GetBufferData(c.resource, c.byteOffset, 64))
        source = select_scene_source(controller, reads, textures, names, raw)
        desc = textures[str(source.resource)]
        if desc.format.compByteWidth != 4 or desc.format.compCount != 4:
            raise RuntimeError("Expected FP32 accumulation")
        view.update(width=desc.width, height=desc.height,
                    mapper=struct.unpack_from("<I", raw, 12)[0],
                    gamma=struct.unpack_from("<f", raw, 20)[0])
        view["P"] = struct.unpack("<f", bytes(controller.GetBufferData(frames[0].resource, 0, 4)))[0]
        view["gain"] = struct.unpack("<f", bytes(controller.GetBufferData(states[0].resource, 0, 4)))[0]
        state = bytes(controller.GetBufferData(states[0].resource, 0, 80))
        view["target_gain"] = struct.unpack_from("<f", state, 4)[0]
        view["metered_luminance"] = struct.unpack_from("<f", state, 16)[0]
        view["state_flags"] = struct.unpack_from("<I", state, 24)[0]
        view["requested_generation"], view["applied_generation"], view["frame"] = struct.unpack_from("<3Q", state, 40)
        hdr_path = base_path.with_name(base_path.name.replace("-base.rgba8", "-scene.rgba32f"))
        hdr_path.write_bytes(bytes(controller.GetTextureData(source.resource, rd.Subresource())))
        view["scene"] = str(hdr_path)
        target = pipeline.GetOutputTargets()[0].resource
        mapped_path = base_path.with_name(base_path.name.replace("-base.rgba8", "-mapped.rgba8"))
        output = textures[str(target)]
        if output.format.compByteWidth != 1 or output.format.compCount != 4:
            raise RuntimeError("Expected byte RGBA output")
        mapped_path.write_bytes(bytes(controller.GetTextureData(target, rd.Subresource())))
        view["mapped"] = str(mapped_path)
        solves = []
        for action in actions:
            if not previous_tone < action.event_id < tone.event_id or not action.flags & rd.ActionFlags.Dispatch:
                continue
            controller.SetFrameEvent(action.event_id, True)
            compute = controller.GetPipelineState()
            shader = compute.GetShaderReflection(rd.ShaderStage.Compute)
            if shader and shader.entryPoint == "VortexExposureHistogramCS":
                reads = [x.descriptor for x in compute.GetReadOnlyResources(rd.ShaderStage.Compute, True)]
                constants = [x for x in reads if x.byteSize == x.elementByteSize == 64]
                writes = [x.descriptor for x in compute.GetReadWriteResources(rd.ShaderStage.Compute, True)]
                histograms = [x for x in writes if names.get(str(x.resource)) == "Vortex.PostProcess.Exposure.Histogram"]
                if len(constants) != 1 or len(histograms) != 1:
                    raise RuntimeError("Missing histogram inputs/output")
                c = constants[0]
                raw = bytes(controller.GetBufferData(c.resource, c.byteOffset, 64))
                view["meter"] = {
                    "rectangle": struct.unpack_from("<4I", raw, 16),
                    "profile": struct.unpack_from("<I", raw, 32)[0],
                    "spot_radius": struct.unpack_from("<f", raw, 36)[0],
                    "mask": struct.unpack_from("<I", raw, 40)[0],
                    "coverage_enabled": struct.unpack_from("<I", raw, 44)[0],
                    "black_influence": struct.unpack_from("<f", raw, 52)[0],
                    "words": struct.unpack("<264I", bytes(controller.GetBufferData(histograms[0].resource, 0, 1056))),
                }
            if not shader or shader.entryPoint != "VortexExposureAverageCS":
                continue
            constants = [x.descriptor for x in compute.GetReadOnlyResources(rd.ShaderStage.Compute, True)
                         if x.descriptor.byteSize == x.descriptor.elementByteSize == 112]
            if len(constants) != 1:
                raise RuntimeError("Missing exposure solve constants")
            c = constants[0]
            raw = bytes(controller.GetBufferData(c.resource, c.byteOffset, 112))
            log_dt = struct.unpack_from("<f", raw, 40)[0]
            solves.append({"event": action.event_id,
                           "mode": struct.unpack_from("<I", raw, 72)[0],
                           "delta_seconds": 0 if log_dt == -256 else math.exp2(log_dt),
                           "fixed_gain": struct.unpack_from("<f", raw, 68)[0],
                           "log_window": struct.unpack_from("<2f", raw, 8),
                           "percentiles": struct.unpack_from("<2f", raw, 16),
                           "transition_policy": struct.unpack_from("<I", raw, 88)[0]})
        if len(solves) != 1:
            raise RuntimeError(f"Expected one exposure solve per view: {solves}")
        view["solve"] = solves[0]
        views.append(view)
        report.append(str(view))
        previous_tone = tone.event_id
    if len(views) != 2:
        raise RuntimeError("Expected main and lit PiP")
    Path(report_path).with_suffix(".json").write_text(json.dumps({"views": views}, indent=2) + "\n")
    report.append("lit_readability_export=pass")


if __name__ == "__main__":
    run_ui_script("_lit_readability.txt", build_report)
