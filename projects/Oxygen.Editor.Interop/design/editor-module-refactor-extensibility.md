# EditorModule Refactor - Extensibility & Future Enhancements

**Version:** 2.0
**Date:** December 6, 2025
**Status:** ✅ Core Implementation Complete

---

## Overview

This document outlines how the refactored EditorModule architecture enables future enhancements and provides guidance for extending the system. The modular design makes it straightforward to add new capabilities without breaking existing code.

---

## Extension Points

### 1. Custom Render Graphs

**Current Capability:**
Views can have custom render graph factories via `SetViewRenderGraph()`.

**Extension Pattern:**

```cpp
class CustomRenderGraphFactory {
public:
    auto operator()(ViewId id,
                   const engine::RenderContext& ctx,
                   graphics::CommandRecorder& recorder) -> co::Co<> {
        // Custom render logic
        co_await ExecuteCustomPasses(ctx, recorder);
        co_return;
    }
};

// Usage
auto factory = std::make_shared<CustomRenderGraphFactory>();
editor_module->SetViewRenderGraph(view_id, factory);
```

**Future Enhancements:**

#### A. Render Graph Library

```cpp
namespace RenderGraphs {
    // Pre-built graph factories
    auto Solid() -> std::shared_ptr<RenderGraphFactory>;
    auto Wireframe() -> std::shared_ptr<RenderGraphFactory>;
    auto Unlit() -> std::shared_ptr<RenderGraphFactory>;
    auto DebugNormals() -> std::shared_ptr<RenderGraphFactory>;
    auto DebugUVs() -> std::shared_ptr<RenderGraphFactory>;
    auto ShadowMapDebug() -> std::shared_ptr<RenderGraphFactory>;

    // Composable graphs
    auto Combine(std::vector<std::shared_ptr<RenderGraphFactory>> graphs)
        -> std::shared_ptr<RenderGraphFactory>;
}

// Usage
auto main_graph = RenderGraphs::Solid();
auto pip_graph = RenderGraphs::Wireframe();
auto debug_graph = RenderGraphs::Combine({
    RenderGraphs::Solid(),
    RenderGraphs::DebugNormals()
});
```

#### B. Graph Parameters

```cpp
class ParameterizedRenderGraph {
public:
    struct Params {
        bool show_wireframe = false;
        bool show_normals = false;
        bool show_lighting = true;
        float wireframe_thickness = 1.0f;
    };

    void SetParameters(const Params& params);
    auto operator()(ViewId, const RenderContext&, CommandRecorder&) -> co::Co<>;
};

// Runtime switching
graph->SetParameters({.show_wireframe = true, .wireframe_thickness = 2.0f});
```

#### C. Post-Process Stack

```cpp
class PostProcessStack {
public:
    void AddEffect(std::unique_ptr<PostProcessEffect> effect);
    void RemoveEffect(std::string_view name);
    auto Execute(Texture& input, Texture& output, CommandRecorder&) -> co::Co<>;
};

// View-specific post-processing
auto bloom = std::make_unique<BloomEffect>();
auto ssao = std::make_unique<SSAOEffect>();
view->GetPostProcessStack().AddEffect(std::move(bloom));
view->GetPostProcessStack().AddEffect(std::move(ssao));
```

---

### 2. View Templates

**Problem:** Creating views with common configurations is repetitive.

**Solution:** Template system for pre-configured views.

