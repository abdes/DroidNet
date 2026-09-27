"""Inspect production LUT uploads and their exact retained-owner GPU readback."""

from pathlib import Path
import os
import struct
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "shadows"))
from renderdoc_ui_analysis import (
    collect_action_records,
    renderdoc_module,
    resource_id_to_name,
    run_ui_script,
)


def inspect_production_consumers(controller, report, textures, names):
    rd = renderdoc_module()
    modes = {
        "ProductionDeferred": {
            "IBL.BrdfLookup": "Vortex.Stage13.IndirectLighting",
            "LightingService.BrdfEnergy": "Vortex.Stage12.DirectionalLight",
        },
        "ProductionForwardIbl": {"IBL.BrdfLookup": "Vortex.Stage9.BasePass.Forward"},
        "ProductionForwardEnergy": {
            "LightingService.BrdfEnergy": "Vortex.Stage9.BasePass.Forward"
        },
        "ProductionTranslucent": {
            "IBL.BrdfLookup": "Vortex.Stage18.Translucency",
            "LightingService.BrdfEnergy": "Vortex.Stage18.Translucency",
        },
        "ProductionTranslucentIbl": {"IBL.BrdfLookup": "Vortex.Stage18.Translucency"},
    }
    required = modes[os.environ["OXYGEN_RENDERDOC_PASS_NAME"]]
    actions = collect_action_records(controller)
    selected = os.environ.get("OXYGEN_RENDERDOC_EVENT_ID", "")
    if selected:
        event_ids = {int(value) for value in selected.split(",")}
    else:
        event_ids = {
            max(
                action.event_id
                for action in actions
                if action.flags & rd.ActionFlags.Drawcall and stage in action.path
            )
            for stage in required.values()
        }
    observed = set()
    resources = {
        str(texture.resourceId): names[str(texture.resourceId)] for texture in textures
    }
    for action in actions:
        if action.event_id not in event_ids:
            continue
        if not action.flags & rd.ActionFlags.Drawcall:
            raise RuntimeError("Selected event is not a draw")
        controller.SetFrameEvent(action.event_id, True)
        state = controller.GetPipelineState()
        reflection = state.GetShaderReflection(rd.ShaderStage.Pixel)
        access = controller.GetDescriptorAccess()
        reads = state.GetReadOnlyResources(rd.ShaderStage.Pixel, True)
        report.append(
            f"draw_event={action.event_id} shader={reflection.entryPoint} accesses={len(access)} reads={len(reads)}"
        )
        for use in reads:
            name = resources.get(str(use.descriptor.resource))
            if name not in required or required[name] not in action.path:
                continue
            layout = state.GetResourceLayout(use.descriptor.resource)
            if "SHADER_RESOURCE" not in layout:
                raise RuntimeError(
                    f"LUT consumed outside shader-resource state: {layout}"
                )
            observed.add(name)
            report.append(
                f"production_consumer_event={action.event_id} resource={name} pass={action.path} state={layout}"
            )
    if observed != set(required):
        raise RuntimeError(
            f"Incomplete production consumer coverage: expected={set(required)} observed={observed}"
        )
    report.append(f"production_lut_consumer_verdict=pass resources={len(observed)}")


def build_report(controller, report, capture_path, report_path):
    rd = renderdoc_module()
    names = resource_id_to_name(controller)
    textures = [
        t
        for t in controller.GetTextures()
        if names.get(str(t.resourceId))
        in ("IBL.BrdfLookup", "LightingService.BrdfEnergy")
    ]
    if os.environ.get("OXYGEN_RENDERDOC_PASS_NAME", "").startswith("Production"):
        inspect_production_consumers(controller, report, textures, names)
        report.append(f"capture={capture_path}")
        return
    if len(textures) != 1:
        raise RuntimeError("Expected one production LUT in this native fixture")
    texture = textures[0]
    consumers = []
    for action in collect_action_records(controller):
        if not action.flags & rd.ActionFlags.Dispatch:
            continue
        controller.SetFrameEvent(action.event_id, True)
        state = controller.GetPipelineState()
        reads = state.GetReadOnlyResources(rd.ShaderStage.Compute, True)
        if not any(use.descriptor.resource == texture.resourceId for use in reads):
            continue
        outputs = [
            use.descriptor
            for use in state.GetReadWriteResources(rd.ShaderStage.Compute, True)
            if names.get(str(use.descriptor.resource)) == "Tone bound arithmetic output"
        ]
        if len(outputs) != 1:
            raise RuntimeError("Missing exact-texel qualification output")
        output = outputs[0]
        actual = bytes(
            controller.GetBufferData(
                output.resource, output.byteOffset, output.byteSize
            )
        )
        raw = bytes(controller.GetTextureData(texture.resourceId, rd.Subresource()))
        texels = texture.width * texture.height
        if len(actual) != texels * 32:
            raise RuntimeError("Incomplete texel output")
        unorm = texture.format.compByteWidth == 2
        stride = 4 if unorm else 8
        if len(raw) != texels * stride:
            raise RuntimeError("Unexpected native LUT storage layout")
        for index in range(texels):
            if unorm:
                expected = struct.unpack_from("<2H", raw, index * stride)
                observed = struct.unpack_from("<2f", actual, index * 32)
            else:
                expected = struct.unpack_from("<2I", raw, index * stride)
                observed = struct.unpack_from("<2I", actual, index * 32)
            if expected != observed:
                raise RuntimeError(f"Native LUT/readback mismatch at texel {index}")
        report.append(
            f"consumer_event={action.event_id} texels={texels} exact_channels={texels * 2}"
        )
        report.append(
            f"consumer_resource_state={state.GetResourceLayout(texture.resourceId)}"
        )
        report_path.with_suffix(".bin").write_bytes(raw)
        consumers.append(action.event_id)
    if len(consumers) != 1:
        raise RuntimeError(f"Expected one retained-owner consumer, got {consumers}")
    copies = [
        use.eventId
        for use in controller.GetUsage(texture.resourceId)
        if use.usage == rd.ResourceUsage.CopyDst
    ]
    if len(copies) != 1 or copies[0] >= consumers[0]:
        raise RuntimeError(
            f"Expected one producer copy before consumption, got {copies}"
        )
    report.append(f"producer_copy_event={copies[0]}")
    report.append(
        f"resource={names[str(texture.resourceId)]} extent={texture.width}x{texture.height}"
    )
    report.append(f"capture={capture_path}")
    report.append("immutable_lut_upload_verdict=pass")


if __name__ == "__main__":
    run_ui_script("_immutable_lut.txt", build_report)
