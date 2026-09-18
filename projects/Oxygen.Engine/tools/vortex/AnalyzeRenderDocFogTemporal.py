"""Export consecutive fixed-camera fog frames with their exposure/history inputs."""

import hashlib
import json
from pathlib import Path
import struct
import sys

sys.path.insert(0, str(Path(__file__).resolve().parent))
from AnalyzeRenderDocMultiViewExposure import save_image
from AnalyzeRenderDocSceneExposure import select_scene_source
from renderdoc_ui_analysis import collect_action_records, renderdoc_module, resource_id_to_name, run_ui_script


def build_report(controller, report, capture_path, report_path):
    rd = renderdoc_module()
    names = resource_id_to_name(controller)
    textures = {str(t.resourceId): t for t in controller.GetTextures()}
    actions = collect_action_records(controller)
    tones = [a for a in actions if a.flags & rd.ActionFlags.Drawcall
             and 'Vortex.PostProcess.Tonemap' in a.path]
    records = []
    previous = 0
    for index, tone in enumerate(tones):
        fog = [a for a in actions if previous < a.event_id < tone.event_id
               and a.flags & rd.ActionFlags.Dispatch and 'Vortex.Stage14.VolumetricFog' in a.path]
        if len(fog) != 1:
            raise RuntimeError('Expected one fog producer per view/frame')
        controller.SetFrameEvent(fog[0].event_id, True)
        pipeline = controller.GetPipelineState()
        reads = [x.descriptor for x in pipeline.GetReadOnlyResources(rd.ShaderStage.Compute, True)]
        writes = [x.descriptor for x in pipeline.GetReadWriteResources(rd.ShaderStage.Compute, True)]
        constants = [x for x in reads if x.byteSize == x.elementByteSize == 544]
        histories = [x for x in reads if str(x.resource) in textures and textures[str(x.resource)].depth > 1]
        outputs = [x for x in writes if str(x.resource) in textures and textures[str(x.resource)].depth > 1]
        views = [x for x in reads if x.elementByteSize == 576]
        if any(len(x) != 1 for x in (constants, histories, outputs, views)):
            raise RuntimeError(f'Missing fog bindings: {[len(x) for x in (constants, histories, outputs, views)]}')
        c = constants[0]
        raw = bytes(controller.GetBufferData(c.resource, c.byteOffset, 544))
        v = views[0]
        view = bytes(controller.GetBufferData(v.resource, v.byteOffset, 576))
        history_flags = struct.unpack_from('<I', raw, 148)[0]
        validity = struct.unpack_from('<I', view, 560)[0]
        if not history_flags & 1 or not validity & 1 or view[:128] != view[256:384]:
            raise RuntimeError('Expected enabled history and stationary camera/projection')
        stem = Path(report_path).with_name(f'{Path(report_path).stem}-{index}')
        volume = textures[str(outputs[0].resource)]
        volume_path = Path(str(stem) + '.volume.rgba32f')
        volume_data = bytes(controller.GetTextureData(outputs[0].resource, rd.Subresource()))
        if volume.format.compByteWidth != 4 or len(volume_data) != volume.width * volume.height * volume.depth * 16:
            raise RuntimeError('Expected complete FP32 volume')
        volume_path.write_bytes(volume_data)
        entry = {'view': index % 2, 'fog_event': fog[0].event_id,
                 'history_flags': history_flags, 'view_validity': validity,
                 'history_weight': struct.unpack_from('<f', raw, 152)[0],
                 'jitter': struct.unpack_from('<3f', raw, 160),
                 'camera_sha256': hashlib.sha256(view[:128]).hexdigest(),
                 'history_resource': str(histories[0].resource),
                 'output_resource': str(outputs[0].resource),
                 'volume_extent': [volume.width, volume.height, volume.depth],
                 'volume': str(volume_path)}
        controller.SetFrameEvent(tone.event_id, True)
        pipeline = controller.GetPipelineState()
        reads = [x.descriptor for x in pipeline.GetReadOnlyResources(rd.ShaderStage.Pixel, True)]
        constants = [x for x in reads if x.byteSize == x.elementByteSize == 64]
        frames = [x for x in reads if names.get(str(x.resource)) == 'Vortex.PostProcess.Exposure.Frame']
        states = [x for x in reads if names.get(str(x.resource)) == 'Vortex.PostProcess.Exposure.State']
        if any(len(x) != 1 for x in (constants, frames, states)):
            raise RuntimeError('Missing tonemap/exposure inputs')
        c = constants[0]
        source = select_scene_source(controller, reads, textures, names,
            bytes(controller.GetBufferData(c.resource, c.byteOffset, 64)))
        p, inv_p = struct.unpack('<2f', bytes(controller.GetBufferData(frames[0].resource, frames[0].byteOffset, 8)))
        state = bytes(controller.GetBufferData(states[0].resource, states[0].byteOffset, 80))
        gain, target = struct.unpack_from('<2f', state)
        sequence = struct.unpack_from('<Q', state, 56)[0]
        if (p, inv_p, gain, target) != (1.0, 1.0, 2**-15, 2**-15):
            raise RuntimeError('Exposure changed from the fixed EV15 control')
        texture = textures[str(source.resource)]
        scene_data = bytes(controller.GetTextureData(source.resource, rd.Subresource()))
        if texture.format.compByteWidth != 4 or len(scene_data) != texture.width * texture.height * 16:
            raise RuntimeError('Expected complete FP32 scene')
        scene_path = Path(str(stem) + '.scene.rgba32f')
        scene_path.write_bytes(scene_data)
        image_path = Path(str(stem) + '.png')
        save_image(controller, rd, pipeline.GetOutputTargets()[0].resource, image_path)
        entry.update({'frame': sequence, 'pre_exposure': p, 'gain': gain, 'target_gain': target,
                      'extent': [texture.width, texture.height], 'scene': str(scene_path),
                      'image': str(image_path)})
        records.append(entry)
        previous = tone.event_id
    if len(records) != 6:
        raise RuntimeError(f'Expected three frames with two views, got {len(records)}')
    for view_index in range(2):
        view_records = records[view_index::2]
        for before, after in zip(view_records, view_records[1:]):
            if (after['frame'] != before['frame'] + 1
                    or after['camera_sha256'] != before['camera_sha256']
                    or after['history_resource'] != before['output_resource']):
                raise RuntimeError('Nonconsecutive camera/history sequence')
    result = {'verdict': 'pass', 'capture': str(capture_path), 'frames': records,
              'scope': 'Fixed exposure, stationary camera and consecutive native history; temporal differences measured separately'}
    Path(report_path).with_suffix('.json').write_text(json.dumps(result, indent=2) + '\n')
    report.append('fog_temporal_inputs_verdict=pass frames=3 views=2')


if __name__ == '__main__':
    run_ui_script('_fog_temporal.txt', build_report)
