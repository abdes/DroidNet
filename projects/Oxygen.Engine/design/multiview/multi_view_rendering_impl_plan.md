# Multi-View/Surface Rendering Design

> This document defines a clean, minimal, and authoritative approach for multi-view rendering in Oxygen. The `View` is the single source of truth for rendering and presentation. The Renderer orchestrates all rendering automatically while apps provide configuration.

---

## Implementation Status

### ✅ Phase 1: Core Architecture & Data Structures (COMPLETED)

### ✅ Phase 2: Renderer Orchestration & Automatic Rendering (COMPLETED)

**Completed Items:**

- ✅ Defined `ViewResolver` type as `std::function<ResolvedView(const ViewContext&)>`
- ✅ Defined `RenderGraphFactory` type for per-view rendering
- ✅ Added `Renderer::RegisterView()` for registering a per-view resolver + render graph factory
- ✅ Added `Renderer::UnregisterView()` to allow views to be unregistered / cleaned up
- ✅ Added `Renderer::IsViewReady()` for querying view render status
- ✅ Implemented automatic rendering in `Renderer::OnPreRender()`:
  - Iterates all registered views from FrameContext
  - Resolves each view using registered resolver
  - Calls `BuildFrame()` for scene preparation
-- ✅ Implemented automatic rendering in `Renderer::OnRender()`:
  - Iterates all registered render graphs
  - Acquires command recorder per view
  - Sets up framebuffer resource tracking and barriers
  - Executes registered render graph factory
  - Executes per-view render graph factories via coroutines
  - Acquires per-view command recorders and prepares per-view framebuffer tracking
  - Wires per-view scene constants and prepared frames into the `RenderContext`
- ✅ Exposed `ViewId` in `ViewContext` for cleaner API usage
-- ✅ Extended `ViewMetadata` with:
  - `name` and `purpose` fields
  - Note: a `PresentPolicy` enum is *documented* in design notes but is NOT present in the current FrameContext implementation; presentation decisions remain application-driven.
- ✅ `ViewContext` now includes complete view configuration:
  - `ViewId id` - unique identifier (assigned by AddView)
  - `View view` - viewport and scissors
  - `ViewMetadata metadata` - name, purpose
  - `surface` - reference to rendering surface
  - `output` - framebuffer. Typically provided/updated by the application (e.g. via `FrameContext::UpdateView`) before `OnRender` so the Renderer can target it when executing the per-view render graph.

**Key Architectural Decisions:**

1. **Renderer Orchestrates Rendering**: The Renderer is no longer passive—it automatically handles all rendering infrastructure:
   - Apps register views (once per frame in `OnSceneMutation`)
   - Apps register resolvers and render graphs (once at startup in `OnPreRender`)
   - Renderer executes everything automatically in `OnPreRender` and `OnRender`
   - Apps do not need to acquire command recorders for render-time recording, and the Renderer handles render-graph resource transitions; applications may still acquire command recorders in mutation phases for resource setup.

2. **Registration Pattern**: Clean separation of configuration vs execution:
   - `RegisterView(ViewId, ViewResolver, RenderGraphFactory)`: Registers both
     the resolver (ViewContext → ResolvedView) and the per-view render
     graph factory in one call.
   - `UnregisterView(ViewId)`: Removes the per-view resolver, render graph
     factory, and any cached per-view state.
   - Both registration and unregistration are expected to be done at
     setup/teardown times (not each frame). Registered factories are
     executed automatically every frame.

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

### 🔄 Phase 3: Multi-View Support & Parallel Rendering (IN PROGRESS)

**Status & Remaining Work (updated to reflect actual implementation):**

- [x] Support multiple views rendering per frame (sequential execution): The Renderer now iterates all registered per-view render graph factories (see `render_graphs_`) and executes them in `OnRender()` using per-view command recorders and a pooled `RenderContext`.
- [ ] Parallel per-view culling and command recording: *not implemented* — the Renderer performs scene preparation and render graph execution sequentially today.
- [ ] Add `OnFrameGraphPerView` module hook: *not present* — module-level per-view graph hooks remain a future enhancement.
- [x] Compositing phase available and used by examples (app-driven): The engine exposes `PhaseId::kCompositing` and the MultiView example performs application-driven compositing in `MainModule::OnCompositing()` (examples composite offscreen views into the swapchain backbuffer).
- [ ] Multi-surface automatic presentation support: *not implemented* — the Renderer does not automatically present surfaces; examples call `FrameContext::SetSurfacePresentable()` explicitly (composition & presentation remain application responsibilities).
- [x] Render-context pooling and per-view scene-constant management: partially implemented - A `RenderContextPool` plus `Renderer::render_context_` is used to claim a per-frame render context; `SceneConstantsManager` writes per-view constant buffers for rendering.
- [ ] Descriptor reuse and comprehensive render-target pooling: *planned* — full render-target pooling / descriptor reuse is still a Phase‑3 goal.
- [ ] View ordering and complex composition/dependency logic: *planned* — apps/example code currently compose in a simple order (MainView then PiP), but engine-level ordering & composition logic is a future improvement.

