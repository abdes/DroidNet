# VSM Performance Optimization Playbook

Status: `in_progress`
Audience: engineer profiling or redesigning Oxygen VSM hot paths
Scope: capture the successful Stage 13 optimization, the general workflow that
made it successful, the UE5 reference model used to guide the redesign, and
the repo-owned RenderDoc tooling used to measure and validate the change

This document records a method, not just an outcome. The Stage 13 merge
optimization succeeded because the work was narrowed to one hot pass, measured
on replay-safe late-frame captures, compared against a known-good reference
implementation shape, and then redesigned around work elimination instead of
micro-tuning instruction count.

## 1. What Made The Optimization Successful

The successful path had six properties:

1. The problem was localized to one pass.
   `VsmStaticDynamicMergePass` was isolated as the hot path instead of treating
   VSM cost as one opaque frame-level blob.
2. Timing came from replay-safe late-frame captures.
   The analysis intentionally targeted stable late frames, because early frames
   include warm-up and transient work that would have hidden the steady-state
   cost shape.
3. The investigation looked at workload shape, not only milliseconds.
   RenderDoc timing was paired with event inspection, dispatch size, and copy
   count so the real cost driver was obvious.
4. Oxygen was compared against the UE5 design, not against memory or guesswork.
   The fix came from recognizing that UE5 does selective per-page work while
   the old Oxygen pass was doing whole-slice movement and whole-pool launch.
5. The redesign removed unnecessary work instead of tuning the wrong work.
   The winning change was not "make the big dispatch cheaper"; it was "stop
   dispatching and copying when there is no eligible Stage 13 merge work."
6. Tests were corrected to the real stage boundary.
   The first regression was not the implementation. The regression was that
   Stage 13 tests were asserting Stage 12 facts as if they implied Stage 13
   merge work.

## 2. Stage 13 Case Study

### Before The Fix

The old Stage 13 path did three expensive things every frame:

- copied the dynamic slice out to a scratch atlas
- dispatched merge work over the full logical-page domain
- copied the scratch result back into the dynamic slice

That cost was measured directly from replay-safe RenderDoc captures:

- `out/build-ninja/analysis/merge_timing/late_frame35.merge_timing.json`
- `out/build-ninja/analysis/merge_timing/late_frame39.merge_timing.json`

Representative measured data from `late_frame35.merge_timing.json`:

- `VsmStaticDynamicMergePass = 2.073568 ms`
- dispatch event cost `0.982016 ms`
- two `CopyTextureRegion()` events cost `0.331296 ms` and `0.760256 ms`
- `cs_invocations = 69337088`

The important conclusion was not just "2 ms is bad." It was:

- the pass was paying whole-slice copy cost
- the pass was paying whole-pool dispatch cost
- the dirty-page decision happened too late to save launch cost

### UE5 Reference Model

The redesign was guided by the UE5 virtual shadow map implementation, primarily:

- `F:/projects/UnrealEngine2/Engine/Shaders/Private/VirtualShadowMaps/VirtualShadowMapPhysicalPageManagement.usf`
- `F:/projects/UnrealEngine2/Engine/Source/Runtime/Renderer/Private/VirtualShadowMaps/VirtualShadowMapArray.cpp`
- `F:/projects/UnrealEngine2/Engine/Shaders/Private/ShadowDepthPixelShader.usf`

The useful reference ideas were:

- keep one logical page namespace and treat static/dynamic as paired slices
- scan metadata and compact only the pages that need follow-up work
- launch expensive page work only for those compacted pages
- avoid whole-atlas copy-out and copy-back for steady-state frames

The value of the UE5 comparison was architectural, not line-by-line cloning.
It gave a correct workload shape to aim for.

### After The Fix

The current Stage 13 implementation uses selective page-local merge:

- `VsmShadowRenderer` publishes merge candidates from Stage 12 static-raster
  results
- `VsmStaticDynamicMergePass` deduplicates and validates logical-page
  candidates
- the shader merges one page at a time through a page-sized scratch surface
- dynamic-only invalidation becomes a Stage 13 no-op
- steady-state late frames with no eligible static merge work produce zero
  Stage 13 GPU work

Recorded evidence:

- before:
  - `out/build-ninja/analysis/merge_timing/late_frame35.merge_timing.json`
  - `out/build-ninja/analysis/merge_timing/late_frame39.merge_timing.json`
- after:
  - `out/build-ninja/analysis/merge_timing_post_selective/late_frame35.timing.txt`
  - `out/build-ninja/analysis/merge_timing_post_selective/late_frame40.timing.txt`

Representative late-frame result after the selective redesign:

- `work_event_count = 0`
- `total_gpu_duration_ms = 0.000000`

This was successful because the redesign removed the old pass shape entirely in
the no-work case. It did not merely shave fractions off the previous full-pool
path.

## 3. General Workflow For Future VSM Hot Paths

When another VSM pass looks expensive, use this order:

