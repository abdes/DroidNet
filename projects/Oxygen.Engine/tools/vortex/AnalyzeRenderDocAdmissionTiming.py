"""Measure the existing admission dispatches in an opt-in native capture."""

from pathlib import Path
import json
import math
import os
import re
import statistics
import struct
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'shadows'))
from renderdoc_ui_analysis import collect_action_records, renderdoc_module, resource_id_to_name, run_ui_script


def build_scene_report(controller, report, capture_path, report_path, rd, durations):
    qualification_entries = {'ClearSuitability', 'GatherSuitabilityMaximum',
                             'SelectSuitabilityCandidate', 'CheckSuitabilityProduct',
                             'FinalizeFp16Suitability', 'ConvertQualifiedSceneColor'}
    textures = {str(t.resourceId): t for t in controller.GetTextures()}
    rows, dispatches = [], []
    event_total = 0.0
    for action in collect_action_records(controller):
        duration = durations.get(action.event_id)
        if duration is not None:
            if not math.isfinite(duration) or duration < 0:
                raise RuntimeError('Invalid scene GPU duration')
            event_total += duration
        if action.flags & rd.ActionFlags.Dispatch:
            controller.SetFrameEvent(action.event_id, True)
            pipeline = controller.GetPipelineState()
            shader = pipeline.GetShaderReflection(rd.ShaderStage.Compute)
            if shader and shader.entryPoint in qualification_entries:
                if duration is None:
                    raise RuntimeError('Missing qualification duration')
                dispatches.append({'event': action.event_id, 'entry': shader.entryPoint,
                                   'gpu_ms': duration})
        if action.flags & rd.ActionFlags.Drawcall and 'Vortex.PostProcess.Tonemap' in action.path:
            controller.SetFrameEvent(action.event_id, True)
            target = textures[str(controller.GetPipelineState().GetOutputTargets()[0].resource)]
            if not dispatches or not any(x['entry'] == 'FinalizeFp16Suitability' for x in dispatches):
                raise RuntimeError('Incomplete scene qualification timing')
            by_entry = {}
            for item in dispatches:
                by_entry[item['entry']] = by_entry.get(item['entry'], 0.0) + item['gpu_ms']
            rows.append({'view': len(rows), 'width': target.width, 'height': target.height,
                         'qualification_gpu_ms': sum(x['gpu_ms'] for x in dispatches),
                         'all_event_gpu_ms_through_tonemap': event_total,
                         'by_entry_gpu_ms': by_entry, 'dispatches': dispatches})
            event_total, dispatches = 0.0, []
    if len(rows) != 2:
        raise RuntimeError('Expected one main/PiP scene frame')
    result = {'capture': str(capture_path), 'views': rows,
              'qualification_gpu_ms': sum(x['qualification_gpu_ms'] for x in rows),
              'scope': 'Actual scene replay event GPU durations including all explicit qualification dispatches and finalization. Embedded producer/consumer checks remain inside their draw/dispatch costs; event sums are not CPU/frame latency.'}
    Path(report_path).with_suffix('.json').write_text(json.dumps(result, indent=2) + '\n')
    report.append(f'scene_admission_timing_collection=complete total_gpu_ms={result["qualification_gpu_ms"]}')