```cpp
class ViewTemplate {
public:
    struct Config {
        std::string name_pattern;      // "Panel_{index}"
        EditorView::Config view_config;
        glm::vec3 camera_position;
        glm::quat camera_rotation;
        std::shared_ptr<RenderGraphFactory> render_graph;
    };

    static void RegisterTemplate(std::string_view name, Config config);
    static auto CreateFromTemplate(std::string_view template_name) -> ViewId;
};

// Register templates
ViewTemplate::RegisterTemplate("default", {
    .name_pattern = "View_{index}",
    .view_config = {.clear_color = {0.1f, 0.2f, 0.38f, 1.0f}},
    .camera_position = {0, 0, 10},
    .render_graph = RenderGraphs::Solid()
});

ViewTemplate::RegisterTemplate("wireframe_pip", {
    .name_pattern = "PiP_{index}",
    .view_config = {.clear_color = {0.03f, 0.03f, 0.03f, 1.0f}},
    .camera_position = {5, 5, 5},
    .render_graph = RenderGraphs::Wireframe()
});

// Usage
auto main_view = ViewTemplate::CreateFromTemplate("default");
auto pip_view = ViewTemplate::CreateFromTemplate("wireframe_pip");
```

---

### 3. View Pooling

**Problem:** Creating/destroying views allocates resources repeatedly.

**Solution:** Pool views for reuse.

```cpp
class ViewPool {
public:
    explicit ViewPool(EditorModule& module, size_t initial_capacity = 10);

    // Acquire a view (create or reuse)
    auto Acquire(std::string_view purpose) -> ViewId;

    // Release view back to pool (hides but doesn't destroy)
    void Release(ViewId view_id);

    // Shrink pool to target size
    void Trim(size_t target_size);

    [[nodiscard]] auto GetPoolSize() const -> size_t;
    [[nodiscard]] auto GetActiveCount() const -> size_t;

private:
    EditorModule& module_;
    std::vector<ViewId> available_;
    std::unordered_set<ViewId> in_use_;
};

// Usage
ViewPool pool(editor_module, 5);

// Fast - reuses pooled view
auto view = pool.Acquire("auxiliary");
editor_module->AttachViewToSurface(view, surface);

// Returns to pool (hidden, resources retained)
pool.Release(view);
```

**Performance Benefits:**

- No texture allocation/deallocation
- No camera creation/destruction
- No framebuffer recreation
- Instant show/hide vs create/destroy

---

### 4. View Groups

**Problem:** Managing related views individually is cumbersome.

**Solution:** Group views for bulk operations.

```cpp
class ViewGroup {
public:
    explicit ViewGroup(std::string_view name);

    void AddView(ViewId view_id);
    void RemoveView(ViewId view_id);

    // Bulk operations
    void ShowAll();
    void HideAll();
    void DestroyAll();
    void SetRenderGraph(std::shared_ptr<RenderGraphFactory> graph);

    [[nodiscard]] auto GetViews() const -> const std::vector<ViewId>&;
    [[nodiscard]] auto GetVisibleCount() const -> size_t;

private:
    std::string name_;
    std::vector<ViewId> views_;
};

// Usage
ViewGroup auxiliary_views("Auxiliary");
auxiliary_views.AddView(top_view);
auxiliary_views.AddView(side_view);
auxiliary_views.AddView(front_view);

// Hide all auxiliary views at once
auxiliary_views.HideAll();

// Switch all to wireframe
auxiliary_views.SetRenderGraph(RenderGraphs::Wireframe());
```

---

### 5. Advanced Compositing

**Current:** Views blit fullscreen or to fixed regions.

**Future Enhancements:**

#### A. Custom Composite Regions

```cpp
struct CompositeRegion {
    ViewPort viewport;          // Where to composite
    float opacity = 1.0f;       // Blend opacity
    graphics::BlendMode mode = graphics::BlendMode::kAlpha;
};

void SetViewCompositeRegion(ViewId view_id, CompositeRegion region);

// Example: Semi-transparent overlay
editor_module->SetViewCompositeRegion(overlay_view, {
    .viewport = {100, 100, 400, 300},
    .opacity = 0.7f,
    .mode = graphics::BlendMode::kAlpha
});
```

#### B. Custom Compositors

