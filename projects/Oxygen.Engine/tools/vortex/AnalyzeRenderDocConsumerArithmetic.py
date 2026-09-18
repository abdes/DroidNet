"""Check native consumer bounds with exact perturbed products and roundoff extremes."""

from fractions import Fraction as F
from itertools import product
import json
import math
from pathlib import Path
import struct
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'shadows'))
from renderdoc_ui_analysis import collect_action_records, renderdoc_module, resource_id_to_name, run_ui_script


def check_case(raw, output):
    if not all(math.isfinite(x) and x >= 0 for x in output):
        raise RuntimeError('Invalid native consumer bound')
    r, a, rt, at, peak, steps, inverse_p, _ = map(F, raw)
    atten_r, atten_a, final_r, final_a, trans_r, trans_a, peak_r, peak_a = map(F, output)
    if trans_r < rt or trans_a < at + (F(16, 2**23) if rt or at else 0):
        raise RuntimeError('Inward transmittance cancellation bound')
    if peak_r != 0 or peak_a < r * peak + a:
        raise RuntimeError('Inward source maximum bound')
    # Independently vary both input errors and both consumer chains' accumulated
    # rounding. Gamma_n and n*min_normal cover normal rounding and FTZ loss.
    nu = steps / 2**23
    gamma = nu / (1 - nu)
    tiny = steps * F(2)**-126 * inverse_p
    checks = 2
    for source, transmission in product((F(0), peak / 4, peak), (F(0), F(1, 4), F(1))):
        reference = source * transmission
        for source_sign, t_sign in product((-1, 1), repeat=2):
            altered_source = max(F(0), source + source_sign * (r * source + a))
            altered_t = min(F(1), max(F(0), transmission + t_sign * (rt * transmission + at)))
            altered = altered_source * altered_t
            if abs(altered - reference) > atten_r * reference + atten_a:
                raise RuntimeError('Native attenuation underbounds an exact product')
            checks += 1
            if not atten_r and not atten_a:
                # An exact zero contribution also remains identical when only
                # its unused transmission is uncertain. Such chains share
                # their rounding; independently perturbing them is invalid.
                if altered != reference:
                    raise RuntimeError('Zero attenuation bound hides a changed product')
                if (final_r, final_a) != (0, 0):
                    raise RuntimeError('Zero-error identity lost')
                continue
            for reference_sign, observed_sign in product((-1, 1), repeat=2):
                ref_rounded = max(F(0), reference * (1 + reference_sign * gamma) + reference_sign * tiny)
                obs_rounded = max(F(0), altered * (1 + observed_sign * gamma) + observed_sign * tiny)
                if abs(obs_rounded - ref_rounded) > final_r * ref_rounded + final_a:
                    raise RuntimeError(f'Native arithmetic underbound: input={raw}')
                checks += 1
    return checks


def build_report(controller, report, capture_path, report_path):
    rd = renderdoc_module()
    names = resource_id_to_name(controller)
    count = checks = 0
    for action in collect_action_records(controller):
        if not action.flags & rd.ActionFlags.Dispatch:
            continue
        controller.SetFrameEvent(action.event_id, True)
        pipeline = controller.GetPipelineState()
        reads = [x.descriptor for x in pipeline.GetReadOnlyResources(rd.ShaderStage.Compute, True)]
        writes = [x.descriptor for x in pipeline.GetReadWriteResources(rd.ShaderStage.Compute, True)]
        inputs = [x for x in reads if names.get(str(x.resource)) == 'Tone bound arithmetic inputs']
        outputs = [x for x in writes if names.get(str(x.resource)) == 'Tone bound arithmetic output']
        if not inputs and not outputs:
            continue
        controls = [x for x in reads if x.byteSize == x.elementByteSize == 16]
        if any(len(x) != 1 for x in (inputs, outputs, controls)):
            raise RuntimeError('Missing native consumer arithmetic bindings')
        c = controls[0]
        params = struct.unpack('<4I', bytes(controller.GetBufferData(c.resource, c.byteOffset, 16)))
        if params[3] != 64:
            raise RuntimeError('Wrong native probe mode')
        source, target = inputs[0], outputs[0]
        cases = list(struct.iter_unpack('<8f', bytes(controller.GetBufferData(source.resource, source.byteOffset, source.byteSize))))
        results = list(struct.iter_unpack('<8f', bytes(controller.GetBufferData(target.resource, target.byteOffset, len(cases) * 32))))
        if len(results) != len(cases):
            raise RuntimeError('Incomplete native output')
        for raw, output in zip(cases, results):
            checks += check_case(raw, output)
            count += 1
    if count != 1728:
        raise RuntimeError(f'Incomplete consumer matrix: {count}')
    Path(report_path).with_suffix('.json').write_text(json.dumps({
        'verdict': 'pass', 'cases': count, 'exact_checks': checks,
        'scope': 'Captured attenuation, source maxima, cancellation and positive-chain rounding bounds; integrated scene and performance gates remain separate'}, indent=2) + '\n')
    report.append(f'consumer_arithmetic_verdict=pass cases={count} exact_checks={checks}')


if __name__ == '__main__':
    run_ui_script('_consumer_arithmetic.txt', build_report)
