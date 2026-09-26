"""Verify Stage 13 adds sky lighting once to real deferred scene pixels."""

import json
import math
import os
from pathlib import Path
import struct
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "shadows"))
from renderdoc_ui_analysis import collect_action_records, renderdoc_module, resource_id_to_name, run_ui_script


def precision_report(controller, report, capture_path, report_path, indirect):
    rd = renderdoc_module()
    if len(indirect) != 2:
        raise RuntimeError(f"Expected canonical and half draws, got {len(indirect)}")
    names = resource_id_to_name(controller)
    textures = {str(t.resourceId): t for t in controller.GetTextures()}
    rows = []
    for action in indirect:
        controller.SetFrameEvent(action.event_id, True)
        pipeline = controller.GetPipelineState()
        if pipeline.GetShaderReflection(rd.ShaderStage.Pixel).entryPoint != "DeferredIblPS":
            raise RuntimeError("Unexpected precision-comparison consumer")
        reads = [x.descriptor for x in pipeline.GetReadOnlyResources(rd.ShaderStage.Pixel, True)]
        metadata = next(x for x in reads if names.get(str(x.resource)) == "IBL.Metadata")
        scale, brightness, flags, revision, gain, precision, half_source, half_specular = struct.unpack(
            "<ffIIfIII", bytes(controller.GetBufferData(metadata.resource, metadata.byteOffset, 32)))
        if flags != 3 or revision == 0 or not all(math.isfinite(v) for v in (scale, brightness, gain)):
            raise RuntimeError("Incomplete/nonfinite generation")
        if gain <= 1 or half_specular == 0xffffffff:
            raise RuntimeError("Captured generation is not eligible for half sampling")
        expected_source = "IBL.SpecularCube" if precision == 0 else "IBL.SpecularHalfCube"
        other_source = "IBL.SpecularHalfCube" if precision == 0 else "IBL.SpecularCube"
        sampled_names = {names.get(str(x.resource), "") for x in reads}
        if expected_source not in sampled_names or other_source in sampled_names:
            raise RuntimeError("The draw did not sample the selected representation")
        sampled = next(x.resource for x in reads if names.get(str(x.resource)) == expected_source)
        if textures[str(sampled)].format.compByteWidth != (4 if precision == 0 else 2):
            raise RuntimeError("Unexpected selected cube format")
        target = pipeline.GetOutputTargets()[0].resource
        desc = textures[str(target)]
        if (desc.width, desc.height, desc.format.compByteWidth) != (1, 1, 4):
            raise RuntimeError("Expected one FP32 scene-color pixel")
        after = struct.unpack("<4f", bytes(controller.GetTextureData(target, rd.Subresource())))
        controller.SetFrameEvent(action.event_id - 1, True)
        before = struct.unpack("<4f", bytes(controller.GetTextureData(target, rd.Subresource())))
        if after[3] != before[3]:
            raise RuntimeError("IBL changed coverage")
        rows.append({"event": action.event_id, "revision": revision, "precision_flags": precision,
                     "maximum_half_gain": gain, "half_specular_srv": half_specular,
                     "before": before, "after": after,
                     "contribution": [after[c] - before[c] for c in range(3)],
                     "sampled_resources": sorted(sampled_names),
                     "sampled_ibl_format": textures[str(sampled)].format.Name()})
    if [row["precision_flags"] for row in rows] != [0, 1] or rows[0]["revision"] != rows[1]["revision"]:
        raise RuntimeError("Expected one generation with only its precision admission changed")
    errors = []
    for reference, actual in zip(rows[0]["contribution"], rows[1]["contribution"]):
        if not (math.isfinite(reference) and math.isfinite(actual) and reference > 0 and actual > 0):
            raise RuntimeError("Missing positive IBL contribution")
        relative, ev = abs(actual - reference) / reference, abs(math.log2(actual / reference))
        if relative > 0.0025 or ev > 2 / 1024:
            raise RuntimeError(f"Native half output exceeds qualification: {relative}, {ev}")
        errors.append({"relative_rgb": relative, "ev": ev})
    if all(row["relative_rgb"] == 0 for row in errors):
        raise RuntimeError("Comparison did not distinguish the two representations")
    result = {"verdict": "pass", "capture": str(capture_path), "draws": rows, "errors": errors,
              "scope": "Captured atmosphere/fog; same-generation FP32 versus admitted FP16 Stage 13 contribution"}
    Path(report_path).with_suffix(".json").write_text(json.dumps(result, indent=2) + "\n")
    report.append("ibl_precision_verdict=pass same_generation=true draws=2")


