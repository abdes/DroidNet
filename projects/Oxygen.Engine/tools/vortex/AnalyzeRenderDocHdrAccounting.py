"""Inspect HDR bindings, direct texture traffic and replay GPU durations."""

import json
import math
import statistics
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from AnalyzeRenderDocSceneExposure import select_scene_source
from renderdoc_ui_analysis import (
    collect_action_records, renderdoc_module, resource_id_to_name, run_ui_script,
)


REPLAY_SCOPE = (
    "Replay event durations: two warm samples after one warmup. "
    "Direct primary texture traffic is derived from actual resource formats and dispatch extents. "
    "Unmodeled traffic remains zero, not a claim of no accesses. Buffer, mask/depth side reads, "
    "filtered taps, blending and cache/compression traffic are not included in byte totals. "
    "These are logical transfer quantities, not measured DRAM bandwidth or frame latency."
)


def summarize_events(rows):
    full_volume_producers = {
        "VortexAtmosphereSkyViewLutCS",
        "VortexAtmosphereCameraAerialPerspectiveCS",
        "VortexVolumetricFogCS",
    }
    totals = {}
    for row in rows:
        if row["entry"] in full_volume_producers:
            # Each in-bounds output texel is stored once. Fog dispatches XY and
            # loops over Z; the atmosphere dispatches address the full output.
            assert len(row["writes"]) == 1, row
            output = row["writes"][0]
            assert output["bytes_per_texel"] in (8, 16), output
            row["logical_write_bytes"] = (output["width"] * output["height"]
                                          * output["depth"] * output["bytes_per_texel"])
            row["traffic_model"] = "One typed UAV store per output texel; filtered/source reads unmodeled"
        total = totals.setdefault(row["entry"], {"events": 0, "gpu_ms": 0.0,
                                 "logical_primary_read_bytes": 0, "logical_write_bytes": 0})
        total["events"] += 1
        total["gpu_ms"] += row["gpu_ms_warm_median"] or 0
        total["logical_primary_read_bytes"] += row["logical_primary_read_bytes"]
        total["logical_write_bytes"] += row["logical_write_bytes"]
    return totals


