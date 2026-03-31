# RenderScene

This example mounts a cooked `.pak` file, resolves a `SceneAsset` key (via the
PAK embedded browse index virtual paths or manual key entry), loads the cooked
scene through the engine `AssetLoader`, instantiates a runtime `scene::Scene`
hierarchy, and renders it using the standard single-view example pipeline.

The ImGui overlay is intentionally focused on PAK mounting and scene loading.

## Frame Capture CLI

`RenderScene` configures backend frame capture through the example capture CLI.

Supported flags:

- `--capture-provider off|renderdoc|pix`
- `--capture-load attached|search|path`
- `--capture-library <path-to-capture-runtime>`
- `--capture-output <capture-file-template>`
- `--capture-from-frame <zero-based-frame>`
- `--capture-frame-count <count>`

Examples:

- capture frame 30 with the installed RenderDoc runtime:
  `render-scene --capture-provider renderdoc --capture-load search --capture-from-frame 30 --capture-frame-count 1`
- load `renderdoc.dll` from an explicit path:
  `render-scene --capture-provider renderdoc --capture-load path --capture-library C:/Tools/RenderDoc/renderdoc.dll`

The runtime also exposes dev-console commands:

- `gfx.capture.status`
- `gfx.capture.frame`
- `gfx.capture.begin`
- `gfx.capture.end`
- `gfx.capture.discard`
- `gfx.capture.open_ui`

## RenderDoc K-a Validation Workflow

These commands assume the current working directory is the repository root:
`H:\projects\DroidNet\projects\Oxygen.Engine`.

Replay-safe capture helper:

- `Examples/RenderScene/Run-RenderSceneCapture.ps1`
- intentionally narrow: it always runs `RenderScene` with
  `-v=-1 --fps 0 --directional-shadows vsm --capture-provider renderdoc --capture-load search`
- only exposes:
  - `-Frame`
  - `-Count`
  - `-Output`

Example capture run:

```powershell
powershell -ExecutionPolicy Bypass -File `
  'H:\projects\DroidNet\projects\Oxygen.Engine\Examples\RenderScene\Run-RenderSceneCapture.ps1' `
  -Frame 30 `
  -Count 1 `
  -Output 'H:\projects\DroidNet\projects\Oxygen.Engine\out\build-ninja\analysis\k_a_baseline\k_a_vsm_frame30'
```

All RenderDoc analysis scripts in this folder are UI-only. Do not run them with
standalone Python replay. Use:

`qrenderdoc.exe --ui-python <script.py> <capture.rdc>`

The baseline K-a analyzer is:

- `Examples/RenderScene/AnalyzeRenderDocCapture.py`

It stays bounded and answers whether a replay-safe capture is valid automated
Phase K-a evidence. The baseline report records:

- whether the VSM shell markers are present from `VsmPageRequestGeneratorPass`
  through `VsmProjectionPass`
- whether the capture reaches `VsmProjectionPass`
- whether both Stage 15 mask resources are present
- whether `VsmProjectionPass` writes both Stage 15 mask resources
- which K-a gates remain manual or blocked

Example baseline run:

```powershell
$env:OXYGEN_RENDERDOC_REPORT_PATH = 'H:\projects\DroidNet\projects\Oxygen.Engine\out\build-ninja\analysis\k_a_baseline\k_a_vsm_40frames_frame10_analysis_report.txt'
& 'C:\Program Files\RenderDoc\qrenderdoc.exe' --ui-python `
  'H:\projects\DroidNet\projects\Oxygen.Engine\Examples\RenderScene\AnalyzeRenderDocCapture.py' `
  'H:\projects\DroidNet\projects\Oxygen.Engine\out\build-ninja\analysis\k_a_baseline\k_a_vsm_40frames_frame10.rdc'
```

Deep-dive scripts stay separate from the baseline:

- `Examples/RenderScene/AnalyzeRenderDocPassFocus.py`
  - inspect one pass with `OXYGEN_RENDERDOC_PASS_NAME`
- `Examples/RenderScene/AnalyzeRenderDocPassTiming.py`
  - measure GPU time and work events for one pass with
    `OXYGEN_RENDERDOC_PASS_NAME`
- `Examples/RenderScene/AnalyzeRenderDocEventFocus.py`
  - inspect one event with `OXYGEN_RENDERDOC_EVENT_ID`
- `Examples/RenderScene/AnalyzeRenderDocStage15Masks.py`
  - inspect Stage 15 mask presence, writer events, and any observed shader-side
    consumers

Example deep dives:

