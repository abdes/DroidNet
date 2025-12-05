# EditorModule Refactor - External API Specification

**Version:** 2.0
**Date:** December 6, 2025
**Audience:** Interop Layer Developers

---

## Overview

This document specifies the **external API** of the refactored `EditorModule` that will be exposed to the Interop layer for C++/CLI consumption by the .NET editor. This is the **only** public interface the editor should use.

### Design Principles

1. **Minimal Surface** - Expose only what's needed, hide implementation details
2. **Type-Safe** - Use strong types (ViewId, not raw integers)
3. **Clear Ownership** - API consumer doesn't manage resources directly
4. **Async-First** - Operations use callbacks to avoid blocking, respect frame lifecycle
5. **Error Resilient** - Invalid operations are no-ops, not crashes
6. **Thread-Safe** - All calls safe from any thread, execution deferred to engine thread

---

## Core Types

### ViewId

```cpp
// Strong type for view identification (defined in Oxygen/Core/Types/View.h)
struct ViewId {
    [[nodiscard]] auto get() const noexcept -> uint32_t;
    [[nodiscard]] explicit operator bool() const noexcept;
    [[nodiscard]] auto operator==(const ViewId&) const noexcept -> bool;
};

// Invalid view
constexpr ViewId kInvalidViewId{0};
```

**Usage:**

- Returned by `CreateView()`
- Used to identify views in all subsequent operations
- **Persistent**: Valid for the lifetime of the view
- **Shared**: Same ID used internally by the Engine's FrameContext
- Check validity with `if (view_id)` or `view_id == kInvalidViewId`

### ViewConfig (Optional)

```cpp
struct EditorViewConfig {
    std::string name;                    // Debug name (e.g., "MainPanel")
    std::string purpose;                 // Purpose tag (e.g., "scene_view")
    bool render_offscreen = true;        // Render to texture vs direct
    graphics::Color clear_color{0.1f, 0.2f, 0.38f, 1.0f};

    // Future: Add FOV, near/far plane overrides, etc.
};
```

---

## Public API - EditorModule

### View Management

#### CreateViewAsync

```cpp
using OnViewCreated = std::function<void(bool success, ViewId engine_id)>;

void CreateViewAsync(EditorView::Config config, OnViewCreated callback);
```

**Purpose:** Create a new editor view asynchronously.

**Parameters:**

- `config` - View configuration including name, purpose, and compositing target
- `callback` - Callback invoked when view is created (on engine thread)

**Callback Signature:**

- `success` - `true` if view created successfully, `false` otherwise
- `engine_id` - The engine-assigned ViewId (valid only if `success == true`)

**Behavior:**

- View creation is queued and executed during next `OnFrameStart` phase
- View is created in `kCreating` state
- Initialized during `OnSceneMutation` → `kReady`
- Callback invoked on engine thread when creation completes
- Visible by default (starts rendering immediately after initialization)

**Thread Safety:** Call from **any thread**. Callback executes on engine thread.

**Example:**

```cpp
EditorView::Config config {
    .name = "MainView",
    .purpose = "scene_view",
    .compositing_target = main_surface  // Bind surface at creation
};

view_manager->CreateViewAsync(config, [](bool success, ViewId view_id) {
    if (success) {
        LOG_F(INFO, "View created with ID: {}", view_id.get());
        // View is now ready and rendering
    } else {
        LOG_F(ERROR, "View creation failed");
    }
});
```

---

#### EditorView::Config Structure

```cpp
struct EditorView::Config {
    std::string name;                    // Debug name (e.g., "MainPanel")
    std::string purpose;                 // Purpose tag (e.g., "scene_view")
    std::optional<graphics::Surface*> compositing_target;  // Surface to composite to
    uint32_t width = 1;                  // Fallback width if no target
    uint32_t height = 1;                 // Fallback height if no target
    graphics::Color clear_color{0.1f, 0.2f, 0.38f, 1.0f};
};
```

**Purpose:** Configuration for view creation.

**Fields:**

