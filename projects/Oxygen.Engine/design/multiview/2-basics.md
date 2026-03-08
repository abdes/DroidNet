# Multi-View Rendering: Basic Types

## Application Layer Types (Active Objects & Data)

- **View**: View configuration (viewport, scissor, jitter, flags) without camera matrices
- **ResolvedView**: Complete view with camera matrices and derived camera/frustum data
- **ViewMetadata**: Descriptive data defining view semantics (`name`, `purpose`, `is_scene_view`, `with_atmosphere`)
- **ViewResolver**: Application-provided callback `ResolvedView(const ViewContext&)` that resolves a registered view to a `ResolvedView` with current camera transforms. This is the right place to apply sub-pixel jitter (TAA) to the projection matrix.

## FrameContext Types (Engine Layer - Registration & Frame Setup)

- **ViewId**: Strongly typed, unique identifier for a registered view
- **SurfaceId**: Strongly typed unique identifier for a presentation surface

## Renderer Layer Types (Per-View Rendering Domain)

- **ViewOutput**: Per-view framebuffer state produced and/or tracked during rendering and later consumed by compositing/presentation

## RenderContext Data (Renderer Layer - Per-View State Values)

**Live multi-view data in `RenderContext`:**

- **ViewSpecific (struct)**: Inner struct containing the active per-view iteration state
  - `view_id`: `ViewId` of the active view being rendered
  - `resolved_view`: `observer_ptr<const ResolvedView>` to the resolved per-view snapshot
  - `prepared_frame`: `observer_ptr<const PreparedSceneFrame>` to per-view scene-prep results
  - `atmo_lut_manager`: `observer_ptr<SkyAtmosphereLutManager>` for view-local atmosphere LUT state
- **current_view**: Single `ViewSpecific` instance for the currently executing view
- **view_outputs**: Map of `ViewId` to `observer_ptr<Framebuffer>` for completed per-view outputs captured during the frame

**Other live renderer-owned fields used by passes:**

- **pass_target**: Current render target for the active pass/view
- **view_constants**: Root-CBV buffer containing `ViewConstants`
- **material_constants**: Optional root-CBV buffer used by passes that still require a dedicated material CBV
- **frame_slot** / **frame_sequence**: Frame lifecycle state used for transient allocations
- **pass_enable_flags**: Pass enable/disable flags shared across views

**Current architectural note**: `ViewConstants` is now the per-view root CBV and only carries view invariants plus the top-level `bindless_view_frame_bindings_slot`. System-owned data is routed through `ViewFrameBindings` and then into contracts such as `DrawFrameBindings`, `LightingFrameBindings`, and `EnvironmentFrameBindings`. This document previously referred to `scene_constants`; that is stale.

**Design Rationale**: The application/frame system owns view registration and resolved-view lifetime for the frame. The renderer keeps non-owning pointers in `current_view`, and keeps renderer-owned GPU bindings (`view_constants`, `pass_target`, outputs) at the top level. Grouping the view-local state inside `ViewSpecific` keeps multi-view iteration explicit and avoids reintroducing a misleading single-view top-level `prepared_frame`.

## RenderGraph and Pass Config (Application & Renderer Layers)

### Layer 1: Pass Configuration Structs (Setup Time)

- Pass config structs (e.g., `DepthPrePassConfig`) contain view-independent or per-view pass setup data
- Created by application during render graph construction
- May be shared across all views OR created per-view depending on requirements
- Examples: texture resources, debug names, pass-specific parameters

### Layer 2: Runtime View Awareness (Execution Time)

- Render graph queries `current_view` from RenderContext during execution
- Enables conditional pass execution based on active view
- **View-Independent Passes**: Passes can be configured to execute only on the first view (e.g., `if (ctx.current_view_index > 0 && pass.is_view_independent) continue;`)
- Aligns with PRINCIPLE-04: "Applications can query current view identity to customize behavior per-view if needed"

### Pattern Example

- Application creates pass configs (shared or per-view) during setup
- Render graph passes query `RenderContext.current_view` at runtime
- Conditional logic selects appropriate config or conditionally executes passes
- All within existing RenderPass coroutine model (PrepareResources/Execute)

---

## Implementation status

| Type | Layer | Status | Location | Notes |
| ---- | ----- | ------ | -------- | ----- |
| **View** | Application | ✅ Implemented | `src/Oxygen/Core/Types/View.h` | Lightweight view configuration (no matrices); viewport, scissor, jitter |
| **ResolvedView** | Application | ✅ Implemented | `src/Oxygen/Core/Types/ResolvedView.h` | Immutable view snapshot with matrices, frustum and derived data |
| **ViewMetadata** | Application | ✅ Implemented | `src/Oxygen/Core/FrameContext.h` | Live fields are `name`, `purpose`, `is_scene_view`, and `with_atmosphere` |
| **ViewResolver** | Application | ✅ Implemented | `src/Oxygen/Core/Types/ViewResolver.h` | Alias `using ViewResolver = std::function<ResolvedView(const ViewContext&)>` |
| **ViewId** | FrameContext | ✅ Implemented | `src/Oxygen/Core/Types/View.h` | Strongly typed using NamedType pattern |
| **SurfaceId** | FrameContext | ✅ Implemented | `src/Oxygen/Core/FrameContext.h` | Strongly typed using NamedType pattern |
| **ViewOutput** | Renderer / FrameContext | Partial | `src/Oxygen/Core/FrameContext.h`, `src/Oxygen/Renderer/RenderContext.h` | `ViewContext` now carries `render_target` and `composite_source`; renderer also tracks per-view outputs in `RenderContext::view_outputs` during execution |
| **ViewSpecific struct** | RenderContext | ✅ Implemented | `src/Oxygen/Renderer/RenderContext.h` | Live per-view state: `view_id`, `resolved_view`, `prepared_frame`, `atmo_lut_manager` |
| **current_view** | RenderContext | ✅ Implemented | `src/Oxygen/Renderer/RenderContext.h` | Active view iteration state for the currently executing view |
| **view_outputs** | RenderContext | ✅ Implemented | `src/Oxygen/Renderer/RenderContext.h` | Renderer-owned map of `ViewId` to completed per-view framebuffers |

---

**Document Status**: Updated to current renderer contracts
**Last Updated**: 2026-03-08
**Part of**: Multi-View Rendering Design Series
