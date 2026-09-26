"""Verify Stage 13 adds sky lighting once to real deferred scene pixels."""

import json
import gzip
import hashlib
import math
import os
from pathlib import Path
import struct
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "shadows"))
from renderdoc_ui_analysis import collect_action_records, renderdoc_module, resource_id_to_name, run_ui_script


def appearance_report(controller, report, capture_path, report_path, actions, enabled):
    """Export matched town images and the actual inputs at the IBL boundary."""
    rd = renderdoc_module()
    names = resource_id_to_name(controller)
    textures = {str(t.resourceId): t for t in controller.GetTextures()}
    output = Path(report_path)
    products = {}

    def draw(scope, count=1):
        found = [a for a in actions if a.flags & rd.ActionFlags.Drawcall and scope in a.path]
        if len(found) != count:
            raise RuntimeError(f"Expected {count} {scope} draws, got {len(found)}")
        return found[0] if found else None

    def save_texture(label, resource):
        desc = textures[str(resource)]
        raw = bytes(controller.GetTextureData(resource, rd.Subresource()))
        path = output.with_name(output.stem + "-" + label + ".bin.gz")
        path.write_bytes(gzip.compress(raw, mtime=0))
        products[label] = {"file": path.name, "width": desc.width, "height": desc.height,
                           "format": desc.format.Name(), "component_bytes": desc.format.compByteWidth,
                           "component_count": desc.format.compCount,
                           "raw_bytes": len(raw), "raw_sha256": hashlib.sha256(raw).hexdigest()}

    direct = draw("Stage12.DirectionalLight")
    indirect = draw("Stage13.IndirectLighting", int(enabled))
    controller.SetFrameEvent(direct.event_id, True)
    pipeline = controller.GetPipelineState()
    scene_color = pipeline.GetOutputTargets()[0].resource
    save_texture("direct", scene_color)
    reads = [x.descriptor for x in pipeline.GetReadOnlyResources(rd.ShaderStage.Pixel, True)]
    for name in ("SceneDepth", "GBufferBaseColor", "GBufferNormal", "GBufferMaterial"):
        resources = [x.resource for x in reads if names.get(str(x.resource)) == name]
        if len(resources) != 1:
            raise RuntimeError(f"Missing/ambiguous input {name}")
        save_texture(name, resources[0])
    metadata = None
    if indirect:
        controller.SetFrameEvent(indirect.event_id, True)
        pipeline = controller.GetPipelineState()
        if pipeline.GetOutputTargets()[0].resource != scene_color:
            raise RuntimeError("IBL did not add to the direct-light target")
        reads = [x.descriptor for x in pipeline.GetReadOnlyResources(rd.ShaderStage.Pixel, True)]
        binding = next(x for x in reads if names.get(str(x.resource)) == "IBL.Metadata")
        scale, brightness, flags, revision = struct.unpack(
            "<ffII", bytes(controller.GetBufferData(binding.resource, binding.byteOffset, 16)))
        if flags != 3 or revision == 0 or not all(math.isfinite(v) and v > 0 for v in (scale, brightness)):
            raise RuntimeError("IBL used incomplete or nonfinite products")
        metadata = {"scale": scale, "brightness": brightness, "flags": flags, "revision": revision}
    save_texture("indirect", scene_color)
    tone = draw("Vortex.PostProcess.Tonemap")
    controller.SetFrameEvent(tone.event_id, True)
    pipeline = controller.GetPipelineState()
    reads = [x.descriptor for x in pipeline.GetReadOnlyResources(rd.ShaderStage.Pixel, True)]
    state = next(x for x in reads if names.get(str(x.resource)) == "Vortex.PostProcess.Exposure.State")
    raw = bytes(controller.GetBufferData(state.resource, state.byteOffset, 80))
    gain = struct.unpack_from("<f", raw)[0]
    flags = struct.unpack_from("<I", raw, 24)[0]
    if gain != 2 ** -12 or flags & 15 != 3:
        raise RuntimeError(f"Expected fixed manual EV12, got gain={gain}, flags={flags}")
    final = pipeline.GetOutputTargets()[0].resource
    save_texture("output", final)
    save = rd.TextureSave()
    save.resourceId = final
    save.destType = rd.FileType.PNG
    save.mip = save.slice.sliceIndex = save.sample.sampleIndex = 0
    save.alpha = rd.AlphaMapping.Preserve
    if not controller.SaveTexture(save, str(output.with_suffix(".png"))):
        raise RuntimeError("Could not export the tonemapped scene")
    result = {"verdict": "pass", "capture": str(capture_path), "enabled": enabled,
              "direct_event": direct.event_id, "indirect_event": indirect.event_id if indirect else None,
              "tonemap_event": tone.event_id, "exposure_gain": gain, "exposure_flags": flags,
              "ibl_metadata": metadata, "products": products,
              "scope": "Single-view export; pair comparison remains a separate check"}
    output.with_suffix(".json").write_text(json.dumps(result, indent=2) + "\n")
    report.append(f"ibl_appearance_export=pass enabled={enabled} exposure_gain={gain}")


