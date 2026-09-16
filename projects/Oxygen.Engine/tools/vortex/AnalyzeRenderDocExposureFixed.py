"""Qualify VortexBasic's constant-4096 fixed-exposure native fixture.

Run through Invoke-RenderDocUiAnalysis.ps1 with PassName EV14, EV15 or EV16.
Expected gains/pixels are independent literals; this does not import production
exposure math. The shared replay runner owns controller and capture lifetime.
"""

import os
import math
from pathlib import Path
import struct
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "shadows"))
from renderdoc_ui_analysis import (  # noqa: E402
    collect_action_records,
    renderdoc_module,
    resource_id_to_name,
    run_ui_script,
)


def build_report(controller, report, capture_path, report_path):
    rd = renderdoc_module()
    case = os.environ.get("OXYGEN_RENDERDOC_PASS_NAME", "")
    reference = {"EV14": (1 / 16384, 0.25), "EV15": (1 / 32768, 0.125),
                 "EV16": (1 / 65536, 0.0625),
                 "Auto160": (1, 0.25), "Auto-160": (1, 0.25),
                 "AutoCurveLocked": (2, 0.5),
                 "EV32": (1 / 4294967296, 1 / 1048576),
                 "EV-32": (4294967296, 1), "Bias": (1 / 32768, 0.125),
                 "Disabled": (1, 1), "Camera": (1 / 15125, 4096 / 15125),
                 "InvalidRetained": (1 / 16384, 0.25)}
    if case not in reference:
        raise ValueError("Unknown fixed-gain or locked-Auto fixture case")
    automatic = case.startswith("Auto")
    expected_input = 0.25 if automatic else 4096
    expected_gain, expected_linear = reference[case]
    failures = []
    report.append("capture_path={}".format(capture_path))
    report.append("case={}".format(case))
    report.append("expected_gain={}".format(expected_gain))
    report.append("expected_pre_dither_pixel={}".format(expected_linear))
    actions = collect_action_records(controller)
    draws = [a for a in actions if "Vortex.PostProcess.Tonemap" in a.path
             and a.flags & rd.ActionFlags.Drawcall]
    if len(draws) != 1:
        raise RuntimeError("Expected one production tonemap draw")
    controller.SetFrameEvent(draws[0].event_id, True)
    report.append("tonemap_event={}".format(draws[0].event_id))
    state = controller.GetPipelineState()
    names = resource_id_to_name(controller)
    constants = [u.descriptor for u in state.GetReadOnlyResources(rd.ShaderStage.Pixel, True)
                 if u.descriptor.byteSize == 48 and u.descriptor.elementByteSize == 48]
    if len(constants) != 1:
        raise RuntimeError("Bound tonemap constants not identified uniquely")
    constant = constants[0]
    data = bytes(controller.GetBufferData(constant.resource,
                                         constant.byteOffset, 48))
    source_slot, exposure_slot, bloom_slot, curve = struct.unpack_from("<4I", data)
    gain, gamma, bloom, _ = struct.unpack_from("<4f", data, 16)
    report.append("bound_constant_offset={}".format(constant.byteOffset))
    report.append("source_slot={}".format(source_slot))
    report.append("exposure_slot={}".format(exposure_slot))
    report.append("bloom_slot={}".format(bloom_slot))
    report.append("uploaded_gain={}".format(gain))
    report.append("gamma={}".format(gamma))
    report.append("tone_mapper={}".format(curve))
    gain_matches = gain == expected_gain if case.startswith("EV") else math.isclose(
        gain, expected_gain, rel_tol=2e-5, abs_tol=2**-120)
    if (not automatic and not gain_matches) or gamma != 1 or curve != 0 or bloom != 0:
        raise RuntimeError("Production tonemap constants differ from fixture")
    # Every authored mode now consumes the same GPU-owned exposure record.
    states = [u.descriptor for u in state.GetReadOnlyResources(rd.ShaderStage.Pixel, True)
              if names.get(str(u.descriptor.resource))
              == "Vortex.PostProcess.Exposure.State"]
    if exposure_slot == 0xFFFFFFFF or len(states) != 1:
        raise RuntimeError("Tonemap did not bind the unified exposure state")
    raw_state = bytes(controller.GetBufferData(states[0].resource,
                                               states[0].byteOffset, 80))
    displayed, target_gain, latent, latent_target = struct.unpack_from("<4f", raw_state)
    measured_luminance, meter_ev, state_flags, fallback = struct.unpack_from("<2f2I", raw_state, 16)
    settings_revision = struct.unpack_from("<Q", raw_state, 32)[0]
    state_frame = struct.unpack_from("<Q", raw_state, 56)[0]
    report.append("consumed_gpu_gain={}".format(displayed))
    report.append("latent_gpu_gain={}".format(latent))
    report.append("raw_meter_ev={}".format(meter_ev))
    report.append("state_flags={}".format(state_flags))
    report.append("state_settings_revision={}".format(settings_revision))
    report.append("state_frame_sequence={}".format(state_frame))
    if not all(math.isfinite(v) for v in (latent, displayed, meter_ev, target_gain, latent_target, measured_luminance)):
        raise RuntimeError("Nonfinite exposure state")
    valid_flags = 15 if automatic else 3
    if (not math.isclose(displayed, expected_gain, rel_tol=2e-5, abs_tol=2**-120)
            or state_flags & 15 != valid_flags):
        failures.append("GPU gain/meter differs: latent={} displayed={} EV={} flags={} expected={}".format(
            latent, displayed, meter_ev, state_flags, expected_gain))
    if settings_revision == 0 or state_frame == 0:
        failures.append("GPU state did not carry settings/frame identity")

    # Audit the actual solve dispatch and its immutable prior-state binding.
    solves = []
    for action in actions:
        if not action.flags & rd.ActionFlags.Dispatch:
            continue
        controller.SetFrameEvent(action.event_id, True)
        compute = controller.GetPipelineState()
        reads = [u.descriptor for u in compute.GetReadOnlyResources(rd.ShaderStage.Compute, True)]
        solve_constants = [d for d in reads if d.byteSize == 112 and d.elementByteSize == 112]
        if not solve_constants:
            continue
        if len(solve_constants) != 1:
            raise RuntimeError("Unified solve constants not identified uniquely")
        d = solve_constants[0]
        raw = bytes(controller.GetBufferData(d.resource, d.byteOffset, 112))
        previous_slot, fixed_scale, mode, controls = struct.unpack_from("<If2I", raw, 64)
        report.append("solve_event={} previous_slot={} fixed_scale={} mode={} controls={}".format(
            action.event_id, previous_slot, fixed_scale, mode, controls))
        expected_mode = 2 if automatic else 3 if case == "Disabled" else 1 if case == "Camera" else 0
        if mode != expected_mode:
            failures.append("Unified solve mode differs from the fixture")
        prior = [d for d in reads if names.get(str(d.resource)) == "Vortex.PostProcess.Exposure.State"]
        if previous_slot == 0xFFFFFFFF or len(prior) != 1:
            raise RuntimeError("Steady-state solve did not read its prior GPU state")
        if prior[0].resource == states[0].resource:
            failures.append("Solve overwrote its pinned prior state")
        solves.append(action.event_id)
    if len(solves) != 1:
        raise RuntimeError("Expected exactly one unified exposure solve")
    controller.SetFrameEvent(draws[0].event_id, True)
    state = controller.GetPipelineState()

    textures = {str(t.resourceId): t for t in controller.GetTextures()}
    sources = [u.descriptor for u in state.GetReadOnlyResources(rd.ShaderStage.Pixel, True)
               if str(u.descriptor.resource) in textures
               and names.get(str(u.descriptor.resource))
               in ("SceneColor", "ResolvedSceneColor")]
    if len(sources) != 1:
        raise RuntimeError("Expected one actually bound SceneColor input; resources: {}".format(
            [(names.get(str(u.descriptor.resource)), str(u.descriptor.resource))
             for u in state.GetReadOnlyResources(rd.ShaderStage.Pixel, True)]))
    source = sources[0]
    report.append("bound_source_product={}".format(names[str(source.resource)]))
    target = state.GetOutputTargets()[0]
    desc = textures[str(target.resource)]
    report.append("target_format={}".format(desc.format.Name()))
    report.append("target_dimensions={}x{}".format(desc.width, desc.height))
    sub = rd.Subresource()
    sub.mip = sub.slice = sub.sample = 0
    bayer = (0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5)
    # A 4x4 block includes every Bayer phase and avoids all geometry edges.
    for dy in range(4):
        for dx in range(4):
            x, y = int(desc.width) // 2 + dx, int(desc.height) // 2 + dy
            src = controller.PickPixel(source.resource, x, y, sub,
                                       rd.CompType.Float).floatValue
            out = controller.PickPixel(target.resource, x, y, sub,
                                       rd.CompType.UNorm).floatValue
            dither = (bayer[(x & 3) | ((y & 3) << 2)] / 16 - 0.5) / 255
            expected_code = round(max(0, min(1, expected_linear + dither)) * 255)
            actual = [round(float(out[c]) * 255) for c in range(3)]
            report.append("pixel_{}_{}=source:{},output_codes:{},expected:{}".format(
                x, y, [float(src[c]) for c in range(4)], actual, expected_code))
            if any(float(src[c]) != expected_input for c in range(3)) or float(src[3]) != 1:
                failures.append("input differs from exact fixture/full coverage")
            if any(abs(code - expected_code) > 1 for code in actual):
                failures.append("output exceeds frozen one-code tolerance")

    output = Path(report_path).with_suffix(".png")
    save = rd.TextureSave()
    save.resourceId = target.resource
    save.destType = rd.FileType.PNG
    save.mip = 0
    save.slice.sliceIndex = 0
    save.sample.sampleIndex = 0
    save.typeCast = rd.CompType.UNorm
    save.alpha = rd.AlphaMapping.Preserve
    controller.SaveTexture(save, str(output))
    if not output.is_file() or not output.stat().st_size:
        raise RuntimeError("Tonemap output image was not exported")
    report.append("stage22_output_image={}".format(output))
    # PixelHistory's shaderOut is the float shader result before UNorm storage.
    # Bayer phase 8 contributes zero, exposing gains below display quantization.
    probe_x, probe_y = (int(desc.width) // 2 & ~3) + 1, int(desc.height) // 2 & ~3
    modifications = controller.PixelHistory(target.resource, probe_x, probe_y, sub, rd.CompType.UNorm)
    shader_outputs = [m.shaderOut.col.floatValue for m in modifications
                      if m.eventId == draws[0].event_id]
    if len(shader_outputs) != 1:
        raise RuntimeError("Expected one floating-point production shader output")
    raw_output = [float(shader_outputs[0][c]) for c in range(3)]
    report.append("pre_storage_shader_output={}".format(raw_output))
    if any(not math.isclose(v, expected_linear, rel_tol=2e-5, abs_tol=2**-120)
           for v in raw_output):
        failures.append("float shader output differs from independent pre-storage reference")
    if automatic:
        solves = []
        dispatch_resources = []
        for action in actions:
            if not action.flags & rd.ActionFlags.Dispatch:
                continue
            controller.SetFrameEvent(action.event_id, True)
            compute = controller.GetPipelineState()
            dispatch_resources.append((action.event_id, [
                (str(u.descriptor.resource), names.get(str(u.descriptor.resource)), u.descriptor.byteOffset,
                 u.descriptor.byteSize, u.descriptor.elementByteSize)
                for u in compute.GetReadOnlyResources(rd.ShaderStage.Compute, True)]))
            targets = [u.descriptor for u in compute.GetReadOnlyResources(rd.ShaderStage.Compute, True)
                       if u.descriptor.byteSize == 560]
            if targets:
                solves.append((action, targets))
        if len(solves) != 1:
            raise RuntimeError("Expected one Auto target solve: {}".format(dispatch_resources))
        action, targets = solves[0]
        controller.SetFrameEvent(action.event_id, True)
        report.append("normalized_target_consumer_event={}".format(action.event_id))
        if len(targets) != 1:
            raise RuntimeError("Expected one bound 560-byte normalized target record")
        raw = bytes(controller.GetBufferData(targets[0].resource, targets[0].byteOffset, 560))
        key_count, flags, initial, dark = struct.unpack_from("<2I2f", raw)
        knot_ev, knot_gain = struct.unpack_from("<2f", raw, 16)
        report.append("target_key_count={}".format(key_count))
        report.append("target_flags={}".format(flags))
        report.append("target_knot_ev={}".format(knot_ev))
        report.append("target_knot_log_gain={}".format(knot_gain))
        if case == "AutoCurveLocked":
            if key_count != 1 or flags != 1 or knot_ev != 0:
                raise RuntimeError("Incorrect normalized curve target record")
            if abs(initial - 1) > 2e-5 or abs(dark - 1) > 2e-5 or abs(knot_gain - 1) > 2e-5:
                raise RuntimeError("Large curve cancellation lost the key's one-stop bias")
            knots = [struct.unpack_from("<2f", raw, 16 + 8 * i) for i in range(key_count)]
            report.append("normalized_knots={}".format(knots))
            if failures:
                failures.append("normalized_knots={}".format(knots))
            if not all(math.isfinite(x) and math.isfinite(q) and -32 <= q <= 32
                       for x, q in knots):
                raise RuntimeError("Nonfinite/unbounded normalized GPU knots")
        else:
            if key_count != 1 or flags != 1 or knot_ev != float(case[4:]):
                raise RuntimeError("Incorrect locked target record")
            if not all(math.isfinite(v) and abs(v) <= 2e-5 for v in (initial, dark, knot_gain)):
                raise RuntimeError("Auto cancellation did not produce finite bounded GPU constants")
    report.append("numerical_verdict={}".format("fail" if failures else "pass"))
    if failures:
        raise RuntimeError("; ".join(sorted(set(failures))))


if __name__ == "__main__":
    run_ui_script("_exposure_fixed.txt", build_report)
