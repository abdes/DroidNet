"""Replay the native fog-only sky fixture at EV0/EV2 with background on/off."""

import json
import math
from pathlib import Path
import struct
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "shadows"))
from renderdoc_ui_analysis import collect_action_records, renderdoc_module, resource_id_to_name, run_ui_script


def build_report(controller, report, capture_path, report_path):
    rd = renderdoc_module()
    names = resource_id_to_name(controller)
    textures = {str(t.resourceId): t for t in controller.GetTextures()}
    frames, draws = [], []

    def read_pixels(target):
        texture = textures[str(target)]
        if (texture.width, texture.height, texture.format.compCount) != (4, 4, 4):
            raise RuntimeError("Expected the native 4x4 RGBA fixture")
        width = texture.format.compByteWidth
        if width not in (2, 4):
            raise RuntimeError("Expected floating-point scene storage")
        raw = bytes(controller.GetTextureData(target, rd.Subresource()))
        return list(struct.iter_unpack("<4" + ("f" if width == 4 else "e"), raw))

    def check(actual, expected, label):
        if len(actual) != len(expected) or any(
            not math.isfinite(a) or abs(a - e) > 0.0005
            for a, e in zip(actual, expected)
        ):
            raise RuntimeError(f"{label}: actual={actual} expected={expected}")

    def srgb_to_linear(value):
        return value / 12.92 if value <= 0.04045 else ((value + 0.055) / 1.055) ** 2.4

    def linear_to_srgb(value):
        return 12.92 * value if value <= 0.0031308 else 1.055 * value ** (1 / 2.4) - 0.055

    for action in collect_action_records(controller):
        if not action.flags & rd.ActionFlags.Drawcall:
            continue
        controller.SetFrameEvent(action.event_id, True)
        pipeline = controller.GetPipelineState()
        shader = pipeline.GetShaderReflection(rd.ShaderStage.Pixel)
        if not shader:
            continue
        entry = shader.entryPoint
        if entry not in ("VortexSkyPassPS", "VortexFogPassPS") and "Vortex.PostProcess.Tonemap" not in action.path:
            continue
        target = pipeline.GetOutputTargets()[0].resource
        after = read_pixels(target)
        index = len(frames)
        background = bool((index // 2) % 2)
        active = index % 2 == 0
        ev = 0 if index < 4 else 2
        if entry in ("VortexSkyPassPS", "VortexFogPassPS"):
            if not active and entry == "VortexSkyPassPS":
                raise RuntimeError("Disabled main-pass sky fog still drew")
            if entry == "VortexSkyPassPS":
                reads = [x.descriptor for x in pipeline.GetReadOnlyResources(rd.ShaderStage.Pixel, True)]
                if not any(x.elementByteSize == 304 for x in reads):
                    raise RuntimeError("Missing 304-byte environment view ABI")
                # The first frame's unit domain can later adapt to EV2. The
                # fixture records the current P through its exposure binding.
                # Identify by resource name, avoiding other 16-byte structures.
                frames_data = [x for x in reads if names.get(str(x.resource)) == "Vortex.PostProcess.Exposure.Frame"]
                if len(frames_data) != 1:
                    raise RuntimeError("Missing sky pre-exposure binding")
                frame = frames_data[0]
                p = struct.unpack("<f", bytes(controller.GetBufferData(frame.resource, frame.byteOffset, 4)))[0]
                for pixel in after:
                    check(pixel, (0.1 * p, 0.2 * p, 0.3 * p, 0.5), "sky fog")
            else:
                controller.SetFrameEvent(action.event_id - 1, True)
                before = read_pixels(target)
                if before != after:
                    raise RuntimeError("Depth fog applied a second time to the sky")
            draws.append({"event": action.event_id, "shader": entry, "pixel": after[1]})
            continue
        coverage = 0.5 if active else 0.0
        expected = []
        for color in (0.1, 0.2, 0.3):
            fog = 2 * color * 2 ** -ev
            expected.append(linear_to_srgb(coverage * srgb_to_linear(fog) + (1 - coverage) * color)
                            if background else coverage * fog)
        check(after[1][:3], expected, "final display pixel (1,0)")
        if sum(d["shader"] == "VortexSkyPassPS" for d in draws) != int(active):
            raise RuntimeError("Expected exactly one sky draw when main fog is enabled")
        if sum(d["shader"] == "VortexFogPassPS" for d in draws) != 1:
            raise RuntimeError("Missing depth-fog exclusion check")
        frames.append({"ev": ev, "background": background, "main_fog": active,
                       "draws": draws, "display_event": action.event_id, "display_pixel": after[1]})
        draws = []
    if len(frames) != 8 or draws:
        raise RuntimeError(f"Expected eight complete fixture frames, got {len(frames)}")
    Path(report_path).with_suffix(".json").write_text(json.dumps({
        "verdict": "pass", "capture": str(capture_path), "frames": frames,
        "scope": "Fog-only scene and final display; atmosphere capture and surface IBL are separate checkpoints"
    }, indent=2) + "\n")
    report.append("sky_height_fog_verdict=pass frames=8 sky_draws=4 depth_exclusions=8")


if __name__ == "__main__":
    run_ui_script("_sky_height_fog.txt", build_report)