def scheduling_report(controller, report, capture_path, report_path, indirect):
    rd = renderdoc_module()
    if len(indirect) != 4:
        raise RuntimeError(f"Expected four scheduled scene draws, got {len(indirect)}")
    names = resource_id_to_name(controller)
    initialized = {}
    completed = []
    producers = []
    for action in collect_action_records(controller):
        if not (action.flags & rd.ActionFlags.Dispatch and "Vortex.Environment.IBL.Process" in action.path):
            continue
        controller.SetFrameEvent(action.event_id, True)
        pipeline = controller.GetPipelineState()
        shader = pipeline.GetShaderReflection(rd.ShaderStage.Compute)
        if not shader or shader.entryPoint not in ("IblInitializeCS", "IblCompleteCS"):
            continue
        writes = [x.descriptor for x in pipeline.GetReadWriteResources(rd.ShaderStage.Compute, True)]
        metadata = next(x for x in writes if names.get(str(x.resource)) == "IBL.Metadata")
        _, _, flags, revision = struct.unpack(
            "<ffII", bytes(controller.GetBufferData(metadata.resource, metadata.byteOffset, 16)))
        frame_index = next(i for i, draw in enumerate(indirect) if draw.event_id > action.event_id)
        if shader.entryPoint == "IblInitializeCS":
            if flags != 0 or revision <= 1 or revision in initialized:
                raise RuntimeError("Invalid or duplicate candidate initialization")
            # The native fixture changes uniform fog to frame_index + 2.
            initialized[revision] = (frame_index, float(frame_index + 2))
        else:
            if flags != 3 or revision not in initialized or frame_index - initialized[revision][0] >= 4:
                raise RuntimeError("Candidate completion is invalid or missed its deadline")
            completed.append((action.event_id, revision))
        producers.append({"shader": shader.entryPoint, "event": action.event_id,
                          "revision": revision, "frame": frame_index + 1})
    if not completed or completed[0][1] != 2:
        raise RuntimeError("The first candidate did not complete within the four-frame capture")
    rows = []
    for action in indirect:
        ready = [(event, revision) for event, revision in completed if event < action.event_id]
        expected_revision = ready[-1][1] if ready else 1
        expected_light = initialized[expected_revision][1] if ready else 1.0
        controller.SetFrameEvent(action.event_id, True)
        pipeline = controller.GetPipelineState()
        if pipeline.GetShaderReflection(rd.ShaderStage.Pixel).entryPoint != "DeferredIblPS":
            raise RuntimeError("Unexpected scheduled IBL consumer")
        reads = [x.descriptor for x in pipeline.GetReadOnlyResources(rd.ShaderStage.Pixel, True)]
        metadata = next(x for x in reads if names.get(str(x.resource)) == "IBL.Metadata")
        _, _, flags, revision = struct.unpack(
            "<ffII", bytes(controller.GetBufferData(metadata.resource, metadata.byteOffset, 16)))
        if flags != 3 or revision != expected_revision:
            raise RuntimeError(f"Wrong/incomplete bound generation at {action.event_id}: {flags}, {revision}")
        target = pipeline.GetOutputTargets()[0].resource
        raw = bytes(controller.GetTextureData(target, rd.Subresource()))
        if len(raw) != 16:
            raise RuntimeError("Expected one FP32 scene-color pixel")
        pixel = struct.unpack("<4f", raw)
        if any(not math.isfinite(value) or abs(value - expected_light) > 0.002 for value in pixel[:3]):
            raise RuntimeError(f"Scheduled lighting changed before complete publication: {pixel}")
        rows.append({"event": action.event_id, "bound_revision": revision, "flags": flags, "pixel": pixel})
    if rows[0]["bound_revision"] != 1:
        raise RuntimeError("Fixture did not exercise retained lighting during incremental work")
    result = {"verdict": "pass", "capture": str(capture_path), "draws": rows,
              "candidate_producers": producers,
              "scope": "Four diagnostic scene frames: each draw uses the latest complete generation and its frozen fog value. Adaptive completion may precede frame four; no timing or maximum-in-flight claim."}
    Path(report_path).with_suffix(".json").write_text(json.dumps(result, indent=2) + "\n")
    report.append("ibl_scheduling_verdict=pass consumer_revisions=" + ",".join(str(row["bound_revision"]) for row in rows))


