# Multi-View/Surface Rendering Design

> This document defines a clean, minimal, and authoritative approach for multi-view rendering in Oxygen. The `View` is the single source of truth for rendering and presentation. The Renderer orchestrates all rendering automatically while apps provide configuration.

---

## Implementation Status

### ✅ Phase 1: Core Architecture & Data Structures (COMPLETED)

### ✅ Phase 2: Renderer Orchestration & Automatic Rendering (COMPLETED)

### ✅ Phase 2: Renderer Orchestration & Automatic Rendering (COMPLETED)

**Completed Items:**

- ✅ Defined `ViewResolver` type as `std::function<ResolvedView(const ViewContext&)>`
- ✅ Defined `RenderGraphFactory` type for per-view rendering
- ✅ Added `Renderer::RegisterViewResolver()` for registering view resolution logic
- ✅ Added `Renderer::RegisterRenderGraph()` for registering per-view render graph factories
- ✅ Added `Renderer::IsViewReady()` for querying view render status
- ✅ Implemented automatic rendering in `Renderer::OnPreRender()`:
  - Iterates all registered views from FrameContext
  - Resolves each view using registered resolver
  - Calls `BuildFrame()` for scene preparation
- ✅ Implemented automatic rendering in `Renderer::OnRender()`:
  - Iterates all registered render graphs
  - Acquires command recorder per view
  - Sets up framebuffer resource tracking and barriers
  - Executes registered render graph factory
  - Handles presentation based on `PresentPolicy`
  - Marks surfaces presentable automatically
- ✅ Exposed `ViewId` in `ViewContext` for cleaner API usage
- ✅ Extended `ViewMetadata` with:
  - `name` and `purpose` fields
  - `PresentPolicy` enum (`DirectToSurface`, `Hidden`, `Composite`)
- ✅ `ViewContext` now includes complete view configuration:
  - `ViewId id` - unique identifier (assigned by AddView)
  - `View view` - viewport and scissors
  - `ViewMetadata metadata` - name, purpose, present policy
  - `surface` - reference to rendering surface
  - `output` - framebuffer (wired by Renderer)

**Key Architectural Decisions:**

1. **Renderer Orchestrates Rendering**: The Renderer is no longer passive—it automatically handles all rendering infrastructure:
   - Apps register views (once per frame in `OnSceneMutation`)
   - Apps register resolvers and render graphs (once at startup in `OnPreRender`)
   - Renderer executes everything automatically in `OnPreRender` and `OnRender`
   - Apps no longer manually acquire command recorders or manage resource barriers

2. **Registration Pattern**: Clean separation of configuration vs execution:
   - `RegisterViewResolver()`: Maps `ViewContext` → `ResolvedView` (camera/projection)
   - `RegisterRenderGraph()`: Maps `ViewId` → rendering coroutine
   - Both registered once, executed automatically every frame

3. **SceneCameraViewResolver Helper**: Provides convenient resolver for SceneNode-based cameras:
   ```cpp
   renderer::SceneCameraViewResolver([](ViewId id) -> SceneNode {
     return my_camera_node;
   })
   ```

4. **App Responsibility Simplified**: Apps only need to:
   - Register views each frame before `kSnapshot` phase
   - Register resolver and render graphs once
   - Configure render passes (e.g., wire framebuffer attachments)

### 🔄 Phase 3: Multi-View Support & Parallel Rendering (NEXT)

**Remaining Work:**

- [ ] Support multiple views rendering simultaneously per frame
- [ ] Implement parallel per-view culling and command recording
- [ ] Add `OnFrameGraphPerView` module hook
- [ ] Implement compositing logic in `OnCompositing` phase hook for multi-view composition
- [ ] Add multi-surface presentation support
- [ ] Implement render target pooling and descriptor reuse
- [ ] Add view ordering and composition logic

---

## 1. Summary

- **Goal:** Render multiple, independent views per engine frame with automatic orchestration by the Renderer. Supports editor layouts, multi-viewport rendering, and flexible presentation policies.
- **Constraint:** Clean separation between app configuration (what to render) and renderer infrastructure (how to render). Minimal app-facing API surface.

## 2. Principles

1. **Single source of truth:** View ownership and configuration reside in `FrameContext`. Views registered before `kSnapshot` phase become immutable for the frame.
2. **Renderer orchestrates, apps configure:** Renderer handles all rendering infrastructure (command recorders, barriers, presentation). Apps provide configuration (views, resolvers, render graphs).
3. **Registration pattern:** Apps register resolvers and render graphs once; Renderer executes them automatically every frame.
4. **Small types, no duplication:** Per-view descriptors are small (refs, indices, flags) and do not duplicate scene data.