- `name` - Human-readable name for debugging
- `purpose` - Semantic purpose tag
- `compositing_target` - Optional surface to composite to (bound at creation)
- `width`, `height` - Fallback dimensions if no compositing target
- `clear_color` - Background color for the view

**Example:**

```cpp
EditorView::Config cfg {
    .name = "WireframePiP",
    .purpose = "wireframe_overlay",
    .compositing_target = main_surface,
    .clear_color = {0.03f, 0.03f, 0.03f, 1.0f}
};
view_manager->CreateViewAsync(cfg, callback);
```

---

#### DestroyView

```cpp
void DestroyView(ViewId view_id);
```

**Purpose:** Destroy a view and release its resources.

**Parameters:**

- `view_id` - View to destroy

**Behavior:**

- View immediately enters `kReleasing` state
- Unregistered from FrameContext and Renderer
- GPU resources scheduled for deferred destruction
- View removed during next `OnFrameStart::ProcessDestroyedViews()`
- No-op if `view_id` is invalid or already destroyed

**Thread Safety:** Call from **any thread**. Execution deferred to next frame.

**Example:**

```cpp
editor_module->DestroyView(old_view);
// View will be fully destroyed in next frame
```

---

### View Visibility

#### ShowView

```cpp
void ShowView(ViewId view_id);
```

**Purpose:** Make a hidden view visible (resume rendering).

**Parameters:**

- `view_id` - View to show

**Behavior:**

- Sets view's `visible` flag to `true`
- View will be registered with FrameContext/Renderer in next `OnSceneMutation`
- Rendering resumes using existing resources (no allocation)
- No-op if view is already visible or invalid

**Use Case:** Toggle debug overlays, auxiliary views on/off

**Thread Safety:** Call from **any thread**. Takes effect next frame.

**Example:**

```cpp
// User presses F1 to toggle debug overlay
if (overlay_hidden) {
    editor_module->ShowView(debug_overlay_view);
    overlay_hidden = false;
}
```

---

#### HideView

```cpp
void HideView(ViewId view_id);
```

**Purpose:** Make a visible view hidden (pause rendering).

**Parameters:**

- `view_id` - View to hide

**Behavior:**

- Sets view's `visible` flag to `false`
- View will be unregistered from FrameContext/Renderer
- Resources are **retained** (fast to show again)
- No rendering occurs while hidden
- No-op if view is already hidden or invalid

**Use Case:** Temporarily disable views without destroying resources

**Thread Safety:** Call from **any thread**. Takes effect next frame.

**Example:**

```cpp
// User presses F1 to toggle debug overlay
if (!overlay_hidden) {
    editor_module->HideView(debug_overlay_view);
    overlay_hidden = true;
}
```

---

### View-Surface Association

> **Note:** Surface association is configured at view creation via `EditorView::Config::compositing_target`.

#### Surface Binding at Creation

```cpp
EditorView::Config config {
    .name = "MainView",
    .purpose = "scene_view",
    .compositing_target = main_surface  // ← Bind surface here
};

view_manager->CreateViewAsync(config, callback);
```

**Purpose:** Associate a view with a surface at creation time.

**Behavior:**

- View's offscreen render target will be composited to the specified surface's backbuffer
- Multiple views can target the same surface (e.g., main + PiP)
- Compositing happens during `OnCompositing` phase
- Surface binding is **immutable** after creation (by design, for simplicity)
- If no surface specified, view renders offscreen but doesn't composite

**Constraints:**

- Surface must be registered in SurfaceRegistry
- Surface must remain valid for the view's lifetime

**Example - Multiple Views on Same Surface:**

```cpp
// Main view (fullscreen)
EditorView::Config main_config {
    .name = "MainView",
    .compositing_target = main_surface
};
view_manager->CreateViewAsync(main_config, callback1);

// PiP view (overlay on same surface)
EditorView::Config pip_config {
    .name = "PiPView",
    .compositing_target = main_surface  // Same surface!
};
view_manager->CreateViewAsync(pip_config, callback2);
```

---

### View Queries (Read-Only)

