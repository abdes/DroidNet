# CSM Tooling

This directory is the canonical home for **conventional-shadow-specific**
validation tools.

Ownership split:

- `tools/shadows/`
  - shared/common RenderDoc helpers used by both conventional shadows and VSM
  - current shared pass analyzers:
    - `AnalyzeRenderDocPassTiming.py`
    - `AnalyzeRenderDocPassFocus.py`
    - `renderdoc_ui_analysis.py`
- `tools/csm/`
  - conventional-shadow-specific analyzers, comparison scripts, and PowerShell
    workflows
  - imports shared RenderDoc helpers directly from `tools/shadows/`
- `tools/vsm/`
  - VSM-specific RenderDoc entrypoints and wrappers

Use `tools/csm/` for three things:

- building and running the unified conventional-shadow test target
- capturing a conventional-shadow baseline from `RenderScene`
- replaying existing RenderDoc captures for `CSM-2` through `CSM-5`

## Test Workflow

Build the unified test executable:

```powershell
cmake --build out/build-ninja --config Release --target Oxygen.Renderer.ConventionalShadows.Tests --parallel 8
```

Run the conventional-shadow suites:

```powershell
ctest --test-dir out/build-ninja -C Release -R "Oxygen\\.Renderer\\.ConventionalShadows\\.Tests" --output-on-failure
```

The executable contains separate GoogleTest suites for:

- draw-record publication
- raster contract coverage
- raster config coverage
- receiver analysis
- receiver mask
- caster culling

## Capture Workflow

Capture a fresh conventional-shadow baseline from `RenderScene`:

```powershell
.\tools\csm\Run-ConventionalShadowBaseline.ps1 `
  -Frame 350 `
  -RunFrames 420 `
  -Fps 50 `
  -Output out/build-ninja/analysis/csm/baseline_capture/release_frame350_baseline
```

That produces:

- `*.rdc`
- `*.benchmark.log`
- `*.stdout.log`
- `*.stderr.log`

`Run-ConventionalShadowBaseline.ps1` treats [demo_settings.json](/H:/projects/DroidNet/projects/Oxygen.Engine/tools/csm/demo_settings.json) as the canonical capture baseline, copies it into the live [demo_settings.json](/H:/projects/DroidNet/projects/Oxygen.Engine/Examples/RenderScene/demo_settings.json) before launch, then restores the original live file after the run.

## Replay Workflow

Replay an existing capture through the phase analyzers:

```powershell
.\tools\csm\Analyze-ConventionalShadowBaseline.ps1 `
  -CapturePath out/build-ninja/analysis/csm/baseline_capture/release_frame256_conventional_frame256.rdc
```

```powershell
.\tools\csm\Analyze-ConventionalShadowCsm2.ps1 `
  -CapturePath out/build-ninja/analysis/csm/csm2_validation/release_frame350_csm2_fps50_frame350.rdc
```

```powershell
.\tools\csm\Analyze-ConventionalShadowCsm3.ps1 `
  -CapturePath out/build-ninja/analysis/csm/csm3_validation/release_frame350_csm3_fps50_frame350.rdc
```

```powershell
.\tools\csm\Analyze-ConventionalShadowCsm4.ps1 `
  -CapturePath out/build-ninja/analysis/csm/csm4_validation/release_frame350_csm4_fps50_frame350.rdc
```

```powershell
.\tools\csm\Analyze-ConventionalShadowCsm5.ps1 `
  -CapturePath out/build-ninja/analysis/csm/csm5_validation/release_frame350_csm5_fps50_frame350.rdc
```

Each phase script:

- runs the required RenderDoc UI analyses
- writes phase reports next to the capture stem
- runs the matching Python comparison/report step when that phase requires it

## Shared RenderDoc helpers

Shared pass analyzers and helper modules live in `tools/shadows/`.
The CSM PowerShell workflows call those shared scripts directly.

## RenderDoc Python constraint

RenderDoc's embedded Python may execute entry scripts **without defining
`__file__`**.

Do not "simplify" the path-resolution logic back to unconditional
`Path(__file__)` access unless you have verified that the exact qrenderdoc
execution mode still provides it.

## Notes

- Run the RenderDoc-backed scripts on Windows with RenderDoc installed at
  `C:\Program Files\RenderDoc\qrenderdoc.exe`.
- The CSM PowerShell entrypoints are the supported interface. Do not launch the
  phase-specific Python analyzers directly unless you are debugging the tooling.