def build_report(controller, report, capture_path, report_path):
    rd = renderdoc_module()
    names = resource_id_to_name(controller)
    durations = {r.eventId: r.value.d * 1000 for r in controller.FetchCounters([rd.GPUCounter.EventGPUDuration])}
    if os.environ.get('OXYGEN_RENDERDOC_PASS_NAME') == 'SceneAdmissionTiming':
        return build_scene_report(controller, report, capture_path, report_path, rd, durations)
    rows = []
    current_dispatches = []
    for action in collect_action_records(controller):
        if not action.flags & rd.ActionFlags.Dispatch:
            continue
        controller.SetFrameEvent(action.event_id, True)
        pipeline = controller.GetPipelineState()
        shader = pipeline.GetShaderReflection(rd.ShaderStage.Compute)
        if not shader or shader.entryPoint not in ('ClearSuitability', 'GatherSuitabilityMaximum',
                                                  'SelectSuitabilityCandidate', 'CheckSuitabilityProduct'):
            continue
        reads = [x.descriptor for x in pipeline.GetReadOnlyResources(rd.ShaderStage.Compute, True)]
        constants = [x for x in reads if x.byteSize == x.elementByteSize == 128]
        if len(constants) != 1:
            raise RuntimeError('Missing admission controls')
        c = constants[0]
        words = struct.unpack('<32I', bytes(controller.GetBufferData(c.resource, c.byteOffset, 128)))
        if shader.entryPoint == 'ClearSuitability' and words[7] == 0:
            current_dispatches = []
        duration = durations.get(action.event_id)
        if duration is None or not math.isfinite(duration) or duration <= 0:
            raise RuntimeError('GPU duration is unavailable')
        current_dispatches.append({'entry': shader.entryPoint, 'event': action.event_id, 'gpu_ms': duration})
        if shader.entryPoint != 'CheckSuitabilityProduct' or words[8] != 11 or not words[7] & 256:
            continue
        statuses = [x for x in reads if names.get(str(x.resource)) == 'Vortex.PostProcess.Exposure.Status']
        if len(statuses) != 1:
            raise RuntimeError('Missing composition certificate')
        alpha_error = struct.unpack('<f', bytes(controller.GetBufferData(statuses[0].resource, 284, 4)))[0]
        states = [x for x in reads if names.get(str(x.resource)) == 'Vortex.PostProcess.Exposure.State']
        if len(states) != 1:
            raise RuntimeError('Missing timing state identity')
        sequence = struct.unpack('<Q', bytes(controller.GetBufferData(states[0].resource, 56, 8)))[0]
        writes = [x.descriptor for x in pipeline.GetReadWriteResources(rd.ShaderStage.Compute, True)]
        reports = [x for x in writes if names.get(str(x.resource)) == 'Vortex.Exposure.Suitability']
        if len(reports) != 1:
            raise RuntimeError('Missing qualification result')
        flags = struct.unpack('<I', bytes(controller.GetBufferData(reports[0].resource, 12, 4)))[0]
        rows.append({'event': action.event_id, 'width': words[4], 'height': words[5],
                     'coverage': 'uncertain' if alpha_error else 'opaque', 'gpu_ms': duration,
                     'sequence': sequence, 'mapper': words[27],
                     'gamma': struct.unpack_from('<f', struct.pack('<32I', *words), 112)[0],
                     'total_dispatch_gpu_ms': sum(x['gpu_ms'] for x in current_dispatches),
                     'failure_flags': flags, 'dispatches': list(current_dispatches)})
    sequences = sorted({r['sequence'] for r in rows})
    if len(rows) != 21 or len(sequences) != 6:
        raise RuntimeError(f'Expected 21 image dispatches across six cases, found {len(rows)}/{len(sequences)}')
    summary = {}
    for sequence, kind in zip(sequences, ('opaque', 'uncertain', 'filmic', 'gamma', 'wide', 'main_pip')):
        group = [r for r in rows if r['sequence'] == sequence]
        views = 2 if kind == 'main_pip' else 1
        if len(group) != 3 * views:
            raise RuntimeError('Missing repeated timing case')
        iterations = [group[i:i+views] for i in range(0, len(group), views)]
        warm = [sum(r['gpu_ms'] for r in iteration) for iteration in iterations[1:]]
        summary[kind] = {'width': group[0]['width'], 'height': group[0]['height'],
                         'views': views,
                         'warm_min_ms': min(warm), 'warm_median_ms': statistics.median(warm),
                         'warm_max_ms': max(warm),
                         'warm_total_dispatch_median_ms': statistics.median(sum(r['total_dispatch_gpu_ms'] for r in iteration) for iteration in iterations[1:]),
                         'accepted': [all(r['failure_flags'] == 0 for r in iteration) for iteration in iterations]}
    result = {'scope': 'RenderDoc replay GPU durations for image checking and all qualification dispatches; excludes CPU submission and inter-dispatch barriers',
              'summary': summary, 'records': rows}
    Path(report_path).with_suffix('.json').write_text(json.dumps(result, indent=2) + '\n')
    report.append(json.dumps(result))
    report.append('admission_timing_collection=complete')


def parse_native_log(path):
    pattern = re.compile(r'admission_native case=(\w+) width=(\d+) height=(\d+) views=(\d+) iteration=(\d+) queue_ms=([\d.]+) first_flags=(\d+) second_flags=(\d+)')
    records = []
    for match in pattern.finditer(Path(path).read_text(errors='replace')):
        name, width, height, views, iteration, duration, first, second = match.groups()
        records.append({'case': name, 'width': int(width), 'height': int(height),
                        'views': int(views), 'iteration': int(iteration), 'queue_ms': float(duration),
                        'failure_flags': [int(first)] + ([int(second)] if int(views) == 2 else [])})
    summary = {}
    for name in ('opaque', 'uncertain', 'filmic', 'gamma', 'wide', 'main_pip'):
        group = [r for r in records if r['case'] == name]
        if [r['iteration'] for r in group] != [0, 1, 2]:
            raise RuntimeError(f'Incomplete native timing case: {name}')
        warm = [r['queue_ms'] for r in group[1:]]
        summary[name] = {'width': group[0]['width'], 'height': group[0]['height'],
                         'views': group[0]['views'], 'warm_min_ms': min(warm),
                         'warm_median_ms': statistics.median(warm), 'warm_max_ms': max(warm),
                         'accepted': [not any(r['failure_flags']) for r in group]}
    return {'scope': 'Warmed native graphics-queue timestamp intervals around controlled qualification; includes barriers and CPU submission gaps, excludes scene rendering and composition',
            'summary': summary, 'records': records}


if __name__ == '__main__':
    if '--native-log' in sys.argv:
        import argparse
        parser = argparse.ArgumentParser()
        parser.add_argument('--native-log', type=Path, required=True)
        parser.add_argument('--output', type=Path, required=True)
        args = parser.parse_args()
        result = parse_native_log(args.native_log)
        args.output.write_text(json.dumps(result, indent=2)+'\n')
        print(json.dumps(result['summary'], indent=2))
    else:
        run_ui_script('_admission_timing.txt', build_report)
