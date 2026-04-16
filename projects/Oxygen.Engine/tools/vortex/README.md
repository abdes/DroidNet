# Vortex RenderDoc Toolkit

This directory contains Vortex-specific capture analysis and probe scripts.

## Durable VortexBasic Validation Path

- `Run-VortexBasicRuntimeValidation.ps1`
  - supported one-command VortexBasic validation entrypoint
  - builds `oxygen-vortex`, `oxygen-graphics-direct3d12`, and
    `oxygen-examples-vortexbasic` in the standard `out/build-ninja` tree
  - launches VortexBasic once with RenderDoc capture enabled
  - discovers the newly produced capture and feeds it into the existing proof
    wrapper

- `Verify-VortexBasicRuntimeProof.ps1`
  - lower-level VortexBasic-only wrapper for existing capture + runtime log
    validation
  - runs both:
    - `AnalyzeRenderDocVortexBasicCapture.py` for structural/action-count checks
    - `AnalyzeRenderDocVortexBasicProducts.py` for product-correctness checks
  - then gates the combined result in `Assert-VortexBasicRuntimeProof.ps1`

- `AnalyzeRenderDocVortexBasicCapture.py`
  - structural analyzer for the current VortexBasic scene/runtime shape
  - verifies Stage 3 / 9 / 12 / compositing scopes and action counts

- `AnalyzeRenderDocVortexBasicProducts.py`
  - product analyzer for the current VortexBasic scene/runtime shape
  - verifies that Stage 3, Stage 9, point/spot/directional Stage 12
    SceneColor accumulation, and final present produce the expected
    non-broken outputs

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