def build_report(controller, report, capture_path, report_path):
    rd = renderdoc_module()
    raster_reference = os.environ.get("OXYGEN_RENDERDOC_PASS_NAME") == "IblRaster"
    actions = collect_action_records(controller)
    if any("Stage12.StaticSkyLight" in a.path for a in actions):
        raise RuntimeError("The retired Stage 12 sky-light draw is still present")
    indirect = [a for a in actions if a.flags & rd.ActionFlags.Drawcall and "Stage13.IndirectLighting" in a.path]
    if os.environ.get("OXYGEN_RENDERDOC_PASS_NAME") == "IblScheduling":
        scheduling_report(controller, report, capture_path, report_path, indirect)
        return
    if os.environ.get("OXYGEN_RENDERDOC_PASS_NAME") == "IblPrecision":
        precision_report(controller, report, capture_path, report_path, indirect)
        return
    if len(indirect) != 1:
        raise RuntimeError(f"Expected one deferred indirect draw, got {len(indirect)}")
    event = indirect[0].event_id
    controller.SetFrameEvent(event, True)
    pipeline = controller.GetPipelineState()
    shader = pipeline.GetShaderReflection(rd.ShaderStage.Pixel)
    if shader.entryPoint != "DeferredIblPS":
        raise RuntimeError("Stage 13 is not using the common IBL surface pass")
    names = resource_id_to_name(controller)
    textures = {str(t.resourceId): t for t in controller.GetTextures()}
    reads = [x.descriptor for x in pipeline.GetReadOnlyResources(rd.ShaderStage.Pixel, True)]
    metadata = next(x for x in reads if names.get(str(x.resource)) == "IBL.Metadata")
    scale, brightness, flags, revision = struct.unpack("<ffII", bytes(controller.GetBufferData(metadata.resource, metadata.byteOffset, 16)))
    if flags != 3 or revision == 0 or not all(math.isfinite(v) and v > 0 for v in (scale, brightness)):
        raise RuntimeError("The deferred consumer does not have complete finite IBL")
    depth_resource = next(x.resource for x in reads if names.get(str(x.resource)) == "SceneDepth")
    depth_desc = textures[str(depth_resource)]
    raw = bytes(controller.GetTextureData(depth_resource, rd.Subresource()))
    depth_stride = len(raw) // (depth_desc.width * depth_desc.height)
    if depth_stride not in (4, 8):
        raise RuntimeError(f"Unexpected depth storage: {depth_desc.format.Name()}, {depth_stride}")
    depth = [struct.unpack_from("<f", raw, offset)[0] for offset in range(0, len(raw), depth_stride)]
    target = pipeline.GetOutputTargets()[0].resource
    desc = textures[str(target)]
    kind = "f" if desc.format.compByteWidth == 4 else "e"
    after = list(struct.iter_unpack("<4" + kind, bytes(controller.GetTextureData(target, rd.Subresource()))))
    controller.SetFrameEvent(event - 1, True)
    before = list(struct.iter_unpack("<4" + kind, bytes(controller.GetTextureData(target, rd.Subresource()))))
    if len(before) != desc.width * desc.height or len(after) != len(before) or len(depth) != len(before):
        raise RuntimeError("Mismatched scene/depth dimensions")
    illuminated, untouched_sky, examples = 0, 0, []
    for index, (a, b, z) in enumerate(zip(before, after, depth)):
        if any(not math.isfinite(v) for v in b):
            raise RuntimeError("Non-finite scene output")
        if z == 0:
            if a != b:
                raise RuntimeError("Deferred IBL modified a sky/background pixel")
            untouched_sky += 1
        elif max(a[:3]) <= 1.0e-7 and max(b[:3]) > 1.0e-6:
            illuminated += 1
            if len(examples) < 8:
                examples.append({"x": index % desc.width, "y": index // desc.width,
                                 "before": a, "after": b, "depth": z})
        if abs(a[3] - b[3]) > 1.0e-6:
            raise RuntimeError("Indirect lighting changed opaque coverage")
    reference = None
    if raster_reference:
        if (desc.width, desc.height) != (1, 1):
            raise RuntimeError("Expected the one-pixel IBL raster reference")
        controller.SetFrameEvent(event, True)
        material_id = next(x.resource for x in reads if names.get(str(x.resource)) == "GBufferMaterial")
        material = bytes(controller.GetTextureData(material_id, rd.Subresource()))
        if len(material) != 4 or material[0] != 0 or material[2] != 0:
            raise RuntimeError("Reference must be a smooth dielectric")
        f0 = 0.08 * material[1] / 255.0
        reference = [(1.0 + f0) * 0.25, f0 * 0.5, 1.0 + f0, 1.0]
        for actual, expected in zip(after[0], reference):
            if abs(actual - expected) > 1.0e-5:
                raise RuntimeError(f"Raster reference mismatch: {after[0]} vs {reference}")
    if illuminated < (1 if raster_reference else 1000) or untouched_sky < (0 if raster_reference else 1000):
        raise RuntimeError(f"Insufficient geometry/sky proof: {illuminated}, {untouched_sky}")
    result = {"verdict": "pass", "capture": str(capture_path), "event": event,
              "width": desc.width, "height": desc.height, "revision": revision,
              "source_radiance_scale": scale, "average_brightness": brightness,
              "newly_illuminated_geometry_pixels": illuminated, "unchanged_sky_pixels": untouched_sky,
              "examples": examples, "raster_reference": reference,
              "scope": "One-pixel deferred diffuse/specular reference and absent ambient bridge" if raster_reference else "Same-frame deferred contribution and absent ambient bridge"}
    Path(report_path).with_suffix(".json").write_text(json.dumps(result, indent=2) + "\n")
    report.append(f"ibl_integration_verdict=pass illuminated={illuminated} unchanged_sky={untouched_sky}")


if __name__ == "__main__":
    run_ui_script("_ibl_integration.txt", build_report)
