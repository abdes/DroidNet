"""Audit real scene-certificate bindings, source maxima and exact transfers."""

from fractions import Fraction as F
from pathlib import Path
import json
import math
import os
import random
import struct
import sys

sys.path.insert(0, str(Path(__file__).resolve().parent))
from VerifyHardwareFilterBounds import coefficients
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'shadows'))
from renderdoc_ui_analysis import collect_action_records, renderdoc_module, resource_id_to_name, run_ui_script


def transfer_checks(raw, control, current_p, candidate_p):
    def values(offset, count):
        return tuple(map(F, struct.unpack_from('<' + 'f' * count, raw, offset)))
    usage = struct.unpack_from('<I', raw, 360)[0]
    opaque = values(128, 1)[0] / current_p
    translucent, height = values(352, 2)
    gain = F(struct.unpack('<f', struct.pack('<I', control[20]))[0])
    sky_gain = values(364, 1)[0]
    maxima = values(368, 3)
    rng = random.Random(0x45585035)
    count = 0
    for candidate in (False, True):
        p = candidate_p if candidate else current_p
        sampled = []
        for index in range(3):
            shape = control[8 + index * 4:12 + index * 4]
            if not all(shape[:3]):
                sampled.append(((F(0), F(0)), (F(0), F(0))))
                continue
            store = values((304 if candidate else 80) + 16 * index, 4)
            rgb_g = values(160 + 32 * index, 3)
            t_g = values(176 + 32 * index, 3)
            channels = []
            for alpha in (False, True):
                bound = store[2:] if alpha else (store[0], store[1] * p)
                gradient = t_g if alpha else tuple(g * p for g in rgb_g)
                arithmetic, hull = coefficients(bound, gradient, shape[:3], candidate or shape[3] != 0)
                chosen = hull if all(a <= b for a, b in zip(hull, arithmetic)) else arithmetic
                channels.append(chosen if alpha else (chosen[0], chosen[1] / p))
            sampled.append(channels)
        result = values(272 if candidate else 256, 4)
        for trial in range(512):
            reference = [(maxima[i] * rng.choice((F(0), F(1, 2), F(1))),
                          rng.choice((F(0), F(1, 2), F(1)))) for i in range(3)]
            changed = []
            for pair, bounds in zip(reference, sampled):
                color, t = pair
                r, a = bounds[0]
                rt, at = bounds[1]
                changed.append((max(F(0), color + rng.choice((-1, 1)) * (r * color + a)),
                                min(F(1), max(F(0), t + rng.choice((-1, 1)) * (rt * t + at)))))
            is_sky = bool(usage & 16) and trial % 2 == 0
            alpha = rng.choice((F(0), F(1, 2), F(1)))
            base = opaque * rng.choice((F(0), F(1)))
            weight = rng.choice((F(0), F(1, 2), F(1)))
            material_alpha = rng.choice((F(0), F(1, 2), F(1)))
            def compose(products):
                sky, ap, fog = products
                color, coverage = base, alpha
                ai, at = ap[0] * gain * weight, 1 - weight * (1 - ap[1])
                if is_sky:
                    color, coverage = sky[0] * sky_gain, sky[1]
                else:
                    if usage & 32 and gain >= F(1, 10000):
                        color = ai + color * at
                        coverage = 1 - (1 - coverage) * at
                    if usage & 2 and all(control[16:19]):
                        color = fog[0] + (height + color) * fog[1]
                        coverage = 1 - (1 - coverage) * fog[1]
                # A common local-fog transfer can add radiance and attenuate
                # both paths without increasing retained absolute error.
                if trial % 3 == 0:
                    color = F(1, 8) + color * F(3, 4)
                    coverage = 1 - (1 - coverage) * F(3, 4)
                if usage & 1:
                    source = translucent
                    if usage & 64 and gain >= F(1, 10000):
                        source = ai + source * at
                    color = material_alpha * source + (1 - material_alpha) * color
                    coverage = material_alpha + (1 - material_alpha) * coverage
                return color, coverage
            original, altered = compose(reference), compose(changed)
            for channel, (a, b) in enumerate(zip(original, altered)):
                r, absolute = result[channel * 2:channel * 2 + 2]
                if abs(b - a) > r * a + absolute:
                    raise RuntimeError(f'Consumer transfer underbound candidate={candidate} trial={trial} channel={channel}')
                count += 1
    return count