#### IsViewVisible

```cpp
[[nodiscard]] auto IsViewVisible(ViewId view_id) const -> bool;
```

**Purpose:** Check if a view is currently visible (rendering).

**Returns:**

- `true` if view exists and is visible
- `false` if view is hidden, destroyed, or invalid

**Thread Safety:** Call from **any thread**. Reads current state.

---

#### GetViewSurface

```cpp
[[nodiscard]] auto GetViewSurface(ViewId view_id) const
    -> const graphics::Surface*;
```

**Purpose:** Get the surface a view is attached to.

**Returns:**

- Pointer to surface if attached
- `nullptr` if view is detached or invalid

**Thread Safety:** Call from **any thread**. Reads current state.

---

#### GetSurfaceViews

```cpp
[[nodiscard]] auto GetSurfaceViews(const graphics::Surface* surface) const
    -> std::vector<ViewId>;
```

**Purpose:** Get all views attached to a surface.

**Returns:**

- Vector of ViewIds (may be empty)
- Empty vector if surface is invalid

**Thread Safety:** Call from **any thread**. Returns snapshot.

**Example:**

```cpp
auto views = editor_module->GetSurfaceViews(main_surface);
LOG_F(INFO, "Surface has {} views attached", views.size());
```

---

### Render Graph Customization

#### SetViewRenderGraph

```cpp
void SetViewRenderGraph(ViewId view_id,
                       std::shared_ptr<RenderGraphFactory> factory);
```

**Purpose:** Assign a custom render graph to a view.

**Parameters:**

- `view_id` - Target view
- `factory` - Render graph factory (coroutine that executes passes)

**Behavior:**

- Replaces view's default render graph with custom one
- Takes effect in next `OnPreRender` phase
- Factory is a coroutine: `auto(ViewId, const RenderContext&, CommandRecorder&) -> co::Co<>`
- Can switch render graphs at runtime (e.g., solid ↔ wireframe)
- Pass `nullptr` to reset to default graph

**Thread Safety:** Call from **any thread**. Takes effect next frame.

**Example:**

```cpp
// Apply wireframe render graph
auto wireframe_factory = std::make_shared<WireframeRenderGraphFactory>();
editor_module->SetViewRenderGraph(pip_view, wireframe_factory);

// Reset to default
editor_module->SetViewRenderGraph(pip_view, nullptr);
```

---

### Scene Management

#### CreateScene

```cpp
void CreateScene(std::string_view name);
```

**Purpose:** Create a new scene (destroys existing scene).

**Parameters:**

- `name` - Scene name

**Behavior:**

- Creates new empty scene
- Scene is set on FrameContext during `OnFrameStart`
- All views use this scene (single scene per editor module)

**Thread Safety:** Call from **any thread**. Execution deferred.

**Example:**

```cpp
editor_module->CreateScene("UntitledScene");
```

---

#### GetScene

```cpp
[[nodiscard]] auto GetScene() const -> std::shared_ptr<scene::Scene>;
```

**Purpose:** Get the current scene.

**Returns:**

- Shared pointer to scene
- `nullptr` if no scene created

**Thread Safety:** Call from **any thread**. Returns current scene.

---

#### Enqueue

```cpp
void Enqueue(std::unique_ptr<EditorCommand> cmd);
```

**Purpose:** Enqueue a scene mutation command for execution in next frame.

**Parameters:**

- `cmd` - Command to execute (must derive from `EditorCommand`)

**Behavior:**

- Commands execute during `OnSceneMutation` phase
- Executed in FIFO order
- Thread-safe queue

**Thread Safety:** Call from **any thread**. Thread-safe queue.

**Example:**

```cpp
struct CreateCubeCommand : EditorCommand {
    void Execute(CommandContext& ctx) override {
        auto cube = CreateCube(ctx.Scene);
        ctx.Scene->GetRootNodes()[0].AddChild(cube);
    }
};

editor_module->Enqueue(std::make_unique<CreateCubeCommand>());
```

---

### Input Handling