def build_report(controller, report, capture_path, report_path):
    rd = renderdoc_module()
    names = resource_id_to_name(controller)
    textures = {str(t.resourceId): t for t in controller.GetTextures()}
    counters = [{c.eventId: c.value.d * 1000 for c in controller.FetchCounters(
        [rd.GPUCounter.EventGPUDuration])} for _ in range(3)]
    native_actions = {}

    def walk(actions):
        for action in actions:
            native_actions[action.eventId] = action
            walk(action.children)
    walk(controller.GetRootActions())

    def descriptors(values):
        return list({(str(v.descriptor.resource), v.descriptor.byteOffset,
                      v.descriptor.byteSize): v.descriptor for v in values}.values())

    def metadata(resource):
        t = textures[str(resource)]
        return {"resource": str(resource), "name": names.get(str(resource), ""),
                "width": t.width, "height": t.height, "depth": t.depth,
                "format": t.format.Name(), "bytes_per_texel": t.format.compCount * t.format.compByteWidth}

    def read(descriptor, size):
        return bytes(controller.GetBufferData(descriptor.resource, descriptor.byteOffset, size))

    rows, views = [], []
    for action in collect_action_records(controller):
        if not action.flags & (rd.ActionFlags.Dispatch | rd.ActionFlags.Drawcall | rd.ActionFlags.Copy):
            continue
        warm = [sample[action.event_id] for sample in counters[1:] if action.event_id in sample]
        if any(not math.isfinite(v) or v < 0 for v in warm):
            raise RuntimeError("Invalid replay GPU duration")
        row = {"event": action.event_id, "path": action.path,
               "gpu_ms_warm_samples": warm,
               "gpu_ms_warm_median": statistics.median(warm) if warm else None,
               "logical_primary_read_bytes": 0, "logical_write_bytes": 0}
        if action.flags & rd.ActionFlags.Copy:
            native = native_actions[action.event_id]
            source, target = str(native.copySource), str(native.copyDestination)
            if source not in textures or target not in textures or names.get(source) != "SceneColor":
                continue
            src, dst = metadata(native.copySource), metadata(native.copyDestination)
            assert (src["width"], src["height"]) == (dst["width"], dst["height"])
            row.update(entry="CopySceneColor", reads=[src], writes=[dst],
                       logical_primary_read_bytes=src["width"] * src["height"] * src["bytes_per_texel"],
                       logical_write_bytes=dst["width"] * dst["height"] * dst["bytes_per_texel"])
            rows.append(row)
            continue
        controller.SetFrameEvent(action.event_id, True)
        pipe = controller.GetPipelineState()
        stage = rd.ShaderStage.Compute if action.flags & rd.ActionFlags.Dispatch else rd.ShaderStage.Pixel
        shader = pipe.GetShaderReflection(stage)
        entry = shader.entryPoint if shader else "DepthOnly"
        reads = descriptors(pipe.GetReadOnlyResources(stage, True))
        writes = descriptors(pipe.GetReadWriteResources(stage, True))
        texture_reads = [d for d in reads if str(d.resource) in textures]
        texture_writes = [d for d in writes if str(d.resource) in textures]
        row.update(entry=entry, reads=[metadata(d.resource) for d in texture_reads],
                   writes=[metadata(d.resource) for d in texture_writes])
        if entry in ("GatherSuitabilityMaximum", "CheckSuitabilityProduct"):
            constants = [d for d in reads if d.byteSize == d.elementByteSize == 128]
            assert len(constants) == 1, (entry, action.event_id, len(constants))
            words = struct.unpack("<32I", read(constants[0], 128))
            width, height, depth, flags, product = words[4:9]
            primary = [d for d in texture_reads
                       if (textures[str(d.resource)].width, textures[str(d.resource)].height,
                           textures[str(d.resource)].depth) == (width, height, depth)
                       and textures[str(d.resource)].format.compCount == 4
                       and textures[str(d.resource)].format.compByteWidth in (2, 4)]
            assert len(primary) == 1, (entry, action.event_id, [metadata(d.resource) for d in texture_reads])
            count = width * height * depth
            loads = count
            if entry == "GatherSuitabilityMaximum" and flags & 128:
                loads += (width - 1) * height * depth + width * (height - 1) * depth + width * height * (depth - 1)
            row.update(product=product, controls=flags, extent=[width, height, depth],
                       logical_primary_read_bytes=loads * metadata(primary[0].resource)["bytes_per_texel"],
                       traffic_model="Primary mip-zero loads; gradient mode includes valid-axis neighbors")
        elif entry == "ConvertQualifiedSceneColor":
            assert len(texture_reads) == len(texture_writes) == 1
            reports = [d for d in reads if names.get(str(d.resource)) == "Vortex.Exposure.Conversion"]
            assert len(reports) == 1
            checked, failed = struct.unpack_from("<2I", read(reports[0], 48), 8)
            assert checked == 1024 and failed == 0
            src, dst = metadata(texture_reads[0].resource), metadata(texture_writes[0].resource)
            assert src["bytes_per_texel"] == 16 and dst["bytes_per_texel"] == 8
            count = dst["width"] * dst["height"]
            row.update(logical_primary_read_bytes=count * 16, logical_write_bytes=count * 8,
                       traffic_model="Accepted whole-image FP32 read and FP16 write")
        elif "Vortex.PostProcess.Tonemap" in action.path:
            constants = [d for d in reads if d.byteSize == d.elementByteSize == 64]
            frames = [d for d in reads if names.get(str(d.resource)) == "Vortex.PostProcess.Exposure.Frame"]
            states = [d for d in reads if names.get(str(d.resource)) == "Vortex.PostProcess.Exposure.State"]
            assert len(constants) == len(frames) == len(states) == 1
            controls = read(constants[0], 64)
            source = select_scene_source(controller, reads, textures, names, controls)
            src, dst = metadata(source.resource), metadata(pipe.GetOutputTargets()[0].resource)
            p, inverse_p = struct.unpack_from("<2f", read(frames[0], 16))
            gain = struct.unpack_from("<f", read(states[0], 80))[0]
            assert p > 0 and math.isfinite(gain) and abs(p * inverse_p - 1) < 1e-6
            assert struct.unpack_from("<I", controls, 12)[0] == 0  # None mapper
            assert struct.unpack_from("<f", controls, 20)[0] == 1  # Linear gamma
            assert struct.unpack_from("<f", controls, 24)[0] == 0  # No bloom
            x, y = src["width"] // 2, src["height"] // 2
            pixel = list(controller.PickPixel(source.resource, x, y, rd.Subresource(), rd.CompType.Float).floatValue)
            actual = list(controller.PickPixel(pipe.GetOutputTargets()[0].resource, x, y,
                                              rd.Subresource(), rd.CompType.Float).floatValue)
            bayer = (0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5)
            dither = (bayer[(x & 3) | ((y & 3) << 2)] / 16 - .5) / 255
            expected = [min(1, max(0, min(1, max(0, c * gain / p)) + dither)) for c in pixel[:3]]
            assert max(abs(a - b) for a, b in zip(actual[:3], expected)) <= 2e-6
            count = src["width"] * src["height"]
            row.update(reads=[src], writes=[dst], logical_primary_read_bytes=count * src["bytes_per_texel"],
                       logical_write_bytes=count * dst["bytes_per_texel"],
                       traffic_model="Selected source Load and full-target write; uniform buffers excluded")
            views.append({"event": action.event_id, "source": src, "target": dst,
                          "p": p, "gain": gain, "pixel": pixel, "mapped": actual, "expected_rgb": expected})
        rows.append(row)
    assert len(views) == 2
    totals = summarize_events(rows)
    result = {"status": "pass", "capture": str(capture_path), "views": views, "by_entry": totals,
              "events": rows, "scope": REPLAY_SCOPE}
    Path(report_path).with_suffix(".json").write_text(json.dumps(result, indent=2) + "\n")
    report.append("hdr_accounting_replay=pass")


if __name__ == "__main__":
    run_ui_script("_hdr_accounting.txt", build_report)
