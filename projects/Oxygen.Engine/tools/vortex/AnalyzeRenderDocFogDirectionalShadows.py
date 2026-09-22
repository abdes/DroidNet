"""Check both atmosphere shadow sources in the three-directional fog recipe."""

import json
from pathlib import Path
import struct
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "shadows"))
from renderdoc_ui_analysis import collect_action_records, renderdoc_module, run_ui_script


def build_report(controller, report, capture_path, report_path):
    rd = renderdoc_module()
    textures = {str(t.resourceId): t for t in controller.GetTextures()}
    views = set()
    for dispatch in collect_action_records(controller):
        if not dispatch.flags & rd.ActionFlags.Dispatch or "Vortex.Stage14.VolumetricFog" not in dispatch.path:
            continue
        controller.SetFrameEvent(dispatch.event_id, True)
        pipeline = controller.GetPipelineState()
        reads = pipeline.GetReadOnlyResources(rd.ShaderStage.Compute, True)
        by_slot = {int(x.access.arrayElement): x.descriptor for x in reads
                   if x.access.index == rd.DescriptorAccess.NoShaderBinding}

        def read(descriptor, size, offset=0):
            return bytes(controller.GetBufferData(descriptor.resource, descriptor.byteOffset + offset, size))

        constants = next(x.descriptor for x in reads if x.descriptor.elementByteSize == 544)
        raw = read(constants, 544)
        if struct.unpack_from("<I", raw, 44)[0] != 1:
            raise RuntimeError("Directional fog shadows are disabled")
        if any(struct.unpack_from("<f", raw, offset)[0] != 1.0 for offset in (476, 508)):
            raise RuntimeError("Both atmosphere sources must be active")
        lighting = next(x.descriptor for x in reads if x.descriptor.elementByteSize == 96)
        words = struct.unpack("<24I", read(lighting, 96))
        if words[4] != 3:
            raise RuntimeError("Expected complete three-source lighting publication")
        header = next(x.descriptor for x in reads if x.descriptor.elementByteSize == 112)
        shadow = struct.unpack("<28I", read(header, 112))
        if shadow[16:24] != words[12:20] or shadow[9] != words[10]:
            raise RuntimeError("Shadow and lighting publication identities differ")
        if shadow[1] != 2 or shadow[7] != 5:
            raise RuntimeError("Expected two directional families and five cascades")
        refs = by_slot[words[8]]
        references = list(struct.iter_unpack("<4I", read(refs, refs.byteSize)))
        sources = []
        for index in range(words[4]):
            record = read(by_slot[words[0]], 64, index * 64)
            slot = struct.unpack_from("<I", record, 12)[0]
            selection = struct.unpack_from("<I", record, 48)[0]
            if selection != index:
                raise RuntimeError("Directional selection identity mismatch")
            if slot not in (0, 1):
                continue
            reference = next(r for r in references if r[2] == selection)
            if reference[0] != 1 or reference[3] != 2:
                raise RuntimeError("Missing complete cascaded shadow reference")
            family = struct.unpack("<4I", read(by_slot[shadow[0]], 16, reference[1] * 16))
            if family[0] != selection or family[2] != (2 if slot == 0 else 3):
                raise RuntimeError("Fog resolved a different source's cascade family")
            surfaces = set()
            for cascade_index in range(family[1], family[1] + family[2]):
                cascade = read(by_slot[shadow[6]], 128, cascade_index * 128)
                surface = struct.unpack_from("<I", cascade, 80)[0]
                if surface not in by_slot:
                    raise RuntimeError("Fog dispatch did not access the source shadow surface")
                descriptor = by_slot[surface]
                texture = textures[str(descriptor.resource)]
                if texture.width != (2048 if slot == 0 else 1024):
                    raise RuntimeError("Wrong source shadow resolution")
                surfaces.add(str(descriptor.resource))
            sources.append({"atmosphere_slot": slot, "selection": selection,
                            "cascades": family[2], "surfaces": sorted(surfaces)})
        if {s["atmosphere_slot"] for s in sources} != {0, 1}:
            raise RuntimeError("Missing atmosphere shadow source")
        if set(sources[0]["surfaces"]) & set(sources[1]["surfaces"]):
            raise RuntimeError("Distinct source families unexpectedly share a surface")
        view = tuple(words[18:20])
        views.add(view)
        report.append(json.dumps({"event": dispatch.event_id, "view": view, "sources": sources}))
    if len(views) != 2:
        raise RuntimeError(f"Expected both MultiView fog consumers, got {views}")
    report.append("fog_directional_shadows_verdict=pass")
    report.append("scope=live per-source publication and accessed surfaces; numerical visibility is tested by FogShadowsFollowAtmosphereSourceIdentity")


if __name__ == "__main__":
    run_ui_script("_fog_directional_shadows.txt", build_report)
