import hashlib
import base64
import json
import shutil
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[4]
RUN = Path(__file__).resolve().parent
DOC = ROOT / 'design/vortex/plan'
ARCHIVE = DOC / 'baselines/ex07d-20260924'
ARCHIVE.mkdir(parents=True, exist_ok=True)
sys.path.insert(0,str(ROOT/'tools/vortex'))
from RunManyLightBaseline import presets
from SummarizeManyLightBaseline import summarize

def read(path): return json.loads(Path(path).read_text(encoding='utf-8-sig'))
def sha(path):
    with Path(path).open('rb') as f: return hashlib.file_digest(f,'sha256').hexdigest()
def relative(path): return Path(path).resolve().relative_to(ROOT).as_posix()
def save(path,value): Path(path).write_text(json.dumps(value,indent=2,allow_nan=False)+'\n',encoding='utf-8',newline='\n')
def reference(path): return {'path':relative(path),'sha256':sha(path),'bytes':Path(path).stat().st_size}

rows=[]
for number,(case,variant) in enumerate(presets().items(),1):
    for family in ['deferred','forward']:
        directory=RUN/'benchmarks-headroom'/f'{case}-{family}'/'baseline'
        controlled=(directory/'image-comparison.json').exists()
        if not controlled:
            directory=RUN/'benchmarks'/f'{case}-{family}'/'baseline'
        r=summarize(directory)
        gate=read(directory/'load-preflight.json') if controlled else None
        if controlled: assert gate['accepted']
        manifest=read(directory/'manifest.json')
        memory=r['memory_after']
        allocator=memory['allocator']['samples'][0]
        artifacts=['request.json','manifest.json','frames.csv','cpu-scopes.csv','gpu.json','image-comparison.json']
        if controlled: artifacts.append('load-preflight.json')
        artifacts += [i['file'] for i in r['images']]
        row={'id':f'B{number:02d}-{family[0].upper()}', 'case':case,'family':family,'request':manifest['request'],
             'headroom_checked':controlled,'comparison_limit':None if controlled else 'Captured before the CPU headroom requirement; GPU hardware snapshots exist, but CPU preflight is not recorded. Supporting timing reference; confirm load before a small-delta performance claim.',
             'timed_frames':r['frames'], 'frame_ms':r['frame_ms'],'gpu_frame_ms':r['gpu_frame_ms'],
             'cpu_lighting_union_ms':r['cpu_lighting_union_ms'],'cpu_scopes_ms':r['cpu_scopes_ms'],
             'gpu_scopes_ms':r['gpu_scopes_ms'],'noise':r['noise'],
             'memory':{'lighting_budget':memory['lighting_budget'],'allocator_local':allocator['local'],'allocator_non_local':allocator['non_local'],
                       'steady_buffer_creations':r['steady_buffer_creations'],'steady_texture_creations':r['steady_texture_creations'],
                       'lighting_staging_bytes_per_frame':r['lighting_staging_bytes_per_frame'],'shared_staging_bytes_per_frame':r['shared_staging_bytes_per_frame']},
             'images':r['images'],'image_comparison':r['image_comparison'],'load_preflight':gate,
             'artifacts':[reference(directory/a) for a in artifacts]}
        if controlled:
            earlier=RUN/'benchmarks'/f'{case}-{family}'/'baseline'
            old=summarize(earlier)
            row['earlier_same_binary_capture']={'directory':relative(earlier),
                'frame_ms':old['frame_ms'],'gpu_scopes_ms':old['gpu_scopes_ms'],
                'cpu_lighting_union_ms':old['cpu_lighting_union_ms'],'noise':old['noise'],
                'headroom_checked':False,'manifest':reference(earlier/'manifest.json')}
        rows.append(row)

scenes=[]
replay={}
for scene,short in [('InstancingTestScene','instancing'),('NewSponza_Main_glTF_003','sponza')]:
    for mode in ['tracy','native']:
        directory=RUN/'scenes'/f'{scene}-{mode}'
        data=read(directory/'summary.json')
        assert data['complete'] and data['load_preflight']['accepted']
        assert (directory/'exit-code.txt').read_text().strip()=='0'
        data['id']=f'A-{short.upper()}-{mode.upper()}'
        data['directory']=relative(directory)
        data['scene_inventory']=read(RUN/f'{scene}-inventory.json')
        scenes.append(data)
        save(ARCHIVE/f'{short}-{mode}.json',data)
        if mode=='tracy':
            shutil.copyfile(directory/'scene.png',ARCHIVE/f'{short}.png')
            shutil.copyfile(directory/'native.tracy',ARCHIVE/f'{short}.tracy')
            shutil.copyfile(directory/'demo_settings.json',ARCHIVE/f'{short}-settings.json')
            shutil.copyfile(directory/'demo_settings.final.json',ARCHIVE/f'{short}-resolved-settings.json')
            shutil.copyfile(directory/'imgui.ini',ARCHIVE/f'{short}-imgui.ini')
            replay[short]={name:{'sha256':sha(directory/name),'base64':base64.b64encode((directory/name).read_bytes()).decode('ascii')}
                           for name in ['demo_settings.json','imgui.ini']}

