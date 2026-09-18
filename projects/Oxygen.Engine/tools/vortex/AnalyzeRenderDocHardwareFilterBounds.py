"""Audit native filter-enclosure arithmetic against independent exact rationals."""

from fractions import Fraction as F
from pathlib import Path
import json
import struct
import sys

sys.path.insert(0, str(Path(__file__).resolve().parent))
from VerifyHardwareFilterBounds import arithmetic_parameters, intervals
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
            raise RuntimeError('Incomplete native filter probe')
        controls = [x for x in reads if x.byteSize == x.elementByteSize == 16]
        if len(controls) != 1:
            raise RuntimeError('Missing native filter probe controls')
        control = controls[0]
        if struct.unpack('<4I', bytes(controller.GetBufferData(control.resource, control.byteOffset, 16)))[3] != 32:
            raise RuntimeError('Unexpected native filter probe mode')
        source, target = inputs[0], outputs[0]
        cases = list(struct.iter_unpack('<12f', bytes(controller.GetBufferData(source.resource, source.byteOffset, source.byteSize))))
        results = list(struct.iter_unpack('<8f', bytes(controller.GetBufferData(target.resource, target.byteOffset, len(cases) * 32))))
        for raw, actual in zip(cases, results):
            observed, r, a = map(F, raw[:3])
            half = raw[3] != 0
            expected = intervals(observed, (r, a), tuple(map(F, raw[4:7])), tuple(map(int, raw[8:11])), half)
            if not F(actual[0]) <= expected[0] <= expected[1] <= F(actual[1]):
                raise RuntimeError(f'Inward native filter interval {count}: {actual[:2]} vs {tuple(map(float, expected))}, input={raw}')
            for value, exact in zip(actual[2:4], arithmetic_parameters(half)):
                if F(value) < exact:
                    raise RuntimeError(f'Inward filter arithmetic coefficient {count}')
            checks += 4
            count += 1
    if count != 433:
        raise RuntimeError(f'Incomplete filter matrix: {count}')
    Path(report_path).with_suffix('.json').write_text(json.dumps({
        'verdict': 'pass', 'cases': count, 'exact_endpoint_coefficient_checks': checks,
        'scope': 'Native bound arithmetic including subnormal operands; hardware samples qualified separately',
    }, indent=2) + '\n')
    report.append(f'hardware_filter_bounds_verdict=pass cases={count} exact_checks={checks}')


if __name__ == '__main__':
    run_ui_script('_hardware_filter_bounds.txt', build_report)