```powershell
$env:OXYGEN_RENDERDOC_PASS_NAME = 'VsmProjectionPass'
$env:OXYGEN_RENDERDOC_RESOURCE_NAME = 'VsmProjectionPass.'
$env:OXYGEN_RENDERDOC_REPORT_PATH = 'H:\projects\DroidNet\projects\Oxygen.Engine\out\build-ninja\analysis\k_a_baseline\k_a_vsm_40frames_frame10_pass_focus_report.txt'
& 'C:\Program Files\RenderDoc\qrenderdoc.exe' --ui-python `
  'H:\projects\DroidNet\projects\Oxygen.Engine\Examples\RenderScene\AnalyzeRenderDocPassFocus.py' `
  'H:\projects\DroidNet\projects\Oxygen.Engine\out\build-ninja\analysis\k_a_baseline\k_a_vsm_40frames_frame10.rdc'

$env:OXYGEN_RENDERDOC_PASS_NAME = 'VsmStaticDynamicMergePass'
$env:OXYGEN_RENDERDOC_REPORT_PATH = 'H:\projects\DroidNet\projects\Oxygen.Engine\out\build-ninja\analysis\merge_timing_post_selective\late_frame35.timing.txt'
& 'C:\Program Files\RenderDoc\qrenderdoc.exe' --ui-python `
  'H:\projects\DroidNet\projects\Oxygen.Engine\Examples\RenderScene\AnalyzeRenderDocPassTiming.py' `
  'H:\projects\DroidNet\projects\Oxygen.Engine\out\build-ninja\analysis\merge_timing_post_selective\late_frame35.rdc'

$env:OXYGEN_RENDERDOC_EVENT_ID = '14967'
$env:OXYGEN_RENDERDOC_RESOURCE_NAME = 'VsmProjectionPass.'
$env:OXYGEN_RENDERDOC_REPORT_PATH = 'H:\projects\DroidNet\projects\Oxygen.Engine\out\build-ninja\analysis\k_a_baseline\k_a_vsm_40frames_frame10_event_focus_report.txt'
& 'C:\Program Files\RenderDoc\qrenderdoc.exe' --ui-python `
  'H:\projects\DroidNet\projects\Oxygen.Engine\Examples\RenderScene\AnalyzeRenderDocEventFocus.py' `
  'H:\projects\DroidNet\projects\Oxygen.Engine\out\build-ninja\analysis\k_a_baseline\k_a_vsm_40frames_frame10.rdc'

$env:OXYGEN_RENDERDOC_REPORT_PATH = 'H:\projects\DroidNet\projects\Oxygen.Engine\out\build-ninja\analysis\k_a_baseline\k_a_vsm_40frames_frame10_stage15_masks_report.txt'
& 'C:\Program Files\RenderDoc\qrenderdoc.exe' --ui-python `
  'H:\projects\DroidNet\projects\Oxygen.Engine\Examples\RenderScene\AnalyzeRenderDocStage15Masks.py' `
  'H:\projects\DroidNet\projects\Oxygen.Engine\out\build-ninja\analysis\k_a_baseline\k_a_vsm_40frames_frame10.rdc'
```

Automated versus manual K-a evidence:

- Automated from replay-safe capture:
  - shell reaches `VsmProjectionPass`
  - both Stage 15 mask textures exist in the capture
  - `VsmProjectionPass` writes both Stage 15 mask textures
- Still manual or external:
  - `Virtual Shadow Mask` visual sign-off in a live engine run
  - stabilization of the live run for that manual checkpoint
  - re-enabled analytic bridge GPU gate execution

## RenderDoc Performance Workflow

For VSM performance investigations, keep the baseline analyzer bounded and use
the narrow helper plus the timing/pass deep dives instead.

Working rules:

- capture replay-safe late frames, not early warm-up frames
- time one pass at a time with `AnalyzeRenderDocPassTiming.py`
- inspect the same pass with `AnalyzeRenderDocPassFocus.py` when the timing
  alone does not explain the cost
- keep each script focused on one question and route shared UI/capture/report
  behavior through `renderdoc_ui_analysis.py`

The Stage 13 optimization followed exactly that workflow:

1. capture late frames with `Run-RenderSceneCapture.ps1`
2. time `VsmStaticDynamicMergePass`
3. inspect the merge pass scope and event mix
4. redesign the workload shape
5. rerun the same capture-and-timing flow for before/after evidence

The longer write-up, including the UE5 reference model that guided that
optimization, lives in `design/VsmPerformanceOptimizationPlaybook.md`.

## Depth PrePass Measurement Workflow

For complex scenes, prefer `Release` captures, do not capture before frame 40,
and use `--directional-shadows conventional` unless the specific investigation
is about VSM interaction. Late-frame VSM captures can carry a very large but
legal shadow-pool/HZB state that makes RenderDoc load and replay much less
usable for depth-prepass work while adding noise unrelated to the pass being
reviewed. The generic RenderDoc timing and pass-focus scripts can measure
`DepthPrePass` directly once the capture is steady-state.

Important:

- do not assume frame 45 is always valid for every content set
- choose a scene-loaded late frame for the current content
- if the example scene loads slowly, lower `--fps` or run for many more total
  frames so the capture target and post-capture tail both occur after the scene
  is live
