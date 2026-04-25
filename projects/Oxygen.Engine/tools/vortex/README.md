# Vortex RenderDoc Toolkit

This directory contains Vortex-specific capture analysis and probe scripts.

## Durable VortexBasic Validation Path

- `Run-VortexBasicRuntimeValidation.ps1`
  - supported one-command VortexBasic validation entrypoint
  - builds `oxygen-vortex`, `oxygen-graphics-direct3d12`, and
    `oxygen-examples-vortexbasic` in the standard `out/build-ninja` tree
  - runs a debugger-backed no-capture audit under `cdb.exe` with the D3D12
    debug layer enabled before any RenderDoc capture is attempted
  - launches VortexBasic once with RenderDoc capture enabled
  - discovers the newly produced capture and feeds it into the existing proof
    wrapper

- `Assert-VortexBasicDebugLayerAudit.ps1`
  - asserts the debugger-backed no-capture D3D12 audit from the `cdb.exe`
    transcript
  - fails on breakpoint exceptions, D3D12/DXGI errors, or untriaged warnings
  - explicitly documents the shutdown `DXGI WARNING: Live IDXGIFactory ...`
    line as normal and accepted for this audit surface

- `Verify-VortexBasicRuntimeProof.ps1`
  - lower-level VortexBasic-only wrapper for existing capture + runtime log
    validation
  - now also requires the debugger-backed audit report from the no-capture run
  - runs both:
    - `AnalyzeRenderDocVortexBasicCapture.py` for structural/action-count checks
    - `AnalyzeRenderDocVortexBasicProducts.py` for product-correctness checks
  - then gates the combined result in `Assert-VortexBasicRuntimeProof.ps1`
  - the blocker-closing `04-08` proof also requires:
    - `stage15_sky_scene_color_changed=true`
    - `stage15_atmosphere_scene_color_changed=true`
    - `stage15_fog_scene_color_changed=true`

- `AnalyzeRenderDocVortexBasicCapture.py`
  - structural analyzer for the current VortexBasic scene/runtime shape
  - verifies Stage 3 / 9 / 12 / 15 / compositing scopes and action counts
  - `04-08` requires:
    - `stage15_sky_draw_count_match=true`
    - `stage15_atmosphere_draw_count_match=true`
    - `stage15_fog_draw_count_match=true`

- `AnalyzeRenderDocVortexBasicProducts.py`
  - product analyzer for the current VortexBasic scene/runtime shape
  - verifies that Stage 3, Stage 9, point/spot/directional Stage 12
    SceneColor accumulation, Stage 15 sky/atmosphere/fog `SceneColor` deltas,
    and final present produce the expected non-broken outputs

## Current Local-Light Gate

The durable wrapper currently treats point/spot local lights as:

- structurally gated by `AnalyzeRenderDocVortexBasicCapture.py`
  - scope counts
  - draw counts
  - zero stencil-clear counts for the current one-pass local-light path
- product-gated in `AnalyzeRenderDocVortexBasicProducts.py`
  - `stage12_spot_scene_color_nonzero=true`
  - `stage12_point_scene_color_nonzero=true`

For the current VortexBasic scene, point and spot now use the truthful
one-pass bounded-volume Stage 12 path:

- one local-light draw per point/spot light
- zero stencil clears for those lights
- nonzero Stage 12 point/spot `SceneColor` RGB is part of the durable gate

## Stage 15 Blocker-Closing Gate

The `04-08` validator closes the environment blocker only when Stage 15 is
proven to be real instead of merely live:

- structural gate in `AnalyzeRenderDocVortexBasicCapture.py`
  - one Stage 15 scope each for sky / atmosphere / fog
  - one draw each for sky / atmosphere / fog
  - ordered Stage 15 execution between Stage 12 and compositing
- product gate in `AnalyzeRenderDocVortexBasicProducts.py`
  - `stage15_sky_scene_color_changed=true`
  - `stage15_atmosphere_scene_color_changed=true`
  - `stage15_fog_scene_color_changed=true`

If any Stage 15 pass disappears or stops changing `SceneColor`, the validator
must fail.

## Current D3D12 Debug-Layer Gate