historical=[]
for ident,path,role in [
    ('H-MULTIVIEW',ROOT/'out/build-tracy-ninja/analysis/vortex/exposure-lightbench/ex07c/model2-results.json','Accepted model-2 MultiView control; not a current-code performance gate'),
    ('H-INSTANCING',ROOT/'out/analysis/light-shadow-audit-20260924/instancing-final-analysis.json','Accepted earlier conventional-shadow scene control; superseded by A-INSTANCING for new performance comparisons'),
    ('H-SPONZA-10M',ROOT/'out/analysis/light-shadow-audit-20260924/new-sponza-final-analysis.json','Accepted earlier 10 m-range Sponza; different content from current 4096 m-range scene'),
    ('H-BENCH-TRACY',ROOT/'out/analysis/ex07d/qualified-20260924/primary-summary.json','Established pre-optimization fully traced primary, before receiver-footprint correction'),
    ('H-BENCH-OPT-TRACY',ROOT/'out/analysis/ex07d/optimization-submission/summary.json','Intermediate fully traced optimization, before receiver-footprint correction'),
    ('H-BENCH-OPT-NATIVE',ROOT/'out/analysis/ex07d/optimization-production/summary.json','Intermediate native-stage-only primary/count runs, before receiver-footprint correction'),
    ('H-SPONZA-4096M-DIAGNOSTIC',ROOT/'out/analysis/ex07-regressions-20260924/sponza-profile-analysis.json','Post-fix diagnostic capture with no CPU/GPU headroom preflight; superseded by A-SPONZA')]:
    data=read(path)
    if ident.startswith('H-BENCH'):
        data=[{**{k:r[k] for k in ['case','family','frame_ms','gpu_frame_ms','gpu_scopes_ms','cpu_lighting_union_ms','noise','lighting_staging_bytes_per_frame']},'lighting_allocated_bytes':r['memory_after']['lighting_budget']['allocated_bytes'],'local_gpu_allocated_bytes':r['memory_after']['allocator']['samples'][0]['local']['allocation_bytes']} for r in data]
    historical.append({'id':ident,'role':role,'evidence':reference(path),'measurements':data})

checkpoint=read(RUN/'benchmarks-headroom/checkpoint.json')
save(ARCHIVE/'benchmark-checkpoint.json',checkpoint)
save(ARCHIVE/'benchmark-baselines.json',rows)
save(ARCHIVE/'historical-controls.json',historical)
save(ARCHIVE/'replay-inputs.json',{'encoding':'base64; authoritative original bytes, preserved across repository text formatting','scenes':replay})
shutil.copyfile(ROOT/'src/Oxygen/Vortex/Test/Lighting/LightingWorkloads.json',ARCHIVE/'LightingWorkloads.json')
shutil.copyfile(RUN/'CaptureScene.ps1',ARCHIVE/'CaptureScene.snapshot.ps1')
shutil.copyfile(RUN/'AnalyzeScene.py',ARCHIVE/'AnalyzeScene.snapshot.py')
shutil.copyfile(RUN/'BuildRegister.py',ARCHIVE/'BuildRegister.snapshot.py')
for family in ['deferred','forward']:
    shutil.copyfile(RUN/'benchmarks-headroom'/f'sparse-1024-{family}'/'baseline/phase-0-view-0.png',ARCHIVE/f'primary-{family}.png')
save(ARCHIVE/'register.json',{'schema_version':1,'renderer_commit':'137b681b2','collection_protocol_commits':['250917c9e','d90e15823'],
     'benchmark_rows':len(rows),'application_runs':len(scenes),'benchmark_record':'benchmark-baselines.json',
     'application_records':[f'{s}-{m}.json' for s in ['instancing','sponza'] for m in ['tracy','native']],
     'historical_record':'historical-controls.json','raw_capture_root':relative(RUN),
     'headroom_checked_benchmark_rows':sum(r['headroom_checked'] for r in rows),
     'preflight':'Five samples; ordinary desktop activity allowed. CPU mean 25%, NVIDIA GPU mean 50%; reject two samples above CPU 50% or GPU 70%. Early successful checks used a stricter single-spike rule and also satisfy this policy. Per-run observations retained.',
     'collection_limit':'The complete initial 54-row matrix preceded the CPU headroom requirement. 24 timed rows were repeated with preflight, then the blanket repeat stopped at user request to release the machine. The other 30 image-qualified timing records are retained with missing CPU-preflight coverage explicitly marked. All four subsequent scene runs use headroom checks.'})
print('Registered',len(rows),'benchmark baselines and',len(scenes),'application runs')
