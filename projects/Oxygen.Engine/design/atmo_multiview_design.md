# Multi-View Atmosphere: Per-View LUTs, Sky Capture, and IBL

## Problem Statement
Atmosphere LUT generation is currently global. This couples physics LUTs and subsequent sky rendering across views, and allows non-physical settings to leak between views. We need per-view LUTs and per-view sky capture/IBL while preserving Oxygen’s mult-view render loop.

## Goals
- Make LUTs (Transmittance, MultiScat, SkyIrradiance, SkyView, CameraVolume) unique per view.
- Keep Atmosphere simulation exposure-agnostic without changing shader helpers.
- Make Sky Capture and IBL view-specific.
- Maintain mult-view execution as the first-class path.
- Avoid backward-compat shims or wrappers; upgrade existing code directly.

## Non-Goals
- Changing exposure code or shader helpers.
- Changing LUT algorithms, mappings, or quality.
- Adding new authoring parameters to the Scene environment.

## High-Level Architecture

### Current
- SkyAtmosphereLutManager is a single global instance.
- EnvironmentStaticDataManager publishes a single EnvStatic SRV for all views.
- Sky capture and IBL are single global outputs.

### Target
- One SkyAtmosphereLutManager per view.
- One EnvironmentStaticData buffer + SRV per view.
- Sky capture is per view.
- IBL is per view.

## Key Design Decisions

### 1) Per-View State Lives With the View
Per-view resources are created and owned via view-keyed maps:
- Renderer maintains a per-view LUT manager.
- EnvironmentStaticDataManager maintains per-view EnvStatic buffers/SRVs.
- SkyCapturePass maintains per-view capture state.
- IblManager maintains per-view outputs and state.

This aligns with Oxygen’s mult-view render loop and avoids global leakage.

### 1.1) ViewId as Unordered-Map Key
- `ViewId` is already a `NamedType` with `Hashable` skill (`src/Oxygen/Core/Types/View.h`).
- No additional hash/equality wrapper is required for `std::unordered_map<ViewId, ...>`.

### 2) Scene Environment Is the Single Source of Truth
Atmosphere LUT generation is enabled only if:
- SceneEnvironment exists and
- SkyAtmosphere system is enabled.

No additional view flags are introduced.

### 3) SceneConstants Bind EnvStatic Per View
SceneConstants.bindless_env_static_slot becomes view-specific. Each view binds the EnvStatic SRV published for that view.

### 4) LUT Compute Pass Uses View’s LUT Manager
SkyAtmosphereLutComputePass uses the LUT manager associated with RenderContext.current_view.

## Data Flow Per View

### Layer A: Atmosphere Simulation
- Check if SkyAtmosphere exists and is enabled.
- Acquire or create SkyAtmosphereLutManager for the current view.
- Update LUT manager with:
  - Scene atmosphere params from EnvironmentStaticDataManager
  - Sun state from EnvironmentDynamicDataManager
- If dirty, run SkyAtmosphereLutComputePass for this view.
- Swap LUT buffers for this view.

### Layer B: Opaque Scene
- Bind per-view EnvStatic SRV.
- Render opaque scene using per-view LUTs.

### Layer C: Sky Rendering
- Bind per-view EnvStatic SRV.
- Render sky using per-view LUTs.
- Exposure can be applied in the sky pass as usual.

### Sky Capture
- If LUTs changed for this view or capture missing/dirty, run SkyCapture for this view.
- Publish captured cubemap slot into this view’s EnvStatic.

### IBL
- If capture changed or IBL flagged dirty, run IBL for this view.
- Publish IBL slots into this view’s EnvStatic.

## Core Type and API Changes

### Renderer
Add per-view LUT manager storage:
- std::unordered_map<ViewId, std::unique_ptr<internal::SkyAtmosphereLutManager>> per_view_atmo_luts;

Update per-view logic in RunScenePrep:
- Create/update LUT manager if atmosphere enabled.
- Update sun state on the LUT manager per view.

Update per-view execution in OnRender:
- Remove global ran_atmo_lut_compute_this_frame gating.
- For each view, run LUT compute if dirty.

### RenderContext
Add view-specific pointers:
- observer_ptr<internal::SkyAtmosphereLutManager> atmo_lut_manager; (nullable)
- observer_ptr<internal::EnvironmentStaticDataManager> env_static_manager; (existing)
- observer_ptr<internal::IblManager> ibl_manager; (existing, but must be used per view)

