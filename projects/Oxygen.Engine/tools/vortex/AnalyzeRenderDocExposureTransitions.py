"""Inspect native exposure solves and independently check their numeric response.

Run in RenderDoc through Invoke-RenderDocUiAnalysis.ps1. Multi-frame captures
are supported; output includes the existing per-view S/P and image evidence.
"""
import json
import math
import os
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from AnalyzeRenderDocMultiViewExposure import build_report as inspect_views
from ExposureCaptureReference import trimmed_histogram_log_luminance, target_gain
from renderdoc_ui_analysis import collect_action_records, renderdoc_module, resource_id_to_name, run_ui_script


def decode_state(raw):
    values = struct.unpack("<6f14I", raw)
    return {"gain": values[0], "target": values[1], "latent": values[2],
            "latent_target": values[3], "raw_luminance": values[4], "raw_ev": values[5],
            "flags": values[6],
            "requested": values[10] | values[11] << 32,
            "applied": values[12] | values[13] << 32,
            "frame": values[14] | values[15] << 32}


def adapted(previous, target, dt, up, down, distance):
    start, end = math.log2(previous), math.log2(target)
    radius = abs(end - start)
    speed = up if target < previous else down
    if radius == 0 or dt == 0 or speed == 0:
        return previous
    crossing = max(radius - distance, 0) / speed
    remaining = (radius - speed * dt if dt <= crossing else
                 min(radius, distance) * math.exp(-speed * (dt - crossing) / distance))
    return 2 ** (end - (1 if end > start else -1) * remaining)



def decode_log_rate(value):
    return 0.0 if value == -256.0 else 2.0 ** value


def state_at_slot(inputs, slot):
    if slot == 0xffffffff:
        return None
    matches = [item["state"] for item in inputs
               if item["binding_index"] == 0xffff and item["array_element"] == slot]
    if len(matches) != 1:
        raise RuntimeError(f"Missing or ambiguous bindless state at slot {slot}")
    return matches[0]


def response(previous, current, *, mode, policy, generation, fixed, seed_log,
             dt, up, down, distance, target_flags=0, initial_gain=1,
             borrowed=None, source_loss=False):
    if borrowed is not None and not source_loss:
        return borrowed["gain"], "borrowed"
    if mode != 2:
        return fixed, "fixed"
    if target_flags & 2:
        return 0.0, "zero_target"
    valid = (previous is not None and previous["flags"] & 1
             and math.isfinite(previous["latent"]) and previous["latent"] > 0)
    pending = generation > (previous["applied"] if previous else 0)
    pending = pending and generation >= (previous["requested"] if previous else 0)
    if policy == 3 and pending:
        return 2 ** seed_log, "seed"
    if source_loss and borrowed is not None and not target_flags & 1 and not (policy == 2 and pending):
        return borrowed["gain"], "source_loss"
    if target_flags & 1 or (valid and previous["flags"] & 64):
        return current["latent_target"], "locked_or_restored"
    metered = bool(current["flags"] & (4 | 16))
    if policy == 2 and pending:
        return (current["latent_target"] if metered else
                previous["latent"] if valid else initial_gain), "remeter"
    if not valid or not previous["flags"] & 2:
        return current["latent_target"] if metered else initial_gain, "initialization"
    if policy == 1 and pending:
        return previous["latent"], "preserve"
    if (previous["flags"] >> 10) & 3 != 2:
        return previous["latent"], "mode_entry"
    if not metered:
        return previous["latent"], "invalid_meter"
    return adapted(previous["latent"], current["latent_target"], dt,
                   up, down, distance), "hybrid"