The one-command validator now owns a distinct debugger-backed audit stage before
RenderDoc capture:

- VortexBasic is launched once under `cdb.exe` with `--capture-provider off`
- the audit fails on:
  - breakpoint exceptions
  - `D3D12 ERROR:` / `D3D12 CORRUPTION:`
  - `DXGI ERROR:`
  - any `D3D12 WARNING:` / `DXGI WARNING:` that is not explicitly triaged
- the shutdown `DXGI WARNING: Live IDXGIFactory ...` line is documented as
  normal and accepted for this audit surface

This keeps the debugger-only D3D12 validation surface real instead of pretending
that the RenderDoc capture run can own it.

## Auxiliary Probe Scripts

These scripts are intentionally preserved because they were useful for live
forensics during the Phase 03 runtime bring-up. They are not the primary
approval path, but they remain valuable microscope tools:

- `DumpRenderDocActions.py`
- `InspectRenderDocApi.py`
- `ProbeRenderDocConstantBlocks.py`
- `ProbeRenderDocDrawAction.py`
- `ProbeRenderDocPixelHistory.py`
- `ProbeRenderDocStage3DepthHistory.py`
- `ProbeRenderDocStage9ColorHistory.py`
- `ProbeRenderDocStage9State.py`
- `ProbeRenderDocTextureSampling.py`

Use `Run-VortexBasicRuntimeValidation.ps1` for the normal end-to-end validation
flow. Use `Verify-VortexBasicRuntimeProof.ps1` only when you already have a
capture and runtime log that need replay. Use the probes when a bug needs
focused inspection that the durable analyzers do not already explain.

The normal VortexBasic validation combines runtime logs, action-scope counts,
and products analysis. Capture-only product fields that RenderDoc cannot
observe reliably in this path, such as bindless SRV consumption, cached LUT
dispatches, and zero-delta Stage-15 atmosphere/local-fog samples, are emitted
with `diagnostic_products_` prefixes in the final validation report.

## Focused Volumetric Fog Proof

- `Verify-VortexBasicVolumetricProof.ps1`
  - focused VTX-M04D.4 wrapper for an existing VortexBasic capture and runtime
    log
  - runs `AnalyzeRenderDocVortexBasicVolumetric.py`
  - asserts runtime `volumetric_fog_executed=true` and
    `integrated_light_scattering_valid=true`
  - asserts the capture contains one `Vortex.Stage14.VolumetricFog` dispatch,
    one later `Vortex.Stage15.Fog` draw, and a named
    `Vortex.Environment.IntegratedLightScattering` resource bound as the
    Stage-14 compute UAV
  - asserts the Stage-15 fog draw has a captured 672-byte
    `EnvironmentStaticData` payload with a valid
    `volumetric_fog.integrated_light_scattering_srv`, enabled/valid
    volumetric flags, nonzero grid dimensions, and valid log-depth grid
    parameters
  - samples the captured `Vortex.Environment.IntegratedLightScattering` 3D
    resource on representative z slices and requires nonzero RGB with
    transmittance alpha in range

The broader `Run-VortexBasicRuntimeValidation.ps1` wrapper now reflects the
VTX-M04D.4 local-fog ownership split: Stage 14 local-fog tiled culling still
runs, local fog is injected into volumetric fog, and the old Stage 15
local-fog compose draw is expected to be absent for the volumetric proof path.
`Oxygen.Examples.VortexBasic.exe` also exposes narrow proof controls for
term-variant captures: `--volumetric-local-fog false` disables local-fog
injection into the volumetric product, and
`--sky-light-volumetric-scattering 0` disables the SkyLight volumetric
ambient term while preserving the rest of the validation scene.
`--volumetric-directional-shadows false` disables only the directional
shadow-map visibility multiplier inside volumetric fog so paired captures can
prove the shadow term separately from surface projected shadows. Focused proof
runs can amplify or isolate the additive terms with
`--local-fog-volumetric-max-density`, `--local-fog-emissive-scale`, and
`--volumetric-fog-emissive-scale`; the normal defaults remain unchanged.
`Compare-VortexBasicVolumetricTermReports.ps1` compares paired reports and
requires both the runtime term gates and the expected sampled
integrated-scattering delta: positive enabled-minus-disabled increase for
additive terms, and positive disabled-minus-enabled decrease for the
`directional-shadow` term.

