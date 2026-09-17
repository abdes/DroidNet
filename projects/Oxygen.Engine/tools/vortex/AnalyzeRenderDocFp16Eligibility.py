"""Inspect independent GPU precision history and the matching completed status."""

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
    found = 0
    for action in collect_action_records(controller):
        if not action.flags & rd.ActionFlags.Dispatch:
            continue
        controller.SetFrameEvent(action.event_id, True)
        pipeline = controller.GetPipelineState()
        reads = [x.descriptor for x in pipeline.GetReadOnlyResources(rd.ShaderStage.Compute, True)]
        writes = [x.descriptor for x in pipeline.GetReadWriteResources(rd.ShaderStage.Compute, True)]
        constants = [x for x in reads if x.byteSize == 64 and x.elementByteSize == 64]
        statuses = [x for x in writes if names.get(str(x.resource)) == "Vortex.PostProcess.Exposure.Status"]
        if not constants or not statuses:
            continue
        current = [x for x in writes if names.get(str(x.resource)) == "Vortex.PostProcess.Exposure.State"]
        previous = [x for x in reads if names.get(str(x.resource)) == "Vortex.PostProcess.Exposure.State"]
        if any(len(x) != 1 for x in (constants, statuses, current, previous)):
            raise RuntimeError("Eligibility bindings are not uniquely identified")
        if current[0].resource == previous[0].resource:
            raise RuntimeError("Eligibility overwrote its previous precision record")
        c = constants[0]
        words = struct.unpack("<16I", bytes(controller.GetBufferData(c.resource, c.byteOffset, 64)))
        if words[4:8] != (7, 0, 1024, 1) or words[9:12] != (2, 0, 0xffffffff) or words[14:] != (0, 0):
            raise RuntimeError("Finalizer constants ABI/layout/sequence mismatch")
        old = bytes(controller.GetBufferData(previous[0].resource, 0, 80))
        new = bytes(controller.GetBufferData(current[0].resource, 0, 80))
        if struct.unpack_from("<f3I", old, 64) != (8192.0, 1, 7, 0):
            raise RuntimeError("Previous frame did not carry the first qualification")
        if struct.unpack_from("<f3I", new, 64) != (8192.0, 2, 7, 0):
            raise RuntimeError("Current frame did not complete the eligibility streak")
        if old[:24] != new[:24] or struct.unpack_from("<4f", new) != (1, 1, 1, 1):
            raise RuntimeError("Finalization changed numerical exposure")
        flags = struct.unpack_from("<I", new, 24)[0]
        if flags & 256 == 0 or flags & 32:
            raise RuntimeError("Qualified state eligibility/range flags are incorrect")
        status = struct.unpack("<20I", bytes(controller.GetBufferData(statuses[0].resource, 0, 80)))
        if status != (*words[12:14], 2, 0, 1, 0, 1, 0, 1, 0, 7, 0, 5, 0, 0, 2, 2, 0, 0, 0):
            raise RuntimeError(f"Completed status does not identify its qualified record: {status}")
        report.append(f"event={action.event_id} previous={previous[0].resource} current={current[0].resource} status={status}")
        found += 1
    if found != 1:
        raise RuntimeError(f"Expected one captured eligibility finalization, found {found}")
    report.append("fp16_eligibility_verdict=pass")
    report.append("scope=GPU eligibility/status publication; CPU format admission remains separate")


if __name__ == "__main__":
    run_ui_script("_fp16_eligibility.txt", build_report)
