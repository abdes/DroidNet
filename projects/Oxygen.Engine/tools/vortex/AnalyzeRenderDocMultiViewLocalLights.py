"""Check that both local lights contribute in the default MultiView scene."""

import json
import math
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
    textures = {str(t.resourceId): t for t in controller.GetTextures()}
    actions = collect_action_records(controller)
    results = []
    for kind in ("Point", "Spot"):
        draws = [a for a in actions if a.flags & rd.ActionFlags.Drawcall
                 and f"Vortex.Stage12.{kind}Light" in a.path]
        if len(draws) != 1:
            raise RuntimeError(f"Default MultiView requires one {kind} draw, got {len(draws)}")
        draw = draws[0]
        controller.SetFrameEvent(draw.event_id, True)
        target = controller.GetPipelineState().GetOutputTargets()[0].resource
        texture = textures[str(target)]
        if texture.format.compCount != 4 or texture.format.compByteWidth not in (2, 4):
            raise RuntimeError("Expected an RGBA floating-point HDR target")
        subresource = rd.Subresource()
        after = bytes(controller.GetTextureData(target, subresource))
        controller.SetFrameEvent(draw.event_id - 1, True)
        before = bytes(controller.GetTextureData(target, subresource))
        changed_target = before != after
        pixel_format = "<4e" if texture.format.compByteWidth == 2 else "<4f"
        pixel_stride = texture.format.compByteWidth * 4
        changed_samples = 0
        maximum_delta = 0.0
        for grid_y in range(1, 64):
            y = grid_y * texture.height // 64
            for grid_x in range(1, 64):
                x = grid_x * texture.width // 64
                offset = (y * texture.width + x) * pixel_stride
                pre = struct.unpack_from(pixel_format, before, offset)[:3]
                post = struct.unpack_from(pixel_format, after, offset)[:3]
                if not all(math.isfinite(v) for v in (*pre, *post)):
                    raise RuntimeError(f"Nonfinite {kind} lighting at ({x}, {y})")
                delta = max(post[c] - pre[c] for c in range(3))
                changed_samples += delta > 0.0
                maximum_delta = max(maximum_delta, delta)
        result = {"kind": kind, "event": draw.event_id,
                  "target": names.get(str(target)), "target_changed": changed_target,
                  "positive_samples": changed_samples, "maximum_delta": maximum_delta}
        results.append(result)
        report.append(json.dumps(result))
        if not changed_target or not changed_samples:
            raise RuntimeError(f"The default {kind} light contributes no sampled HDR signal")
    Path(report_path).with_suffix(".json").write_text(json.dumps(results, indent=2))
    report.append("local_light_coverage_verdict=pass")
    report.append("scope=default MultiView local-light coverage; not calibrated photometric or BRDF parity")


if __name__ == "__main__":
    run_ui_script("_multiview_local_lights.txt", build_report)
