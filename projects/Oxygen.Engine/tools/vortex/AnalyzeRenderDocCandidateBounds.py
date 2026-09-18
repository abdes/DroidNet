"""Check prospective HDR store certificates against exact captured inputs."""

from fractions import Fraction as F
from pathlib import Path
import json
import math
import struct
import sys

sys.path.insert(0, str(Path(__file__).resolve().parent))
from VerifyComposedExposureAdmission import half, reference_interval
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'shadows'))
from renderdoc_ui_analysis import collect_action_records, renderdoc_module, resource_id_to_name, run_ui_script


def build_report(controller, report, capture_path, report_path):
    rd = renderdoc_module()
    names = resource_id_to_name(controller)
    textures = {str(t.resourceId): t for t in controller.GetTextures()}
    records = []
    clears = checks = 0
    for action in collect_action_records(controller):
        if not action.flags & rd.ActionFlags.Dispatch:
            continue
        controller.SetFrameEvent(action.event_id, True)
        pipeline = controller.GetPipelineState()
        shader = pipeline.GetShaderReflection(rd.ShaderStage.Compute)
        if not shader or shader.entryPoint not in ('ClearSuitability', 'GatherSuitabilityMaximum'):
            continue
        reads = [x.descriptor for x in pipeline.GetReadOnlyResources(rd.ShaderStage.Compute, True)]
        writes = [x.descriptor for x in pipeline.GetReadWriteResources(rd.ShaderStage.Compute, True)]
        constants = [x for x in reads if x.byteSize == x.elementByteSize == 128]
        if len(constants) != 1:
            raise RuntimeError('Missing suitability constants')
        c = constants[0]
        words = struct.unpack('<32I', bytes(controller.GetBufferData(c.resource, c.byteOffset, 128)))
        if not words[7] & 512:
            continue
        statuses = [x for x in writes if names.get(str(x.resource)) == 'Vortex.PostProcess.Exposure.Status']
        if len(statuses) != 1:
            raise RuntimeError('Missing candidate-bound UAV')
        status = statuses[0].resource
        if shader.entryPoint == 'ClearSuitability':
            if bytes(controller.GetBufferData(status, 304, 48)) != bytes(48):
                raise RuntimeError('A repeated evaluation retained stale candidate bounds')
            clears += 1
            continue
        frames = [x for x in reads if names.get(str(x.resource)) == 'Vortex.PostProcess.Exposure.Frame']
        reports = [x for x in reads if names.get(str(x.resource)) == 'Vortex.Exposure.Suitability']
        sources = [x for x in reads if str(x.resource) in textures]
        if any(len(x) != 1 for x in (frames, reports, sources)):
            raise RuntimeError('Missing candidate source, frame, or selected-P report')
        p, inverse_p = struct.unpack('<2f', bytes(controller.GetBufferData(frames[0].resource, 0, 8)))
        selected_p = struct.unpack('<f', bytes(controller.GetBufferData(reports[0].resource, 0, 4)))[0]
        if F(p) * F(inverse_p) != 1 or not math.isfinite(selected_p) or selected_p <= 0:
            raise RuntimeError('Invalid numerical scale')
        product = words[8]
        index = {5: 0, 6: 1, 10: 2}[product]
        prior = struct.unpack('<4f', bytes(controller.GetBufferData(status, 80 + 16 * index, 16)))
        actual = struct.unpack('<4f', bytes(controller.GetBufferData(status, 304 + 16 * index, 16)))
        transmission = bool(words[7] & 4)
        relevant = prior if transmission else prior[:2]
        valid = all(math.isfinite(v) and v >= 0 for v in relevant) and prior[0] < 1 and (not transmission or prior[2] < 1)
        source = sources[0].resource
        desc = textures[str(source)]
        if (desc.width, desc.height, desc.depth) != words[4:7] or desc.format.compByteWidth != 4:
            raise RuntimeError('Source layout mismatch')
        pixels = list(struct.iter_unpack('<4f', bytes(controller.GetTextureData(source, rd.Subresource()))))
        if len(pixels) != desc.width * desc.height * desc.depth:
            raise RuntimeError('Incomplete source readback')
        if valid:
            if not all(math.isfinite(v) and v >= 0 for v in actual):
                raise RuntimeError('Valid fixture produced an invalid candidate certificate')
            for pixel in pixels:
                for channel in range(4 if transmission else 3):
                    rgb = channel < 3
                    offset = 0 if rgb else 2
                    observed = F(pixel[channel]) * (F(inverse_p) if rgb else 1)
                    reference = reference_interval(observed, *prior[offset:offset + 2])
                    if not rgb:
                        reference = tuple(min(F(1), v) for v in reference)
                    scale = F(selected_p) if rgb else F(1)
                    proposed = half(observed * scale) / scale
                    for endpoint in reference:
                        error = abs(proposed - endpoint)
                        allowance = F(actual[offset]) * endpoint + F(actual[offset + 1])
                        if error > allowance:
                            raise RuntimeError(f'Underbound at {action.event_id}, product {product}, channel {channel}: {float(error)} > {float(allowance)}')
                        checks += 1
        elif not math.isinf(actual[1]) or actual[1] < 0:
            raise RuntimeError('Invalid retained input was not rejected')
        records.append({'event': action.event_id, 'product': product, 'valid': valid,
                        'candidate_p': selected_p, 'bounds': actual})
    if not records or not clears:
        raise RuntimeError('No candidate evaluations')
    Path(report_path).with_suffix('.json').write_text(json.dumps({
        'verdict': 'pass', 'clears': clears, 'endpoint_checks': checks, 'records': records,
    }, indent=2) + '\n')
    report.append(f'candidate_bounds_verdict=pass records={len(records)} clears={clears} endpoint_checks={checks}')
    report.append('scope=prospective texture stores only; sampled-consumer composition remains open')


if __name__ == '__main__':
    run_ui_script('_candidate_bounds.txt', build_report)
