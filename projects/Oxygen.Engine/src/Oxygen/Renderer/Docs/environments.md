
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

**Hydration Policy**: Following the engine's established pattern, the application is responsible for hydrating these components by resolving `data::AssetKey` references into `data::TextureAsset` pointers (via the `AssetLoader`) before the scene is submitted for rendering. The `Scene` components store these resolved asset pointers.

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

**Status**: Completed

- Created `EnvironmentHelpers.hlsli` as the canonical access path for environment data.
- Implemented `LoadEnvironmentStaticData` with mandatory bindless index validation and domain guards.
- Standardized `EnvironmentDynamicData` as a global root CBV (b3) and provided helpers for common environment queries.
- Refactored forward shaders to use these helpers, ensuring safe and consistent data consumption.

### Task 3 — Wire Exposure into EnvironmentDynamicData (MVP)

**Status**: Completed

- Implemented per-view exposure resolution in the `Renderer`, applying exposure compensation from `PostProcessVolume` ($2^{EV}$) and wiring the resolved scalar into the global dynamic CBV (b3) for consumption by forward shaders.

### Task 4 — Atmospheric Foundation (Sky + Fog Integration)

Implement a robust, extensible foundation that integrates sky rendering and distance-based fog into the unified data pipeline.

**4.0: Renderer Upload Infrastructure Fixes**

- **`UploadPlanner` Update**: Enhance `PlanTexture2D` and `PlanTexture3D` to support "Full Upload" defaults.
  - When `subresources` is empty, the planner must now generate regions for **all mip levels and all array slices** (faces) defined by the destination texture descriptor, instead of just the first mip.
  - This is a prerequisite for correctly uploading environmental cubemaps with full mip chains.

**4.1: Shared Atmosphere HLSL (`AtmosphereHelpers.hlsli`)**

- Define `GetAtmosphericFog(worldPos, cameraPos, fogParams)`: Returns a fog transmittance and inscattering color.
- Implement exponential-height fog (density, height falloff).
- Standardize the "Inscattering" color to be derived from the sky/sun state if `fog.use_sky_color` is enabled.

**4.2: Atmospheric Render Passes (`SkyPass`)**

- **Engine C++**: Implement `SkyPass.h` and `SkyPass.cpp` in `src/Oxygen/Renderer/Passes/`.
  - Inherit from `GraphicsRenderPass`.
  - Configure for **"Fullscreen Triangle"**: No input layout, no vertex buffers bound.
  - Pipeline State: `Depth Test: EQUAL`, `Depth Write: Disabled`, `Cull: None`.
- **Render Graph Integration**: Update `Examples/Common/RenderGraph.cpp` to include the new `SkyPass`.
  - Add to `RunPasses` sequence: DepthPrePass -> LightCulling -> **SkyPass** -> Opaque Shading.
- **Resource Wiring**:
  - Update `EnvironmentStaticDataManager` to resolve hydrated `data::TextureAsset` pointers from the scene into bindless SRV indices using the `TextureBinder`.
  - Ensure `EnvironmentStaticData.sky_cubemap_index` is correctly populated.
- **Shaders**: Implement `SkySphere_VS.hlsl` (vertex-less triangle) and `SkySphere_PS.hlsl`.
- **Exposure**: Apply `EnvironmentDynamicData.exposure` to ensure the sky is physically matched with scene lighting.

**4.3: Forward Integration**

- Call `ApplyAtmosphericFog` in `ForwardMesh_PS` and any other forward-shading passes (e.g., Unlit, Debug).
- Ensure fog is applied *after* lighting but *before* final exposure (or handled consistently with light-metering).

**Exit Criteria**

- A visible, exposure-correct sky background (cubemap or procedural).
- Analytical fog correctly attenuating objects based on world-space distance and height.
- All new shaders registered in the `ShaderCatalog`.

### Task 5 — Shader Catalog & Boilerplate Update

**Objective**: Formalize the new shaders and ensure the system is ready for production scaling.

**Steps**

- Register `SkySphere_VS` and `SkySphere_PS` in the compile-time `ShaderCatalog`.
- Create a reusable `FullscreenTriangle.hlsli` in `src/Oxygen/Graphics/Direct3D12/Shaders/Include/` to provide `GetFullscreenTrianglePos(uint vertexID)`.
- Ensure the `Graphics` layer's `CreatePipelineStateDesc` supports an empty `InputLayout` for vertex-less draws.
- Update the renderer's `PipelineLibrary` or pass-specific PSO creation to cache the new Sky PSOs.

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
- The mapping of `data::TextureAsset` to bindless SRV indices (via `TextureBinder`).
- Any derived GPU resources (sky cubemaps, LUTs, cloud textures) when those are implemented.

The scene owns:

- Authored parameters and hydrated CPU assets (`std::shared_ptr<data::TextureAsset>`).

## Open Questions (to resolve during Task 0/1)

- What is the Phase 1.5 definition of done: fog only, sky only, or both?
- For SkyLight in Phase 1.5: support “specified cubemap” only and treat “captured scene” as disabled?