Current limitation: RenderDoc does not expose the bindless Stage-15 pixel
shader SRV read for `IntegratedLightScattering` through this analyzer, so the
focused validation report leaves
`integrated_light_scattering_consumed_by_fog=false` as a diagnostic instead of
treating it as a closure gate. The accepted focused proof for Stage-15 wiring
is the captured static payload plus the fog shader contract that samples
`ResourceDescriptorHeap[volumetric_fog.integrated_light_scattering_srv]`.

## Historical Frame-10 Closeout Pack

These scripts are preserved only for historical comparison with the older
03-15 non-runtime closeout flow. They are not the active Phase 03 closure
surface:

- `Run-DeferredCoreFrame10Capture.ps1`
- `Analyze-DeferredCoreCapture.ps1`
- `Assert-DeferredCoreCaptureReport.ps1`
- `Verify-DeferredCoreCloseout.ps1`

## Durable Async Validation Path

- `Capture-AsyncLegacyReference.ps1`
  - captures the legacy/reference Async baseline into a dedicated `reference/`
    artifact root
  - produces `reference_frame10.png`, `reference_depth.png`,
    `reference_renderdoc.rdc`, and `reference_behaviors.md`
  - builds and runs the Async example once with RenderDoc capture enabled
  - uses `Verify-AsyncRuntimeProof.ps1 -InitializeBaselineArtifacts` to
    extract the reference frame and depth images from the capture

- `Run-AsyncRuntimeValidation.ps1`
  - one-command Async validation entrypoint
  - builds `oxygen-vortex`, `oxygen-graphics-direct3d12`, and
    `oxygen-examples-async` in the standard `out/build-ninja` tree
  - captures the current Vortex Async runtime into a `current/` artifact root
  - when `-ReferenceRoot` is provided, compares the current capture against
    the external reference baseline instead of self-initializing from the
    current run
  - invokes `Verify-AsyncRuntimeProof.ps1` with `-ReferenceRoot` for
    reference-based parity

- `Verify-AsyncRuntimeProof.ps1`
  - lower-level Async capture validation wrapper
  - runs both:
    - `AnalyzeRenderDocAsyncCapture.py` for structural/stage-ordering checks
    - `AnalyzeRenderDocAsyncProducts.py` for product-correctness checks
  - gates the combined result in `Assert-AsyncRuntimeProof.ps1`
  - when `-ReferenceRoot` is provided, loads baseline frame/depth from the
    external reference directory for PSNR/depth parity comparison

- `AnalyzeRenderDocAsyncCapture.py`
  - structural analyzer for the Async runtime capture
  - verifies stage scopes (3, 8, 12 directional/spot, 15 sky/atmosphere/fog,
    22 tonemap), ImGui overlay, compositing, and stage ordering

- `AnalyzeRenderDocAsyncProducts.py`
  - product analyzer for the Async runtime capture
  - verifies nonzero depth, shadow depth, direct-light SceneColor (directional
    and spot), sky/atmosphere/fog SceneColor deltas, tonemap output, exposure
    clipping, overlay composition, and final present
  - `stage12_directional_scene_color_nonzero` and
    `stage12_spot_scene_color_nonzero` are blocking verdict keys

- `Assert-AsyncRuntimeProof.ps1`
  - final assertion gate for the Async proof
  - gates on all structural capture checks, all product checks including
    direct-light nonzero keys, PSNR threshold (40 dB), depth max error
    (0.001), and Stage 22 input-bundle source contract

### Reference-Based Parity Flow

```powershell
# 1. Capture the legacy/reference baseline
powershell -NoProfile -File tools/vortex/Capture-AsyncLegacyReference.ps1 `
  -Output build/artifacts/vortex/phase-4/async/reference

# 2. Run the current Vortex capture against the reference
powershell -NoProfile -File tools/vortex/Run-AsyncRuntimeValidation.ps1 `
  -Output build/artifacts/vortex/phase-4/async/current `
  -ReferenceRoot build/artifacts/vortex/phase-4/async/reference
```