```cpp
class ICompositor {
public:
    virtual ~ICompositor() = default;

    virtual void Composite(
        graphics::CommandRecorder& recorder,
        graphics::Texture& source,
        graphics::Texture& target,
        const CompositeRegion& region) = 0;
};

// Built-in compositors
class BlitCompositor : public ICompositor { /* Simple copy */ };
class BlendCompositor : public ICompositor { /* Alpha blend */ };
class DownsampleCompositor : public ICompositor { /* With filtering */ };

// Custom compositor
class BorderCompositor : public ICompositor {
    void Composite(...) override {
        // Blit content
        // Draw border
    }
};

void SetViewCompositor(ViewId view_id,
                      std::unique_ptr<ICompositor> compositor);
```

#### C. Multi-Pass Compositing

```cpp
struct CompositePipeline {
    struct Stage {
        ViewId view_id;
        CompositeRegion region;
        std::unique_ptr<ICompositor> compositor;
    };

    std::vector<Stage> stages;
};

void SetSurfaceCompositePipeline(
    const graphics::Surface* surface,
    CompositePipeline pipeline);

// Example: Main + 2 PiPs + Debug overlay
CompositePipeline pipeline;
pipeline.stages.push_back({main_view, fullscreen_region, blit_compositor});
pipeline.stages.push_back({pip1_view, top_right_region, blend_compositor});
pipeline.stages.push_back({pip2_view, top_left_region, blend_compositor});
pipeline.stages.push_back({debug_view, bottom_region, overlay_compositor});
```

---

### 6. Camera Management

**Current:** Views automatically create their own cameras.

**Future Enhancements:**

#### A. External Camera Control

```cpp
struct CameraConfig {
    glm::vec3 position;
    glm::quat rotation;
    float fov = 60.0f;
    float near_plane = 0.1f;
    float far_plane = 10000.0f;
};

void SetViewCamera(ViewId view_id, const CameraConfig& config);
auto GetViewCamera(ViewId view_id) const -> CameraConfig;

// Usage
CameraConfig camera;
camera.position = {10, 5, 10};
camera.rotation = glm::quatLookAt(glm::normalize(-camera.position), {0,1,0});
camera.fov = 45.0f;
editor_module->SetViewCamera(view_id, camera);
```

#### B. Shared Cameras

```cpp
void SetViewCamera(ViewId view_id, scene::SceneNode camera_node);

// Multiple views sharing same camera
auto shared_camera = scene->CreateNode("SharedCamera");
// ... configure camera ...

editor_module->SetViewCamera(view1, shared_camera);
editor_module->SetViewCamera(view2, shared_camera);
// Both views render from same viewpoint (useful for split-screen)
```

#### C. Camera Controllers

```cpp
class ICameraController {
public:
    virtual ~ICameraController() = default;
    virtual void Update(float delta_time) = 0;
    virtual void OnInput(const InputEvent& event) = 0;
};

class OrbitCameraController : public ICameraController { /* ... */ };
class FlyCameraController : public ICameraController { /* ... */ };
class FixedCameraController : public ICameraController { /* ... */ };

void SetViewCameraController(ViewId view_id,
                             std::unique_ptr<ICameraController> controller);
```

---

### 7. Scene Filtering

**Problem:** Want views to render different subsets of the scene.

**Solution:** Per-view scene filters.

```cpp
class ISceneFilter {
public:
    virtual ~ISceneFilter() = default;
    virtual bool ShouldRender(const scene::SceneNode& node) const = 0;
};

class LayerFilter : public ISceneFilter {
public:
    explicit LayerFilter(uint32_t layer_mask);
    bool ShouldRender(const scene::SceneNode& node) const override;
};

class TagFilter : public ISceneFilter {
public:
    explicit TagFilter(std::vector<std::string> tags);
    bool ShouldRender(const scene::SceneNode& node) const override;
};

void SetViewSceneFilter(ViewId view_id,
                       std::unique_ptr<ISceneFilter> filter);

// Example: View only renders "UI" layer
auto ui_filter = std::make_unique<LayerFilter>(LayerMask::kUI);
editor_module->SetViewSceneFilter(ui_view, std::move(ui_filter));
```

---

### 8. Performance Monitoring

**Problem:** Need to track per-view rendering performance.

