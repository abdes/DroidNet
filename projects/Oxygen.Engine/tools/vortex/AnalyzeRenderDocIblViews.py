"""Export IBL view images and compare live-editor views with their cooked scene."""

import gzip
import hashlib
import json
import math
import os
from pathlib import Path
import struct
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "shadows"))
from renderdoc_ui_analysis import collect_action_records, renderdoc_module, resource_id_to_name, run_ui_script


def export_views(controller, report, capture_path, report_path):
    rd = renderdoc_module()
    mode = os.environ["OXYGEN_RENDERDOC_PASS_NAME"]
    expected_views = {"IblEditorViews": 2, "IblNativeViews": 1}[mode]
    path = Path(report_path)
    names = resource_id_to_name(controller)
    textures = {str(t.resourceId): t for t in controller.GetTextures()}
    actions = collect_action_records(controller)
    if any("Stage12.StaticSkyLight" in a.path for a in actions):
        raise RuntimeError("Retired ambient bridge is present")
    indirect = [a for a in actions if a.flags & rd.ActionFlags.Drawcall and "Stage13.IndirectLighting" in a.path]
    tones = [a for a in actions if a.flags & rd.ActionFlags.Drawcall and "Vortex.PostProcess.Tonemap" in a.path]
    records, incomplete, claimed_tones = [], [], set()

    def binding(reads, name):
        found = [x for x in reads if names.get(str(x.resource)) == name]
        if len(found) != 1:
            raise RuntimeError(f"Missing/ambiguous binding {name}")
        return found[0]

    def blob(raw, layout):
        digest = hashlib.sha256(raw).hexdigest()
        destination = path.with_name(path.stem + "-" + digest + ".bin.gz")
        if not destination.exists():
            destination.write_bytes(gzip.compress(raw, mtime=0))
        elif gzip.decompress(destination.read_bytes()) != raw:
            raise RuntimeError("Existing pixel export differs")
        return {**layout, "file": destination.name, "raw_bytes": len(raw), "raw_sha256": digest}

    def texture(resource):
        desc = textures[str(resource)]
        return blob(bytes(controller.GetTextureData(resource, rd.Subresource())),
                    {"width": desc.width, "height": desc.height, "format": desc.format.Name()})

    for index, action in enumerate(indirect):
        controller.SetFrameEvent(action.event_id, True)
        pipeline = controller.GetPipelineState()
        if pipeline.GetShaderReflection(rd.ShaderStage.Pixel).entryPoint != "DeferredIblPS":
            raise RuntimeError("Unexpected indirect-light consumer")
        blocks = [x.descriptor for x in pipeline.GetConstantBlocks(rd.ShaderStage.Pixel) if x.descriptor.byteSize == 256]
        if len(blocks) != 1:
            raise RuntimeError("Missing/ambiguous ViewConstants")
        cb = blocks[0]
        constants = bytes(controller.GetBufferData(cb.resource, cb.byteOffset, 256))
        if not all(math.isfinite(v) for v in struct.unpack_from("<51f", constants, 16)):
            raise RuntimeError("Nonfinite view matrices/camera")
        frame = struct.unpack_from("<Q", constants)[0]
        matching_tones = [a for a in tones if a.event_id > action.event_id
                          and (index + 1 == len(indirect) or a.event_id < indirect[index + 1].event_id)]
        if not matching_tones:
            incomplete.append({"frame": frame, "event": action.event_id, "reason": "capture ends before tonemap"})
            continue
        if len(matching_tones) != 1:
            raise RuntimeError("Ambiguous IBL/tonemap pairing")
        reads = [x.descriptor for x in pipeline.GetReadOnlyResources(rd.ShaderStage.Pixel, True)]
        meta = binding(reads, "IBL.Metadata")
        scale, brightness, flags, revision = struct.unpack("<ffII", bytes(controller.GetBufferData(meta.resource, meta.byteOffset, 16)))
        if flags != 3 or revision == 0 or not all(math.isfinite(v) and v > 0 for v in (scale, brightness)):
            raise RuntimeError("Invalid IBL product metadata")
        products = {name: texture(binding(reads, name).resource)
                    for name in ("SceneDepth", "GBufferBaseColor", "GBufferNormal", "GBufferMaterial")}
        sh = binding(reads, "IBL.DiffuseSH")
        products["SH"] = blob(bytes(controller.GetBufferData(sh.resource, sh.byteOffset, 128)), {"format": "8xRGBA32F"})
        cubes = [x for x in reads if names.get(str(x.resource)) in ("IBL.SpecularCube", "IBL.SpecularHalfCube")]
        if len(cubes) != 1:
            raise RuntimeError("Expected one selected specular representation")
        cube = cubes[0].resource
        desc = textures[str(cube)]
        if desc.arraysize != 6:
            raise RuntimeError("Specular product is not a six-face cube")
        cube_bytes = bytearray()
        for mip in range(desc.mips):
            for face in range(6):
                sub = rd.Subresource()
                sub.mip, sub.slice, sub.sample = mip, face, 0
                cube_bytes.extend(controller.GetTextureData(cube, sub))
        products["specular"] = blob(bytes(cube_bytes), {"width": desc.width, "height": desc.height,
                                    "mips": desc.mips, "faces": 6, "format": desc.format.Name()})
        scene_color = pipeline.GetOutputTargets()[0].resource
        products["indirect"] = texture(scene_color)
        controller.SetFrameEvent(action.event_id - 1, True)
        products["direct"] = texture(scene_color)
        tone = matching_tones[0]
        claimed_tones.add(tone.event_id)
        controller.SetFrameEvent(tone.event_id, True)
        pipeline = controller.GetPipelineState()
        tone_reads = [x.descriptor for x in pipeline.GetReadOnlyResources(rd.ShaderStage.Pixel, True)]
        exposure = binding(tone_reads, "Vortex.PostProcess.Exposure.State")
        raw = bytes(controller.GetBufferData(exposure.resource, exposure.byteOffset, 80))
        gain, exposure_flags = struct.unpack_from("<f", raw)[0], struct.unpack_from("<I", raw, 24)[0]
        if gain != 2 ** -13 or exposure_flags & 15 != 3:
            raise RuntimeError("Expected the editor fixture's manual EV13")
        target = pipeline.GetOutputTargets()[0].resource
        products["output"] = texture(target)
        image_path = path.with_name(path.stem + "-" + products["output"]["raw_sha256"] + ".png")
        if not image_path.exists():
            save = rd.TextureSave()
            save.resourceId, save.destType = target, rd.FileType.PNG
            save.mip = save.slice.sliceIndex = save.sample.sampleIndex = 0
            save.alpha = rd.AlphaMapping.Preserve
            if not controller.SaveTexture(save, str(image_path)):
                raise RuntimeError("Scene image export failed")
        records.append({"frame": frame, "ibl_event": action.event_id, "tone_event": tone.event_id,
                        "view": list(struct.unpack_from("<16f", constants, 16)),
                        "projection": list(struct.unpack_from("<16f", constants, 80)),
                        "camera": list(struct.unpack_from("<3f", constants, 208)),
                        "metadata_resource": str(meta.resource), "revision": revision,
                        "scale": scale, "brightness": brightness, "exposure_gain": gain,
                        "output_resource": str(target), "output_name": names.get(str(target), ""),
                        "products": products, "image": image_path.name})

    all_frames = sorted({r["frame"] for r in records + incomplete})
    if not all_frames:
        raise RuntimeError("No rendered IBL views")
    complete, excluded = [], list(incomplete)
    for tone in tones:
        if tone.event_id not in claimed_tones:
            if not indirect or tone.event_id >= indirect[0].event_id:
                raise RuntimeError("Unpaired tonemap inside the captured interval")
            excluded.append({"event": tone.event_id, "reason": "capture begins after this view's IBL"})
    for frame in all_frames:
        group = [r for r in records if r["frame"] == frame]
        partial = any(r["frame"] == frame for r in incomplete)
        if partial or len(group) != expected_views:
            if frame not in (all_frames[0], all_frames[-1]) or len(group) > expected_views:
                raise RuntimeError("Incomplete or excess views inside the captured interval")
            excluded.extend({"frame": frame, "event": r["ibl_event"], "reason": "partial boundary frame"} for r in group)
            continue
        if len({r["output_resource"] for r in group}) != expected_views:
            raise RuntimeError("Repeated output cannot prove distinct views")
        complete.extend(group)
    frames = sorted({r["frame"] for r in complete})
    if len(frames) < (3 if expected_views == 2 else 1):
        raise RuntimeError("Insufficient complete frames")
    reference = complete[0]
    for row in complete:
        for name in ("camera", "view", "projection", "scale", "brightness", "revision", "metadata_resource"):
            if row[name] != reference[name]:
                raise RuntimeError(f"Editor/native fixture changed within its capture: {name}")
        for name, product in row["products"].items():
            if product != reference["products"][name]:
                raise RuntimeError(f"View/frame product differs: {name}")
    result = {"verdict": "pass", "capture": str(capture_path), "expected_views": expected_views,
              "complete_frames": frames, "excluded_boundaries": excluded, "records": complete}
    path.with_suffix(".json").write_text(json.dumps(result, indent=2) + "\n")
    report.append(f"ibl_views_export=pass frames={len(frames)} views={expected_views} excluded={len(excluded)}")


