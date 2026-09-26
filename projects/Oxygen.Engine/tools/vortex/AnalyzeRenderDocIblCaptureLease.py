"""Verify retained IBL texture/metadata copies after renderer shutdown."""

import json
import math
from pathlib import Path
import struct
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "shadows"))
from renderdoc_ui_analysis import renderdoc_module, resource_id_to_name, run_ui_script


def build_report(controller, report, capture_path, report_path):
    rd = renderdoc_module()
    names = resource_id_to_name(controller)
    textures = {str(t.resourceId): t for t in controller.GetTextures()}
    copies = []

    def visit(action):
        if action.flags & rd.ActionFlags.Copy:
            copies.append(action)
        for child in action.children:
            visit(child)

    for action in controller.GetRootActions():
        visit(action)
    cubes = [a for a in copies if names.get(str(a.copySource)) == "IBL.ProcessedCube"]
    metadata = [a for a in copies if names.get(str(a.copySource)) == "IBL.Metadata"]
    if len(cubes) != 1 or len(metadata) != 1:
        raise RuntimeError("Expected one retained cube and one metadata readback")
    controller.SetFrameEvent(max(a.eventId for a in copies), True)
    desc = textures[str(cubes[0].copySource)]
    if (desc.width, desc.height, desc.arraysize, desc.mips, desc.format.compByteWidth) != (16, 16, 6, 5, 4):
        raise RuntimeError("Unexpected retained cube layout")
    data = bytes(controller.GetTextureData(cubes[0].copySource, rd.Subresource()))
    if len(data) != 16 * 16 * 16:
        raise RuntimeError("Unexpected retained face byte count")
    # This fixture copies face 0/mip 0; its 256-byte row pitch has no padding.
    copied_face = bytes(controller.GetBufferData(cubes[0].copyDestination, 0, len(data)))
    if copied_face != data:
        raise RuntimeError("Texture readback differs from retained contents")
    scalar_checks = 0
    for pixel in struct.iter_unpack("<4f", data):
        if pixel != (2.0, 2.0, 2.0, 1.0):
            raise RuntimeError(f"Retained content changed: {pixel}")
        scalar_checks += 4
    original = bytes(controller.GetBufferData(metadata[0].copySource, 0, 32))
    copied = bytes(controller.GetBufferData(metadata[0].copyDestination, 0, 32))
    if copied != original:
        raise RuntimeError("Metadata readback differs from retained generation")
    scale, brightness, flags, revision, gain, precision, processed, specular = struct.unpack("<ffIIfIII", copied)
    if flags != 3 or revision != 2 or scale != 1 or brightness != 2 or precision != 1 or not math.isfinite(gain):
        raise RuntimeError("Retained generation metadata is incomplete or mismatched")
    result = {"verdict": "pass", "capture": str(capture_path), "revision": revision,
              "cube_copy_event": cubes[0].eventId, "metadata_copy_event": metadata[0].eventId,
              "scalar_checks": scalar_checks, "metadata_bytes": 32,
              "scope": "Renderer shutdown occurs before this fixture's captured readback; native test owns admission/lifetime assertions"}
    Path(report_path).with_suffix(".json").write_text(json.dumps(result, indent=2) + "\n")
    report.append(f"ibl_capture_lease_verdict=pass revision={revision} scalars={scalar_checks}")


if __name__ == "__main__":
    run_ui_script("_ibl_capture_lease.txt", build_report)