#### ProcessInputEvent

```cpp
void ProcessInputEvent(const platform::InputEvent& event);
```

**Purpose:** Inject an input event from the Interop layer.

**Parameters:**

- `event` - The input event (Mouse, Keyboard, etc.)

**Behavior:**

- Event is queued and processed in the next frame
- Routed to the appropriate `EditorView` via `ViewManager`
- Used for camera control, gizmo interaction, and selection

**Thread Safety:** Call from **any thread**.

**Example:**

```cpp
platform::InputEvent click_event;
click_event.type = platform::InputEventType::kMouseBtnDown;
// ... fill event details ...
editor_module->ProcessInputEvent(click_event);
```

---

## Usage Examples

### Example 1: Simple Single View

```cpp
// Create view with surface binding
EditorView::Config config {
    .name = "MainView",
    .purpose = "scene_view",
    .compositing_target = main_surface
};

view_manager->CreateViewAsync(config, [](bool success, ViewId view_id) {
    if (success) {
        LOG_F(INFO, "View {} created and rendering to surface", view_id.get());
        // View is now initialized, rendering, and compositing
    }
});
```

---

### Example 2: Picture-in-Picture

```cpp
// Create main view (fullscreen)
EditorView::Config main_config {
    .name = "MainView",
    .purpose = "scene_view",
    .compositing_target = main_surface
};

view_manager->CreateViewAsync(main_config, [](bool success, ViewId main_view) {
    if (success) {
        LOG_F(INFO, "Main view created");
    }
});

// Create PiP view (same surface, will overlay)
EditorView::Config pip_config {
    .name = "PiPView",
    .purpose = "wireframe_pip",
    .compositing_target = main_surface  // Same surface!
};

view_manager->CreateViewAsync(pip_config, [](bool success, ViewId pip_view) {
    if (success) {
        LOG_F(INFO, "PiP view created");
        // TODO: Apply wireframe render graph when API available
    }
});

// Both views render to main_surface:
// 1. MainView composites fullscreen
// 2. PiPView composites overlaid on top
```

---

### Example 3: Multi-Panel Layout

```cpp
// Left panel
EditorView::Config left_config {
    .name = "LeftPanel",
    .purpose = "scene_view",
    .compositing_target = left_surface
};
view_manager->CreateViewAsync(left_config, callback);

// Center panel (main viewport)
EditorView::Config center_config {
    .name = "CenterPanel",
    .purpose = "main_view",
    .compositing_target = center_surface
};
view_manager->CreateViewAsync(center_config, callback);

// Right panel
EditorView::Config right_config {
    .name = "RightPanel",
    .purpose = "wireframe_view",
    .compositing_target = right_surface
};
view_manager->CreateViewAsync(right_config, callback);

// Three independent surfaces, three independent views
// Each renders and composites independently
```

---

### Example 4: Toggle Debug Overlay

```cpp
// Create overlay once
auto debug_overlay = editor_module->CreateView("DebugOverlay", "debug");
editor_module->AttachViewToSurface(debug_overlay, main_surface);

auto debug_graph = CreateDebugOverlayRenderGraph();
editor_module->SetViewRenderGraph(debug_overlay, debug_graph);

bool overlay_visible = true;

// Toggle handler
void OnToggleDebugOverlay() {
    if (overlay_visible) {
        editor_module->HideView(debug_overlay);
    } else {
        editor_module->ShowView(debug_overlay);
    }
    overlay_visible = !overlay_visible;
}
```

---

### Example 5: Dynamic View Creation/Destruction

```cpp
std::vector<ViewId> auxiliary_views;

// User clicks "Add Top View"
void OnAddTopView() {
    auto top_view = editor_module->CreateView("TopView", "auxiliary");
    editor_module->AttachViewToSurface(top_view, new_panel_surface);

    // Position camera looking down
    // (via command to be executed in next SceneMutation)
    struct PositionTopCameraCommand : EditorCommand {
        ViewId view;
        void Execute(CommandContext& ctx) override {
            // Position camera at (0, 10, 0) looking down
            // Implementation details...
        }
    };

    auto cmd = std::make_unique<PositionTopCameraCommand>();
    cmd->view = top_view;
    editor_module->Enqueue(std::move(cmd));

    auxiliary_views.push_back(top_view);
}

// User clicks "Close All Auxiliary Views"
void OnCloseAuxiliaryViews() {
    for (auto view_id : auxiliary_views) {
        editor_module->DestroyView(view_id);
    }
    auxiliary_views.clear();
}
```