def build_report(controller, report, capture_path, report_path):
    rd = renderdoc_module()
    names = resource_id_to_name(controller)
    textures = {str(x.resourceId): x for x in controller.GetTextures()}
    rows, collected = [], {}
    for action in collect_action_records(controller):
        if not action.flags & rd.ActionFlags.Dispatch:
            continue
        controller.SetFrameEvent(action.event_id, True)
        pipeline = controller.GetPipelineState()
        shader = pipeline.GetShaderReflection(rd.ShaderStage.Compute)
        if not shader or shader.entryPoint not in ('GatherSuitabilityMaximum', 'SelectSuitabilityCandidate'):
            continue
        reads = [x.descriptor for x in pipeline.GetReadOnlyResources(rd.ShaderStage.Compute, True)]
        writes = [x.descriptor for x in pipeline.GetReadWriteResources(rd.ShaderStage.Compute, True)]
        controls = [x for x in reads if x.byteSize == x.elementByteSize == 128]
        if len(controls) != 1:
            raise RuntimeError('Missing evaluator constants')
        c = controls[0]
        words = struct.unpack('<32I', bytes(controller.GetBufferData(c.resource, c.byteOffset, 128)))
        gradient = shader.entryPoint == 'GatherSuitabilityMaximum' and words[7] & 128
        composed = shader.entryPoint == 'SelectSuitabilityCandidate' and words[7] & 1024
        if not gradient and not composed:
            continue
        statuses = [x for x in writes if names.get(str(x.resource)) == 'Vortex.PostProcess.Exposure.Status']
        frames = [x for x in reads if names.get(str(x.resource)) == 'Vortex.PostProcess.Exposure.Frame']
        if len(statuses) != 1 or len(frames) != 1:
            raise RuntimeError('Missing scene-certificate status/frame binding')
        status = statuses[0].resource
        raw = bytes(controller.GetBufferData(status, 0, 384))
        if len(raw) != 384:
            raise RuntimeError('Incomplete current status ABI')
        p = F(struct.unpack('<f', bytes(controller.GetBufferData(frames[0].resource, 0, 4)))[0])
        if gradient:
            sources = [x for x in reads if str(x.resource) in textures]
            if len(sources) != 1:
                raise RuntimeError('Missing gradient source')
            source = sources[0]
            texture = textures[str(source.resource)]
            if texture.format.compByteWidth != 4 or texture.format.compCount != 4:
                raise RuntimeError('Expected an FP32 qualification source')
            data = bytes(controller.GetTextureData(source.resource, rd.Subresource()))
            pixels = list(struct.iter_unpack('<4f', data))
            if len(pixels) != words[4] * words[5] * words[6]:
                raise RuntimeError('Incomplete source volume')
            maximum = max(max(pixel[:3]) for pixel in pixels)
            if any(not math.isfinite(value) or value < 0 for pixel in pixels for value in pixel[:3]):
                raise RuntimeError('Invalid source radiance')
            index = {5: 0, 6: 1, 10: 2}[words[8]]
            r, a = map(F, struct.unpack_from('<2f', raw, 80 + 16 * index))
            expected = (F(maximum) + a * p) / (1 - r) / p
            actual = F(struct.unpack_from('<f', raw, 368 + 4 * index)[0])
            if actual < expected:
                raise RuntimeError('Reference maximum rounds inward')
            collected[(str(status), index)] = {'texels': len(pixels), 'exact_maximum_lower_bound': float(expected), 'gpu_maximum': float(actual)}
            continue
        candidate_p, product_mask, flags, reserved = struct.unpack_from('<f3I', raw, 288)
        if flags != 3 or product_mask != words[22] or reserved != 0:
            raise RuntimeError('Incomplete generated scene certificate')
        if struct.unpack_from('<4I', raw, 256) != (0, 0, 0, 0):
            raise RuntimeError('This FP32 reference scene must retain exact current identity')
        sources = [collected[(str(status), i)] for i in range(3) if all(words[8+i*4:11+i*4])]
        checks = transfer_checks(raw, words, p, F(candidate_p))
        rows.append({'event': action.event_id, 'status': str(status), 'product_mask': product_mask,
                     'usage_flags': struct.unpack_from('<I', raw, 360)[0], 'pre_exposure': float(p),
                     'candidate_pre_exposure': candidate_p, 'candidate_bounds': struct.unpack_from('<4f', raw, 272),
                     'source_maxima': sources, 'exact_transfer_checks': checks})
    expected_frames = 3 if os.environ.get('OXYGEN_RENDERDOC_PASS_NAME') == 'SceneCompositionTemporal' else 1
    if len(rows) != 2 * expected_frames:
        raise RuntimeError(f'Expected main/PiP certificates for {expected_frames} frames')
    for frame in range(expected_frames):
        if rows[2 * frame]['status'] == rows[2 * frame + 1]['status']:
            raise RuntimeError('Main/PiP certificates alias')
    Path(report_path).with_suffix('.json').write_text(json.dumps({'verdict': 'pass', 'views': rows,
        'scope': 'Actual source maxima and bindings; independent exact transfer controls; floating arithmetic/visual/performance qualification is separate'}, indent=2) + '\n')
    report.append(f'scene_composition_verdict=pass views=2 frames={expected_frames} exact_transfer_checks={sum(x["exact_transfer_checks"] for x in rows)}')


if __name__ == '__main__':
    run_ui_script('_scene_composition.txt', build_report)
