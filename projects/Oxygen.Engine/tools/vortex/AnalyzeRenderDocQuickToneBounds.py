"""Check native quick display enclosures against an independent tone reference."""

from itertools import product
from pathlib import Path
import json
import struct
import sys

sys.path.insert(0, str(Path(__file__).resolve().parent))
from VerifyComposedExposureAdmission import display_color
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
            raise RuntimeError('Missing quick tone probe input/output')
        source, target = inputs[0], outputs[0]
        controls = [x for x in reads if x.byteSize == x.elementByteSize == 16]
        if len(controls) != 1:
            raise RuntimeError('Missing quick-probe controls')
        control = controls[0]
        mode = struct.unpack('<4I', bytes(controller.GetBufferData(control.resource, control.byteOffset, 16)))[3]
        mapper = (mode >> 1) & 3
        cases = list(struct.iter_unpack('<8f', bytes(controller.GetBufferData(source.resource, source.byteOffset, source.byteSize))))
        bounds = list(struct.iter_unpack('<8f', bytes(controller.GetBufferData(target.resource, target.byteOffset, len(cases)*32))))
        gamma = struct.unpack('<f', struct.pack('<f', .8 if mode & 16 else 2.2))[0]
        for case, bound in zip(cases, bounds):
            if bound[3] != 1:
                raise RuntimeError('A fixture inside the quick domain was rejected')
            points = set(product(*zip(case[:4], case[4:])))
            points.add(tuple((a+b)/2 for a, b in zip(case[:4], case[4:])))
            for point in points:
                color = display_color(point, gain=1, mapper=mapper, gamma=gamma,
                    background=(.25, .25, .25), position=(1, 0))
                for c, value in enumerate(color):
                    if not bound[c] <= value <= bound[c+4]:
                        raise RuntimeError(f'Quick display underbound in box {count}, channel {c}: {bound[c]} <= {value} <= {bound[c+4]}, input {point}')
                    checks += 1
            count += 1
    if count != 2048:
        raise RuntimeError(f'Incomplete color/coverage boxes: {count}')
    Path(report_path).with_suffix('.json').write_text(json.dumps({
        'verdict': 'pass', 'mapper': mapper, 'gamma': gamma, 'boxes': count, 'sampled_channel_checks': checks,
        'scope': 'Independent corner/interior controls; analytic arithmetic allowances are separately derived',
    }, indent=2)+'\n')
    report.append(f'quick_tone_bounds_verdict=pass boxes={count} sampled_channel_checks={checks}')


if __name__ == '__main__':
    run_ui_script('_quick_tone_bounds.txt', build_report)
