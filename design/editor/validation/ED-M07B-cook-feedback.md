# ED-M07B rendered cook feedback

Validated 2026-09-15 in the packaged Release UI test host, with builds idle.
The saved fixture contains 100 nodes and exactly 1,000 authored inputs from the
existing browser workload. Change a shared scalar material, submit each public
cook scope, then repeat it unchanged. The native pipeline/coordinator execute
both requests; the Cooking view renders their actual snapshots.

The timer starts before submission and stops on a composition frame where the
new run title and status agree with its snapshot, with visible indeterminate
progress when busy. Fast completion is also valid feedback. All eight samples
meet the 100 ms limit; submitting returns in 0.06-0.30 ms.

| Scope | Changed cook feedback | Unchanged repeat feedback |
| --- | --- | --- |
| Asset | 22.35 ms | 13.76 ms |
| Folder | 18.61 ms | 18.95 ms |
| CurrentScene | 18.59 ms | 14.69 ms |
| Project | 13.34 ms | 34.73 ms |

The original full-size no-op folder/project cases delayed feedback for
20,004/18,385 ms. Cook callbacks now yield away from the submitting context after
acquiring the writer. Cooking coalesces pending UI snapshots by operation while
retaining their complete message/asset history, and looks up existing asset rows
by URI rather than scanning the collection repeatedly.

The pipeline suite passes **444/444**. Cooking controls, history/recovery,
1,000-update coalescing and timing cases pass **27/27**. The final isolated timing
run passes **4/4**. Unchanged repeats reuse output and do not publish again.
Changed files have no unsuppressed analyzer or IDE diagnostics.

Evidence:

- `artifacts/TestResults/m07b-cook-feedback-workload.trx`: failing full-size baseline.
- `artifacts/TestResults/m07b-cook-dispatch-final.trx`: complete pipeline suite.
- `artifacts/TestResults/m07b-cook-responsiveness-final.trx`: combined UI suite.
- `artifacts/TestResults/m07b-cook-feedback-qualified.trx`: final timing and attached
  fixture source archives/hash manifests.
- `artifacts/m07b-cook-feedback-qualified.json`: extracted raw samples.

The saved fixture and machine inventory are described in
[the browser workload record](ED-M07B-browser-workload.md). This checks M07B's
operation feedback; it does not claim the later ED-M10 GPU/frame profile.
