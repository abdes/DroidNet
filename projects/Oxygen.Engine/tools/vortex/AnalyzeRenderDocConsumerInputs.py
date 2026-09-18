"""Audit shader source-range collection before fog/AP attenuation."""

from pathlib import Path
import json
import math
import os
import struct
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'shadows'))
from renderdoc_ui_analysis import collect_action_records, renderdoc_module, resource_id_to_name, run_ui_script


def build_report(controller, report, capture_path, report_path):
    rd = renderdoc_module()
    height = os.environ.get('OXYGEN_RENDERDOC_PASS_NAME') == 'HeightFogInputs'
    names = resource_id_to_name(controller)
    rows, retained = [], {}
    preserved = 0
    for action in collect_action_records(controller):
        controller.SetFrameEvent(action.event_id, True)
        pipeline = controller.GetPipelineState()
        if action.flags & rd.ActionFlags.Dispatch:
            shader = pipeline.GetShaderReflection(rd.ShaderStage.Compute)
            if shader and shader.entryPoint == 'FinalizeFp16Suitability':
                for access in pipeline.GetReadWriteResources(rd.ShaderStage.Compute, True):
                    resource = access.descriptor.resource
                    if str(resource) in retained:
                        if bytes(controller.GetBufferData(resource, 352, 16)) != retained[str(resource)]:
                            raise RuntimeError('Finalization overwrote consumer inputs')
                        preserved += 1
            continue
        if not action.flags & rd.ActionFlags.Drawcall:
            continue
        wanted_path = 'Vortex.Stage15.Fog' if height else 'Vortex.Stage18.Translucency'
        if wanted_path not in action.path:
            continue
        reads = [x.descriptor for x in pipeline.GetReadOnlyResources(rd.ShaderStage.Pixel, True)]
        writes = [x.descriptor for x in pipeline.GetReadWriteResources(rd.ShaderStage.Pixel, True)]
        statuses = [x for x in writes if names.get(str(x.resource)) == 'Vortex.PostProcess.Exposure.Status']
        environments = [x for x in reads if x.byteSize == x.elementByteSize == 672]
        frames = [x for x in reads if names.get(str(x.resource)) == 'Vortex.PostProcess.Exposure.Frame']
        if any(len(x) != 1 for x in (statuses, environments, frames)):
            raise RuntimeError('Missing source-range status/frame/environment binding')
        status = statuses[0].resource
        raw = bytes(controller.GetBufferData(status, 352, 16))
        translucent_peak, height_peak, flags, sky_gain = struct.unpack('<2fIf', raw)
        p = struct.unpack('<f', bytes(controller.GetBufferData(frames[0].resource, 0, 4)))[0]
        e = environments[0]
        environment = bytes(controller.GetBufferData(e.resource, e.byteOffset, 672))
        if flags & ~255 or flags & 128 or not math.isfinite(sky_gain) or sky_gain < 0:
            raise RuntimeError('Invalid consumer usage/gain record')
        if not flags & 16 and sky_gain != 0:
            raise RuntimeError('Sky gain recorded without a sky consumer')
        if height:
            rgb = struct.unpack_from('<3f', environment)
            density = struct.unpack_from('<f', environment, 12)[0]
            if struct.unpack_from('<3f', environment, 16) != (0, 0, 0) or p != 4:
                raise RuntimeError('Unexpected analytical height-fog fixture')
            finite = all(math.isfinite(v) for v in rgb)
            if translucent_peak != 0 or flags != (2 if finite else 10):
                raise RuntimeError('Height-fog input flags crossed consumer families or lost invalid input')
            expected = max(rgb) * (1 - 2 ** (-math.log(2) * density)) if finite else 0
            if abs(height_peak - expected) > 2e-6:
                raise RuntimeError('Height-fog peak is not scene referred')
            if finite:
                target = pipeline.GetOutputTargets()[0].resource
                pixels = struct.iter_unpack('<4f', bytes(controller.GetTextureData(target, rd.Subresource())))
                if any(pixel[2] / p != height_peak for pixel in pixels):
                    raise RuntimeError('Recorded source peak differs from the actual pre-exposed output')
            actual = height_peak
        else:
            # The mixed proof has one translucent, scalar-only emissive card.
            # Its normals face away from the only sun; local lighting/IBL are off.
            lighting = [x for x in reads if x.byteSize == x.elementByteSize == 208]
            materials = [x for x in reads if names.get(str(x.resource)) == 'MaterialShadingConstantsAtlas']
            metadata = [x for x in reads if x.elementByteSize == 64 and x.byteSize == 320]
            if any(len(x) != 1 for x in (lighting, materials, metadata)):
                raise RuntimeError('Missing mixed-card isolation inputs')
            light = lighting[0]
            light_data = bytes(controller.GetBufferData(light.resource, light.byteOffset, 208))
            direction = struct.unpack_from('<3f', light_data, 112)
            if (struct.unpack_from('<2I', light_data, 60) != (1, 0)
                or direction[1] <= 0 or direction[2] <= 0
                or struct.unpack_from('<I', environment, 460)[0] != 0):
                raise RuntimeError('Lighting isolation is not established')
            m, d = materials[0], metadata[0]
            material_data = bytes(controller.GetBufferData(m.resource, m.byteOffset, m.byteSize))
            draw_data = bytes(controller.GetBufferData(d.resource, d.byteOffset, d.byteSize))
            indices = {struct.unpack_from('<I', draw_data, i + 32)[0] for i in range(0, 320, 64)}
            translucent = []
            for index in indices:
                offset = index * 112
                base = struct.unpack_from('<4f', material_data, offset)
                material_flags = struct.unpack_from('<I', material_data, offset + 28)[0]
                if base[:3] != (0, 0, 0) or material_flags != 1:
                    raise RuntimeError('Expected lit, scalar-only proof materials')
                if base[3] == .5:
                    translucent.append(struct.unpack_from('<3f', material_data, offset + 16))
            if len(translucent) != 1:
                raise RuntimeError('Expected one partial-coverage proof material')
            vertex_reads = [x.descriptor for x in pipeline.GetReadOnlyResources(rd.ShaderStage.Vertex, True)]
            vertices = [x for x in vertex_reads if x.elementByteSize == 72]
            if len(vertices) != 1:
                raise RuntimeError('Missing proof-card vertices')
            v = vertices[0]
            data = bytes(controller.GetBufferData(v.resource, v.byteOffset, v.byteSize))
            if any(struct.unpack_from('<3f', data, i + 12) != (0, -1, 0) for i in range(0, len(data), 72)):
                raise RuntimeError('Proof-card normals are not isolated from sunlight')
            expected = max(translucent[0])
            if flags & 15 != 1 or height_peak != 0 or translucent_peak != expected:
                raise RuntimeError(f'Translucent source peak mismatch: {translucent_peak}, expected {expected}, flags {flags}')
            actual = translucent_peak
        retained[str(status)] = raw
        rows.append({'event': action.event_id, 'pre_exposure': p, 'expected_scene_peak': expected,
                     'actual_scene_peak': actual, 'flags': flags, 'sky_rgb_gain_max': sky_gain,
                     'status': str(status)})
    if len(rows) != (3 if height else 2) or (not height and (preserved != 2 or len(retained) != 2)):
        raise RuntimeError(f'Incomplete consumer source proof: {len(rows)} draws, {preserved} finalizers')
    Path(report_path).with_suffix('.json').write_text(json.dumps({
        'verdict': 'pass', 'records': rows, 'preserved_finalizers': preserved,
    }, indent=2) + '\n')
    report.append(f'consumer_inputs_verdict=pass draws={len(rows)} preserved_finalizers={preserved}')
    report.append('scope=source-range routing and units; complete consumer error composition remains open')


if __name__ == '__main__':
    run_ui_script('_consumer_inputs.txt', build_report)
