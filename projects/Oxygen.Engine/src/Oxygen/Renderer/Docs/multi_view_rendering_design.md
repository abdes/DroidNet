# Multi-View/Surface Rendering Design

> This document defines a clean, minimal, and authoritative approach for multi-view rendering in Oxygen. The `View` is the single source of truth for rendering and presentation. Breaking, clean API changes are preferred over backward-compatible shims.

---

## Implementation Status

### ✅ Phase 1: Core Architecture & Data Structures (COMPLETED)

**Completed Items:**
- ✅ Added `PhaseId::kCompositing` phase to engine lifecycle
- ✅ Defined `ViewId` as `Named Type<uint64_t>` with `Comparable`, `Hashable`, `Printable` skills
- ✅ Defined`ViewMetadata` struct with `tag` field
- ✅ Defined `ViewContext` struct (minimal: `name`, `surface`, `metadata`, `output`)
- ✅ Refactored `FrameContext` to use `std::unordered_map<ViewId, ViewContext>` for view storage
- ✅ Updated `FrameContext::AddView()` to return `ViewId` and accept `ViewContext`
- ✅ Added `FrameContext::SetViewOutput()` and `GetViewContext()` APIs
- ✅ Removed legacy `RenderableView` class (replaced with functional approach)
- ✅ Updated `Renderer` to be passive (removed view iteration loops)
- ✅ Updated modules (`EditorModule`, `Async` example) to drive rendering explicitly
- ✅ Modules now call `renderer.BuildFrame()` and mark surfaces presentable

**Key Architectural Decisions:**
1. **Removed `RenderableView` abstraction**: Originally ViewContext held a `std::shared_ptr<RenderableView>`. This was removed in favor of a cleaner separation where:
   - `CameraView` is no longer a polymorphic class
   - Modules own `CameraView` instances and resolve them explicitly
   - `ViewContext` stores only data (name, surface ref, metadata, output FB)

2. **Module-Driven Rendering**: The renderer is now fully passive:
   - Modules create views in `OnSceneMutation` and store `ViewId`s
   - Modules resolve views and call `renderer.BuildFrame(view, context)` in `OnCommandRecord`
   - Modules call `context.SetSurfacePresentable()` after rendering
   - No automatic view iteration in the renderer

3. **Simplified Phase 1 Scope**: ViewMetadata currently only contains `tag`. Extended fields (PresentPolicy, target_surfaces, viewport, flags) are deferred to Phase 2.

### 🔄 Phase 2: Renderer Passivation & Module-Driven Rendering (NEXT)

**Remaining Work:**
- [ ] Refactor `Renderer::BuildFrame()` to support multiple views per frame (currently overwrites state)
- [ ] Add `Renderer::PrepareView()` and `Renderer::RenderView()` APIs for explicit per-view control
- [ ] Update `RenderContext` to include `observer_ptr<const View> view` member
- [ ] Implement compositing logic in `OnCompositing` phase hook
- [ ] Extend `ViewMetadata` with:
  - `PresentPolicy` enum
  - `std::vector<SurfaceId> target_surfaces`
  - `ViewPort viewport` and `Scissors scissor`
  - `uint32_t flags` for HDR, MSAA, etc.

### 📋 Phase 3: Multi-View Support & Compositing (FUTURE)

**Planned Work:**
- [ ] Add `OnFrameGraphPerView` module hook
- [ ] Support parallel per-view culling and command recording
- [ ] Implement view ordering and composition logic
- [ ] Add multi-surface presentation support
- [ ] Implement render target pooling and descriptor reuse

---

## 1. Summary

- **Goal:** Render multiple, independent views per engine frame, each with explicit per-view rendering and presentation metadata. Supports editor layouts (perspective, ortho/top/left/right) and arbitrary surface mappings.
- **Constraint:** No legacy hacks, no duplicate authoritative state. A `View` (or tightly-coupled `ViewMetadata`) is authoritative.

## 2. Principles

1. **Single source of truth:** View ownership, ordering, and presentation policy reside with the `View` (or associated `ViewMetadata` in `FrameContext`, owned by the game/editor).
2. **Explicit contracts:** Renderer consumes complete, deterministic `FrameContext` state for the frame; no implicit guessing.
3. **Small types, no duplication:** Per-view descriptors are small (refs, indices, flags) and do not duplicate scene data.

---

## 3. Design Overview

### 3.1. Authoritative View Metadata

**Current Implementation (Phase 1):**

`FrameContext` maintains a single map keyed by `ViewId`, storing a consolidated `ViewContext`:

