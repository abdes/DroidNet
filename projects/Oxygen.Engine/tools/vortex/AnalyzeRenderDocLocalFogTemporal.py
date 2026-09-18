"""Trace local-fog culling/draw inputs over every captured view and frame."""

import hashlib
import json
from pathlib import Path
import struct
import sys

sys.path.insert(0, str(Path(__file__).resolve().parent))
from AnalyzeRenderDocMultiViewExposure import save_image
from renderdoc_ui_analysis import collect_action_records, renderdoc_module, resource_id_to_name, run_ui_script


def build_report(controller, report, capture_path, report_path):
    rd = renderdoc_module()
    actions = collect_action_records(controller)
    names = resource_id_to_name(controller)
    textures = {str(t.resourceId): t for t in controller.GetTextures()}
    rows = []
    batches, culls, draws = [], [], []
    for action in actions:
        if action.flags & rd.ActionFlags.Dispatch and 'Vortex.Stage14.LocalFogTiledCulling' in action.path:
            culls.append(action)
        if action.flags & rd.ActionFlags.Drawcall:
            if 'Vortex.Stage15.LocalFog' in action.path:
                draws.append(action)
            if 'Vortex.PostProcess.Tonemap' in action.path:
                batches.append((action, culls, draws))
                culls, draws = [], []
    for tone, culls, draws in batches:
        if len(culls) != 1 or len(draws) != 1:
            raise RuntimeError('Missing local culling/draw in the diagnostic sequence')
        controller.SetFrameEvent(culls[0].event_id, True)
        pipe = controller.GetPipelineState()
        reads = [x.descriptor for x in pipe.GetReadOnlyResources(rd.ShaderStage.Compute, True)]
        writes = [x.descriptor for x in pipe.GetReadWriteResources(rd.ShaderStage.Compute, True)]
        controls = [x for x in reads if x.elementByteSize == 144]
        args = [x for x in writes if names.get(str(x.resource)) == 'Vortex.Environment.LocalFogTileDrawArgs']
        tiles = [x for x in writes if names.get(str(x.resource)) == 'Vortex.Environment.LocalFogTileData']
        if any(len(x) != 1 for x in (controls, args, tiles)):
            raise RuntimeError('Missing local culling controls/outputs')
        c = controls[0]
        raw = bytes(controller.GetBufferData(c.resource, c.byteOffset, 144))
        words = struct.unpack_from('<12I', raw)
        argument_resource = args[0].resource
        cull_args = struct.unpack('<4I', bytes(controller.GetBufferData(argument_resource, 0, 16)))
        tile_resource = tiles[0].resource
        tile_data = bytes(controller.GetTextureData(tile_resource, rd.Subresource()))
        tile_desc = textures[str(tile_resource)]
        counts = [tile_data[y * tile_desc.width + x] for y in range(words[7]) for x in range(words[6])]
        row = {'view': len(rows) % 2, 'cull_event': culls[0].event_id,
               'draw_event': draws[0].event_id, 'use_hzb': words[9],
               'tile_extent': list(words[6:8]), 'cull_arguments': list(cull_args),
               'occupied_tiles': sum(x > 0 for x in counts),
               'tile_hash': hashlib.sha256(bytes(counts)).hexdigest(),
               'cull_constants_sha256': hashlib.sha256(raw).hexdigest(),
               'tile_resource': str(tile_resource), 'args_resource': str(argument_resource)}
        controller.SetFrameEvent(draws[0].event_id, True)
        pipe = controller.GetPipelineState()
        row['draw_arguments'] = list(struct.unpack('<4I', bytes(controller.GetBufferData(argument_resource, 0, 16))))
        pixel_reads = [x.descriptor for x in pipe.GetReadOnlyResources(rd.ShaderStage.Pixel, True)]
        controls = [x for x in pixel_reads if x.elementByteSize == 56]
        if len(controls) != 1:
            raise RuntimeError('Missing local compose constants')
        c = controls[0]
        row['compose_constants'] = list(struct.unpack('<14I', bytes(controller.GetBufferData(c.resource, c.byteOffset, 56))))
        target = pipe.GetOutputTargets()[0].resource
        target_desc = textures[str(target)]
        data = bytes(controller.GetTextureData(target, rd.Subresource()))
        row['scene_hash'] = hashlib.sha256(data).hexdigest()
        row['probes'] = [struct.unpack_from('<4f', data, 16 * ((target_desc.height * y // 16) * target_desc.width + target_desc.width * x // 16))
                         for y in range(1, 16) for x in range(1, 16)]
        controller.SetFrameEvent(tone.event_id, True)
        pipe = controller.GetPipelineState()
        reads = [x.descriptor for x in pipe.GetReadOnlyResources(rd.ShaderStage.Pixel, True)]
        frames = [x for x in reads if names.get(str(x.resource)) == 'Vortex.PostProcess.Exposure.Frame']
        states = [x for x in reads if names.get(str(x.resource)) == 'Vortex.PostProcess.Exposure.State']
        if len(frames) != 1 or len(states) != 1:
            raise RuntimeError('Missing exposure controls')
        row['pre_exposure'] = struct.unpack('<f', bytes(controller.GetBufferData(frames[0].resource, frames[0].byteOffset, 4)))[0]
        raw = bytes(controller.GetBufferData(states[0].resource, states[0].byteOffset, 80))
        row['gain'], row['target_gain'] = struct.unpack_from('<2f', raw)
        row['frame'] = struct.unpack_from('<Q', raw, 56)[0]
        if (row['pre_exposure'], row['gain'], row['target_gain']) != (1, 2**-15, 2**-15):
            raise RuntimeError('Diagnostic exposure changed')
        # Export every frame in short traces; longer traces export transitions.
        prior = rows[-2] if len(rows) >= 2 else None
        if prior is None or prior['scene_hash'] != row['scene_hash']:
            image_path = Path(report_path).with_name(f'{Path(report_path).stem}-{row["frame"]}-{row["view"]}.png')
            save_image(controller, rd, pipe.GetOutputTargets()[0].resource, image_path)
            row['image'] = str(image_path)
        rows.append(row)
    if len(rows) < 4 or len(rows) % 2:
        raise RuntimeError('Requires a multi-frame main/PiP capture')
    for view in range(2):
        for before, after in zip(rows[view::2], rows[view::2][1:]):
            if after['frame'] != before['frame'] + 1:
                raise RuntimeError('Nonconsecutive local-fog sequence')
    result = {'capture': str(capture_path), 'frames': rows,
              'scope': 'Per-frame diagnostic data; this is not a flicker acceptance verdict'}
    Path(report_path).with_suffix('.json').write_text(json.dumps(result, indent=2) + '\n')
    report.append(f'local_fog_trace_collected frames={len(rows)//2} views=2')


if __name__ == '__main__':
    run_ui_script('_local_fog_temporal.txt', build_report)
