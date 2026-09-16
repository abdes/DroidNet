"""Capture audit of production histogram bindings and bounded sample mass.

Use the existing fixed/locked fixture capture and replay runner. The fixed-gain
analyzer first qualifies final consumption; this audit then reads the actual
histogram UAV bound to the solve. No expected values use engine math.
"""
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from AnalyzeRenderDocExposureFixed import build_report as fixed_report
from renderdoc_ui_analysis import (
    collect_action_records, renderdoc_module, resource_id_to_name, run_ui_script,
)


def build_report(controller, report, capture_path, report_path):
    fixed_report(controller, report, capture_path, report_path)
    rd = renderdoc_module()
    names = resource_id_to_name(controller)
    solves = []
    histogram_dispatches = []
    for action in collect_action_records(controller):
        if not action.flags & rd.ActionFlags.Dispatch:
            continue
        controller.SetFrameEvent(action.event_id, True)
        state = controller.GetPipelineState()
        reads = [u.descriptor for u in state.GetReadOnlyResources(rd.ShaderStage.Compute, True)]
        writes = [u.descriptor for u in state.GetReadWriteResources(rd.ShaderStage.Compute, True)]
        histograms = [d for d in writes if names.get(str(d.resource)) == "Vortex.PostProcess.Exposure.Histogram"]
        if not histograms:
            continue
        if any(d.byteSize == 560 for d in reads):
            solves.append((action.event_id, histograms[0]))
        elif any(names.get(str(d.resource)) in ("SceneColor", "ResolvedSceneColor") for d in reads):
            constants = [d for d in reads if d.byteSize == 64 and d.elementByteSize == 64]
            if len(constants) != 1:
                raise RuntimeError("Histogram constants not identified uniquely")
            d = constants[0]
            raw = bytes(controller.GetBufferData(d.resource, d.byteOffset, 64))
            left, top, width, height = struct.unpack_from("<4I", raw, 16)
            inverse_p, influence = struct.unpack_from("<2f", raw, 48)
            histogram_dispatches.append((width, height))
            report.append("histogram_event={} content={},{},{},{} inverse_P={} black_influence={}".format(
                action.event_id, left, top, width, height, inverse_p, influence))
    if len(solves) != 1 or len(histogram_dispatches) != 1:
        raise RuntimeError("Expected one production histogram and solve")
    event, descriptor = solves[0]
    controller.SetFrameEvent(event, True)
    words = struct.unpack("<264I", bytes(controller.GetBufferData(descriptor.resource, descriptor.byteOffset, 1056)))
    width, height = histogram_dispatches[0]
    samples = min(width, 512) * min(height, 512)
    expected = [0] * 264
    expected[102] = samples * 4095  # L=.25 in [-12,13] lands exactly at bin 102.
    expected[256] = expected[257] = samples
    report.append("histogram_mass={}".format(sum(words[:256])))
    report.append("histogram_counters={}".format(list(words[256:])))
    report.append("expected_samples={}".format(samples))
    report.append("histogram_nonzero_bins={}".format([(i, x) for i, x in enumerate(words[:256]) if x]))
    if list(words) != expected:
        raise RuntimeError("Bound histogram differs from independent uniform-input reference")
    report.append("histogram_verdict=pass")


if __name__ == "__main__":
    run_ui_script("_exposure_meter.txt", build_report)
