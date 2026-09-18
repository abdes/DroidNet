"""Verify fog history source formats, retained bounds and paired native outputs."""

from fractions import Fraction as F
from pathlib import Path
import json
import math
import struct
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'shadows'))
from renderdoc_ui_analysis import collect_action_records, renderdoc_module, resource_id_to_name, run_ui_script


def compare_volume_pair(observed, reference):
    extent = tuple(observed['extent'])
    if len(extent) != 3 or any(n <= 0 for n in extent) or extent != tuple(reference['extent']):
        raise RuntimeError('Paired history volumes require identical nonempty extents')
    expected_texels = math.prod(extent)
    if len(observed['values']) != expected_texels or len(reference['values']) != expected_texels:
        raise RuntimeError('Paired history volumes require complete matching texel counts')
    bounds = tuple(max(a, b) for a, b in zip(observed['bounds'], reference['bounds']))
    checks = 0
    maximum_error = 0.0
    for i, (actual, expected) in enumerate(zip(observed['values'], reference['values'])):
        if len(actual) != 4 or len(expected) != 4:
            raise RuntimeError('Paired history texels require all four channels')
        for c, (a, b) in enumerate(zip(actual, expected)):
            if not math.isfinite(a) or not math.isfinite(b):
                raise RuntimeError('Nonfinite fixture output')
            error = abs(F(a) - F(b))
            r, absolute = bounds[2:] if c == 3 else bounds[:2]
            if error > F(r) * F(b) + F(absolute):
                raise RuntimeError(f'Native history underbound voxel={i} channel={c}')
            maximum_error = max(maximum_error, float(error))
            checks += 1
    if checks != 4 * expected_texels:
        raise RuntimeError('Incomplete paired history channel comparison')
    return checks, maximum_error


def build_report(controller, report, capture_path, report_path):
    rd = renderdoc_module()
    names = resource_id_to_name(controller)
    textures = {str(t.resourceId): t for t in controller.GetTextures()}
    records = []
    for action in collect_action_records(controller):
        if not action.flags & rd.ActionFlags.Dispatch or 'Vortex.Stage14.VolumetricFog' not in action.path:
            continue
        controller.SetFrameEvent(action.event_id, True)
        pipeline = controller.GetPipelineState()
        reads = [x.descriptor for x in pipeline.GetReadOnlyResources(rd.ShaderStage.Compute, True)]
        writes = [x.descriptor for x in pipeline.GetReadWriteResources(rd.ShaderStage.Compute, True)]
        constants = [x for x in reads if x.byteSize == x.elementByteSize == 544]
        histories = [x for x in reads if str(x.resource) in textures and textures[str(x.resource)].depth > 1]
        outputs = [x for x in writes if str(x.resource) in textures and textures[str(x.resource)].depth > 1]
        statuses = [x for x in writes if names.get(str(x.resource)) == 'Vortex.PostProcess.Exposure.Status']
        previous = [x for x in reads if names.get(str(x.resource)) == 'Vortex.PostProcess.Exposure.Status']
        frames = [x for x in reads if names.get(str(x.resource)) == 'Vortex.PostProcess.Exposure.Frame']
        if any(len(x) != 1 for x in (constants, histories, outputs, statuses, previous)):
            raise RuntimeError('Incomplete paired history capture bindings')
        if not frames or any(struct.unpack('<2f', bytes(controller.GetBufferData(x.resource, 0, 8))) != (1.0, 1.0) for x in frames):
            raise RuntimeError('This paired fixture requires unit P for its scene-unit comparison')
        c = constants[0]
        raw = bytes(controller.GetBufferData(c.resource, c.byteOffset, 544))
        history_flags = struct.unpack_from('<I', raw, 148)[0]
        output_half = struct.unpack_from('<I', raw, 536)[0]
        prior = textures[str(histories[0].resource)]
        output = textures[str(outputs[0].resource)]
        if history_flags != (3 if prior.format.compByteWidth == 2 else 1):
            raise RuntimeError('History precision flag does not describe its sampled resource')
        if bool(output_half) != (output.format.compByteWidth == 2):
            raise RuntimeError('Output precision flag does not describe its stored resource')
        if previous[0].resource == statuses[0].resource:
            raise RuntimeError('History aliases the writable current certificate')
        gradients = bytes(controller.GetBufferData(previous[0].resource, 224, 32))
        flags, count = struct.unpack_from('<I', gradients, 12)[0], struct.unpack_from('<I', gradients, 28)[0]
        if flags & 3 != 1 or count != prior.width * prior.height * prior.depth:
            raise RuntimeError('History gradient record is incomplete')
        current = bytes(controller.GetBufferData(statuses[0].resource, 112, 16))
        bounds = struct.unpack('<4f', current)
        data = bytes(controller.GetTextureData(outputs[0].resource, rd.Subresource()))
        values = list(struct.iter_unpack('<4e' if output_half else '<4f', data))
        if len(values) != output.width * output.height * output.depth:
            raise RuntimeError('Unexpected volume data layout')
        records.append({'event': action.event_id, 'history_flags': history_flags,
                        'history_component_bytes': prior.format.compByteWidth,
                        'output_component_bytes': output.format.compByteWidth,
                        'extent': (output.width, output.height, output.depth),
                        'gradient_texels': count, 'bounds': bounds, 'values': values})
    if len(records) != 2:
        raise RuntimeError(f'Expected two independent volume producers, got {len(records)}')
    observed, reference = records
    if reference['history_component_bytes'] != 4 or reference['output_component_bytes'] != 4:
        raise RuntimeError('The reference must retain FP32 storage and history')
    checks, maximum_error = compare_volume_pair(observed, reference)
    for entry in records:
        del entry['values']
    result = {'verdict': 'pass', 'producers': records,
              'exact_texel_enclosure_checks': checks, 'maximum_error': maximum_error,
              'scope': 'Source-format identity, prior gradient lease and native history enclosure; whole-scene admission remains open'}
    Path(report_path).with_suffix('.json').write_text(json.dumps(result, indent=2) + '\n')
    report.append(f'hardware_history_verdict=pass exact_checks={checks} maximum_error={maximum_error}')


if __name__ == '__main__':
    run_ui_script('_hardware_history.txt', build_report)