```cpp
// In Oxygen/Core/Types/View.h
using ViewIdTag = struct ViewIdTag;
using ViewId = NamedType<uint64_t, ViewIdTag, Comparable, Hashable, Printable>;

// In Oxygen/Core/FrameContext.h (oxygen::engine namespace)
using ViewId = oxygen::ViewId; // namespace alias

struct ViewMetadata {
  std::string tag; // e.g. "MainScene", "Minimap", "EditorViewport"
};

struct ViewContext {
  std::string name;
  std::variant<std::reference_wrapper<graphics::Surface>, std::string> surface;
  ViewMetadata metadata;
  std::shared_ptr<graphics::Framebuffer> output; // Set by module after rendering
};

// In FrameContext private members:
std::unordered_map<ViewId, ViewContext> views_;
```

**Contract:**

- **ViewId Generation:** `FrameContext::AddView` generates and returns a unique `ViewId` using atomic incrementation.
- **Mutable Window:** Views and metadata can be added/updated only during **PhaseSceneMutation**.
- **Freeze:** At `PhaseSnapshot`, `FrameContext::PublishSnapshots()` copies views into `GameStateSnapshot`.
- **Output Update:** The `output` field of `ViewContext` is set by modules during `PhaseCommandRecord` via `FrameContext::SetViewOutput()`.

**Future `ViewMetadata` fields (Phase 2+):**

| Field | Description | Status |
|---|---|---|
| `std::string tag` | Human-readable tag for discovery | ✅ Implemented |
| `PresentPolicy` | Presentation mode (e.g., `DirectToSurface`, `Hidden`) | 📋 Planned |
| `std::vector<SurfaceId> target_surfaces` | Surfaces to present to | 📋 Planned |
| `ViewPort viewport` | Normalized or pixel viewport, scissor | 📋 Planned |
| `uint32_t flags` | Flags for HDR, MSAA, etc. | 📋 Planned |

### 3.2. FrameContext Responsibilities

**Current Implementation:**
- **Storage:** Owns the authoritative set of `ViewContext`s in `std::unordered_map<ViewId, ViewContext> views_`.
- **ID Generation:** Generates unique `ViewId`s via atomic counter (scoped per FrameContext instance).
- **Lifecycle:** Enforces mutation windows (views can only be added before `PhaseSnapshot`).
- **Publishing:** Copies `ViewContext` objects into `GameStateSnapshot.views` during `PublishSnapshots()`.
- **Passive:** Does not dictate rendering order or drive rendering.
- **Output Management:** Provides `SetViewOutput(ViewId, Framebuffer)` to update the `output` field.

**APIs:**
```cpp
// Add a view (returns unique ViewId)
auto AddView(ViewContext view) noexcept -> ViewId;

// Set the output framebuffer after rendering
auto SetViewOutput(ViewId id, std::shared_ptr<graphics::Framebuffer> output) noexcept -> void;

// Get the full context for a view
auto GetViewContext(ViewId id) const -> const ViewContext&;

// Get all views (returns range of const ViewContext&)
auto GetViews() const noexcept; // returns transform_view
```

### 3.3. RenderContext / Per-View Render State

`RenderContext` remains the per-frame execution wrapper.

**Future Change (Phase 2):** Add `observer_ptr<const View> view` to `RenderContext`.
- Explicitly links the context to the view being rendered.
- Cleared on `Reset()`.

### 3.4. Renderer Execution Model (Passive)

**Current Implementation (Phase 1):**

The Renderer is **passive** and does not iterate views. Modules are responsible for:
1. Creating `CameraView` instances
2. Adding `ViewContext` to `FrameContext` (storing returned `ViewId`)
3. Resolving `CameraView` to get `View` snapshot
4. Calling `renderer.BuildFrame(view, context)`
5. Calling `renderer.ExecuteRenderGraph(...)` with render pass lambdas
6. Calling `context.SetViewOutput(viewId, framebuffer)`
7. Calling `context.SetSurfacePresentable(surfaceIndex, true)`

**Example (EditorModule):**
```cpp
// OnSceneMutation:
camera_view_ = std::make_shared<CameraView>(params, surface);
view_id_ = context.AddView(ViewContext {
  .name = "EditorView",
  .surface = *surface,
  .metadata = { .tag = "Surface_0x12345" }
});

// OnCommandRecord:
const auto view = camera_view_->Resolve();
renderer.BuildFrame(view, context);
co_await renderer.ExecuteRenderGraph([&](auto& ctx) {
  co_await runPasses(ctx, recorder);
}, render_context, context);
context.SetViewOutput(view_id_, framebuffer);
context.SetSurfacePresentable(surfaceIndex, true);
```

