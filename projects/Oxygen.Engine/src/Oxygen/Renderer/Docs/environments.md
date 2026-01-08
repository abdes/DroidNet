
# Environment Systems Integration (Phase 1.5)

This document defines the detailed implementation plan for integrating scene-authored **Environment Systems** into the renderer under the **pure bindless Forward+** architecture.

## Goals

- Make environment data available to shaders in a **bindless-compliant** way.
- Establish a **single clear owner** responsible for uploading and keeping GPU environment static data in sync.
- Integrate the first environment features with the **smallest viable pass footprint**.
- Ensure all new shaders are declared in the compile-time shader catalog.

## Non-Goals (for this phase)

- Full physically-based SkyAtmosphere LUT precomputation pipeline.
- Full volumetric clouds raymarching pipeline.
- Full post-processing chain (tonemapping/bloom/auto-exposure) beyond plumbing values required by shading.
- SkyLight scene capture pipeline.

## Constraints / ABI

- Bindless-only shader resource access via the global descriptor heap.
- Root bindings (current ABI):
  -`SceneConstants` at root CBV `b1` and it contains `bindless_env_static_slot`.
  -`EnvironmentDynamicData` at root CBV `b3`.
- Environment “static” parameters are consumed in shaders as a structured payload (`EnvironmentStaticData`) addressed through `SceneConstants.bindless_env_static_slot`.
- Shaders must guard against invalid bindless indices using the engine-provided bindless sentinel (`kInvalidShaderVisibleIndex` / `kInvalidBindlessIndex`) and apply any required domain validation conventions.

### Enable/Disable Convention (Decision)

- **All environment features are gated by an explicit `enabled` field (0/1) in their GPU-facing structs.**
- Enums **must only represent behavior/mode**, never a “disabled” state.
- Authoring enable/disable uses the shared entry point `EnvironmentSystem::SetEnabled(...)` / `IsEnabled()`, and the renderer translates that into the GPU `enabled` field.

## Data Model Overview

### Authoring (Scene)

Environment systems are authored as components under `SceneEnvironment`:

- `SkyAtmosphere`
- `SkySphere`
- `SkyLight`
- `Fog`
- `VolumetricClouds`
- `PostProcessVolume`

The scene owns authored data; the renderer owns GPU resources derived from it.

### GPU-Facing Payloads

- **Hot / per-frame (root CBV b3)**: `EnvironmentDynamicData`
  -Clustered lighting wiring (cluster grid + index list) and Z-binning params.
	-Exposure (a single scalar used by forward shading).

- **Cold / per-frame snapshot (bindless SRV via SceneConstants)**: `EnvironmentStaticData`
  -Fog / sky / atmosphere / clouds / post-process settings.
  -Designed for shader consumption as a single structured blob.

## Plan

### Task 0 — Data Model Review, Alignment, and Update (CPU + HLSL)

**Status**: Completed

- Locked CPU/HLSL parity for `EnvironmentDynamicData` (root CBV `b3`) and `EnvironmentStaticData` (bindless SRV via `SceneConstants.bindless_env_static_slot`).
- Removed `white point` from the dynamic payload; dynamic exposure is a single scalar `EnvironmentDynamicData.exposure`.
- Enforced explicit `enabled` (0/1) gating for environment systems; enums represent behavior/mode only.
- Standardized authored exposure compensation naming to `exposure_compensation`.
- Updated struct packing and compile-time size/alignment checks; rebuilt and verified shader compilation.

### Task 1 — Single-Owner EnvironmentStaticData Upload + Sync API

**Status**: Completed

- Implemented a single renderer-owned `EnvironmentStaticDataManager` that builds a canonical CPU snapshot from `SceneEnvironment` and guarantees deterministic defaults with per-system `enabled = 0/1` gating.
- Allocated and maintained a GPU structured buffer containing one `EnvironmentStaticData` element per `frame::Slot`; shaders index it using `SceneConstants.frame_slot`.
- Used dirty-only tracking (no monotonic versioning): when the canonical snapshot changes, the manager refreshes the current slot’s element and ensures frames-in-flight safety via the per-slot layout.
- Published the bindless SRV slot into `SceneConstants.bindless_env_static_slot` through the renderer-only setter (`RendererTag`) each frame.

### Task 2 — HLSL Access Helpers + Validation Gates

**Objective**: standardize and enforce safe access patterns for environment data.

**Steps**

1. Add an HLSL helper to fetch `EnvironmentStaticData` from the bindless SRV slot:
	 - Must check invalid-slot sentinel.
	 - Must satisfy the project’s bindless domain validation conventions.
	 - **Mandatory**: environment-aware shaders must use this helper (no direct ad-hoc heap indexing).

2. Add a small helper for `EnvironmentDynamicData` usage as needed (already root CBV at `b3`).

**Deliverables**

- A single canonical HLSL helper used by all environment-aware shaders.

### Task 3 — Wire Exposure into EnvironmentDynamicData (MVP)

**Objective**: populate a single resolved exposure scalar named `exposure` in `EnvironmentDynamicData` so forward shading has deterministic exposure inputs.

**Steps**

1. Define MVP mapping from camera + authored post-process to dynamic exposure:
	 - Default: `exposure = 1.0`.
	 - If authored `exposure_compensation` exists, apply it multiplicatively.
	 - Do not introduce physical camera parameters in this phase.

2. Ensure the existing per-view dynamic data upload path writes these values.

**Exit Criteria**

- Forward shaders see stable exposure values (even if tonemapping is not implemented yet).

### Task 4 — First Render Integration (Choose Minimal Feature Set)

Pick the smallest set that produces visible correctness improvements without adding major new pipelines.

**Option A (recommended first): Forward fog (no new pass)**

- Implement exponential-height fog application in `ForwardMesh_PS` based on `EnvironmentStaticData.fog`.
- Keep volumetric fog as disabled/stub.

**Option B: Sky background pass (one new pass)**

- Add a simple sky pass (fullscreen triangle) that renders:
  -solid color sky, or
  -specified cubemap sky.
- Run before opaque forward shading.

**Exit Criteria**

- At least one environment system produces visible output and is fully fed by the unified environment data path.

### Task 5 — Shader Catalog Registration (Required for Any New Shaders)

**Objective**: ensure every new shader is declared in the compile-time shader catalog.

**Steps**

- For each new shader file, add a `ShaderFileSpec` entry with:
  -path
  -entry point(s)
  -minimal boolean permutations (prefer runtime branching on enums first)

Update the compile-time shader count assertion accordingly.

## Integration Notes

### Defaults Policy

This phase must define a single policy and apply it consistently.

- **Policy (recommended)**: Slot is normally valid; content encodes disabled
  - Always upload a 1-element buffer each frame.
  - Subsystems use `enabled = 0` to disable.
  - Shaders can skip the “slot exists?” branch in steady-state.
  - If a slot ever must be invalid (bootstrap/failure), it must be `kInvalidShaderVisibleIndex` (never `-1` or hardcoded).

### Ownership

The renderer owns:

- The environment static SRV buffer allocation and upload.
- Any derived GPU resources (sky cubemaps, LUTs, cloud textures) when those are implemented.

The scene owns:

- Authored parameters only (no GPU resources).

## Open Questions (to resolve during Task 0/1)

- What is the Phase 1.5 definition of done: fog only, sky only, or both?
- For SkyLight in Phase 1.5: support “specified cubemap” only and treat “captured scene” as disabled?