---

## 1. Summary

- **Goal:** Render multiple, independent views per engine frame with automatic orchestration by the Renderer. Supports editor layouts and multi-viewport rendering; presentation/compositing is application-driven in the current implementation.
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

struct ViewMetadata {
  std::string name;        // e.g. "MainView", "Minimap"
  std::string purpose;     // e.g. "primary", "shadow", "reflection"
  // Note: The live FrameContext implementation currently exposes only
  // `name` and `purpose` in ViewMetadata. Presentation policy data (eg
  // PresentPolicy) is a design concept but not present in the current
  // FrameContext layout. Applications currently decide presentation timing
  // explicitly.
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
| `PresentPolicy present_policy` | Presentation mode | ❌ Not implemented (apps decide presentation explicitly today) |
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
   - Register per-view resolver + render graph factories via
     `RegisterView(ViewId, ViewResolver, RenderGraphFactory)`
   - When a view is removed permanently or temporarily, call
     `UnregisterView(ViewId)` to clean up renderer-side registrations
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
     - Populate/wire per-view SceneConstants and prepared-frame data into `RenderContext`
     - Note: Renderer does not perform automatic surface presentation — modules and apps must call `FrameContext::SetSurfacePresentable()` when they decide to present.
     - Update `view_ready_states_` to track success/failure

**Example (Async Example):**

```cpp
// OnSceneMutation (every frame):
ViewContext view_ctx {
  .view = { .viewport = {...}, .scissor = {...} },
  .metadata = {
    .name = "MainView",
    .purpose = "primary",
  },
  .surface = std::ref(*surface),
};
view_id_ = context.AddView(std::move(view_ctx));

// OnPreRender (once at startup):
static bool registered = false;
if (!registered) {
  // Register per-view resolver + render graph factory in one call
  app_.renderer->RegisterView(view_id_,
    [this](const engine::ViewContext& vc) -> ResolvedView {
      renderer::SceneCameraViewResolver resolver(
        [this](const ViewId&) { return main_camera_; });
      return resolver(vc.id);
    },
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

**Key Benefits (practical):**

- Renderer centralizes per-view orchestration: it takes care of scene prep, per-view prepared frames, command-recorder acquisition for render-time workloads, and constant-buffer wiring.
- Applications keep responsibility for resource creation/staging and may acquire a `CommandRecorder` during mutation phases (for setup) — this pattern allows deterministic resource initialization while the Renderer handles render-time recording.
- Presentation and compositing remain application-driven; apps/modules explicitly set surface present flags when appropriate.
- Clean separation maintained: apps express "what" to render (views, resolvers, render graphs) while the Renderer handles the "how" (execution and resource state wiring).

### 3.5. Compositing (New Phase)

**Status:** Phase infrastructure added, implementation pending Phase 3.

- `PhaseId::kCompositing` exists with `kBarrieredConcurrency` execution model.
- Runs between `PhaseRender` and `PhasePresent`.
- Allows mutation of frame state (including view outputs).

**Future Mechanism (Phase 3):**

- Modules query `FrameContext::GetViewContext(ViewId)` to access the `output` framebuffer.
- Use attachments for composition (e.g., combine multiple views into final backbuffer).
- Register compositing render graphs via `RegisterView()` (resolver + factory) with `Composite` present policy.

### 3.6. Presentation

Presentation is application-driven in the current implementation. The engine provides per-surface presentable flags on `FrameContext` (`SetSurfacePresentable` / `GetPresentableSurfaces`) which modules or apps set during rendering or compositing. The engine coordinator reads those flags during the `PhasePresent` step and performs presentation (e.g., swapchain present).

**Current Implementation (Phase 2 — actual behavior):**

- Renderer runs per-view render graphs and populates `RenderContext` / `PreparedSceneFrame` data but does not automatically change surface present flags.
- Modules and examples perform composition and set presentable flags explicitly when ready — see `Examples/MultiView::MainModule::OnCompositing()` where the module composites offscreen colour textures into the backbuffer and calls `FrameContext::SetSurfacePresentable()`.

**App Responsibility:**

- Ensure the view's `output` framebuffer is provided to `FrameContext` before `OnRender` (via `UpdateView()` or initial registration) and set presentable flags when appropriate.

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
// Register both resolver and factory for the view
renderer->RegisterView(view_id, resolver, factory);
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
  auto RegisterView(ViewId view_id, ViewResolver resolver,
    RenderGraphFactory factory) -> void;
  auto UnregisterView(ViewId view_id) -> void;

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

// Usage (per-view registration):
renderer->RegisterView(view_id,
  renderer::SceneCameraViewResolver([](ViewId id) {
    return my_camera_node;  // SceneNode with attached camera component
  }),
  factory // RenderGraphFactory coroutine defined elsewhere
);
```

### 4.3. File Changes (Phase 2)

**Core Types:**

- **`Oxygen/Core/Types/View.h`**: `ViewId` definition
- **`Oxygen/Core/Types/ViewResolver.h`**: `ViewResolver` type alias
- **`Oxygen/Core/FrameContext.h`**:
  - `ViewContext::id` field added
  - `ViewMetadata` extended with `name` and `purpose` (note: `present_policy` is a design concept but is not present in the live `FrameContext` layout)

**Renderer:**

- **`Oxygen/Renderer/Renderer.h`**:
  - `RenderGraphFactory` type alias
  - `RegisterView()`, `UnregisterView()`, `IsViewReady()` APIs
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
  .metadata = { .name = "MainView" },
  // Application must also ensure the view's output framebuffer is available
  // before render (either via a subsequent UpdateView() that includes
  // `.output` or by providing the framebuffer during initial registration
  // when available). Renderer requires `ViewContext::output` to exist in
  // order to execute a registered render graph for that view.
  .surface = std::ref(*surface),
});

