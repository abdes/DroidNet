
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

**Objective**: ensure every environment system has a well-defined, minimal, and consistent set of GPU-facing properties, with strict CPU/HLSL parity.

**Actions (execute in order)**

1. Locate the authoritative struct definitions.
	 - Find the CPU definitions of `EnvironmentDynamicData` and `EnvironmentStaticData`.
	 - Find the HLSL definitions of the same structs (or the closest equivalents) in `*.hlsli`.
	 - Record the exact file paths and struct names.
	 - Confirm current ABI bindings match this doc: root CBV `b3` for `EnvironmentDynamicData`, bindless SRV slot for `EnvironmentStaticData` via `SceneConstants.bindless_env_static_slot`.

2. Build a CPU/HLSL parity table for each struct.
	 - For every field: name, type, element count, and semantic meaning.
	 - For every enum: list all values and confirm CPU == HLSL numeric values.
	 - For packing: document alignment requirements and any padding expectations.
	 - Output: a short “parity checklist” that can be verified mechanically (see Deliverables).

3. Decide the Phase 1.5 minimal field set and which payload owns it.
	 - For each environment system (`Fog`, `SkySphere`, `SkyLight`, `SkyAtmosphere`, `VolumetricClouds`, `PostProcessVolume`):
		 - Decide the minimal GPU-facing fields required for Phase 1.5.
		 - Assign each field to exactly one payload: static (`EnvironmentStaticData`) or dynamic (`EnvironmentDynamicData`).
		 - Specify units and coordinate space for each numeric field.
		 - Mark any renderer-derived fields explicitly (bindless indices, LUT slots, cubemap indices) and define their sentinel values.

4. Enforce the enable/disable convention consistently.
	 - Ensure every GPU-facing per-system struct has an explicit `enabled` field (0/1).
	 - Ensure enums represent only behavior/mode (no “Disabled” enum values).
	 - Define deterministic defaults for disabled systems (including `enabled = 0`).

5. Normalize invalid-handle behavior.
	 - Ensure all bindless handles/slots use engine-provided sentinels (`kInvalidShaderVisibleIndex` / `kInvalidBindlessIndex`).
	 - Remove any hard-coded invalid values (e.g. `-1`).
	 - Ensure shader-side access can guard safely against invalid indices.

6. Apply the changes to CPU and HLSL.
	 - Update CPU structs/enums to match the agreed model.
	 - Update HLSL structs/enums to match exactly.
	 - Remove or rename legacy fields that cause ambiguity/duplication.

7. Verify and lock parity.
	 - Add/confirm compile-time checks on the CPU side (size and alignment at minimum; offsets where practical).
	 - Recompile shaders and confirm there are no layout/packing regressions.
	 - Re-check the parity table and update it if any last-minute changes occurred.

8. Review and sign off.
	 - Confirm: minimal field set, naming/unit conventions, and the Exposure Contract (below).
	 - Confirm: which environment features are expected to be functional in Phase 1.5 vs. kept `enabled = 0`.

**Deliverables**

- Updated CPU structs (and any related authored→GPU translation notes).
- Updated HLSL `*.hlsli` models.
- A short “parity checklist” including: struct sizes/alignment, enum numeric values, required sentinel guards.

**Exit Criteria**

- CPU and shader side structures are consistent and reviewed.
- All features have a consistent `enabled` gating mechanism.

#### Exposure Contract (Decision)

This phase intentionally does **not** model cameras as physical cameras.

- **Camera exposure** is a single scalar (per view) with a reasonable default based on common practice.
- **Post-process exposure compensation** is an authored scalar named `exposure_compensation`.

To avoid duplication in shader inputs, forward shading consumes a single scalar:

- **Dynamic (per-frame):** `EnvironmentDynamicData.exposure`
  - Computed once per frame/view from the resolved camera exposure and the authored compensation.
  - Recommended convention: `exposure = camera_exposure * exposure_compensation`.
  - Recommended defaults: `camera_exposure = 1.0`, `exposure_compensation = 1.0`.

Policy:

- If exposure is globally disabled for the view, publish `exposure = 1.0`.

### Task 1 — Single-Owner EnvironmentStaticData Upload + Sync API

**Objective**: define and implement the mechanism that keeps the environment static SRV in sync between CPU and GPU, with a clear single owner for upload responsibility and a clean API for multiple setters.

#### Design Requirements

- **Single owner**: exactly one component is responsible for:
  -Maintaining the canonical CPU-side `EnvironmentStaticData` snapshot.
  -Allocating/uploading the GPU structured buffer.
  -Publishing the bindless SRV slot into `SceneConstants.bindless_env_static_slot`.

- **Multiple setters**: multiple systems and/or codepaths can contribute values (Fog/SkySphere/SkyLight/PostProcess), but they do so through a single aggregator API.

- **Determinism**:
	 -If the scene has no environment (or a system is absent/disabled), publish a snapshot with `enabled = 0` for that system and deterministic defaults for the rest.
  -No uninitialized shader reads.

- **Bindless compliance**: invalid slot sentinel checks and any domain validation rules must be satisfiable.

#### Proposed Implementation

1. Introduce a renderer-internal aggregator/uploader (name suggestion):
	 - `EnvironmentStaticDataManager` (or `EnvironmentStaticUploader`)

2. Responsibilities:
	 - Own `EnvironmentStaticData cpu_snapshot_`.
	 - Own a “dirty” state and versioning, e.g. `MonotonicVersion` or similar.
	 - Provide setter methods used by translation code:
		 - `SetFog(const Fog&)`
		 - `SetSkySphere(const SkySphere&)`
		 - `SetSkyLight(const SkyLight&, /*optional derived slots*/)`
		 - `SetSkyAtmosphere(const SkyAtmosphere&)`
		 - `SetVolumetricClouds(const VolumetricClouds&)`
		 - `SetPostProcess(const PostProcessVolume&)`
	 - Provide `SetDefaults()` that writes deterministic defaults and sets `enabled = 0` for missing/disabled systems.

3. Upload strategy:
	 - Allocate a structured buffer sized for exactly 1 element of `EnvironmentStaticData`.
	 - Use the existing per-frame transient upload infrastructure (recommended): `TransientStructuredBuffer` with `stride = sizeof(EnvironmentStaticData)`.
	 - Store the returned `ShaderVisibleIndex` as the bindless SRV slot.

4. Sync point / wiring:
	 - During frame preparation (after scene prep has access to the scene and before `SceneConstants` snapshots are written per view), call:
		 - `env_static_manager_->BuildFromSceneEnvironment(scene.GetEnvironment())`
		 - `env_static_manager_->UploadIfDirty(frame_sequence, frame_slot)`
		 - `scene_const_cpu_.SetBindlessEnvironmentStaticSlot(slot, SceneConstants::kRenderer)`
	 - Ensure the slot is present in the per-view snapshot written by `SceneConstantsManager`.

5. API ergonomics:
	 - Prefer “build from scene” as the only public entry point from `Renderer` (one call).
	 - Keep individual `Set*` methods either:
		 - private (used internally by `BuildFromSceneEnvironment`), or
		 - public but scoped to a small translation layer so there is still one authority.

**Deliverables**

- `EnvironmentStaticDataManager` implementation.
- A renderer integration point that sets `SceneConstants.bindless_env_static_slot` every frame.
- A clear policy for when `bindless_env_static_slot` may be invalid; when invalid is used, it must be `kInvalidShaderVisibleIndex` (never `-1` or a hardcoded literal).

**Exit Criteria**

- Shaders can safely read environment static data (or safely detect absence) every frame.
- There is one obvious owner for upload and slot publication.

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
