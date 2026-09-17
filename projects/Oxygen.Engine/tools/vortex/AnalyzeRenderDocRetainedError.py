"""Independently check retained-bound qualification inputs and final eligibility."""

import math
from pathlib import Path
import struct
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "shadows"))
from renderdoc_ui_analysis import collect_action_records, renderdoc_module, resource_id_to_name, run_ui_script


def interval(observed, relative, absolute):
    return max(0, (observed - absolute) / (1 + relative)), (observed + absolute) / (1 - relative)


def build_report(controller, report, capture_path, report_path):
    rd = renderdoc_module()
    names = resource_id_to_name(controller)
    textures = {str(t.resourceId) for t in controller.GetTextures()}
    pending = None
    checked = finalized = 0
    for action in collect_action_records(controller):
        if not action.flags & rd.ActionFlags.Dispatch:
            continue
        controller.SetFrameEvent(action.event_id, True)
        pipeline = controller.GetPipelineState()
        shader = pipeline.GetShaderReflection(rd.ShaderStage.Compute)
        if not shader:
            continue
        reads = [x.descriptor for x in pipeline.GetReadOnlyResources(rd.ShaderStage.Compute, True)]
        writes = [x.descriptor for x in pipeline.GetReadWriteResources(rd.ShaderStage.Compute, True)]
        if shader.entryPoint == "CheckSuitabilityProduct":
            constants = [x for x in reads if x.byteSize == x.elementByteSize == 96]
            if len(constants) != 1:
                raise RuntimeError("Missing qualification constants")
            c = constants[0]
            raw = bytes(controller.GetBufferData(c.resource, c.byteOffset, 96))
            words = struct.unpack("<24I", raw)
            if words[8] != 10:
                continue
            if words[21] == 0xffffffff or words[22:] != (0, 0) or words[7] & 2:
                raise RuntimeError("Incorrect retained-bound SRV/coverage ABI")
            statuses = [x for x in reads if names.get(str(x.resource)) == "Vortex.PostProcess.Exposure.Status"]
            sources = [x for x in reads if str(x.resource) in textures]
            outputs = [x for x in writes if names.get(str(x.resource)) == "Vortex.Exposure.Suitability"]
            if any(len(x) != 1 for x in (statuses, sources, outputs)):
                raise RuntimeError("Missing source, status or report")
            status = statuses[0]
            bounds = struct.unpack("<4f", bytes(controller.GetBufferData(status.resource, 112, 16)))
            unrelated = struct.unpack("<4f", bytes(controller.GetBufferData(status.resource, 80, 16)))
            if not math.isinf(unrelated[1]):
                raise RuntimeError("Fixture did not exercise unrelated producer isolation")
            sample = struct.unpack("<4f", bytes(controller.GetTextureData(sources[0].resource, rd.Subresource())))
            result = struct.unpack("<2f10I", bytes(controller.GetBufferData(outputs[0].resource, 0, 48)))
            gain = struct.unpack_from("<f", raw, 80)[0]
            relative_budget = .0025 * struct.unpack_from("<f", raw, 68)[0]
            absolute_budget = 1e-5 * struct.unpack_from("<f", raw, 68)[0]
            expected = 0
            valid = all(math.isfinite(v) and v >= 0 for v in bounds) and bounds[0] < 1 and bounds[2] < 1
            if not valid:
                expected = 4
            else:
                rgb = interval(sample[0], bounds[0], bounds[1])
                t = tuple(min(1, max(0, v)) for v in interval(sample[3], bounds[2], bounds[3]))
                candidate = struct.unpack("<e", struct.pack("<e", sample[0] * result[0]))[0] / result[0]
                candidate_t = struct.unpack("<e", struct.pack("<e", sample[3]))[0]
                image_failed = any(abs(candidate - v) > relative_budget * v + absolute_budget / max(gain, 1) for v in rgb)
                image_failed |= any(abs(candidate_t - v) > relative_budget * v + absolute_budget / max(result[1], 1e-30) for v in t)
                expected = 4 if image_failed else 0
                if words[7] & 1:
                    # The controlled metered case retains dark mass (D=1).
                    cutoff = 2 ** struct.unpack_from("<f", raw, 72)[0]
                    if any((v <= cutoff) != (candidate <= cutoff) or (v == 0) != (candidate == 0)
                           or (v > 0 and (candidate <= 0 or abs(math.log2(candidate / v)) > 1/1024)) for v in rgb):
                        expected |= 8
                if bounds[1] < 1e30 and result[1] < rgb[1]:
                    raise RuntimeError("Maximum reduction missed a finite reference upper bound")
            if result[3] != expected or result[9] != 2 or result[10] != 1536:
                raise RuntimeError(f"Retained-error mismatch: sample={sample}, bounds={bounds}, result={result}, expected={expected}")
            if pending is not None:
                raise RuntimeError("Previous case was not finalized")
            pending = (str(status.resource), expected)
            checked += 1
            report.append(f"case={checked} event={action.event_id} bounds={bounds} gain={gain} failures={expected} maximum={result[1]}")
        elif shader.entryPoint == "FinalizeFp16Suitability" and pending is not None:
            statuses = [x for x in writes if names.get(str(x.resource)) == "Vortex.PostProcess.Exposure.Status"]
            if len(statuses) != 1 or str(statuses[0].resource) != pending[0]:
                raise RuntimeError("Finalizer changed status identity")
            fields = struct.unpack("<20I", bytes(controller.GetBufferData(statuses[0].resource, 0, 80)))
            if pending[1] and (fields[12] & 4 or fields[15] != 0):
                raise RuntimeError("Failed interval accumulated eligibility")
            pending = None
            finalized += 1
    if checked != 18 or finalized != 18 or pending is not None:
        raise RuntimeError(f"Incomplete retained-error matrix: {checked}/{finalized}")
    report.append("retained_error_verdict=pass")
    report.append("scope=local product/history interval qualification; final composition/coverage propagation remains open")


if __name__ == "__main__":
    run_ui_script("_retained_error.txt", build_report)