// OnPreRender (once at startup):
static bool registered = false;
if (!registered) {
  // Register resolver + render graph together for this view id
  renderer->RegisterView(view_id_,
    SceneCameraViewResolver([this](ViewId) { return camera_node_; }),
    [this](ViewId, const RenderContext& rc, CommandRecorder& rec) -> co::Co<void> {
      render_graph_->PrepareForRenderFrame(rc.framebuffer);
      co_await render_graph_->RunPasses(rc, rec);
    });

  registered = true;
}

// OnRender: empty - Renderer handles everything automatically
```

**Key Changes:**

1. ✅ Apps no longer call `BuildFrame()` directly — the Renderer runs scene preparation and finalization in `OnPreRender`.
2. ⚠️ Apps still perform resource creation and may acquire command recorders in mutation phases (e.g. `OnSceneMutation`) for setup; the Renderer acquires command recorders for per-view render-time command recording.
3. ⚠️ Surface presentation is *not* automatic — modules and apps are still responsible for marking surfaces presentable (for example `FrameContext::SetSurfacePresentable()`), particularly for composed or offscreen workflows.
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
- ✅ Renderer executes per-view render graphs and wires per-view outputs (see example uses)
- ✅ `SceneCameraViewResolver` helper available
- ✅ Single view renders correctly end-to-end

**Phase 3 (In progress / Partial):**

- ✅ Multiple views can be rendered per frame (sequential execution in the Renderer)
- [ ] Parallel per-view rendering works correctly (future)
- ✅ Compositing and multi-view composition are demonstrated by examples (app-driven composition in `Examples/MultiView::OnCompositing`), but engine-level automatic composition/policy handling is incomplete
- [ ] GPU validation and robust hazard-free multi-view parallel execution (future)

---

## 9. References

**Core / Frame lifecycle:**

- [`Oxygen/Core/Types/View.h`](../../Core/Types/View.h) — ViewId definition
- [`Oxygen/Core/Types/ViewResolver.h`](../../Core/Types/ViewResolver.h) — ViewResolver type
-- [`Oxygen/Core/FrameContext.h`](../../Core/FrameContext.h) — ViewContext, ViewMetadata (name, purpose), view registration/update APIs
- [`Oxygen/Core/FrameContext.cpp`](../../Core/FrameContext.cpp) — View management implementation

**Renderer:**

- [`Oxygen/Renderer/Renderer.h`](../Renderer.h) — Automatic orchestration APIs
- [`Oxygen/Renderer/Renderer.cpp`](../Renderer.cpp) — OnPreRender/OnRender implementation
- [`Oxygen/Renderer/SceneCameraViewResolver.h`](../SceneCameraViewResolver.h) — Helper for SceneNode cameras

**Modules (Examples):**

- `Examples/Async/MainModule.cpp` — Registration pattern implementation
- `Examples/Async/MainModule.h` — View storage pattern

**Examples / Demonstrations:**

- **`Examples/MultiView`** demonstrates the new per-view registration / render-graph model in practice:
  - Views are added/updated during `OnSceneMutation` (see `DemoView::AddViewToFrameContext`).
  - Per-view GPU resources (color/depth textures and a framebuffer) are created by the view and the app updates the `FrameContext` with the view `output` framebuffer before rendering.
  - Each view registers a resolver + render-graph factory using `ViewRenderer::RegisterWithEngine` which forwards render work into `ViewRenderer::Render()`.
  - Compositing is performed in `MainModule::OnCompositing()` where all views are composited into the swapchain backbuffer and the module explicitly sets the swapchain surface presentable via `FrameContext::SetSurfacePresentable()`.

**Design docs:**

- This document — Multi-view rendering design and status
