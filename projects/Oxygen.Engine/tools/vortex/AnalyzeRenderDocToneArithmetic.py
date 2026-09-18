"""Exact-rational audit of the native signed tone-bound arithmetic probe."""

from fractions import Fraction as F
from pathlib import Path
import json
import math
import struct
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'shadows'))
from renderdoc_ui_analysis import collect_action_records, renderdoc_module, resource_id_to_name, run_ui_script


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
        if len(inputs) != 1 or len(outputs) != 1:
            raise RuntimeError('Incomplete arithmetic probe bindings')
        source = inputs[0]
        cases = list(struct.iter_unpack('<4f', bytes(controller.GetBufferData(source.resource, source.byteOffset, source.byteSize))))
        target = outputs[0]
        results = list(struct.iter_unpack('<8f', bytes(controller.GetBufferData(target.resource, target.byteOffset, len(cases)*32))))
        def enclosure(low, value, high):
            nonlocal checks
            if math.isnan(low) or math.isnan(high):
                raise RuntimeError('NaN arithmetic bound')
            if (not math.isinf(low) and F(low) > value) or (not math.isinf(high) and F(high) < value):
                raise RuntimeError(f'Arithmetic underbound: {low} <= {value} <= {high}')
            if low == math.inf or high == -math.inf:
                raise RuntimeError('Infinity points inward')
            checks += 1
        for raw, result in zip(cases, results):
            values = tuple(map(F, raw))
            enclosure(result[0], values[0], result[1])
            for a in values[:2]:
                for b in values[2:]:
                    enclosure(result[2], a+b, result[3])
                    enclosure(result[4], a*b, result[5])
                enclosure(result[6], max(F(0), a)**2, result[7])
            count += 1
    if count < 67:
        raise RuntimeError('Missing signed-zero/subnormal/boundary controls')
    Path(report_path).with_suffix('.json').write_text(json.dumps({
        'verdict': 'pass', 'cases': count, 'exact_enclosure_checks': checks,
    }, indent=2) + '\n')
    report.append(f'tone_arithmetic_verdict=pass cases={count} exact_checks={checks}')


if __name__ == '__main__':
    run_ui_script('_tone_arithmetic.txt', build_report)