def compare_views(editor_path, native_path, report_path):
    """Check the same cooked scene, allowing only camera round-trip arithmetic."""
    import numpy as np

    paths = [Path(editor_path), Path(native_path)]
    reports = [json.loads(path.read_text()) for path in paths]
    if [r["expected_views"] for r in reports] != [2, 1] or any(r["verdict"] != "pass" for r in reports):
        raise RuntimeError("Expected qualified editor/native exports")
    limits = {"view_rotation_absolute": 1e-6, "view_translation_absolute": 1e-5,
              "depth_absolute": 1e-6, "hdr_absolute": 1e-6, "hdr_relative": 1e-4,
              "sky_display_codes": 1, "geometry_display_codes": 0}
    rows = [r["records"][0] for r in reports]
    raw_cache = {}

    def raw(host, product):
        key = (host, product["file"])
        if key not in raw_cache:
            raw_cache[key] = gzip.decompress((paths[host].parent / product["file"]).read_bytes())
        data = raw_cache[key]
        if len(data) != product["raw_bytes"] or hashlib.sha256(data).hexdigest() != product["raw_sha256"]:
            raise RuntimeError("Changed raw product")
        return data

    for host, record in enumerate(reports):
        minimum = 3 if host == 0 else 1
        if len(record["complete_frames"]) < minimum:
            raise RuntimeError("Insufficient complete-frame evidence")
        if sorted({r["frame"] for r in record["records"]}) != record["complete_frames"]:
            raise RuntimeError("Frame inventory differs")
        for frame in record["complete_frames"]:
            group = [r for r in record["records"] if r["frame"] == frame]
            if len(group) != record["expected_views"] or len({r["output_resource"] for r in group}) != len(group):
                raise RuntimeError("Missing or repeated view in complete frame")
        for row in record["records"]:
            for key in ("camera", "view", "projection", "scale", "brightness", "revision", "metadata_resource", "exposure_gain"):
                if row[key] != rows[host][key]:
                    raise RuntimeError("View/frame inputs changed within a host")
            for name, product in row["products"].items():
                if product != rows[host]["products"][name]:
                    raise RuntimeError("View/frame pixels changed within a host")
                raw(host, product)
    for key in ("camera", "projection", "scale", "brightness", "exposure_gain"):
        if rows[0][key] != rows[1][key]:
            raise RuntimeError(f"Cross-host input mismatch: {key}")
    matrices = [np.array(row["view"]).reshape((4, 4), order="F") for row in rows]
    for row, matrix in zip(rows, matrices):
        values = row["camera"] + row["projection"] + [row["scale"], row["brightness"], row["exposure_gain"]]
        if not np.isfinite(matrix).all() or not np.isfinite(values).all():
            raise RuntimeError("Nonfinite cross-host view inputs")
    matrix_delta = np.abs(matrices[0] - matrices[1])
    if (np.max(matrix_delta[:3, :3]) > limits["view_rotation_absolute"]
            or np.max(matrix_delta[:3, 3]) > limits["view_translation_absolute"]
            or not np.array_equal(matrices[0][3], matrices[1][3])):
        raise RuntimeError("Camera quaternion round-trip exceeds matrix limits")
    products = [row["products"] for row in rows]
    for label in products[0]:
        layouts = [{k: v for k, v in p[label].items() if k not in ("file", "raw_sha256")} for p in products]
        if layouts[0] != layouts[1]:
            raise RuntimeError(f"Cross-host layout differs: {label}")
    exact = ("GBufferBaseColor", "GBufferNormal", "GBufferMaterial", "SH", "specular", "direct")
    for name in exact:
        if raw(0, products[0][name]) != raw(1, products[1][name]):
            raise RuntimeError(f"Cross-host product differs: {name}")
    size = products[0]["indirect"]
    height, width = size["height"], size["width"]
    if size["format"] != "R32G32B32A32_FLOAT" or min(height, width) < 200:
        raise RuntimeError("Expected a material image in FP32 HDR")
    if products[0]["SceneDepth"]["format"] != "D32S8_TYPELESS" or products[0]["output"]["format"] != "R8G8B8A8_UNORM":
        raise RuntimeError("Unexpected depth/display layout")
    depth = [np.frombuffer(raw(i, p["SceneDepth"]), dtype=[("z", "<f4"), ("s", "<u4")])["z"].reshape(height, width)
             for i, p in enumerate(products)]
    geometry = depth[0] != 0
    if not np.array_equal(geometry, depth[1] != 0) or min(int(geometry.sum()), int((~geometry).sum())) < 100:
        raise RuntimeError("Geometry/background coverage differs or is insufficient")
    hdr = [np.frombuffer(raw(i, p["indirect"]), dtype="<f4").reshape(height, width, 4).astype("f8")
           for i, p in enumerate(products)]
    if not all(np.isfinite(value).all() for value in depth + hdr):
        raise RuntimeError("Nonfinite HDR/depth product")
    depth_delta = np.abs(depth[0].astype("f8") - depth[1])
    if np.max(depth_delta) > limits["depth_absolute"]:
        raise RuntimeError("Cross-host depth arithmetic exceeds its limit")
    difference = np.abs(hdr[0] - hdr[1])
    if np.any(difference > limits["hdr_absolute"] + limits["hdr_relative"] * np.abs(hdr[0])):
        raise RuntimeError("Cross-host HDR arithmetic exceeds its limit")
    direct = np.frombuffer(raw(0, products[0]["direct"]), dtype="<f4").reshape(height, width, 4)
    if np.count_nonzero(geometry & np.any(hdr[0][:, :, :3] > direct[:, :, :3] + 1e-6, axis=2)) < 100:
        raise RuntimeError("IBL did not visibly illuminate geometry")
    if any(not np.array_equal(direct[:, :, 3], value[:, :, 3]) for value in hdr):
        raise RuntimeError("IBL changed surface coverage")
    displayed = [np.frombuffer(raw(i, p["output"]), dtype="u1").reshape(height, width, 4).astype("i2")
                 for i, p in enumerate(products)]
    codes = np.abs(displayed[0] - displayed[1])
    if np.any(codes[geometry]) or np.max(codes[~geometry]) > limits["sky_display_codes"] or np.any(codes[:, :, 3]):
        raise RuntimeError("Displayed geometry/sky exceeds the cross-host image limits")
    result = {"verdict": "pass", "width": width, "height": height,
              "editor_complete_frames": reports[0]["complete_frames"],
              "editor_excluded_boundaries": reports[0]["excluded_boundaries"],
              "native_excluded_boundaries": reports[1]["excluded_boundaries"],
              "byte_identical_products": list(exact), "geometry_pixels": int(geometry.sum()),
              "maximum_view_matrix_difference": float(matrix_delta.max()),
              "maximum_depth_difference": float(depth_delta.max()),
              "maximum_hdr_absolute_difference": float(difference.max()),
              "maximum_hdr_scaled_difference": float(np.max(difference / np.maximum(1, np.abs(hdr[0])))),
              "changed_display_pixels": int(np.count_nonzero(np.any(codes, axis=2))),
              "maximum_display_code_difference": int(codes.max()), "limits": limits,
              "scope": "Cross-host scene/camera transport; FP16 storage and filtering gates are unchanged",
              "inputs": {str(p): hashlib.sha256(p.read_bytes()).hexdigest() for p in paths}}
    Path(report_path).write_text(json.dumps(result, indent=2) + "\n")
    print(json.dumps(result, indent=2))


if __name__ == "__main__":
    if len(sys.argv) == 5 and sys.argv[1] == "--compare":
        compare_views(*sys.argv[2:])
    else:
        run_ui_script("_ibl_views.txt", export_views)
