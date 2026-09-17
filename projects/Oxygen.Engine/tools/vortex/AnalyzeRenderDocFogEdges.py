"""Audit controlled fog composition/history edge samples from their GPU inputs."""

import json
import math
import os
from pathlib import Path
import struct
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "shadows"))
from renderdoc_ui_analysis import collect_action_records, renderdoc_module, run_ui_script


def clamp(value):
    return min(1, max(0, value))


def build_report(controller, report, capture_path, report_path):
    rd = renderdoc_module()
    history = os.environ.get("OXYGEN_RENDERDOC_PASS_NAME") == "FogHistoryEdges"
    textures = {str(t.resourceId): t for t in controller.GetTextures()}
    checked = rejected = scalar_checks = 0
    maximum_error = 0.0
    for action in collect_action_records(controller):
        flag = rd.ActionFlags.Dispatch if history else rd.ActionFlags.Drawcall
        if not action.flags & flag:
            continue
        controller.SetFrameEvent(action.event_id, True)
        pipeline = controller.GetPipelineState()
        stage = rd.ShaderStage.Compute if history else rd.ShaderStage.Pixel
        shader = pipeline.GetShaderReflection(stage)
        entry = "VortexVolumetricFogCS" if history else "VortexFogPassPS"
        if not shader or shader.entryPoint != entry:
            continue
        reads = [x.descriptor for x in pipeline.GetReadOnlyResources(stage, True)]
        volumes = [x for x in reads if str(x.resource) in textures
                   and (textures[str(x.resource)].width, textures[str(x.resource)].height,
                        textures[str(x.resource)].depth) == (2, 2, 2)]
        for volume in volumes:
            desc = textures[str(volume.resource)]
            raw = bytes(controller.GetTextureData(volume.resource, rd.Subresource()))
            samples = list(struct.iter_unpack("<4e" if desc.format.compByteWidth == 2 else "<4f", raw))
            expected = [(x, y, z, 1-z) for z in range(2) for y in range(2) for x in range(2)]
            if samples != expected:
                raise RuntimeError("Contrasting source faces/slices were not bound")
        if history:
            controls = [x for x in reads if x.byteSize == x.elementByteSize == 544]
            histories = [x for x in reads if x.byteSize == x.elementByteSize == 576]
            if len(controls) != 1 or len(histories) != 1:
                raise RuntimeError("Missing history fixture constants")
            c, h = controls[0], histories[0]
            controls_data = bytes(controller.GetBufferData(c.resource, c.byteOffset, 544))
            if struct.unpack_from("<3I", controls_data, 4) != (1, 1, 2):
                raise RuntimeError("Unexpected history output extent")
            if struct.unpack_from("<If", controls_data, 148) != (1, 1):
                raise RuntimeError("History was not enabled at unit weight")
            raw = bytes(controller.GetBufferData(h.resource, h.byteOffset, 576))
            depth = -struct.unpack_from("<f", raw, 312)[0]
            ndc_x, ndc_y = struct.unpack_from("<2f", raw, 368)
            u, v = (ndc_x + 1) / 2, (1 - ndc_y) / 2
            outside = u < 0 or u > 1 or v < 0 or v > 1 or depth < 1 or depth >= 4
            rejected += int(outside)
            z = clamp(math.log2(depth))
            expected = (0, 0, 0, 1) if outside else (clamp(2*u-.5), clamp(2*v-.5), z, 1-z)
            writes = [x.descriptor for x in pipeline.GetReadWriteResources(stage, True)]
            outputs = [x for x in writes if str(x.resource) in textures]
            if len(outputs) != 1 or (not outside and len(volumes) != 1):
                raise RuntimeError("Missing history source/output")
            values = list(struct.iter_unpack("<4f", bytes(controller.GetTextureData(outputs[0].resource, rd.Subresource()))))
            expectations = [expected] * 2
            tolerance = 3e-6
        else:
            blocks = [x.descriptor for x in pipeline.GetConstantBlocks(stage) if x.descriptor.byteSize == 256]
            if len(blocks) != 1 or len(volumes) != 1:
                raise RuntimeError("Missing composition view/source")
            c = blocks[0]
            raw = bytes(controller.GetBufferData(c.resource, c.byteOffset, 256))
            distance = -struct.unpack_from("<f", raw, 200)[0]
            z = clamp(2 * math.sqrt(clamp(distance)) - .5)
            target = pipeline.GetOutputTargets()[0].resource
            values = list(struct.iter_unpack("<4f", bytes(controller.GetTextureData(target, rd.Subresource()))))
            expectations = [(clamp((x+.5)/4-.5), clamp((y+.5)/4-.5), z, z)
                            for y in range(8) for x in range(8)]
            tolerance = 3e-7
        if len(values) != len(expectations):
            raise RuntimeError("Output extent mismatch")
        for actual, expected in zip(values, expectations):
            for a, e in zip(actual, expected):
                error = abs(a-e)
                if not math.isfinite(a) or error > tolerance:
                    raise RuntimeError(f"Edge sampling mismatch at {action.event_id}: {actual} vs {expected}")
                maximum_error = max(maximum_error, error)
                scalar_checks += 1
        checked += 1
    if checked != (150 if history else 8):
        raise RuntimeError(f"Incomplete edge cases: {checked}")
    result = {"cases": checked, "scalar_checks": scalar_checks, "rejected_history_cases": rejected,
              "maximum_error": maximum_error, "verdict": "pass"}
    Path(report_path).with_suffix(".json").write_text(json.dumps(result, indent=2) + "\n")
    report.append(f"fog_edge_verdict=pass {result}")


if __name__ == "__main__":
    run_ui_script("_fog_edges.txt", build_report)
