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

## Historical Frame-10 Closeout Pack

These scripts are preserved only for historical comparison with the older
03-15 non-runtime closeout flow. They are not the active Phase 03 closure
surface:

- `Run-DeferredCoreFrame10Capture.ps1`
- `Analyze-DeferredCoreCapture.ps1`
- `Assert-DeferredCoreCaptureReport.ps1`
- `Verify-DeferredCoreCloseout.ps1`
