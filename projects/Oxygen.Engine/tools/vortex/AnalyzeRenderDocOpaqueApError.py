"""Check the retained opaque-AP contribution with exact rational arithmetic."""

from fractions import Fraction
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
    retained = {}
    checked = preserved = 0
    maximum_float = struct.unpack("<f", struct.pack("<I", 0x7f7fffff))[0]
    disabled_threshold = struct.unpack("<f", struct.pack("<f", .0001))[0]
    for action in collect_action_records(controller):
        if not action.flags & rd.ActionFlags.Dispatch:
            continue
        controller.SetFrameEvent(action.event_id, True)
        pipeline = controller.GetPipelineState()
        shader = pipeline.GetShaderReflection(rd.ShaderStage.Compute)
        if not shader:
            continue
        writes = [x.descriptor for x in pipeline.GetReadWriteResources(rd.ShaderStage.Compute, True)]
        statuses = [x for x in writes if names.get(str(x.resource)) == "Vortex.PostProcess.Exposure.Status"]
        if shader.entryPoint in ("VortexExposureFrameCS", "ClearSuitability"):
            is_input_clear = shader.entryPoint == "VortexExposureFrameCS"
            if shader.entryPoint == "ClearSuitability" and statuses:
                clears = [x.descriptor for x in pipeline.GetReadOnlyResources(rd.ShaderStage.Compute, True)
                          if x.descriptor.byteSize == x.descriptor.elementByteSize == 128]
                if len(clears) != 1:
                    raise RuntimeError("Missing clear constants")
                c = clears[0]
                flags = struct.unpack("<I", bytes(controller.GetBufferData(c.resource, c.byteOffset + 28, 4)))[0]
                is_input_clear = bool(flags & 32)
            for status in statuses:
                data = bytes(controller.GetBufferData(status.resource, 0, 384))
                if shader.entryPoint == "VortexExposureFrameCS" and data != bytes(384):
                    raise RuntimeError("Frame preparation did not clear the entire status allocation")
                if len(data) != 384 or (is_input_clear and struct.unpack_from("<I", data, 152)[0] != 0):
                    raise RuntimeError("Input replacement left the AP record valid")
                if is_input_clear:
                    retained.pop(str(status.resource), None)
        if shader.entryPoint in ("VortexExposureAverageCS", "FinalizeFp16Suitability"):
            for status in statuses:
                if str(status.resource) in retained:
                    actual = bytes(controller.GetBufferData(status.resource, 144, 16))
                    if actual != retained[str(status.resource)]:
                        raise RuntimeError("Solve/finalizer overwrote the AP intermediate record")
                    preserved += 1
        if shader.entryPoint != "SelectSuitabilityCandidate":
            continue
        reads = [x.descriptor for x in pipeline.GetReadOnlyResources(rd.ShaderStage.Compute, True)]
        constants = [x for x in reads if x.byteSize == x.elementByteSize == 128]
        if len(constants) != 1:
            raise RuntimeError("Missing qualification constants")
        c = constants[0]
        raw = bytes(controller.GetBufferData(c.resource, c.byteOffset, 128))
        if not struct.unpack_from("<I", raw, 28)[0] & 64:
            continue
        frames = [x for x in reads if names.get(str(x.resource)) == "Vortex.PostProcess.Exposure.Frame"]
        if len(statuses) != 1 or len(frames) != 1:
            raise RuntimeError("Missing frame/status bindings")
        gain = struct.unpack_from("<f", raw, 80)[0]
        inverse_p = struct.unpack("<f", bytes(controller.GetBufferData(frames[0].resource, 4, 4)))[0]
        status = statuses[0].resource
        data = bytes(controller.GetBufferData(status, 0, 384))
        if len(data) != 384:
            raise RuntimeError("Incorrect status allocation size")
        bounds = struct.unpack_from("<4f", data, 96)
        peak, flags, count, reserved = struct.unpack_from("<f3I", data, 128)
        r, a, valid, output_reserved = struct.unpack_from("<2f2I", data, 144)
        well_formed = (flags == 1 and count > 0 and math.isfinite(peak) and peak >= 0
                       and math.isfinite(inverse_p) and inverse_p > 0
                       and math.isfinite(gain) and gain >= 0)
        expected_r = Fraction(0)
        expected_a = Fraction(0)
        operand_ceiling = Fraction(0)
        if well_formed and gain >= disabled_threshold:
            well_formed = all(math.isfinite(v) and v >= 0 for v in bounds) and bounds[0] < 1 and bounds[2] < 1
            if well_formed:
                maximum = Fraction(peak) * Fraction(inverse_p)
                expected_r = Fraction(max(bounds[0], bounds[2]))
                expected_a = Fraction(gain) * Fraction(bounds[1]) + maximum * Fraction(bounds[3])
                def promote(value):
                    return max(Fraction(value), Fraction(1, 2**126)) if value > 0 else Fraction(0)
                operand_ceiling = promote(gain) * promote(bounds[1]) + promote(maximum) * promote(bounds[3])
                well_formed = maximum <= Fraction(maximum_float) and expected_a <= Fraction(maximum_float)
        if bool(valid) != well_formed or output_reserved != 0 or reserved != 0:
            raise RuntimeError(f"Invalid metadata classification: input={peak, flags, count}, bounds={bounds}, gain={gain}, output={r,a,valid}")
        if well_formed:
            if not math.isfinite(r) or not math.isfinite(a) or Fraction(r) < expected_r or Fraction(a) < expected_a:
                raise RuntimeError("GPU coefficient does not enclose the exact transfer")
            if a > float(operand_ceiling) * 1.00001 + math.ldexp(1.00001, -126):
                raise RuntimeError("Unexpectedly loose AP coefficient")
        elif not math.isinf(a) or a < 0:
            raise RuntimeError("Invalid input did not produce an unbounded record")
        retained[str(status)] = data[144:160]
        checked += 1
        report.append(f"event={action.event_id} peak_P={peak} inverse_P={inverse_p} gain={gain} bounds={bounds} output={r,a,valid}")
    if checked == 0:
        raise RuntimeError("No opaque AP contribution was recorded")
    report.append(f"opaque_ap_error_verdict=pass records={checked} preserved_tail_checks={preserved}")
    report.append("scope=retained ideal-transfer contribution only; filtering/arithmetic/coverage/candidate admission remain open")


if __name__ == "__main__":
    run_ui_script("_opaque_ap_error.txt", build_report)