---

## 3. Design Overview

### 3.1. Authoritative View Metadata

**Current Implementation (Phase 2):**

`FrameContext` maintains a single map keyed by `ViewId`, storing a consolidated `ViewContext`:

```cpp
// In Oxygen/Core/Types/View.h
using ViewIdTag = struct ViewIdTag;
using ViewId = NamedType<uint64_t, ViewIdTag, Comparable, Hashable, Printable>;

// In Oxygen/Core/FrameContext.h (oxygen::engine namespace)
using ViewId = oxygen::ViewId; // namespace alias

enum class PresentPolicy : uint8_t {
  DirectToSurface, // Present directly to the surface (default)
  Hidden,          // Don't present (e.g., for offscreen rendering)
  Composite        // Compose with other views before presenting
};

struct ViewMetadata {
  std::string name;        // e.g. "MainView", "Minimap"
  std::string purpose;     // e.g. "primary", "shadow", "reflection"
  PresentPolicy present_policy = PresentPolicy::DirectToSurface;
  std::vector<SurfaceId> surfaces; // TODO: logical target identifiers
};

struct ViewContext {
  ViewId id {};  // Unique identifier assigned by AddView
  View view;     // Viewport and scissors configuration
  ViewMetadata metadata;
  std::variant<std::reference_wrapper<graphics::Surface>, std::string> surface;
  std::shared_ptr<graphics::Framebuffer> output {};  // Set by Renderer during OnRender
};

// In FrameContext private members:
std::unordered_map<ViewId, ViewContext> views_;
```

**Contract:**

- **ViewId Generation:** `FrameContext::AddView` generates and returns a unique `ViewId` using atomic incrementation, and assigns it to `ViewContext::id`.
- **Mutable Window:** Views can be added/updated only during phases **before** `kSnapshot`.
- **Freeze:** At `kSnapshot`, `FrameContext::PublishSnapshots()` copies views into `GameStateSnapshot`.
- **Output Wiring:** The `output` field is set by the Renderer during `OnRender` before executing render graphs.

**Future `ViewMetadata` fields (Phase 3+):**

| Field | Description | Status |
|---|---|---|
| `std::string name` | Human-readable name | ✅ Implemented |
| `std::string purpose` | View purpose tag | ✅ Implemented |
| `PresentPolicy present_policy` | Presentation mode | ✅ Implemented |
| `std::vector<SurfaceId> surfaces` | Logical target identifiers | ⚠️ Placeholder |
| `uint32_t flags` | Flags for HDR, MSAA, etc. | 📋 Planned |

### 3.2. FrameContext Responsibilities

**Current Implementation:**

- **Storage:** Owns the authoritative set of `ViewContext`s in `std::unordered_map<ViewId, ViewContext> views_`.
- **ID Generation:** Generates unique `ViewId`s via atomic counter (scoped per FrameContext instance) and assigns to `ViewContext::id`.
- **Lifecycle:** Enforces mutation windows (views can only be added before `kSnapshot`).
- **Publishing:** Copies `ViewContext` objects into `GameStateSnapshot.views` during `PublishSnapshots()`.
- **Passive:** Does not dictate rendering order or drive rendering.

**APIs:**

```cpp
// Add a view (returns unique ViewId, assigns id to ViewContext)
auto AddView(ViewContext view) noexcept -> ViewId;

// Get the full context for a view
auto GetViewContext(ViewId id) const -> const ViewContext&;

// Get all views (returns range of const ViewContext&)
auto GetViews() const noexcept; // returns transform_view
```

### 3.3. RenderContext / Per-View Render State

`RenderContext` is the per-frame execution wrapper passed to render graph factories.

**Current State (Phase 2):**

- Contains `framebuffer` pointer set by Renderer before executing render graph
- Passed by const-reference to render graph factories
- Apps can use it to access the target framebuffer for wiring pass dependencies

```cpp
struct RenderContext {
  std::shared_ptr<graphics::Framebuffer> framebuffer;
  // ... other rendering state
};
```

**Future Enhancement (Phase 3):** Add `observer_ptr<const View> view` to explicitly link context to the view being rendered.

### 3.4. Renderer Execution Model (Automatic Orchestration)

**Current Implementation (Phase 2):**

The Renderer **automatically orchestrates** all rendering infrastructure. Apps provide configuration, Renderer handles execution.

