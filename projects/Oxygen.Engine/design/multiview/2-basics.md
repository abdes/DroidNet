# Multi-View Rendering: Basic Types

## Application Layer Types (Active Objects & Data)

- **View**: View configuration (viewport, scissor, jitter, flags) - without camera matrices
- **ResolvedView**: Complete view with camera matrices - contains View + resolved camera transforms (view/proj matrices, frustum)
- **ViewMetadata**: Descriptive data defining view semantics (purpose tag, target surfaces, rendering flags, hidden flag)
- **ViewResolver**: Application-provided callback `ResolvedView(ViewId)` that resolves ViewId to ResolvedView with current camera transforms. **Note**: This is the appropriate place to apply sub-pixel jitter (TAA) to the projection matrix.

## FrameContext Types (Engine Layer - Registration & Frame Setup)

- **ViewId**: Strongly typed, unique identifier for a registered view
- **SurfaceId**: Strongly typed unique identifier for a presentation surface

## Renderer Layer Types (Per-View Rendering Domain)

- **ViewOutput**: Renderer-produced output from a completed view render (framebuffer with color/depth attachments)

## RenderContext Data (Renderer Layer - Per-View State Values)

**New multi-view data to add to RenderContext:**

- **current_view_id**: ViewId of the active view being rendered during multi-view iteration
- **current_view**: View snapshot (matrices, frustum, viewport) for the active view being rendered
- **current_view_retained_items**: Map of string keys to type-erased `shared_ptr<void>` for cross-pass resource communication within current view (Blackboard pattern)
- **view_outputs**: Map of ViewId to `shared_ptr<Framebuffer>` for completed view renders awaiting compositing

**Existing RenderContext data (unchanged):**

- **framebuffer**: Current render target for active view (set per-view during iteration)
- **scene_constants**: Scene-wide constant buffer (camera matrices updated per-view)
- **material_constants**: Material constant buffer (shared across views)
- **prepared_frame**: Immutable per-frame scene data (shared across views)
- **pass_enable_flags**: Pass enable/disable flags (shared across views)

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

**Document Status**: Draft
**Last Updated**: 2025-12-02
**Part of**: Multi-View Rendering Design Series