---

## API Guarantees

### Thread Safety

- ✅ **All API calls are thread-safe**
- Operations are queued and executed on the engine thread at appropriate frame phases
- No locks required by caller

### Frame Lifecycle Guarantees

- ✅ View creation takes effect in **next frame's OnSceneMutation**
- ✅ View destruction completes in **next frame's OnFrameStart**
- ✅ Visibility changes take effect in **next frame's OnSceneMutation**
- ✅ Surface attachments take effect in **next frame's OnCompositing**
- ✅ Render graph changes take effect in **next frame's OnPreRender**

### Error Handling

- ✅ **No crashes** - Invalid operations are no-ops
- ✅ **Invalid ViewIds** - Ignored silently (may log warning)
- ✅ **Invalid surfaces** - Ignored silently
- ✅ **Null pointers** - Handled gracefully
- ✅ **Resource exhaustion** - Returns `kInvalidViewId` or no-op

### Resource Management

- ✅ **Automatic cleanup** - Destroyed views automatically release all resources
- ✅ **Deferred GPU release** - GPU resources deleted when safe (after frame in-flight)
- ✅ **No manual memory management** - All resources owned by views
- ✅ **Surface invalidation** - Views automatically cleaned when surface destroyed

### Input Handling

- ✅ **Thread-safe input** - `ProcessInputEvent` can be called from any thread
- ✅ **View routing** - Events automatically routed to focused/hovered view

---

## API Evolution Plan

### Phase 1 (Current Design)

- Basic view creation/destruction
- Show/Hide visibility
- Surface attachment
- Simple render graph customization

### Phase 2 (Near Future)

- **View templates**: `CreateViewFromTemplate("wireframe")`
- **Camera control**: `SetViewCamera(view_id, camera_config)`
- **Viewport regions**: `SetViewCompositeRegion(view_id, viewport)`
- **Query APIs**: `GetViewColorTexture(view_id)` for captures

### Phase 3 (Future)

- **View groups**: Group multiple views for bulk operations
- **View pooling**: Reuse view resources for performance
- **Custom compositors**: Override default blit compositor
- **Per-view scene filters**: Render subsets of scene per view

---

## Deprecated APIs (To Be Removed)

The following APIs from the current implementation will be **removed**:

- ❌ `EnsureEditorCamera(surface, width, height)` - Automatic per-view cameras
- ❌ `CleanupSurfaceCamera(surface)` - Automatic cleanup
- ❌ Direct framebuffer access - Hidden inside EditorView
- ❌ Direct RenderGraph access - Encapsulated in ViewRenderer
- ❌ `surface_view_ids_` map - Managed by ViewManager
- ❌ `surface_cameras_` map - Managed by EditorView

**Migration:**

```cpp
// OLD (monolithic)
auto surface = GetSurface();
EnsureEditorCamera(surface, width, height);
// Camera automatically attached to surface

// NEW (explicit)
auto view = CreateView("MyView", "scene_view");
AttachViewToSurface(view, surface);
// Camera automatically created and managed by view
```

---

## Summary

The refactored EditorModule API provides:

✅ **Minimal surface** - Only 13 public methods
✅ **Type-safe** - Strong ViewId type
✅ **Thread-safe** - All calls safe from any thread
✅ **Clear semantics** - Every operation has well-defined behavior
✅ **Error-resilient** - No crashes on invalid input
✅ **Future-proof** - Easy to extend without breaking changes

The API supports all current editor requirements while providing a clean foundation for future enhancements like view templates, pooling, and advanced compositing.
