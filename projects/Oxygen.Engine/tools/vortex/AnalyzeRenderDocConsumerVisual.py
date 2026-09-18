"""Export the small visual checkpoint and verify actual fog/grid consumers."""

import hashlib
import json
import os
from pathlib import Path
import struct
import sys

sys.path.insert(0, str(Path(__file__).resolve().parent))
from AnalyzeRenderDocSceneExposure import select_scene_source
from renderdoc_ui_analysis import collect_action_records, renderdoc_module, resource_id_to_name, run_ui_script


def build_report(controller, report, capture_path, report_path):
    mode = os.environ.get('OXYGEN_RENDERDOC_PASS_NAME')
    if mode not in ('Clear', 'Volume', 'Local'):
        raise RuntimeError('Expected Clear, Volume or Local checkpoint mode')
    rd = renderdoc_module()
    actions = collect_action_records(controller)
    names = resource_id_to_name(controller)
    textures = {str(t.resourceId): t for t in controller.GetTextures()}
    grid = [a for a in actions if a.flags & rd.ActionFlags.Drawcall and 'GroundGrid' in a.path]
    if grid:
        raise RuntimeError('The visual checkpoint must not render the editor grid')
    previous = 0
    views = []
    for tone in (a for a in actions if a.flags & rd.ActionFlags.Drawcall and 'Vortex.PostProcess.Tonemap' in a.path):
        draws = [a for a in actions if previous < a.event_id < tone.event_id and a.flags & rd.ActionFlags.Drawcall]
        fog = [a for a in draws if 'Vortex.Stage15.Fog' in a.path]
        local = [a for a in draws if 'Vortex.Stage15.LocalFog' in a.path]
        volume = [a for a in actions if previous < a.event_id < tone.event_id
                  and a.flags & rd.ActionFlags.Dispatch and 'Vortex.Stage14.VolumetricFog' in a.path]
        if bool(fog) != (mode == 'Volume') or bool(volume) != (mode == 'Volume') or bool(local) != (mode == 'Local'):
            raise RuntimeError(f'Incorrect fog execution: mode={mode} fog={len(fog)} volume={len(volume)} local={len(local)}')
        changed = 0
        pass_changes = []
        instance_count = 0
        volume_lighting = None
        if volume:
            controller.SetFrameEvent(volume[0].event_id, True)
            reads = [x.descriptor for x in controller.GetPipelineState().GetReadOnlyResources(rd.ShaderStage.Compute, True)]
            controls = [x for x in reads if x.byteSize == x.elementByteSize == 544]
            if len(controls) != 1:
                raise RuntimeError('Missing volumetric lighting controls')
            c = controls[0]
            data = bytes(controller.GetBufferData(c.resource, c.byteOffset, 544))
            volume_lighting = {'sun_direction_enabled': struct.unpack_from('<4f', data, 464),
                               'sun_illuminance_rgb_lux': struct.unpack_from('<3f', data, 480),
                               'sky_ambient_enabled': struct.unpack_from('<I', data, 116)[0]}
        for draw in fog + local:
            controller.SetFrameEvent(draw.event_id, True)
            pipeline = controller.GetPipelineState()
            target = pipeline.GetOutputTargets()[0].resource
            after = bytes(controller.GetTextureData(target, rd.Subresource()))
            if draw in local:
                reads = [x.descriptor for x in pipeline.GetReadOnlyResources(rd.ShaderStage.Pixel, True)]
                constants = [x for x in reads if x.byteSize == x.elementByteSize == 56]
                if len(constants) != 1:
                    raise RuntimeError('Missing local-fog constants')
                c = constants[0]
                instance_count = struct.unpack('<I', bytes(controller.GetBufferData(c.resource, c.byteOffset + 24, 4)))[0]
                if instance_count != 1:
                    raise RuntimeError('Expected one collected local-fog instance')
            # Include copies, clears and dispatches between the preceding draw
            # and this one. A preceding-draw snapshot is not the pass input.
            controller.SetFrameEvent(draw.event_id - 1, True)
            before = bytes(controller.GetTextureData(target, rd.Subresource()))
            if len(before) != len(after) or len(after) % 16:
                raise RuntimeError('Mismatched HDR comparison images')
            delta = sum(before[i:i+12] != after[i:i+12] for i in range(0, len(after), 16))
            changed += delta
            pass_changes.append({'event': draw.event_id, 'before_event': draw.event_id - 1,
                                 'target': str(target), 'changed_hdr_pixels': delta})
        if mode != 'Clear' and changed < 100:
            raise RuntimeError('Fog produced no meaningful localized/fullscreen HDR contribution')
        controller.SetFrameEvent(tone.event_id, True)
        reads = [x.descriptor for x in controller.GetPipelineState().GetReadOnlyResources(rd.ShaderStage.Pixel, True)]
        constants = [x for x in reads if x.byteSize == x.elementByteSize == 64]
        frames = [x for x in reads if names.get(str(x.resource)) == 'Vortex.PostProcess.Exposure.Frame']
        states = [x for x in reads if names.get(str(x.resource)) == 'Vortex.PostProcess.Exposure.State']
        if any(len(x) != 1 for x in (constants, frames, states)):
            raise RuntimeError('Missing view/exposure bindings')
        c = constants[0]
        source = select_scene_source(controller, reads, textures, names,
            bytes(controller.GetBufferData(c.resource, c.byteOffset, 64)))
        texture = textures[str(source.resource)]
        if texture.format.compByteWidth != 4 or texture.format.compCount != 4:
            raise RuntimeError('The checkpoint label requires actual RGBA32F scene storage')
        p, inv_p = struct.unpack('<2f', bytes(controller.GetBufferData(frames[0].resource, frames[0].byteOffset, 8)))
        gain, target_gain = struct.unpack('<2f', bytes(controller.GetBufferData(states[0].resource, states[0].byteOffset, 8)))
        frame_sequence = struct.unpack('<Q', bytes(controller.GetBufferData(states[0].resource, states[0].byteOffset + 56, 8)))[0]
        if (p, inv_p, gain, target_gain) != (1.0, 1.0, 2**-15, 2**-15):
            raise RuntimeError('Comparison exposure is not the fixed EV15 reference')
        data = bytes(controller.GetTextureData(source.resource, rd.Subresource()))
        if len(data) != texture.width * texture.height * 16:
            raise RuntimeError('Incomplete scene image')
        output = Path(report_path).with_name(f'{Path(report_path).stem}-view-{len(views)}.rgba32f')
        output.write_bytes(data)
        views.append({'view_index': len(views), 'frame_sequence': frame_sequence,
                      'width': texture.width, 'height': texture.height, 'format': 'RGBA32F',
                      'gain': gain, 'target_gain': target_gain, 'pre_exposure': p,
                      'fog_draws': len(fog), 'volume_dispatches': len(volume), 'local_draws': len(local),
                      'local_instances': instance_count, 'changed_hdr_pixels': changed,
                      'pass_changes': pass_changes,
                      'volume_lighting': volume_lighting,
                      'radiance': str(output), 'radiance_sha256': hashlib.sha256(data).hexdigest()})
        previous = tone.event_id
    if len(views) != 2:
        raise RuntimeError('Expected main and PiP')
    capture_hash = hashlib.sha256()
    with Path(capture_path).open('rb') as capture:
        for block in iter(lambda: capture.read(1024 * 1024), b''):
            capture_hash.update(block)
    Path(report_path).with_suffix('.json').write_text(json.dumps({'verdict': 'pass', 'mode': mode,
        'capture_identity': {'path': str(Path(capture_path).resolve()), 'sha256': capture_hash.hexdigest()},
        'grid_draws': 0, 'views': views, 'scope': 'Actual consumer execution, fixed exposure, formats and nonzero HDR contribution; human appearance reviewed separately'}, indent=2)+'\n')
    report.append(f'consumer_visual_verdict=pass mode={mode} views=2 grid_draws=0')


if __name__ == '__main__':
    run_ui_script('_consumer_visual.txt', build_report)
