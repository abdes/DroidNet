"""Verify all three directional records in the MultiView forward preview."""

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
        by_slot = {int(x.access.arrayElement): x.descriptor for x in used
                   if x.access.index == rd.DescriptorAccess.NoShaderBinding}
        headers = [x.descriptor for x in used if x.descriptor.elementByteSize == x.descriptor.byteSize == 96]
        if not headers:
            continue
        header = headers[0]
        words = struct.unpack("<24I", bytes(controller.GetBufferData(header.resource, header.byteOffset, 96)))
        if words[4] != 3 or words[0] not in by_slot:
            raise RuntimeError("Forward publication lost directional sources")
        descriptor = by_slot[words[0]]
        if descriptor.elementByteSize != 64 or descriptor.byteSize != 192:
            raise RuntimeError("Incorrect forward directional stride/count")
        records = bytes(controller.GetBufferData(descriptor.resource, descriptor.byteOffset, 192))
        slots = [struct.unpack_from("<I",records,index*64+12)[0] for index in range(3)]
        indices = [struct.unpack_from("<I",records,index*64+48)[0] for index in range(3)]
        if set(slots) != {0,1,0xFFFFFFFF} or indices != [0,1,2]:
            raise RuntimeError("Incorrect source/atmosphere identity")
        references = by_slot[words[8]]
        data = bytes(controller.GetBufferData(references.resource,references.byteOffset,48))
        families = []
        for index,reference in enumerate(struct.iter_unpack("<4I",data)):
            if reference[2] != index: raise RuntimeError("Shadow reference source mismatch")
            if slots[index] == 0xFFFFFFFF:
                if reference[0] != 0: raise RuntimeError("The unassigned fill must be unshadowed in this recipe")
            else:
                if reference[0] != 1: raise RuntimeError("Missing directional family reference")
                families.append(reference[1])
        if sorted(families) != [0,1]: raise RuntimeError("Shadow families were derived from unfiltered source indices")
        target = pipeline.GetOutputTargets()[0].resource
        texture = next(t for t in controller.GetTextures() if t.resourceId == target)
        radiance = controller.PickPixel(target,texture.width//2,texture.height//2,rd.Subresource(),rd.CompType.Float).floatValue
        if not all(math.isfinite(v) for v in radiance): raise RuntimeError("Nonfinite forward output")
        report.append(f"event={draw.event_id} directional_count=3 atmosphere_slots={slots} selection_indices={indices} family_indices={families}")
        checked += 1
    if checked == 0: raise RuntimeError("No forward fragment consumed the directional array")
    report.append(f"forward_draws_checked={checked}")
    report.append("directional_array_forward_verdict=pass")
    report.append("scope=forward record and shadow-reference consumption; physical BRDF parity remains open")


if __name__ == "__main__":
    run_ui_script("_directional_array_forward.txt",build_report)