- on the current complex `RenderScene` content, a validated conventional-shadow
  depth-prepass capture was produced at frame 200 with `--fps 30`

Recommended command shape:

```powershell
H:\projects\DroidNet\projects\Oxygen.Engine\out\build-ninja\bin\Release\Oxygen.Examples.RenderScene.exe `
  -v=-1 `
  --frames 450 `
  --fps 30 `
  --directional-shadows conventional `
  --capture-provider renderdoc `
  --capture-load search `
  --capture-output H:\projects\DroidNet\projects\Oxygen.Engine\out\build-ninja\analysis\depth_prepass_review_release\release_frame200_conventional_fps30 `
  --capture-from-frame 200 `
  --capture-frame-count 1
```

Timing run:

```powershell
$env:OXYGEN_RENDERDOC_PASS_NAME = 'DepthPrePass'
$env:OXYGEN_RENDERDOC_REPORT_PATH = 'H:\projects\DroidNet\projects\Oxygen.Engine\out\build-ninja\analysis\depth_prepass_review_release\release_frame200_conventional_fps30.depth_timing.txt'
& 'C:\Program Files\RenderDoc\qrenderdoc.exe' --ui-python `
  'H:\projects\DroidNet\projects\Oxygen.Engine\Examples\RenderScene\AnalyzeRenderDocPassTiming.py' `
  'H:\projects\DroidNet\projects\Oxygen.Engine\out\build-ninja\analysis\depth_prepass_review_release\release_frame200_conventional_fps30_frame200.rdc'
```

Pass focus run:

```powershell
$env:OXYGEN_RENDERDOC_PASS_NAME = 'DepthPrePass'
$env:OXYGEN_RENDERDOC_REPORT_PATH = 'H:\projects\DroidNet\projects\Oxygen.Engine\out\build-ninja\analysis\depth_prepass_review_release\release_frame200_conventional_fps30.depth_focus.txt'
& 'C:\Program Files\RenderDoc\qrenderdoc.exe' --ui-python `
  'H:\projects\DroidNet\projects\Oxygen.Engine\Examples\RenderScene\AnalyzeRenderDocPassFocus.py' `
  'H:\projects\DroidNet\projects\Oxygen.Engine\out\build-ninja\analysis\depth_prepass_review_release\release_frame200_conventional_fps30_frame200.rdc'
```

Use the timing report for cost and event count, then use the pass-focus report
to inspect the draw/clear mix and bound resources inside the depth pass scope.

## Content Source Behavior (PAK vs Loose Cooked)

The demo supports loading scenes from multiple cooked sources (PAK files and
loose cooked roots). The underlying content loader can mount multiple sources
at once, and the same `SceneAsset` key may exist in more than one source (for
example, a PAK and a loose cooked root built from the same content).

**Demo behavior:** when you select a scene from a source list, the demo
re-mounts that specific source immediately before the load request. This keeps
asset resolution deterministic and avoids ambiguous keys when the same asset
exists in multiple mounts.

In practice, this means:

- Picking a scene from the PAK list re-mounts the selected PAK.
- Picking a scene from the loose cooked list re-mounts the selected cooked
  root (via its `container.index.bin`).

This is intentional to ensure the `AssetLoader` resolves the requested key
against the source the user selected, even if other sources are currently
mounted.

## GLB to PakGen YAML

The helper script [Examples/RenderScene/glb_to_pak_spec.py](Examples/RenderScene/glb_to_pak_spec.py)
can generate a PakGen YAML spec from a `.glb`.

To avoid extremely large YAML files (hex-encoding doubles size and is slow to
parse), the default mode writes binary payloads (vertex/index buffers and
textures) to a sibling folder and references them via `file:` entries.

Example:

`python Examples/RenderScene/glb_to_pak_spec.py Examples/RenderScene/WaterBottle.glb bin/Oxygen/WaterBottle.yaml`

This produces:

- `bin/Oxygen/WaterBottle.yaml`
- `bin/Oxygen/WaterBottle_payload/*` (e.g. `.bin` for buffers, `.rgba8` for textures)

If you explicitly want to embed everything into YAML (slow, for debugging
only), use:

`python Examples/RenderScene/glb_to_pak_spec.py --data-mode hex <in.glb> <out.yaml>`

## One-command GLB -> PAK

If you want a single command that:

- generates the YAML spec + payload files, and
- builds the `.pak`

use [Examples/RenderScene/make_pak.py](Examples/RenderScene/make_pak.py).

Run it from the `Examples/RenderScene` directory:

`python .\make_pak.py .\glb\Tree1.glb`

You can also pass an existing PakGen YAML spec:

`python .\make_pak.py .\pak\Tree1.yaml`

Outputs:

- `pak/Tree1.yaml`
- `pak/Tree1_payload/*`
- `pak/Tree1.pak`
- `pak/Tree1.manifest.json`