**App Responsibilities:**

1. **In `OnSceneMutation` (every frame):**
   - Add views to FrameContext with `AddView(ViewContext)`
   - Store returned `ViewId` for later reference
   - Views must be added before `kSnapshot` phase

2. **In `OnPreRender` (once at startup):**
   - Register view resolver via `RegisterViewResolver(ViewResolver)`
   - Register render graph factories via `RegisterRenderGraph(ViewId, RenderGraphFactory)`
   - Configure render passes (clear colors, debug names, etc.)

3. **In Render Graph Factory (executed by Renderer):**
   - Wire framebuffer attachments to pass configs (e.g., `PrepareForRenderFrame(rc.framebuffer)`)
   - Execute render passes via coroutines
   - Return control to Renderer

**Renderer Responsibilities (Automatic):**

1. **In `OnPreRender`:**
   - Iterate all views from `FrameContext::GetViews()`
   - Resolve each view using registered `ViewResolver`
   - Call `BuildFrame(resolved_view, context)` for scene preparation (culling, draw list generation)

2. **In `OnRender`:**
   - Iterate all registered render graphs (by `ViewId`)
   - For each view:
     - Acquire `CommandRecorder` from graphics backend
     - Get `ViewContext` and validate `output` framebuffer exists
     - Setup resource tracking and barriers for framebuffer attachments
     - Bind framebuffer to command recorder
     - Wire framebuffer into `RenderContext`
     - Execute registered `RenderGraphFactory` coroutine
     - Handle presentation based on `ViewContext::metadata::present_policy`
     - Mark surfaces presentable if policy is `DirectToSurface`
     - Update `view_ready_states_` to track success/failure

**Example (Async Example):**

```cpp
// OnSceneMutation (every frame):
ViewContext view_ctx {
  .view = { .viewport = {...}, .scissor = {...} },
  .metadata = {
    .name = "MainView",
    .purpose = "primary",
    .present_policy = engine::PresentPolicy::DirectToSurface,
  },
  .surface = std::ref(*surface),
};
view_id_ = context.AddView(std::move(view_ctx));

// OnPreRender (once at startup):
static bool registered = false;
if (!registered) {
  // Register view resolver
  app_.renderer->RegisterViewResolver(
    renderer::SceneCameraViewResolver([this](ViewId) {
      return main_camera_;  // SceneNode with camera component
    }));

  // Register render graph factory
  app_.renderer->RegisterRenderGraph(view_id_,
    [this](ViewId id, const engine::RenderContext& rc,
      graphics::CommandRecorder& rec) -> co::Co<void> {
      // Wire framebuffer to pass configs
      render_graph_->PrepareForRenderFrame(rc.framebuffer);

      // Execute passes
      co_await render_graph_->RunPasses(rc, rec);
      co_await imgui_pass->Render(rec);
    });

  registered = true;
}
```

**Key Benefits:**

- Apps never directly acquire command recorders
- Apps never manage resource barriers or transitions
- Apps never manually mark surfaces presentable
- Renderer handles all GPU synchronization automatically
- Clean separation: apps provide "what", Renderer handles "how"

### 3.5. Compositing (New Phase)

**Status:** Phase infrastructure added, implementation pending Phase 3.

- `PhaseId::kCompositing` exists with `kBarrieredConcurrency` execution model.
- Runs between `PhaseRender` and `PhasePresent`.
- Allows mutation of frame state (including view outputs).

**Future Mechanism (Phase 3):**

- Modules query `FrameContext::GetViewContext(ViewId)` to access the `output` framebuffer.
- Use attachments for composition (e.g., combine multiple views into final backbuffer).
- Register compositing render graphs via `RegisterRenderGraph()` with `Composite` present policy.

### 3.6. Presentation

Presentation is handled automatically by the Renderer based on `PresentPolicy`.

**Current Implementation (Phase 2):**

- Renderer checks `ViewContext::metadata::present_policy` after rendering each view
- If policy is `DirectToSurface`, Renderer marks the surface as presentable via `FrameContext::SetSurfacePresentable()`
- Engine presents marked surfaces synchronously at `PhasePresent`

**App Responsibility:** Set appropriate `present_policy` in `ViewMetadata` when registering views.

### 3.7. Pass & Module APIs

Passes operate against the `RenderContext` provided by the Renderer to render graph factories.

**Pass Requirements:**

- Must be reentrant across views
- Receive `RenderContext` by const-reference
- Receive `CommandRecorder` by reference
- Return `co::Co<void>` coroutines