Populate in PrepareAndWireSceneConstantsForView.
- `atmo_lut_manager` is null for views without atmosphere; all atmosphere paths must early-out safely.

### SkyAtmosphereLutComputePass
Remove fixed config pointer to LUT manager:
- Read Context().current_view to find per-view LUT manager.
- No global manager usage.

### EnvironmentStaticDataManager
Refactor to per-view storage:
- std::unordered_map<ViewId, ViewState> where ViewState contains:
  - EnvironmentStaticData cpu_snapshot
  - std::shared_ptr<graphics::Buffer> buffer
  - ShaderVisibleIndex srv_index
  - std::array<uint64_t, frame::kFramesInFlight> slot_uploaded_id

API changes:
- UpdateIfNeeded(RendererTag, const RenderContext&, ViewId view_id)
- ShaderVisibleIndex GetSrvIndex(ViewId view_id) const

Populate LUT slots from view’s LUT manager:
- Use the per-view LUT manager when building atmosphere data.
- Publish slots only if that view’s LUTs are generated.

### SkyCapturePass
Make capture state per view:
- Map ViewId -> CaptureState { bool captured; uint64_t gen; ... }
- Use per-view EnvStatic SRV and per-view LUT slots.
- Mark dirty per view when that view’s LUTs regenerate.

### IBL
Make IBL outputs per view:
- Map ViewId -> IblState { irradiance slot, prefilter slot, generation }
- Run IBL per view when capture changes for that view.
- Publish per-view IBL slots into that view’s EnvStatic.
- Ownership remains in `IblManager` because it is the system-level owner of reusable IBL resources/state.

## Invalidation and Regeneration Rules

### LUTs
- Dirty if:
  - Atmosphere params differ
  - Sun elevation, enabled, or illuminance changes
- Dirty is per view.

### Sky Capture
- Dirty if:
  - Capture missing for view
  - LUT generation changed for view
  - Explicit capture request for view

### IBL
- Dirty if:
  - Capture generation changed for view
  - EnvStatic requested regeneration for view

## Resource Lifetime
- Per-view managers are created lazily at first view use.
- Per-view managers are not eagerly destroyed on view unregistration hooks.
- Resource reclamation is eviction-driven (non-use based) to preserve cache behavior and avoid churn.
- In validation, ensure no stale view IDs remain in maps past eviction windows.

## Eviction Policy (Non-Use Based)
Goal: reclaim per-view resources for views that are no longer rendered, without guessing.

Definitions:
- LastSeenFrameSeq: the last frame sequence in which the view executed a render graph.
- LastSeenFrameSlot: the last frame slot in which the view executed a render graph.
- ViewActive: a view is active if it appears in the render graph registry for the current frame.

Policy:
- Track LastSeenFrameSeq per view in Renderer when iterating views in OnRender.
- A view is eligible for eviction if it has not been active for N consecutive frame sequences.
- Default N: 120 frames (2 seconds at 60Hz). This is long enough to avoid churn on transient hitches.
- Eviction is deterministic: only evict if the view has not been part of any render graph in the last N frames.
- Use the single frame sequence counter from `FrameContext` (provided by `Renderer`) as the authoritative sequence source.

Eviction Actions (per view):
- Remove SkyAtmosphereLutManager entry.
- Remove EnvironmentStaticDataManager ViewState entry and release its buffer.
- Remove SkyCapturePass CaptureState entry.
- Remove IBL ViewState entry and release its textures/slots.

Placement:
- Perform eviction in Renderer::OnRender after the view loop, using current frame sequence.
- Use the render graph registry snapshot to define the active set for that frame.

Rationale:
- This is based on real non-use: the view did not participate in render graph execution.
- Avoids heuristic guesses and preserves correctness.

## Performance Considerations
- LUT compute can be significant per view.
- Ensure only views with atmosphere enabled run LUT compute.
- Keep capture and IBL regeneration per view to avoid unnecessary work.

## Validation Notes
- No new automated tests are included in this change set.
- Validation is done through runtime behavior checks during integration.

## Migration Steps
1. Introduce per-view LUT manager map in Renderer.
2. Refactor SkyAtmosphereLutComputePass to use view-specific manager.
3. Refactor EnvironmentStaticDataManager to per-view buffers.
4. Make SkyCapturePass per view.
5. Make IBL per view.
6. Wire per-view EnvStatic SRV into SceneConstants.
