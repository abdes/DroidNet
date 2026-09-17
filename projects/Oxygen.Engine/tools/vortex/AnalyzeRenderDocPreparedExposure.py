"""Verify that independent scene views solve FP32 exposure before their resolve."""

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
    actions = collect_action_records(controller)
    solves = []
    meters = []
    for action in actions:
        if not action.flags & rd.ActionFlags.Dispatch:
            continue
        controller.SetFrameEvent(action.event_id, True)
        pipeline = controller.GetPipelineState()
        reads = [x.descriptor for x in pipeline.GetReadOnlyResources(rd.ShaderStage.Compute, True)]
        writes = [x.descriptor for x in pipeline.GetReadWriteResources(rd.ShaderStage.Compute, True)]
        histogram = [x.resource for x in writes if names.get(str(x.resource)) == "Vortex.PostProcess.Exposure.Histogram"]
        state = [x.resource for x in writes if names.get(str(x.resource)) == "Vortex.PostProcess.Exposure.State"]
        if state and any(x.byteSize == 112 and x.elementByteSize == 112 for x in reads):
            if len(state) != 1 or len(histogram) != 1:
                raise RuntimeError("Expected one independent solve state and histogram")
            solves.append((action.event_id, state[0], histogram[0]))
        if histogram and any(x.byteSize == 64 and x.elementByteSize == 64 for x in reads):
            sources = [x.resource for x in reads if str(x.resource) in textures]
            if not sources:  # ClearHistogram shares the constants ABI.
                continue
            if len(histogram) != 1 or len(sources) != 1:
                raise RuntimeError("Expected one unmasked FP32 meter source")
            meters.append((action.event_id, histogram[0], sources[0]))
    tones = [a for a in actions if a.flags & rd.ActionFlags.Drawcall
             and "Vortex.PostProcess.Tonemap" in a.path]
    if len(tones) != 2 or len(solves) != 2 or len(meters) != 2:
        raise RuntimeError("Expected exactly one meter/solve/tonemap per independent view")
    for tone in tones:
        controller.SetFrameEvent(tone.event_id, True)
        reads = [x.descriptor for x in controller.GetPipelineState().GetReadOnlyResources(rd.ShaderStage.Pixel, True)]
        states = [x.resource for x in reads if names.get(str(x.resource)) == "Vortex.PostProcess.Exposure.State"]
        sources = [x.resource for x in reads if str(x.resource) in textures]
        if len(states) != 1 or len(sources) != 1:
            raise RuntimeError("Ambiguous tonemap state or resolved source")
        matching = [s for s in solves if s[1] == states[0]]
        if len(matching) != 1:
            raise RuntimeError("Tonemap did not consume exactly one prepared solve")
        solve = matching[0]
        matching = [m for m in meters if m[1] == solve[2]]
        if len(matching) != 1:
            raise RuntimeError("Solve did not consume exactly one scene meter")
        meter = matching[0]
        copies = [u.eventId for u in controller.GetUsage(sources[0])
                  if str(u.usage).endswith("CopyDst") and u.eventId < tone.event_id]
        if not copies:
            raise RuntimeError("No resolved-color copy before tonemap")
        copied = max(copies)
        if not meter[0] < solve[0] < copied < tone.event_id:
            raise RuntimeError("Expected meter -> solve -> color resolve -> tonemap")
        if meter[2] == sources[0] or textures[str(meter[2])].format.compByteWidth != 4:
            raise RuntimeError("Meter must read the distinct FP32 accumulation")
        if not any(u.eventId == copied and str(u.usage).endswith("CopySrc")
                   for u in controller.GetUsage(meter[2])):
            raise RuntimeError("Resolved image did not come from the metered accumulation")
        solved_bytes = bytes(controller.GetBufferData(states[0], 0, 80))
        controller.SetFrameEvent(solve[0], True)
        if solved_bytes != bytes(controller.GetBufferData(states[0], 0, 80)):
            raise RuntimeError("Prepared exposure changed before final consumption")
        gain = struct.unpack_from("<f", solved_bytes)[0]
        report.append(f"meter={meter[0]} solve={solve[0]} resolve={copied} tone={tone.event_id} gain={gain} source={meter[2]} resolved={sources[0]}")
    report.append("prepared_exposure_order_verdict=pass")
    report.append("scope=independent scene execution ordering; FP16 admission remains separate")


if __name__ == "__main__":
    run_ui_script("_prepared_exposure.txt", build_report)