**Module Pattern:**

```cpp
// Define render graph factory
auto factory = [](ViewId id, const RenderContext& rc, CommandRecorder& rec) -> co::Co<void> {
  // Wire framebuffer attachments
  ConfigurePasses(rc.framebuffer);

  // Execute passes sequentially
  co_await depth_pass->Execute(rc, rec);
  co_await shader_pass->Execute(rc, rec);
  co_await transparent_pass->Execute(rc, rec);
};

// Register once
renderer->RegisterRenderGraph(view_id, factory);
```

### 3.8. Concurrency

Authoritative `GameStateSnapshot` and `ViewMetadata` are written only during allowed mutation phases. Once frozen via `PublishSnapshots()`, the snapshot is read-only.

**Current Implementation (Phase 2):**

- `FrameContext::PopulateGameStateSnapshot()` copies `ViewContext` objects into the snapshot under lock.
- Single-threaded rendering per frame (Renderer executes render graphs sequentially).
- Each view gets its own `CommandRecorder` from the graphics backend.

**Future (Phase 3):**

- Per-view culling and command recording may be performed in parallel.
- Parallel execution of render graph factories for independent views.
- Synchronization via GPU timeline semaphores.

---

## 4. Current Architecture (Phase 2)

### 4.1. Renderer Module APIs

```cpp
class Renderer : public EngineModule {
  // Type aliases
  using ViewResolver = std::function<ResolvedView(const ViewContext&)>;
  using RenderGraphFactory = std::function<
    co::Co<void>(ViewId, const RenderContext&, CommandRecorder&)>;

  // Registration APIs (call once at startup)
  auto RegisterViewResolver(ViewResolver resolver) -> void;
  auto RegisterRenderGraph(ViewId view_id, RenderGraphFactory factory) -> void;

  // Query API
  auto IsViewReady(ViewId view_id) const -> bool;

  // Module lifecycle (automatic, called by engine)
  auto OnPreRender(FrameContext& context) -> co::Co<> override;
  auto OnRender(FrameContext& context) -> co::Co<> override;
};
```

### 4.2. Helper Classes

**SceneCameraViewResolver:**

Convenient resolver for `SceneNode`-based cameras:

```cpp
namespace oxygen::renderer {
  template <NodeLookupConcept NodeLookup>
  class SceneCameraViewResolver {
    explicit SceneCameraViewResolver(NodeLookup lookup);
    auto operator()(const ViewId& id) const -> ResolvedView;
  };
}

// Usage:
renderer->RegisterViewResolver(
  renderer::SceneCameraViewResolver([](ViewId id) {
    return my_camera_node;  // SceneNode with attached camera component
  })
);
```

### 4.3. File Changes (Phase 2)

**Core Types:**

- **`Oxygen/Core/Types/View.h`**: `ViewId` definition
- **`Oxygen/Core/Types/ViewResolver.h`**: `ViewResolver` type alias
- **`Oxygen/Core/FrameContext.h`**:
  - `ViewContext::id` field added
  - `ViewMetadata` extended with `name`, `purpose`, `present_policy`
  - `PresentPolicy` enum added

**Renderer:**

- **`Oxygen/Renderer/Renderer.h`**:
  - `RenderGraphFactory` type alias
  - `RegisterViewResolver()`, `RegisterRenderGraph()`, `IsViewReady()` APIs
  - Removed legacy `ExecuteRenderGraph()` and `RenderFrame()` methods
- **`Oxygen/Renderer/Renderer.cpp`**:
  - Implemented automatic view iteration in `OnPreRender()`
  - Implemented automatic rendering orchestration in `OnRender()`
- **`Oxygen/Renderer/SceneCameraViewResolver.h`**: Helper class for SceneNode-based cameras

**Modules:**

- **`Examples/Async/MainModule.cpp`**:
  - Simplified to registration pattern
  - Views added in `OnSceneMutation`
  - Resolver/render graphs registered once in `OnPreRender`
  - `OnRender` is empty (Renderer handles everything)

---

## 5. Migration Guide (Phase 1 → Phase 2)

**For Module Authors:**

**OLD (Phase 1 - Module-Driven):**

```cpp
// OnCommandRecord:
const auto view = camera_view->Resolve();
renderer.BuildFrame(view, context);
auto recorder = AcquireCommandRecorder();
co_await renderer.ExecuteRenderGraph([&](auto& ctx) {
  co_await passes.Run(ctx, *recorder);
}, render_context, context);
context.SetViewOutput(view_id, framebuffer);
context.SetSurfacePresentable(surfaceIndex, true);
```