**Future Renderer API (Phase 2):**
- `PrepareView(ViewId, ...)`: Performs culling/prep for a specific view.
- `RenderView(ViewId, ...)`: Executes render graph for a specific view.
- Support for multiple `BuildFrame` calls per frame without overwriting state.

### 3.5. Compositing (New Phase)

**Status:** Phase infrastructure added, implementation pending.

- `PhaseId::kCompositing` exists with `kBarrieredConcurrency` execution model.
- Runs between `PhaseCommandRecord` and `PhasePresent`.
- Allows mutation of `kFrameState` (including view outputs).

**Future Mechanism (Phase 3):**
- Modules query `FrameContext::GetViewContext(ViewId)` (or find by tag) to access the `output` framebuffer.
- Use attachments for composition (e.g., combine multiple views into final backbuffer).

### 3.6. Presentation

Presentation remains the engine's synchronous responsibility at `PhasePresent`.
- The engine presents surfaces that have been flagged as presentable via `FrameContext::SetSurfacePresentable()`.
- Modules must ensure final images are ready in the backbuffers before this phase.

**Current Responsibility:** Modules explicitly mark surfaces presentable.

### 3.7. Pass & Module APIs

Passes operate against the `RenderContext` constructed by modules for each view. Pass implementations must be reentrant across views.

Module responsibilities:
- Modules register views during `PhaseSceneMutation`.
- Modules drive rendering in `PhaseCommandRecord` by calling renderer APIs and executing passes.
- Modules acquire `CommandRecorder` from the engine's graphics/commander interface.

### 3.8. Concurrency

Authoritative `GameStateSnapshot` and `ViewMetadata` are written only during allowed mutation phases. Once frozen via `PublishSnapshots()`, the snapshot is read-only.

**Current Implementation:**
- `FrameContext::PopulateGameStateSnapshot()` copies `ViewContext` objects into the snapshot under lock.
- Single-threaded rendering per frame (parallel rendering is Phase 3).

**Future (Phase 3):**
- Per-view culling and command recording may be performed in parallel.
- Each parallel task acquires its own `CommandRecorder`.

---

## 4. Implementation Details (Phase 1)

### 4.1. File Changes

**Core Types:**
- **`Oxygen/Core/Types/View.h`**: Added `ViewId` definition using `NamedType`.
- **`Oxygen/Core/FrameContext.h`**:
  - Added `ViewMetadata`, `ViewContext` structs
  - Changed `views_` from `vector` to `unordered_map<ViewId, ViewContext>`
  - Updated `AddView`, added `SetViewOutput`, `GetViewContext`
  - Removed `RenderableView` class
- **`Oxygen/Core/Frame Context.cpp`**: Implemented view management APIs.

**Phase System:**
- **`Oxygen/Core/PhaseRegistry.h`**: Added `PhaseId::kCompositing` with `kBarrieredConcurrency`.
- **`Oxygen/Core/EngineModule.h`**: Added `virtual void OnCompositing(FrameContext&)` hook.
- **`Oxygen/Engine/AsyncEngine.cpp/h`**: Integrated `PhaseCompositing` into `FrameLoop`.

**Renderer:**
- **`Oxygen/Renderer/Renderer.h`**: Removed `skip_frame_render_` flag.
- **`Oxygen/Renderer/Renderer.cpp`**:
  - Removed view iteration from `OnFrameGraph` and `OnTransformPropagation`
  - Removed surface presentation loop from `OnCommandRecord`
  - Renderer is now fully passive

**Camera:**
- **`Oxygen/Renderer/CameraView.h`**: Removed inheritance from `RenderableView`.

**Modules:**
- **`Oxygen.Editor.Interop/EditorModule.cpp`**:
  - Stores `ViewId` per surface
  - Creates `CameraView`, resolves it, calls `BuildFrame`
  - Marks surfaces presentable explicitly
- **`Examples/Async/MainModule.cpp`**:
  - Stores `camera_view_` and `view_id_` members
  - Implements full module-driven rendering pattern

### 4.2. Key Design Decisions

**Why remove `RenderableView`?**
- Eliminated unnecessary polymorphism
- Clearer ownership model (modules own `CameraView`)
- Simplified `ViewContext` to pure data structure
- Modules have explicit control over view resolution timing

