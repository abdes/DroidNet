"""Audit actual composed-scene admission inputs and decisions independently."""

from fractions import Fraction as F
from pathlib import Path
import json
import math
import struct
import sys

sys.path.insert(0, str(Path(__file__).resolve().parent))
from VerifyComposedExposureAdmission import scene_intervals, evaluate, sampled_display_error
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'shadows'))
from renderdoc_ui_analysis import collect_action_records, renderdoc_module, resource_id_to_name, run_ui_script


def build_report(controller, report, capture_path, report_path):
    rd = renderdoc_module()
    names = resource_id_to_name(controller)
    textures = {str(t.resourceId): t for t in controller.GetTextures()}
    records = []
    for action in collect_action_records(controller):
        if not action.flags & rd.ActionFlags.Dispatch:
            continue
        controller.SetFrameEvent(action.event_id, True)
        pipeline = controller.GetPipelineState()
        shader = pipeline.GetShaderReflection(rd.ShaderStage.Compute)
        if not shader or shader.entryPoint != 'CheckSuitabilityProduct':
            continue
        reads = [x.descriptor for x in pipeline.GetReadOnlyResources(rd.ShaderStage.Compute, True)]
        writes = [x.descriptor for x in pipeline.GetReadWriteResources(rd.ShaderStage.Compute, True)]
        constants = [x for x in reads if x.byteSize == x.elementByteSize == 128]
        if len(constants) != 1:
            raise RuntimeError('Missing admission constants')
        c = constants[0]
        data = bytes(controller.GetBufferData(c.resource, c.byteOffset, 128))
        words = struct.unpack('<32I', data)
        if not words[7] & 256:
            continue
        if words[11] != 0 or words[10] != 0xffffffff:
            raise RuntimeError('This fixture audit requires Average metering without a mask')
        statuses = [x for x in reads if names.get(str(x.resource)) == 'Vortex.PostProcess.Exposure.Status']
        frames = [x for x in reads if names.get(str(x.resource)) == 'Vortex.PostProcess.Exposure.Frame']
        states = [x for x in reads if names.get(str(x.resource)) == 'Vortex.PostProcess.Exposure.State']
        sources = [x for x in reads if str(x.resource) in textures]
        if any(len(x) != 1 for x in (statuses, frames, states, sources, writes)):
            raise RuntimeError('Missing composed admission bindings')
        certificate = bytes(controller.GetBufferData(statuses[0].resource, 256, 48))
        errors = struct.unpack_from('<8f', certificate)
        candidate_p, products, flags, reserved = struct.unpack_from('<f3I', certificate, 32)
        output = struct.unpack('<2f10I', bytes(controller.GetBufferData(writes[0].resource, 0, 48)))
        inverse_p = struct.unpack('<f', bytes(controller.GetBufferData(frames[0].resource, 4, 4)))[0]
        gain = struct.unpack('<f', bytes(controller.GetBufferData(states[0].resource, 0, 4)))[0]
        current_store = bool(words[7] & 16)
        required_flags = 1 if current_store else 3
        checked_errors = errors[:4] if current_store else errors
        valid = flags & required_flags == required_flags and reserved == 0 and products == (words[23] or words[9])
        valid = valid and (current_store or candidate_p == output[0])
        valid = valid and all(math.isfinite(x) and x >= 0 for x in checked_errors) and all(checked_errors[i] < 1 for i in range(0, len(checked_errors), 2))
        expected = 0 if valid else 16
        samples = list(struct.iter_unpack('<4f', bytes(controller.GetTextureData(sources[0].resource, rd.Subresource()))))
        if len(samples) != words[4] * words[5]:
            raise RuntimeError('Incomplete source image')
        maximum_display_error = 0.0
        if valid:
            for index, sample in enumerate(samples):
                scene = [F(x) * F(inverse_p) for x in sample[:3]] + [F(sample[3])]
                reference, candidate = scene_intervals(scene, errors[:4], errors[4:], F(output[0]), bool(words[7] & 2), current_store)
                expected |= evaluate(reference, candidate, gain=F(gain),
                    cutoff=F(2) ** int(struct.unpack_from('<f', data, 72)[0]),
                    black_influence=F(struct.unpack_from('<f', data, 76)[0]), meter=bool(words[7] & 1))
                display_error = sampled_display_error(reference, candidate, gain=gain,
                    mapper=words[27], gamma=struct.unpack_from('<f', data, 112)[0],
                    background=struct.unpack_from('<3f', data, 96) if words[7] & 2 else None,
                    position=(index % words[4], index // words[4]))
                maximum_display_error = max(maximum_display_error, display_error)
                if display_error > .5 / 255:
                    expected |= 4
        # A continuous interval may conservatively reject where sampled corners
        # do not. Every independently witnessed failure must still be rejected.
        if output[3] & expected != expected or output[3] & ~(expected | 4):
            raise RuntimeError(f'Admission mismatch at {action.event_id}: GPU {output[3]}, independent {expected}; '
                               f'certificate={errors}, constants={words}, samples={samples}')
        records.append({'event': action.event_id, 'current_store': current_store,
                        'required_failure': expected, 'actual_failure': output[3],
                        'maximum_sampled_display_error': maximum_display_error, 'pixels': len(samples)})
    if not records:
        raise RuntimeError('No composed admission records')
    Path(report_path).with_suffix('.json').write_text(json.dumps({'verdict': 'pass', 'records': records}, indent=2) + '\n')
    report.append(f'composed_admission_verdict=pass records={len(records)}')
    report.append('scope=final interval decision; producer composition validity requires separate evidence')


if __name__ == '__main__':
    run_ui_script('_composed_admission.txt', build_report)
