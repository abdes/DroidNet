import bisect
import csv
import hashlib
import json
import re
import sys
from collections import defaultdict
from pathlib import Path

import numpy as np

directory = Path(sys.argv[1]).resolve()
traced = (directory / 'native.tracy').exists()

def stats(values):
    a = np.asarray(values, dtype=float)
    if not len(a) or not np.isfinite(a).all():
        raise ValueError('Incomplete timing population')
    return dict(count=len(a), mean=float(a.mean()), p50=float(np.percentile(a,50)),
                p95=float(np.percentile(a,95)), p99=float(np.percentile(a,99)), max=float(a.max()))

if traced:
    gpu = list(csv.DictReader((directory/'gpu.csv').open(encoding='utf-8-sig')))
    cpu = list(csv.DictReader((directory/'cpu.csv').open(encoding='utf-8-sig')))
    for row in gpu:
        row['t'] = int(row['Time from start of program'])/1e6
        row['dt'] = int(row['GPU execution time'])/1e6
    gpu.sort(key=lambda r:r['t'])
    markers = [r['t'] for r in gpu if r['name']=='Vortex.Stage3.DepthPrepass' and 20000<=r['t']<=50000]
    frame_scopes = [defaultdict(float) for _ in markers[:-1]]
    scope_counts = [defaultdict(int) for _ in markers[:-1]]
    for row in gpu:
        index=bisect.bisect_right(markers,row['t'])-1
        if 0<=index<len(frame_scopes):
            if row['dt']<0: raise ValueError('Invalid GPU scope')
            frame_scopes[index][row['name']]+=row['dt']
            scope_counts[index][row['name']]+=1
    scopes=sorted({k for frame in frame_scopes for k in frame})
    timing={k:stats([frame[k] for frame in frame_scopes]) for k in scopes}
    calls={k:sorted(set(frame[k] for frame in scope_counts)) for k in scopes}
    cpu_by_name=defaultdict(list)
    for row in cpu:
        t=int(row['ns_since_start'])/1e6
        if markers[0]<=t<markers[-1]: cpu_by_name[row['name']].append(int(row['exec_time_ns'])/1e6)
    cpu_stats={k:stats(v) for k,v in cpu_by_name.items()}
    method='Complete GPU intervals between consecutive depth-prepass starts, 20-50 s of a 55 s Tracy run; scopes inclusive and not additive; CPU statistics per call'
else:
    all_stamps=[]
    for line in (directory/'stderr.log').read_text(errors='replace').splitlines():
        m=re.match(r'(\d\d):(\d\d):(\d\d)\.(\d\d\d)',line)
        if not m: continue
        h,mi,s,ms=map(int,m.groups()); t=((h*60+mi)*60+s)*1000+ms
        if not all_stamps: origin=t
        all_stamps.append(t)
    rows=[]
    for line in (directory/'stderr.log').read_text(errors='replace').splitlines():
        m=re.search(r'^(\d\d):(\d\d):(\d\d)\.(\d\d\d).*SceneTextureLeasePool.Churn frame=(\d+) ',line)
        if m:
            h,mi,s,ms,frame=map(int,m.groups()); t=((h*60+mi)*60+s)*1000+ms-origin
            if 20000<=t<=50000: rows.append((frame,t))
    if any(b[0]!=a[0]+1 for a,b in zip(rows,rows[1:])): raise ValueError('Missing frame log interval')
    markers=[t for f,t in rows]
    timing={}; calls={}; cpu_stats={}
    method='Complete CPU scene-completion log intervals 20-50 s after first startup log, Tracy OFF, informational logging ON, timestamps quantized to 1 ms; not GPU timing'

if len(markers)<100: raise ValueError('Insufficient steady frames')
deltas=np.diff(markers)
blocks=[float(np.mean([d for t,d in zip(markers[:-1],deltas) if lo<=t<lo+10000])) for lo in [20000,30000,40000]]
artifact_names=['run.json','identity.json','demo_settings.json','demo_settings.final.json','scene.png','process.json','hardware-end.csv','exit-code.txt','load-preflight.json']+(['native.tracy','gpu.csv','cpu.csv'] if traced else ['stderr.log'])
hashes={n:hashlib.sha256((directory/n).read_bytes()).hexdigest() for n in artifact_names}
result={'complete':True,'method':method,'frame_ms':stats(deltas),'fps':float(1000/np.mean(deltas)),
        'load_preflight':json.loads((directory/'load-preflight.json').read_text(encoding='utf-8-sig')),
        'window_seconds':[markers[0]/1000,markers[-1]/1000], 'gpu_scopes_ms':timing,'gpu_calls_per_frame':calls,
        'cpu_scopes_per_call_ms':cpu_stats,'noise':{'block_means_ms':blocks,'block_range_percent':100*(max(blocks)-min(blocks))/np.mean(deltas),'scope':'Within-capture three 10-second blocks; no between-run confidence claim'},
        'process':json.loads((directory/'process.json').read_text()),'identity':json.loads((directory/'identity.json').read_text()),
        'run':json.loads((directory/'run.json').read_text()),'artifact_sha256':hashes}
(directory/'summary.json').write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8',newline='\n')
print(directory.name,result['frame_ms'],result['fps'])
