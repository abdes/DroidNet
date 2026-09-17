"""Compare the pre-environment reduction with its actual FP32 source texture."""

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
    pending = {}
    retained = {}
    sources_captured = {}
    boundary_checks = tail_checks = 0
    checked = 0
    for action in collect_action_records(controller):
        if action.flags & rd.ActionFlags.Drawcall and "Vortex.Stage15.Atmosphere" in action.path:
            controller.SetFrameEvent(action.event_id, True)
            target = controller.GetPipelineState().GetOutputTargets()[0].resource
            if str(target) not in sources_captured or sources_captured[str(target)] >= action.event_id:
                raise RuntimeError("Atmosphere consumed SceneColor without an earlier input-range capture")
            boundary_checks += 1
        if not action.flags & rd.ActionFlags.Dispatch:
            continue
        controller.SetFrameEvent(action.event_id, True)
        pipeline = controller.GetPipelineState()
        shader = pipeline.GetShaderReflection(rd.ShaderStage.Compute)
        if shader and shader.entryPoint in ("VortexExposureAverageCS", "FinalizeFp16Suitability"):
            for binding in pipeline.GetReadWriteResources(rd.ShaderStage.Compute, True):
                resource = binding.descriptor.resource
                if str(resource) in retained:
                    tail = bytes(controller.GetBufferData(resource, 128, 16))
                    if tail != retained[str(resource)]:
                        raise RuntimeError("Exposure solve/finalizer overwrote the captured input range")
                    tail_checks += 1
        if not shader or shader.entryPoint not in ("ClearSuitability", "GatherSuitabilityMaximum"):
            continue
        reads = [x.descriptor for x in pipeline.GetReadOnlyResources(rd.ShaderStage.Compute, True)]
        constants = [x for x in reads if x.byteSize == x.elementByteSize == 96]
        if len(constants) != 1:
            raise RuntimeError("Missing reduction constants")
        c = constants[0]
        words = struct.unpack("<24I", bytes(controller.GetBufferData(c.resource, c.byteOffset, 96)))
        if not words[7] & 32:
            continue
        writes = [x.descriptor for x in pipeline.GetReadWriteResources(rd.ShaderStage.Compute, True)]
        statuses = [x for x in writes if names.get(str(x.resource)) == "Vortex.PostProcess.Exposure.Status"]
        if len(statuses) != 1:
            raise RuntimeError("Missing status allocation")
        status = statuses[0].resource
        raw = bytes(controller.GetBufferData(status, 0, 144))
        if len(raw) != 144:
            raise RuntimeError("Truncated status allocation")
        if shader.entryPoint == "ClearSuitability":
            if struct.unpack_from("<4I", raw, 128) != (0, 1, 0, 0):
                raise RuntimeError("Input range was not reset")
            pending[str(status)] = raw[:128]
            continue
        if str(status) not in pending:
            raise RuntimeError("Reduction did not follow its own clear")
        sources = [x for x in reads if str(x.resource) in textures]
        if len(sources) != 1:
            raise RuntimeError("Source texture is ambiguous")
        source = sources[0].resource
        desc = textures[str(source)]
        if (desc.width, desc.height, desc.depth) != (words[4], words[5], 1):
            raise RuntimeError("Source extent disagrees with the constants")
        pixels = bytes(controller.GetTextureData(source, rd.Subresource()))
        if len(pixels) != desc.width * desc.height * 16:
            raise RuntimeError("Expected tightly packed RGBA32 input")
        maximum, flags = 0.0, 1
        for pixel in struct.iter_unpack("<4f", pixels):
            if not all(math.isfinite(v) for v in pixel):
                flags |= 2
            else:
                maximum = max(maximum, *(abs(v) for v in pixel[:3]))
                if any(v < 0 for v in pixel[:3]):
                    flags |= 4
        actual = struct.unpack_from("<f3I", raw, 128)
        expected = (maximum, flags, desc.width * desc.height, 0)
        if actual != expected:
            raise RuntimeError(f"Range mismatch: {actual} != {expected}")
        prefix = pending.pop(str(status))
        if flags & 2:
            fields = struct.unpack_from("<3I", raw, 48)
            if fields[0] & 18 != 18 or fields[1] == 0 or not fields[2] & 1:
                raise RuntimeError("Nonfinite source did not report producer failure")
            if raw[80:128] != prefix[80:128]:
                raise RuntimeError("Producer error bounds were overwritten")
        elif raw[:128] != prefix:
            raise RuntimeError("Finite reduction overwrote existing status/bounds")
        checked += 1
        retained[str(status)] = raw[128:144]
        sources_captured[str(source)] = action.event_id
        report.append(f"event={action.event_id} source={source} status={status} range={actual}")
    if checked == 0 or pending:
        raise RuntimeError(f"Incomplete reductions: checked={checked}, pending={len(pending)}")
    report.append(f"pre_environment_range_verdict=pass reductions={checked}")
    report.append(f"atmosphere_boundary_checks={boundary_checks} preserved_tail_checks={tail_checks}")
    report.append("scope=measured pre-exposed input maximum; composed image/meter admission remains open")


if __name__ == "__main__":
    run_ui_script("_pre_environment_range.txt", build_report)