**NEW (Phase 2 - Automatic Orchestration):**

```cpp
// OnSceneMutation (every frame):
view_id_ = context.AddView(ViewContext {
  .view = view_config,
  .metadata = { .name = "MainView", .present_policy = PresentPolicy::DirectToSurface },
  .surface = std::ref(*surface),
});

// OnPreRender (once at startup):
static bool registered = false;
if (!registered) {
  // Register resolver
  renderer->RegisterViewResolver(
    SceneCameraViewResolver([this](ViewId) { return camera_node_; }));

  // Register render graph
  renderer->RegisterRenderGraph(view_id_,
    [this](ViewId, const RenderContext& rc, CommandRecorder& rec) -> co::Co<void> {
      render_graph_->PrepareForRenderFrame(rc.framebuffer);
      co_await render_graph_->RunPasses(rc, rec);
    });

  registered = true;
}

// OnRender: empty - Renderer handles everything automatically
```

**Key Changes:**

1. ✅ No more manual command recorder acquisition
2. ✅ No more `SetViewOutput()` or `SetSurfacePresentable()` calls
3. ✅ No more `BuildFrame()` calls from app code
4. ✅ Register once, execute automatically every frame
5. ✅ Framebuffer wiring happens in render graph factory via `RenderContext`

---

## 6. Future Work

### Phase 3: Parallel Rendering & Multi-View Support

- [ ] Support multiple views rendering simultaneously per frame
- [ ] Implement parallel per-view culling and command recording
- [ ] Add `OnFrameGraphPerView` module hook for fine-grained control
- [ ] Implement compositing logic for `Composite` present policy
- [ ] Add multi-surface presentation support
- [ ] Implement render target pooling and descriptor reuse
- [ ] Add view ordering and dependency management

### Phase 4: Advanced Features

- [ ] HDR and tone mapping support per view
- [ ] MSAA configuration per view
- [ ] Dynamic resolution scaling per view
- [ ] View-specific quality settings

---

## 7. Risks & Mitigations

- **Risk:** Increased memory/descriptor pressure with many views. **Mitigation:** Render target pooling and descriptor reuse (Phase 3).
- **Risk:** Static registration pattern limits dynamic view scenarios. **Mitigation:** Allow re-registration or provide dynamic registration API in Phase 3.
- **Risk:** Single view resolver limits multi-camera scenarios. **Mitigation:** Extend to map-based resolvers or per-view resolvers in Phase 3.

---

## 8. Acceptance Criteria

**Phase 1 (✅ Complete):**

- ✅ Core view data structures implemented
- ✅ `PhaseCompositing` added to engine lifecycle

**Phase 2 (✅ Complete):**

- ✅ Renderer automatically orchestrates all rendering
- ✅ Apps use registration pattern (resolver + render graphs)
- ✅ Automatic presentation based on `PresentPolicy`
- ✅ `SceneCameraViewResolver` helper available
- ✅ Single view renders correctly end-to-end

**Phase 3 (Future):**

- [ ] Multiple views can be rendered per frame
- [ ] Parallel per-view rendering works correctly
- [ ] Compositing combines multiple views
- [ ] GPU validation shows no hazards

---

## 9. References

**Core / Frame lifecycle:**

- [`Oxygen/Core/Types/View.h`](../../Core/Types/View.h) — ViewId definition
- [`Oxygen/Core/Types/ViewResolver.h`](../../Core/Types/ViewResolver.h) — ViewResolver type
- [`Oxygen/Core/FrameContext.h`](../../Core/FrameContext.h) — ViewContext, ViewMetadata, PresentPolicy
- [`Oxygen/Core/FrameContext.cpp`](../../Core/FrameContext.cpp) — View management implementation

**Renderer:**

- [`Oxygen/Renderer/Renderer.h`](../Renderer.h) — Automatic orchestration APIs
- [`Oxygen/Renderer/Renderer.cpp`](../Renderer.cpp) — OnPreRender/OnRender implementation
- [`Oxygen/Renderer/SceneCameraViewResolver.h`](../SceneCameraViewResolver.h) — Helper for SceneNode cameras

**Modules (Examples):**

- `Examples/Async/MainModule.cpp` — Registration pattern implementation
- `Examples/Async/MainModule.h` — View storage pattern

**Design docs:**

- This document — Multi-view rendering design and status