**Solution:** Per-view metrics.

```cpp
struct ViewMetrics {
    float frame_time_ms;
    uint32_t draw_call_count;
    uint32_t triangle_count;
    uint64_t gpu_memory_bytes;
    std::chrono::steady_clock::time_point last_update;
};

auto GetViewMetrics(ViewId view_id) const -> ViewMetrics;
auto GetAllViewMetrics() const -> std::unordered_map<ViewId, ViewMetrics>;

// Usage
auto metrics = editor_module->GetViewMetrics(main_view);
LOG_F(INFO, "View frame time: {:.2f}ms, draws: {}, tris: {}",
      metrics.frame_time_ms,
      metrics.draw_call_count,
      metrics.triangle_count);
```

---

### 9. Async Resource Loading

**Problem:** Creating views with large scenes causes frame hitches.

**Solution:** Async view initialization.

```cpp
auto CreateViewAsync(EditorView::Config config)
    -> std::future<ViewId>;

auto CreateViewAsync(EditorView::Config config,
                    std::function<void(ViewId)> on_complete)
    -> void;

// Usage with future
auto future = editor_module->CreateViewAsync(config);
// Do other work...
auto view_id = future.get(); // Blocks until ready

// Usage with callback
editor_module->CreateViewAsync(config, [](ViewId view_id) {
    // View fully initialized
    editor_module->AttachViewToSurface(view_id, surface);
});
```

---

### 10. Texture Capture

**Problem:** Need to capture view output for thumbnails, screenshots, etc.

**Solution:** View texture access.

```cpp
auto GetViewColorTexture(ViewId view_id) const
    -> std::shared_ptr<graphics::Texture>;

auto CaptureView(ViewId view_id,
                std::function<void(const graphics::Texture&)> callback)
    -> void;

// Usage
auto texture = editor_module->GetViewColorTexture(view_id);
SaveTextureToFile(texture, "screenshot.png");

// Async capture (waits for GPU)
editor_module->CaptureView(view_id, [](const graphics::Texture& tex) {
    SaveTextureToFile(tex, "thumbnail.png");
});
```

---

## Implementation Roadmap

### Phase 1: Core Refactor (Current Design)

**Timeline:** Sprint 1-2

- ✅ EditorView class
- ✅ ViewRenderer class
- ✅ ViewManager class
- ✅ Basic view lifecycle
- ✅ Surface attachment
- ✅ Simple render graph support

**Deliverable:** Multi-surface rendering works correctly.

---

### Phase 2: Polish & Stability

**Timeline:** Sprint 3-4

- ⬜ Comprehensive error handling
- ⬜ Metrics/profiling hooks
- ⬜ Resize handling refinement
- ⬜ Memory leak auditing
- ⬜ Performance testing (10+ views)

**Deliverable:** Production-ready stability.

---

### Phase 3: Render Graph Library

**Timeline:** Sprint 5-6

- ⬜ Built-in render graph factories
  - Solid shaded
  - Wireframe
  - Unlit
  - Debug (normals, UVs, etc.)
- ⬜ Parameterized graphs
- ⬜ Graph composition

**Deliverable:** Easy switching between render modes.

---

### Phase 4: Advanced Compositing

**Timeline:** Sprint 7-8

- ⬜ Custom composite regions
- ⬜ Opacity/blend modes
- ⬜ Custom compositor interface
- ⬜ Multi-pass compositing

**Deliverable:** Flexible view layout and blending.

---

### Phase 5: View Templates & Pooling

**Timeline:** Sprint 9-10

- ⬜ Template registration system
- ⬜ View pooling
- ⬜ View groups

**Deliverable:** Fast view creation, bulk operations.

---

### Phase 6: Camera Enhancements

**Timeline:** Sprint 11-12

- ⬜ External camera control API
- ⬜ Shared cameras
- ⬜ Camera controllers
- ⬜ Camera transition animations

**Deliverable:** Full camera control from editor.

---

