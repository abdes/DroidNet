"""Qualify the explicit three-directional MultiView recipe and per-source shadows."""

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
    views = {}
    shadow_surfaces = {}
    for draw in collect_action_records(controller):
        if not draw.flags & rd.ActionFlags.Drawcall or "Vortex.Stage12.DirectionalLight" not in draw.path:
            continue
        controller.SetFrameEvent(draw.event_id, True)
        pipeline = controller.GetPipelineState()
        used = pipeline.GetReadOnlyResources(rd.ShaderStage.Pixel, True)
        by_slot = {int(x.access.arrayElement): x.descriptor for x in used
                   if x.access.index == rd.DescriptorAccess.NoShaderBinding}
        lighting = next(x.descriptor for x in used if x.descriptor.elementByteSize == 96)
        words = struct.unpack("<24I", bytes(controller.GetBufferData(lighting.resource, lighting.byteOffset, 96)))
        if words[4] != 3: raise RuntimeError("The recipe must publish all three directional sources")
        descriptor = by_slot[words[0]]
        if descriptor.elementByteSize != 64 or descriptor.byteSize != 192:
            raise RuntimeError("Incorrect counted directional record array")
        records = bytes(controller.GetBufferData(descriptor.resource, descriptor.byteOffset, 192))
        slots = [struct.unpack_from("<I", records, i*64+12)[0] for i in range(3)]
        if set(slots) != {0, 1, 0xFFFFFFFF}: raise RuntimeError(f"Incorrect atmosphere assignments: {slots}")
        constants = [x.descriptor for x in pipeline.GetConstantBlocks(rd.ShaderStage.Pixel)
                     if x.access.index == rd.DescriptorAccess.NoShaderBinding
                     and x.descriptor.byteSize == 256]
        if len(constants) != 1: raise RuntimeError(f"Expected one deferred CBV, got {len(constants)}")
        constant = constants[0]
        if constant.byteOffset % 256:
            raise RuntimeError("Deferred constant slice is not CBV-aligned")
        kind, selection = struct.unpack_from("<2I", bytes(controller.GetBufferData(constant.resource,constant.byteOffset,80)),64)
        if kind != 0 or selection >= 3: raise RuntimeError("Incorrect directional draw identity")
        record_selection = struct.unpack_from("<I", records, selection*64+48)[0]
        if selection != record_selection: raise RuntimeError("Draw and evaluation indices differ")
        slot = slots[selection]
        expected = {0:(80000,80000,80000), 1:(9000,18000,30000), 0xFFFFFFFF:(12000,4800,3000)}[slot]
        illuminance = struct.unpack_from("<3f",records,selection*64+16)
        if any(abs(a-b) > 0.02 for a,b in zip(illuminance,expected)):
            raise RuntimeError(f"Incorrect source transport: {illuminance} vs {expected}")
        refs = by_slot[words[8]]
        reference = struct.unpack("<4I",bytes(controller.GetBufferData(refs.resource,refs.byteOffset+selection*16,16)))
        if reference[2] != selection: raise RuntimeError("Shadow reference belongs to another source")
        if slot == 0xFFFFFFFF:
            if reference[0] != 0 or reference[1] != 0xFFFFFFFF: raise RuntimeError("Unshadowed source unexpectedly requests a map")
        else:
            if reference[0] != 1: raise RuntimeError("Shadowed source has no directional family")
            header = next(x.descriptor for x in used if x.descriptor.elementByteSize == x.descriptor.byteSize == 112)
            header_words = struct.unpack("<28I",bytes(controller.GetBufferData(header.resource,header.byteOffset,112)))
            if header_words[1] != 2 or header_words[7] != 5: raise RuntimeError("Incomplete two-family cascade publication")
            families = by_slot[header_words[0]]
            family = struct.unpack("<4I",bytes(controller.GetBufferData(families.resource,families.byteOffset+reference[1]*16,16)))
            if family[0] != selection or family[2] != (2 if slot == 0 else 3): raise RuntimeError("Wrong family identity or cascade count")
            cascades = by_slot[header_words[6]]
            cascade = bytes(controller.GetBufferData(cascades.resource,cascades.byteOffset+family[1]*128,128))
            surface = struct.unpack_from("<I",cascade,80)[0]
            if surface not in by_slot: raise RuntimeError("Draw did not consume its own shadow surface")
            resource = str(by_slot[surface].resource)
            shadow_surfaces.setdefault(tuple(words[18:20]),{})[slot] = resource
            width = textures[resource].width
            if width != (2048 if slot == 0 else 1024): raise RuntimeError(f"Per-source resolution changed: {width}")
        target = pipeline.GetOutputTargets()[0].resource
        texture = textures[str(target)]
        after = bytes(controller.GetTextureData(target,rd.Subresource()))
        controller.SetFrameEvent(draw.event_id-1,True)
        before = bytes(controller.GetTextureData(target,rd.Subresource()))
        fmt = "<4e" if texture.format.compByteWidth == 2 else "<4f"
        stride = texture.format.compByteWidth * 4
        positive = 0
        maximum = 0.0
        for gy in range(1,32):
            for gx in range(1,32):
                x,y = gx*texture.width//32,gy*texture.height//32
                offset = (y*texture.width+x)*stride
                a,b = struct.unpack_from(fmt,after,offset),struct.unpack_from(fmt,before,offset)
                if not all(math.isfinite(v) for v in (*a,*b)): raise RuntimeError("Nonfinite directional accumulation")
                delta = max(a[c]-b[c] for c in range(3))
                positive += delta > 0
                maximum = max(maximum,delta)
        if not positive: raise RuntimeError(f"Directional source {selection} contributes no sampled radiance")
        view = tuple(words[18:20])
        views.setdefault(view,set()).add(selection)
        report.append(json.dumps({"event":draw.event_id,"view":view,"selection":selection,"atmosphere_slot":slot,"lux":illuminance,"positive_samples":positive,"maximum_delta":maximum}))
    if len(views) != 2 or any(indices != {0,1,2} for indices in views.values()):
        raise RuntimeError(f"Expected three contributions in both views: {views}")
    if any(set(surfaces) != {0,1} or len(set(surfaces.values())) != 2 for surfaces in shadow_surfaces.values()):
        raise RuntimeError("The directional families share or lose a shadow surface")
    report.append("directional_array_verdict=pass")
    report.append("scope=source transport, draw contribution and per-source shadow routing; physical oracle and complete lifetime gates remain open")


if __name__ == "__main__":
    run_ui_script("_directional_array.txt",build_report)
