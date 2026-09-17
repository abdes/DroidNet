"""Audit immutable HZB dispatch records and each queued view's depth pyramids."""

from pathlib import Path
import struct
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "shadows"))
from renderdoc_ui_analysis import (
    collect_action_records, renderdoc_module, resource_id_to_name, run_ui_script,
)


def build_report(controller, report, capture_path, report_path):
    rd = renderdoc_module()
    names = resource_id_to_name(controller)
    actions = collect_action_records(controller)
    snapshots = []
    ranges = set()
    for action in actions:
        if not action.flags & rd.ActionFlags.Dispatch:
            continue
        controller.SetFrameEvent(action.event_id, True)
        reads = controller.GetPipelineState().GetReadOnlyResources(rd.ShaderStage.Compute, True)
        constants = [x.descriptor for x in reads
                     if x.descriptor.byteSize == 48 and x.descriptor.elementByteSize == 48]
        if len(constants) != 1:
            raise RuntimeError("Expected one 48-byte structured HZB record")
        c = constants[0]
        key = (str(c.resource), c.byteOffset)
        if key in ranges:
            raise RuntimeError("Two queued dispatches reused one constant range")
        ranges.add(key)
        data = bytes(controller.GetBufferData(c.resource, c.byteOffset, 48))
        words = struct.unpack("<12I", data)
        if words[10:] != (2, 0):
            raise RuntimeError("HZB constant tail/step mismatch")
        snapshots.append((c.resource, c.byteOffset, data))
        report.append(f"dispatch={action.event_id} offset={c.byteOffset} source_extent={words[4:6]} destination_extent={words[8:10]}")
    if len(snapshots) != 8:
        raise RuntimeError(f"Expected eight dispatches across three pyramids, found {len(snapshots)}")
    controller.SetFrameEvent(actions[-1].event_id, True)
    for resource, offset, data in snapshots:
        if data != bytes(controller.GetBufferData(resource, offset, 48)):
            raise RuntimeError("A later view changed earlier HZB constants")
    textures = {names.get(str(t.resourceId)): t for t in controller.GetTextures()}
    checked = 0
    for view, (width, height) in enumerate(((8, 8), (16, 8), (8, 16))):
        source = [((x * 3 + y * 5 + view * 17) % 63 + 1) / 64
                  for y in range(height) for x in range(width)]
        for semantic, reduce in (("Closest", max), ("Furthest", min)):
            name = f"Vortex.Stage5.ScreenHzbBuild.View{14000 + view}.{semantic}.History0"
            texture = textures[name]
            values, w, h = source, width, height
            for mip in range(texture.mips):
                next_w, next_h = max(1, w // 2), max(1, h // 2)
                expected = [reduce(values[min(y * 2 + dy, h - 1) * w + min(x * 2 + dx, w - 1)]
                                   for dy in range(2) for dx in range(2))
                            for y in range(next_h) for x in range(next_w)]
                sub = rd.Subresource()
                sub.mip = mip
                raw = bytes(controller.GetTextureData(texture.resourceId, sub))
                actual = struct.unpack(f"<{len(expected)}f", raw)
                if list(actual) != expected:
                    raise RuntimeError(f"{name} mip {mip} differs from its own depth source")
                checked += len(expected)
                values, w, h = expected, next_w, next_h
    report.append(f"checked_texels={checked}")
    report.append("queued_hzb_verdict=pass")


if __name__ == "__main__":
    run_ui_script("_queued_hzb.txt", build_report)
