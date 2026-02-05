# Oxygen Engine Examples

This directory contains examples demonstrating the use of the Oxygen Engine. Modern demos in Oxygen are built using the **DemoShell** framework and a **Composition-First** rendering pipeline.

---

## 🏗️ Demo Blueprint: The composition-first approach

To create a robust demo that integrates with the engine's modular tools, follow this blueprint.

### 1. The Foundation: `DemoModuleBase`

Always inherit from `DemoModuleBase`. It provides the necessary plumbing for window management, surface lifecycle, and phase-based dispatching.

```cpp
class MainModule final : public DemoModuleBase {
public:
    explicit MainModule(const DemoAppContext& app) : DemoModuleBase(app) {}

    // Define the phases your module needs
    auto GetSupportedPhases() const noexcept -> engine::ModulePhaseMask override {
        using enum core::PhaseId;
        return engine::MakeModuleMask<kFrameStart, kSceneMutation, kGameplay, kGuiUpdate, kCompositing>();
    }
};
```

### 2. Initialization & Pipeline Setup

In `OnAttached`, create your `ForwardPipeline` and initialize the `DemoShell`. The Shell will automatically hook up standard UI panels for you.

```cpp
auto MainModule::OnAttached(observer_ptr<AsyncEngine> engine) noexcept -> bool {
    if (!Base::OnAttached(engine)) return false;

    // 1. Create the pipeline (handles HDR, Tonemapping, Pass Orchestration)
    pipeline_ = std::make_unique<ForwardPipeline>(observer_ptr{app_.engine.get()});

    // 2. Initialize the DemoShell
    shell_ = std::make_unique<DemoShell>();
    DemoShellConfig config {
        .engine = observer_ptr{app_.engine.get()},
        .panel_config = {
            .content_loader = true,
            .camera_controls = true,
            .lighting = true,
            .rendering = true,
            .post_process = true // Enable Post-Processing controls
        },
        .get_active_pipeline = [this]() { return observer_ptr{pipeline_.get()}; }
    };
    return shell_->Initialize(config);
}
```

### 3. The "Reloadable" Scene Pattern (Critical)

Demos should allow the scene to be unloaded and reloaded (e.g., via the Content Loader). Implement this in `HandleOnFrameStart` to ensure the `FrameContext` always has a valid scene.

```cpp
auto MainModule::OnFrameStart(engine::FrameContext& context) -> void {
    // 1. Sync pipeline settings FIRST to avoid 1-frame lag
    ApplySettingsFromUI();

    // 2. Standard base processing (surface management)
    Base::OnFrameStart(context);
}

auto MainModule::HandleOnFrameStart(engine::FrameContext& context) -> void {
    // 3. Ensure the scene is valid (re-create if unloaded)
    if (!active_scene_.IsValid()) {
        auto scene = CreateMyDemoScene();
        active_scene_ = shell_->SetScene(std::move(scene));
    }
    // 4. Inject the active scene into the frame context
    context.SetScene(shell_->TryGetScene());
}
```

### 4. Mutation & Camera Sync

The `OnSceneMutation` phase is where you finalize the scene state before the renderer takes a "snapshot". **Crucially**, you must sync the camera lifecycle here.

```cpp
auto MainModule::OnSceneMutation(engine::FrameContext& context) -> co::Co<> {
    auto& camera_lifecycle = shell_->GetCameraLifecycle();

    // 1. Sync viewport and camera transforms (baked from UI/Input)
    camera_lifecycle.EnsureViewport(app_window_->GetWindow()->Size());
    camera_lifecycle.ApplyPendingSync();

    // 2. Update the shell (drives rig movement)
    shell_->Update(time::CanonicalDuration{});

    // 3. Delegation: Required to register views with the pipeline!
    co_await Base::OnSceneMutation(context);
}
```

### 5. Declaring View Intents

Instead of manually rendering, describe *what* you want to see by overriding `UpdateComposition`. The pipeline will then manage the HDR/SDR resources and tonemapping automatically.

```cpp
auto MainModule::UpdateComposition(engine::FrameContext& context,
                                 std::vector<CompositionView>& views) -> void {
    auto& active_camera = shell_->GetCameraLifecycle().GetActiveCamera();
    if (!active_camera.IsAlive()) return;

    // 1. Build a valid View with viewport from the window size
    View view {};
    if (app_window_ && app_window_->GetWindow()) {
        const auto extent = app_window_->GetWindow()->Size();
        view.viewport = ViewPort {
            .width = static_cast<float>(extent.width),
            .height = static_cast<float>(extent.height),
            // ... min/max depth ...
        };
    }

    // 2. Register the 3D Scene View
    views.push_back(CompositionView::ForScene(main_view_id_, view, active_camera));

    // 3. Register the ImGui Overlay (Tools) View - MANDATORY for UI visibility
    auto imgui_view_id = GetOrCreateViewId("ImGuiView");
    views.push_back(CompositionView::ForImGui(imgui_view_id, view, [](graphics::CommandRecorder&){}));
}
```

---

## 🚨 Critical "Make-or-Break" Steps

* **`co_await Base::OnSceneMutation(context)`**: If you forget to call the base implementation in mutation, your `UpdateComposition` will never be called, and nothing will render.
* **`UpdateComposition` mandates an ImGui Layer**: Without `ForImGui`, the DemoShell panels and engine tools will not be composited, resulting in no UI.
* **Viewport Construction**: Passing an empty `View {}` to `ForScene` results in a black screen. Always construct a viewport from the current window size.
* **Settings Sync in `OnFrameStart`**: Apply render modes and debug settings *before* `Base::OnFrameStart`. These settings are used for resource allocation; applying them later (e.g., in `OnPreRender`) causes a visible 1-frame lag.
* **`context.SetScene(...)`**: The rendering backend retrieves the scene from the `FrameContext`. If you don't call this in `HandleOnFrameStart`, you will get a black screen or an empty world.
* **`ApplyPendingSync()`**: Without this, the camera will not move or rotate even if the UI/Rig shows it is changing.
* **Controlled Shutdown**: Always call `shell_->SetScene(nullptr)` in `OnShutdown` to ensure scene nodes are destroyed while the graphics systems are still alive.

---

## 🎨 Design Philosophy: Focus on the "What", not the "How"

By following this blueprint, your demo code should focus entirely on **Scene Creation** and **Gameplay Logic**. Resource management, tonemapping, and multi-layered compositing are handled by the framework.