1. Capture replay-safe late frames with the narrow capture helper.
2. Use a pass-timing script to get the exact pass cost.
3. Use pass-focus or event-focus inspection to see what the pass is actually
   doing.
4. Identify the expensive shape:
   full-domain launch, whole-resource copy, redundant state publication,
   unnecessary work on reused pages, or poor compaction.
5. Check the UE5 reference implementation with a subagent instead of reasoning
   from memory.
6. Redesign the workload shape first.
   Prefer compaction, page selection, reuse, and early skip over instruction
   tuning inside an already-wrong dispatch.
7. Fix tests so they validate the stage's actual contract and boundary.
8. Record before and after artifacts in `out/build-ninja/analysis/...` and
   keep the commands in the repo docs.

## 4. Repo-Owned RenderDoc Tooling

The RenderDoc workflow that supported this optimization is intentionally split
between one narrow capture helper and several focused UI-only analyzers.

### Capture Helper

`Examples/RenderScene/Run-RenderSceneCapture.ps1`

Design goals:

- thin wrapper around the known-good RenderScene capture recipe
- always use `-v=-1`, `--fps 0`, `--directional-shadows vsm`,
  `--capture-provider renderdoc`, and `--capture-load search`
- expose only the minimum variables:
  - `-Frame`
  - `-Count`
  - `-Output`
- wait for capture files to stabilize before reporting success

Example:

```powershell
powershell -ExecutionPolicy Bypass -File `
  'H:\projects\DroidNet\projects\Oxygen.Engine\Examples\RenderScene\Run-RenderSceneCapture.ps1' `
  -Frame 35 `
  -Count 6 `
  -Output 'H:\projects\DroidNet\projects\Oxygen.Engine\out\build-ninja\analysis\merge_timing_post_selective\late'
```

### UI-Only Analysis Scripts

All RenderDoc analysis scripts in `Examples/RenderScene` are written to run
inside RenderDoc UI only:

`qrenderdoc.exe --ui-python <script.py> <capture.rdc>`

Key design rules:

- never use standalone Python replay
- keep the baseline analyzer bounded
- move expensive or narrow questions into separate scripts
- reuse `renderdoc_ui_analysis.py` for lifecycle, action traversal, report-path
  resolution, and clean shutdown
- write one bounded report per run through `OXYGEN_RENDERDOC_REPORT_PATH`

Relevant scripts:

- `AnalyzeRenderDocCapture.py`
  - broad K-a validation
- `AnalyzeRenderDocPassFocus.py`
  - pass-local action/resource inspection
- `AnalyzeRenderDocEventFocus.py`
  - one-event drill-down
- `AnalyzeRenderDocStage15Masks.py`
  - Stage 15 resource presence and writer/consumer evidence
- `AnalyzeRenderDocPassTiming.py`
  - exact GPU timing for one pass via `OXYGEN_RENDERDOC_PASS_NAME`

Example timing run:

```powershell
$env:OXYGEN_RENDERDOC_PASS_NAME = 'VsmStaticDynamicMergePass'
$env:OXYGEN_RENDERDOC_REPORT_PATH = 'H:\projects\DroidNet\projects\Oxygen.Engine\out\build-ninja\analysis\merge_timing_post_selective\late_frame35.timing.txt'
& 'C:\Program Files\RenderDoc\qrenderdoc.exe' --ui-python `
  'H:\projects\DroidNet\projects\Oxygen.Engine\Examples\RenderScene\AnalyzeRenderDocPassTiming.py' `
  'H:\projects\DroidNet\projects\Oxygen.Engine\out\build-ninja\analysis\merge_timing_post_selective\late_frame35.rdc'
```

## 5. Test Strategy Lessons

Two testing rules came directly out of the Stage 13 work:

- Stage-owned tests must validate the stage boundary, not upstream preconditions
  that do not imply current-stage work.
- A faster implementation can be correct while old tests fail, if those tests
  were proving the wrong contract.

For Stage 13 specifically, the corrected contract is:

- static-rasterized eligible pages may merge into dynamic
- dynamic-only invalidation can leave Stage 13 with no work
- static-invalidated pages must not contribute stale static depth
- stable late frames are allowed to produce zero Stage 13 work

## 6. Remaining Gap

This playbook records one successful hot-path optimization and the method that
produced it. It does not close overall Phase L-c or Phase L-d.

What remains open:

- no full Phase L-b scene matrix characterization yet
- no many-local-light or explicit benchmark scene profile recorded yet
- no global VSM default-budget selection yet
- late-frame RenderDoc captures on complex scenes can now carry very large but
  legal VSM state because the sticky physical shadow pool retains peak capacity
  and the derived HZB scales with it; on March 31, 2026 this produced captures
  in the `~350 MB` class that loaded poorly in RenderDoc while PIX eventually
  succeeded
- that capture-state bulk should be revisited as a later VSM optimization item,
  likely alongside pool-budget and shrink-policy work, instead of polluting
  unrelated investigations such as depth-prepass review