def build_report(controller, report, capture, path):
    requested = os.environ.get("OXYGEN_RENDERDOC_EXPOSURE_FRAMES", "")
    selected_frames = {int(frame) for frame in requested.split(",")} if requested else None
    inspect_views(controller, report, capture, path, selected_frames)
    output_path = Path(path).with_suffix(".json")
    result = json.loads(output_path.read_text())
    if selected_frames is not None:
        captured_frames = {view["frame"] for view in result["views"]}
        if captured_frames != selected_frames:
            raise RuntimeError(f"Requested frames missing: {selected_frames - captured_frames}")
    rd = renderdoc_module()
    names = resource_id_to_name(controller)
    resources = {str(r.resourceId): r.resourceId for r in controller.GetResources()}
    actions = collect_action_records(controller)
    dispatches = {a.event_id for a in actions
                  if a.flags & rd.ActionFlags.Dispatch}
    marked_solves = {a.event_id for a in actions
                    if a.flags & rd.ActionFlags.Dispatch
                    and "Vortex.PostProcess.Exposure.Solve" in a.path}
    checks = []
    for view in result["views"]:
        state_id = resources[view["state_resource"]]
        boundary = view["previous_tonemap_event"]
        # Older captures have no solve marker. Keep their bounded shader
        # search, while current captures seek directly to the exposure solve.
        events = sorted((event for event in (marked_solves or dispatches)
                         if boundary < event < view["event"]), reverse=True)
        matched = False
        for event in events:
            controller.SetFrameEvent(event, True)
            pipeline = controller.GetPipelineState()
            shader = pipeline.GetShaderReflection(rd.ShaderStage.Compute)
            if not shader or shader.entryPoint != "VortexExposureAverageCS":
                continue
            used_reads = list(pipeline.GetReadOnlyResources(rd.ShaderStage.Compute, True))
            reads = [x.descriptor for x in used_reads]
            writes = [x.descriptor for x in pipeline.GetReadWriteResources(rd.ShaderStage.Compute, True)]
            destinations = [d for d in writes if d.resource == state_id]
            if len(destinations) != 1:
                continue
            constants = [d for d in reads if d.byteSize == d.elementByteSize == 112]
            if len(constants) != 1:
                raise RuntimeError("Missing exposure solve constants")
            read = lambda d, size: bytes(controller.GetBufferData(d.resource, d.byteOffset, size))
            raw = read(constants[0], 112)
            u = struct.unpack("<28I", raw)
            f = struct.unpack("<28f", raw)
            frame = u[14] | u[15] << 32
            if frame != view["frame"]:
                continue
            current = decode_state(read(destinations[0], 80))
            states = [d for d in reads if names.get(str(d.resource)) == "Vortex.PostProcess.Exposure.State"]
            state_inputs = [{"binding_index": int(x.access.index),
                             "array_element": int(x.access.arrayElement),
                             "resource": str(x.descriptor.resource),
                             "offset": x.descriptor.byteOffset,
                             "state": decode_state(read(x.descriptor, 80))}
                            for x in used_reads if x.descriptor in states]
            previous = state_at_slot(state_inputs, u[16])
            borrowed = state_at_slot(state_inputs, u[25])
            source_loss = bool(u[19] & 64 and borrowed is not None)
            borrowing = borrowed is not None and not source_loss
            dt, up, down, distance = (decode_log_rate(f[i]) for i in (10, 8, 9, 7))
            mode, controls, policy = u[18], u[19], u[22]
            generation = u[20] | u[21] << 32
            targets = [d for d in reads if d.byteSize == d.elementByteSize == 560]
            if len(targets) != 1:
                raise RuntimeError("Missing or ambiguous exposure target data")
            target_raw = read(targets[0], 560)
            count, target_flags, initial_log, dark_log = struct.unpack_from("<2I2f", target_raw)
            if count > 68:
                raise RuntimeError("Invalid target knot count")
            keys = [struct.unpack_from("<2f", target_raw, 16 + 8 * i) for i in range(count)]
            initial_gain = 2 ** initial_log
            target_check = None
            reference_state = dict(current)
            if mode == 2 and not borrowing and not controls & 2:
                expected_target = None
                if target_flags & 1:
                    expected_target = 2 ** keys[0][1]
                elif current["flags"] & (4 | 16):
                    histogram_views = {(str(d.resource), d.byteOffset): d for d in reads + writes
                        if names.get(str(d.resource)) == "Vortex.PostProcess.Exposure.Histogram"}
                    if len(histogram_views) != 1:
                        raise RuntimeError("Missing independent histogram input")
                    histogram = struct.unpack("<264I", read(next(iter(histogram_views.values())), 1056))
                    dark = histogram[257] > 0 and histogram[257] == histogram[261]
                    log_luminance = trimmed_histogram_log_luminance(
                        histogram[:256], f[4], f[5], f[2], f[3])
                    if dark:
                        expected_ev, expected_target = f[6], 2 ** dark_log
                    elif log_luminance is not None:
                        expected_ev = log_luminance - math.log2(.18)
                        expected_target = target_gain(keys, expected_ev)
                    else:
                        raise RuntimeError("Valid GPU meter has no histogram mass")
                    meter_error = abs(current["raw_ev"] - expected_ev)
                    if meter_error > 1 / 512:
                        raise RuntimeError(f"Histogram reduction mismatch at {event}: {meter_error} EV")
                    target_check = {"meter_error_ev": meter_error,
                                    "expected_meter_ev": expected_ev,
                                    "mass": sum(histogram[:256]), "dark": dark}
                if expected_target is not None:
                    target_error = abs(math.log2(current["latent_target"] / expected_target))
                    if target_error > 1 / 512:
                        raise RuntimeError(f"Target mismatch at {event}: {target_error} EV")
                    target_check = {**(target_check or {}), "target_error_ev": target_error,
                                    "expected_target": expected_target}
                    reference_state["latent_target"] = expected_target
            expected, reason = None, "unverified_bootstrap"
            if not controls & 2:
                expected, reason = response(previous, reference_state, mode=mode,
                    policy=policy, generation=generation, fixed=f[17], seed_log=f[23],
                    dt=dt, up=up, down=down, distance=distance,
                    target_flags=target_flags, initial_gain=initial_gain,
                    borrowed=borrowed, source_loss=source_loss)
            if borrowing:
                if any(current[key] != borrowed[key]
                       for key in ("gain", "target", "latent", "latent_target")):
                    raise RuntimeError("Borrower did not copy all four source gain fields")
                if not current["flags"] & 128 or current["flags"] & (4 | 8 | 512):
                    raise RuntimeError("Borrower reports independent image metering")
            error = (abs(math.log2(current["gain"] / expected)) if expected and current["gain"] > 0
                     else 0.0 if expected == current["gain"] == 0 else None)
            if expected is not None and error is None:
                raise RuntimeError("Expected and actual displayed-zero states differ")
            if error is not None and error > 1 / 512:
                raise RuntimeError(f"Exposure response mismatch at {event}: {error} EV")
            checks.append({"target_name": view["target_name"], "frame": frame,
                           "event": event, "mode": mode, "policy": policy,
                           "generation": generation, "lifetime": u[26] | u[27] << 32,
                           "delta_seconds": dt, "speed_up": up, "speed_down": down,
                           "transition_distance": distance, "previous": previous,
                           "current": current, "expected_gain": expected,
                           "error_ev": error, "method": reason,
                           "borrowed_state_slot": u[25], "controls": controls,
                           "previous_state_slot": u[16], "state_inputs": state_inputs,
                           "target_check": target_check, "target_flags": target_flags,
                           "target_keys": keys})
            matched = True
            break
        if not matched:
            raise RuntimeError(f"No matching solve for {view['target_name']} frame {view['frame']}")
    result["solves"] = checks
    result["response_verdict"] = "pass" if all(c["error_ev"] is not None for c in checks) else "partial"
    result["response_scope"] = "Independent exact percentile-mass reduction, target-knot interpolation and double-precision response from captured rates and descriptor-identified prior state; borrowed gains and source-loss continuity are checked against their bound source. Cross-view latency is checked separately."
    output_path.write_text(json.dumps(result, indent=2) + "\n")
    report.append("exposure_response_checks=" + str(len(checks)))


if __name__ == "__main__" or "pyrenderdoc" in globals():
    run_ui_script("_exposure_transitions.txt", build_report)