**Why module-driven rendering?**
- Renderer doesn't know which views to render or in what order
- Modules understand their view requirements (editor multi-panel, game HUD, etc.)
- Enables flexible composition strategies per module
- Clearer phase boundaries and responsibilities

**Why passive renderer?**
- Decouples rendering policy from rendering implementation
- Modules can choose when/how to drive BuildFrame and ExecuteRenderGraph
- Supports diverse use cases (editor, game, tools) without renderer knowing specifics
- Easier to test and reason about

---

## 5. Migration Guide (Phase 1 Complete)

**For Module Authors:**

1. **In `OnSceneMutation`:**
   ```cpp
   // Create CameraView
   auto camera_view = std::make_shared<renderer::CameraView>(params, surface);

   // Add to FrameContext
   auto view_id = context.AddView(engine::ViewContext {
     .name = "MyView",
     .surface = *surface,
     .metadata = { .tag = "MyModule_MainView" }
   });

   // Store camera_view and view_id for later use
   ```

2. **In `OnCommandRecord`:**
   ```cpp
   // Resolve view
   const auto view = camera_view->Resolve();

   // Drive renderer
   renderer.BuildFrame(view, context);
   co_await renderer.ExecuteRenderGraph([&](auto& ctx) {
     co_await myPasses.Run(ctx, recorder);
   }, render_context, context);

   // Update output
   context.SetViewOutput(view_id, framebuffer);

   // Mark presentable
   context.SetSurfacePresentable(surfaceIndex, true);
   ```

---

## 6. Future Work (Phase 2+)

**Phase 2: Enhanced ViewMetadata & Multi-View Support**
- Extend `ViewMetadata` with presentation policy, target surfaces, viewport
- Support multiple `BuildFrame` calls per frame
- Add `PrepareView`/`RenderView` APIs
- Add `RenderContext::view` member

**Phase 3: Parallel Rendering & Composition**
- Implement `OnFrameGraphPerView` hook
- Parallel per-view culling and command recording
- View ordering and composition logic
- Multi-surface presentation
- Render target pooling

---

## 7. Risks & Mitigations

- **Risk:** Increased memory/descriptor pressure with many views. **Mitigation:** Use pooled render targets and descriptor reuse (Phase 3).
- **Risk:** Ordering bugs when views present to shared surfaces. **Mitigation:** Engine enforces strict submit → present sequence per surface.
- **Risk:** Module complexity increase. **Mitigation:** Provide clear templates and examples (see `EditorModule` and `Async` example).

---

## 8. Acceptance Criteria

**Phase 1 (✅ Complete):**
- ✅ Single view renders correctly with module-driven pattern
- ✅ `ViewContext` is the authoritative view storage
- ✅ Renderer is passive (no automatic view iteration)
- ✅ Modules explicitly mark surfaces presentable
- ✅ `PhaseCompositing` exists and is callable

**Phase 2:**
- [ ] Multiple views can be rendered per frame
- [ ] Each view has independent camera/projection
- [ ] `ViewMetadata` supports all planned fields

**Phase 3:**
- [ ] Parallel per-view rendering works correctly
- [ ] GPU validation shows no hazards
- [ ] Compositing combines multiple views

---

## 9. References

Below are the key source files modified in Phase 1:

**Core / Frame lifecycle:**
- [`Oxygen/Core/Types/View.h`](../../Core/Types/View.h) — ViewId definition
- [`Oxygen/Core/FrameContext.h`](../../Core/FrameContext.h) — ViewContext, ViewMetadata, view storage
- [`Oxygen/Core/FrameContext.cpp`](../../Core/FrameContext.cpp) — View management implementation
- [`Oxygen/Core/PhaseRegistry.h`](../../Core/PhaseRegistry.h) — kCompositing phase
- [`Oxygen/Engine/AsyncEngine.h`](../../Engine/AsyncEngine.h) / `.cpp` — Phase integration

**Renderer:**
- [`Oxygen/Renderer/Renderer.h`](../Renderer.h) / `.cpp` — Passive renderer implementation
- [`Oxygen/Renderer/CameraView.h`](../CameraView.h) — Removed RenderableView inheritance

**Modules (Examples):**
- `Oxygen.Editor.Interop/EditorModule.cpp` — Editor multi-panel implementation
- `Examples/Async/MainModule.cpp` — Single-view example implementation
- `Examples/Async/MainModule.h` — View storage pattern

**Design docs:**
- This document — Multi-view rendering design and status
- `Oxygen/Renderer/README.md` — Render-graph patterns