def compare_appearance(off_path, on_path, report_path):
    """Compare the fixed town4new camera's IBL-off/on GPU exports."""
    import numpy as np

    paths = (Path(off_path), Path(on_path))
    records = [json.loads(path.read_text()) for path in paths]
    if [record["enabled"] for record in records] != [False, True]:
        raise RuntimeError("Expected an off/on pair")
    if any(record["verdict"] != "pass" or record["exposure_gain"] != 2 ** -12 for record in records):
        raise RuntimeError("Both exports must qualify fixed EV12")

    def raw(index, label):
        product = records[index]["products"][label]
        expected_format = {
            "direct": "R32G32B32A32_FLOAT", "indirect": "R32G32B32A32_FLOAT",
            "SceneDepth": "D32S8_TYPELESS", "GBufferBaseColor": "R8G8B8A8_SRGB",
            "GBufferNormal": "R10G10B10A2_UNORM", "GBufferMaterial": "R8G8B8A8_UNORM",
            "output": "R8G8B8A8_UNORM",
        }[label]
        if (product["width"], product["height"], product["format"]) != (1920, 1080, expected_format):
            raise RuntimeError(f"Unexpected product layout {index}:{label}")
        data = gzip.decompress((paths[index].parent / product["file"]).read_bytes())
        if len(data) != product["raw_bytes"] or hashlib.sha256(data).hexdigest() != product["raw_sha256"]:
            raise RuntimeError(f"Changed product {index}:{label}")
        return data

    matched = ("direct", "SceneDepth", "GBufferBaseColor", "GBufferNormal", "GBufferMaterial")
    for label in matched:
        if raw(0, label) != raw(1, label):
            raise RuntimeError(f"The pair differs before IBL: {label}")
    desc = records[0]["products"]["direct"]
    if (desc["width"], desc["height"], desc["format"]) != (1920, 1080, "R32G32B32A32_FLOAT"):
        raise RuntimeError("Expected the fixed 1080p town4new reference")
    before = np.frombuffer(raw(0, "direct"), dtype="<f4").reshape(1080, 1920, 4)
    off = np.frombuffer(raw(0, "indirect"), dtype="<f4").reshape(before.shape)
    after = np.frombuffer(raw(1, "indirect"), dtype="<f4").reshape(before.shape)
    depth = np.frombuffer(raw(0, "SceneDepth"), dtype=[("depth", "<f4"), ("stencil", "<u4")])["depth"].reshape(1080, 1920)
    sky = depth == 0
    geometry = ~sky
    if not all(np.isfinite(x).all() for x in (before, after, depth)):
        raise RuntimeError("Nonfinite scene/depth data")
    if not np.array_equal(before, off) or not np.array_equal(before[sky], after[sky]):
        raise RuntimeError("Disabled IBL or background preservation failed")
    if not np.array_equal(before[:, :, 3], after[:, :, 3]) or np.any(after[:, :, :3] < before[:, :, :3]):
        raise RuntimeError("IBL changed coverage or subtracted radiance")
    black = geometry & (np.max(before[:, :, :3], axis=2) <= 1e-7)
    lifted = black & (np.max(after[:, :, :3], axis=2) > 1e-6)
    changed = geometry & np.any(before != after, axis=2)
    output = [np.frombuffer(raw(i, "output"), dtype="u1").reshape(1080, 1920, 4) for i in range(2)]
    if not np.array_equal(output[0][sky], output[1][sky]):
        raise RuntimeError("The displayed sky changed between the pair")
    if min(np.count_nonzero(lifted), np.count_nonzero(sky)) < 1000:
        raise RuntimeError("Insufficient shaded geometry/sky coverage")
    regions = {}
    for name, (x0, y0, x1, y1) in {
        "shadow_facade": (580, 420, 680, 760),
        "sunlit_facade": (735, 420, 835, 760),
    }.items():
        area = np.s_[y0:y1, x0:x1]
        if not geometry[area].all():
            raise RuntimeError(f"Facade reference contains background: {name}")
        values = {}
        for index, (phase, data) in enumerate((("off", before), ("on", after))):
            luminance = data[area][:, :, :3] @ np.array([.2126, .7152, .0722])
            display = output[index][area][:, :, :3] @ np.array([.2126, .7152, .0722])
            values[phase] = {"mean_scene_luminance": float(luminance.mean()),
                             "p05": float(np.percentile(luminance, 5)),
                             "p95": float(np.percentile(luminance, 95)),
                             "mean_display_code": float(display.mean()),
                             "display_p05": float(np.percentile(display, 5)),
                             "display_p95": float(np.percentile(display, 95))}
        regions[name] = {"rectangle_xyxy": [x0, y0, x1, y1], **values}
    shadow = regions["shadow_facade"]
    sun = regions["sunlit_facade"]
    if not (shadow["off"]["mean_scene_luminance"] <= 1e-7
            < shadow["on"]["mean_scene_luminance"] < sun["on"]["mean_scene_luminance"]
            and shadow["on"]["p95"] > shadow["on"]["p05"]):
        raise RuntimeError("Shadowed facade lacks readable detail or sun/shadow contrast")
    if not (shadow["off"]["mean_display_code"] < shadow["on"]["mean_display_code"]
            < sun["on"]["mean_display_code"]
            and shadow["on"]["display_p95"] > shadow["on"]["display_p05"]):
        raise RuntimeError("Displayed facade lacks readable detail or sun/shadow contrast")
    result = {"verdict": "pass", "byte_identical_inputs": list(matched), "exposure_gain": 2 ** -12,
              "unchanged_displayed_sky_pixels": int(sky.sum()),
              "newly_illuminated_black_geometry_pixels": int(lifted.sum()),
              "geometry_pixels_receiving_ibl": int(changed.sum()), "facades": regions,
              "sun_shadow_mean_luminance_ratio": sun["on"]["mean_scene_luminance"] / shadow["on"]["mean_scene_luminance"],
              "inputs": {str(p): hashlib.sha256(p.read_bytes()).hexdigest() for p in paths},
              "scope": "Matched native town4new appearance; editor and other-view agreement are separate gates"}
    Path(report_path).write_text(json.dumps(result, indent=2) + "\n")
    print(json.dumps(result, indent=2))


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
    path_modes = {"IblPathDeferred": ("deferred", "Stage13.IndirectLighting"),
                  "IblPathForward": ("forward", "Stage9.BasePass.Forward"),
                  "IblPathTranslucent": ("translucent", "Stage18.Translucency"),
                  "IblPathCanonical": ("canonical", "Stage9.BasePass.Forward")}
    path_mode = os.environ.get("OXYGEN_RENDERDOC_PASS_NAME")
    if path_mode in path_modes:
        names = resource_id_to_name(controller)
        routes = {}
        sampled_cubes = set()
        for name, scope in (path_modes[path_mode],):
            draws = [a for a in actions if a.flags & rd.ActionFlags.Drawcall and scope in a.path]
            if not draws:
                raise RuntimeError(f"Missing actual {name} draws")
            shaders, ibl_draws = set(), []
            # Use one representative event per fresh replay. Bindless feedback
            # for this multi-frame capture is not reliable after shader reuse.
            selected = draws[-1:] if name == "canonical" else draws[:1]
            for draw in selected:
                controller.SetFrameEvent(draw.event_id, True)
                pipeline = controller.GetPipelineState()
                shader = pipeline.GetShaderReflection(rd.ShaderStage.Pixel)
                if not shader:
                    raise RuntimeError("Surface draw has no pixel shader")
                shaders.add(shader.entryPoint)
                reads = [x.descriptor for x in pipeline.GetReadOnlyResources(rd.ShaderStage.Pixel, True)]
                metadata = [x for x in reads if names.get(str(x.resource)) == "IBL.Metadata"]
                if metadata:
                    flags, revision = struct.unpack("<II", bytes(controller.GetBufferData(metadata[0].resource, metadata[0].byteOffset + 8, 8)))
                    if flags != 3 or revision == 0:
                        raise RuntimeError("Surface sampled incomplete IBL")
                    ibl_draws.append(draw.event_id)
                    sampled_cubes.update(names.get(str(x.resource)) for x in reads
                                         if names.get(str(x.resource)) in ("IBL.SpecularCube", "IBL.SpecularHalfCube"))
            if not ibl_draws:
                raise RuntimeError(f"No {name} draw sampled ready IBL")
            routes[name] = {"draw_count": len(draws), "ibl_events": ibl_draws, "pixel_entry_points": sorted(shaders)}
        expected_cube = "IBL.SpecularCube" if path_mode == "IblPathCanonical" else "IBL.SpecularHalfCube"
        if sampled_cubes != {expected_cube}:
            raise RuntimeError(f"Expected {expected_cube}, got {sampled_cubes}")
        result = {"verdict": "pass", "routes": routes, "sampled_cubes": sorted(sampled_cubes),
                  "scope": "Actual surface shader paths; numerical image checks are in the native matrix and its raw-array summary"}
        Path(report_path).with_suffix(".json").write_text(json.dumps(result, indent=2) + "\n")
        report.append(f"ibl_path_matrix=pass route={path_modes[path_mode][0]}")
        return
    appearance = os.environ.get("OXYGEN_RENDERDOC_PASS_NAME", "")
    if appearance in ("IblAppearanceOn", "IblAppearanceOff"):
        appearance_report(controller, report, capture_path, report_path, actions, appearance == "IblAppearanceOn")
        return
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
    if len(sys.argv) == 5 and sys.argv[1] == "--compare-appearance":
        compare_appearance(*sys.argv[2:])
    else:
        run_ui_script("_ibl_integration.txt", build_report)