### Phase 7: Scene Filtering

**Timeline:** Sprint 13-14

- ⬜ Layer-based filtering
- ⬜ Tag-based filtering
- ⬜ Custom filter interface
- ⬜ Filter composition

**Deliverable:** Views render scene subsets.

---

### Phase 8: Advanced Features

**Timeline:** Sprint 15+

- ⬜ Texture capture API
- ⬜ Async view creation
- ⬜ Post-process stack
- ⬜ Per-view metrics

**Deliverable:** Production-complete feature set.

---

## Extension Best Practices

### 1. Prefer Composition Over Inheritance

**Bad:**

```cpp
class MyEditorView : public EditorView {
    // Tightly coupled, hard to maintain
};
```

**Good:**

```cpp
class MyViewExtension {
    ViewId view_id_;
    // Loosely coupled via ViewId
};
```

### 2. Use Interfaces for Extensibility

```cpp
class IRenderGraphFactory {
public:
    virtual ~IRenderGraphFactory() = default;
    virtual auto Create() -> RenderGraphFactory = 0;
};

// Easy to add new graph types without modifying EditorModule
```

### 3. Favor Data Over Code

```cpp
// Bad - hard-coded logic
if (view_type == ViewType::kWireframe) {
    // Special wireframe logic
} else if (view_type == ViewType::kDebug) {
    // Special debug logic
}

// Good - data-driven
struct ViewProfile {
    std::shared_ptr<RenderGraphFactory> graph;
    CameraConfig camera;
    CompositeRegion region;
};

std::unordered_map<std::string, ViewProfile> profiles_;
auto profile = profiles_[profile_name];
```

### 4. Provide Extension Points

```cpp
class EditorView {
protected:
    // Hook for derived classes
    virtual void OnPreRenderCustom() {}
    virtual void OnCompositeCustom(CommandRecorder&, Texture&) {}
};
```

### 5. Document Extension Points

```cpp
/// Extension point: Override to customize view initialization.
/// Called once when view transitions from kCreating to kReady.
/// Default implementation creates a perspective camera at origin.
virtual void OnInitialize(scene::Scene& scene);
```

---

## Testing Strategy for Extensions

### Unit Tests

```cpp
TEST(ViewPoolTests, AcquireRelease) {
    EditorModule module(...);
    ViewPool pool(module, 5);

    auto view1 = pool.Acquire("test");
    EXPECT_NE(view1, kInvalidViewId);

    pool.Release(view1);

    auto view2 = pool.Acquire("test");
    EXPECT_EQ(view1, view2); // Reused
}
```

### Integration Tests

```cpp
TEST(RenderGraphTests, CustomGraphExecution) {
    auto custom_graph = std::make_shared<TestRenderGraph>();
    editor_module->SetViewRenderGraph(view_id, custom_graph);

    // Advance one frame
    engine->RunFrame();

    EXPECT_TRUE(custom_graph->WasExecuted());
}
```

### Performance Tests

```cpp
BENCHMARK(ViewPoolPerformance) {
    ViewPool pool(editor_module, 100);
    for (int i = 0; i < 1000; ++i) {
        auto view = pool.Acquire("bench");
        pool.Release(view);
    }
}
// Target: <1ms for 1000 acquire/release cycles
```

---

## Conclusion

The refactored EditorModule architecture provides multiple well-defined extension points:

✅ **Render Graphs** - Custom rendering logic per view
✅ **Compositors** - Custom compositing strategies
✅ **Cameras** - External camera control and controllers
✅ **Filters** - Scene subset rendering
✅ **Templates** - Pre-configured view patterns
✅ **Pooling** - Resource reuse for performance

Each extension point is designed to be:

- **Non-intrusive** - Doesn't require modifying core classes
- **Type-safe** - Strong types prevent misuse
- **Testable** - Can be unit tested independently
- **Composable** - Extensions combine cleanly

The roadmap provides a clear path from the current monolithic design to a fully extensible, production-ready editor runtime.
