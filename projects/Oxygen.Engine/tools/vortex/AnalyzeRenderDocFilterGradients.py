"""Independently enclose each captured filtered-product reference gradient."""

from fractions import Fraction as F
from pathlib import Path
import struct
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "shadows"))
from renderdoc_ui_analysis import (
    collect_action_records, renderdoc_module, resource_id_to_name, run_ui_script,
)


def exact_texel_gradient_upper(samples, shape, inverse_p, with_transmission, wrap=False):
    """Conservative binary64 shortcut when reference intervals are points.

    Max and round-to-nearest subtraction are monotone. One nextafter above the
    maximum binary64 difference encloses the exact maximum dyadic difference.
    Final P scaling uses Fraction, so it introduces no additional rounding.
    """
    rgb, transmission = [0.0] * 3, [0.0] * 3
    width, height, depth = shape
    for z in range(depth):
        for y in range(height):
            for x in range(width):
                index = (z * height + y) * width + x
                a = samples[index]
                for axis, stride in enumerate((1, width, width * height)):
                    boundary = (x, y, z)[axis] + 1 >= shape[axis]
                    if shape[axis] == 1 or (boundary and not wrap):
                        continue
                    b = samples[index - (shape[axis] - 1) * stride if boundary else index + stride]
                    rgb[axis] = max(rgb[axis], abs(a[0] - b[0]), abs(a[1] - b[1]), abs(a[2] - b[2]))
                    if with_transmission:
                        transmission[axis] = max(transmission[axis], abs(a[3] - b[3]))
    def upper(value):
        if value == 0:
            return F(0)
        bits = struct.unpack("<Q", struct.pack("<d", value))[0]
        return F(struct.unpack("<d", struct.pack("<Q", bits + 1))[0])
    return [upper(v) * inverse_p for v in rgb] + [upper(v) for v in transmission]


def build_report(controller, report, capture_path, report_path):
    rd = renderdoc_module()
    names = resource_id_to_name(controller)
    textures = {str(t.resourceId): t for t in controller.GetTextures()}
    records = {}
    checked = preserved = 0
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
        if shader.entryPoint in ("VortexExposureAverageCS", "FinalizeFp16Suitability"):
            for status in statuses:
                for (resource, product), expected in records.items():
                    if str(status.resource) == resource:
                        offset = {5: 160, 6: 192, 10: 224}[product]
                        if bytes(controller.GetBufferData(status.resource, offset, 32)) != expected:
                            raise RuntimeError("Solve/finalizer overwrote a gradient record")
                        preserved += 1
        if shader.entryPoint != "GatherSuitabilityMaximum":
            continue
        reads = [x.descriptor for x in pipeline.GetReadOnlyResources(rd.ShaderStage.Compute, True)]
        constants = [x for x in reads if x.byteSize == x.elementByteSize == 96]
        if len(constants) != 1:
            raise RuntimeError("Missing gradient constants")
        c = constants[0]
        words = struct.unpack("<24I", bytes(controller.GetBufferData(c.resource, c.byteOffset, 96)))
        if not words[7] & 128:
            continue
        sources = [x for x in reads if str(x.resource) in textures]
        frames = [x for x in reads if names.get(str(x.resource)) == "Vortex.PostProcess.Exposure.Frame"]
        if any(len(x) != 1 for x in (statuses, sources, frames)):
            raise RuntimeError("Missing source/frame/status binding")
        status = statuses[0].resource
        product = words[8]
        offset = {5: 160, 6: 192, 10: 224}[product]
        data = bytes(controller.GetBufferData(status, 0, 256))
        output = struct.unpack_from("<3fI3fI", data, offset)
        if output[3] != 1 or words[7] & 256:
            raise RuntimeError("This capture requires valid reference intervals")
        with_transmission = bool(words[7] & 4)
        bounds = tuple(F(v) if channel < 2 or with_transmission else F(0)
                       for channel, v in enumerate(struct.unpack_from("<4f", data, {5: 80, 6: 96, 10: 112}[product])))
        p, inverse_p = (F(v) for v in struct.unpack("<2f", bytes(controller.GetBufferData(frames[0].resource, 0, 8))))
        if p * inverse_p != 1:
            raise RuntimeError("Invalid P metadata")
        desc = textures[str(sources[0].resource)]
        shape = (desc.width, desc.height, desc.depth)
        if shape != words[4:7] or output[7] != shape[0] * shape[1] * shape[2]:
            raise RuntimeError("Incomplete gradient reduction")
        raw = bytes(controller.GetTextureData(sources[0].resource, rd.Subresource()))
        samples = list(struct.iter_unpack("<4e" if desc.format.compByteWidth == 2 else "<4f", raw))
        if len(samples) != output[7]:
            raise RuntimeError("Unexpected texture layout")
        print(f"Checking event={action.event_id} product={product} shape={shape}", flush=True)
        if all(v == 0 for v in bounds[:4 if with_transmission else 2]):
            expected = exact_texel_gradient_upper(samples, shape, inverse_p, with_transmission)
        else:
            intervals = []
            for sample in samples:
                components = []
                for channel in range(4 if with_transmission else 3):
                    observed = F(sample[channel]) * (1 if channel == 3 else inverse_p)
                    r, a = bounds[2:] if channel == 3 else bounds[:2]
                    low = max(F(0), (observed - a) / (1 + r))
                    high = (observed + a) / (1 - r)
                    components.append((min(F(1), low), min(F(1), high)) if channel == 3 else (low, high))
                intervals.append(components)
            rgb, transmission = [F(0)] * 3, [F(0)] * 3
            width, height, depth = shape
            for z in range(depth):
                for y in range(height):
                    for x in range(width):
                        index = (z * height + y) * width + x
                        for axis, stride in enumerate((1, width, width * height)):
                            if (x, y, z)[axis] + 1 >= shape[axis]:
                                continue
                            for channel, (a, b) in enumerate(zip(intervals[index], intervals[index + stride])):
                                output_axis = transmission if channel == 3 else rgb
                                output_axis[axis] = max(output_axis[axis], a[1] - b[0], b[1] - a[0])
            expected = rgb + transmission
        actual = list(output[:3]) + list(output[4:7])
        if any(F(observed) < reference for observed, reference in zip(actual, expected)):
            raise RuntimeError(f"Gradient underestimates reference: {actual} versus {list(map(float, expected))}")
        for axis in range(3):
            if shape[axis] == 1 and (actual[axis] != 0 or actual[axis + 3] != 0):
                raise RuntimeError("Singleton axis has nonzero gradient")
        records[(str(status), product)] = data[offset:offset + 32]
        checked += 1
        report.append(f"event={action.event_id} product={product} extent={shape} address=clamp inverseP={float(inverse_p)} gradients={actual} count={output[7]}")
    if checked == 0:
        raise RuntimeError("No gradient records")
    report.append(f"filter_gradient_verdict=pass records={checked} preserved_tail_checks={preserved}")
    report.append("scope=reference neighbor-gradient enclosure; filtering displacement/arithmetic and final admission remain open")


if __name__ == "__main__":
    run_ui_script("_filter_gradients.txt", build_report)
