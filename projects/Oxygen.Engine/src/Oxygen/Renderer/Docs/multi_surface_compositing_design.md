# Multi-Surface View Compositing Design

## 1. Objective
Enable the Oxygen Engine renderer to seamlessly composite arbitrary layers (views) onto multiple concurrent presentation surfaces (e.g., Editor Viewports) and allow decoupled modules (e.g., Minimaps, floating UI) to anchor their composition outputs to specific surfaces or existing views. This must be achieved natively by extending the existing `CompositionView`, `CompositionPlanner`, and `CompositingTask` contracts without introducing parallel routing systems.

## 2. Problem Statement
The current compositing phase (`kCompositing`) exhibits a single-surface bottleneck:
*   The `Renderer` stores a singular `std::optional<CompositionSubmission> composition_submission_` and `composition_surface_`.
*   A `CompositionSubmission` strictly defines a single `composite_target` (a `Framebuffer` pointer).
*   If an independent module's Pipeline intends to composite a view onto the Main Window (e.g., a minimap), it lacks the local context to resolve the target `Framebuffer`.
*   If the Editor module spawns multiple OS windows, registering their submissions to the `Renderer` will silently clobber each other's intent.

## 3. Core Architectural Changes

To resolve these holes natively, **routing intent is shifted from the monolithic `CompositionSubmission` down to the atomic `CompositingTask`**. This empowers the `Renderer` to act as an intelligent, late-stage compositor that automatically groups tasks by their physical destination framebuffer.

### 3.1 Upgrading Surface Registration in FrameContext

To provide stable handles for isolated surfaces, `FrameContext` must migrate from index-based surface tracking to a strongly-typed `SurfaceId` system.

**Module:** `src/Oxygen/Core/FrameContext.h`

```cpp
// Change AddSurface to return the strong ID
OXGN_CORE_API auto RegisterSurface(observer_ptr<graphics::Surface> s) noexcept -> SurfaceId;

// Deprecate index-based removal / tagging in favor of stable IDs
OXGN_CORE_API auto UnregisterSurface(SurfaceId id) noexcept -> void;
OXGN_CORE_API auto SetSurfacePresentable(SurfaceId id, bool presentable) noexcept -> void;

// Allow resolving an ID back to a Surface pointer for the Renderer
OXGN_CORE_API auto GetSurface(SurfaceId id) const noexcept -> observer_ptr<graphics::Surface>;
```

### 3.2 Establishing `CompositionTarget`
We introduce an explicit, serializable target type to define *where* a composition operation should land, utilizing the strong IDs defined globally.

**Module:** `src/Oxygen/Renderer/Types/CompositionTarget.h`
```cpp
#include <variant>
#include <Oxygen/Core/FrameContext.h> // For SurfaceId
#include <Oxygen/Core/Types/View.h>   // For ViewId

namespace oxygen::renderer { // or engine

struct CompositionTarget {
  enum class Type : uint8_t {
    kDefault,           // Inherits the context's default surface (Backwards compatibility)
    kPrimarySurface,    // Targets the engine's primary surface (context.GetSurfaces().front())
    kSpecificSurface,   // Targets an explicit surface by SurfaceId
    kRelatedView        // Targets the same underlying surface as an anchor ViewId
  };

  Type type { Type::kDefault };
  std::variant<std::monostate, SurfaceId, ViewId> target_id { std::monostate{} };

  static auto ToDefault() -> CompositionTarget { return {Type::kDefault, std::monostate{}}; }
  static auto ToPrimary() -> CompositionTarget { return {Type::kPrimarySurface, std::monostate{}}; }
  static auto ToSurface(SurfaceId surface_id) -> CompositionTarget { return {Type::kSpecificSurface, surface_id}; }
  static auto ToView(ViewId view_id) -> CompositionTarget { return {Type::kRelatedView, view_id}; }
};

} // namespace oxygen::renderer
```

### 3.3 Enhancing the View and Task Data Contracts

**`src/Oxygen/Renderer/Pipeline/CompositionView.h`**:
The front-end view contract must allow developers to express target intent.
```cpp
struct CompositionView {
  // ... existing fields ...

  //! Defines where this view will be composited. Default inherits standard routing.
  CompositionTarget target { CompositionTarget::ToDefault() };

  // All static layout factories (ForScene, ForPip, ForHud, etc.) are augmented
  // to accept a CompositionTarget argument, defaulting to ToDefault().
};
```

**`src/Oxygen/Renderer/Types/CompositingTask.h`**:
The backend task execution contract carries the intent downstream.
```cpp
struct CompositingTask {
  CompositingTaskType type { CompositingTaskType::kCopy };

  //! Determines the destination for this specific task
  CompositionTarget target { CompositionTarget::ToDefault() };

  // ... existing union variants (copy, blend, texture_blend, taa) ...

  [[nodiscard]] static auto MakeCopy(ViewId view_id, ViewPort viewport,
                                     CompositionTarget target = CompositionTarget::ToDefault()) -> CompositingTask;
  // ... (Identical augmentations for MakeBlend and MakeTextureBlend)
};
```

### 3.4 Enhancing the `CompositionPlanner`
The planner acts purely as a translational bridge, avoiding hardcoded `Framebuffer` coupling and keeping pipeline logic pristine.

**`src/Oxygen/Renderer/Pipeline/Internal/CompositionPlanner.cpp`**:
Inside `PlanCompositingTasks`, the Target intent is forwarded natively:
```cpp
void CompositionPlanner::PlanCompositingTasks() {
  // ...
  for (const auto& packet : frame_view_packets) {
    if (!packet.HasCompositeTexture()) continue;

    planned_composition_tasks.push_back(
      oxygen::engine::CompositingTask::MakeTextureBlend(
        packet.GetCompositeTexture(), packet.GetCompositeViewport(), packet.GetCompositeOpacity(),
        packet.GetCompositionTarget() // Extracted from FrameViewPacket / CompositionView
      )
    );
  }
}
```

The resulting `CompositionSubmission` retains its `composite_target` strictly as the fallback used for `kDefault` routing:
```cpp
auto CompositionPlanner::BuildCompositionSubmission(std::shared_ptr<graphics::Framebuffer> default_output)
  -> oxygen::engine::CompositionSubmission {
    engine::CompositionSubmission submission;
    submission.composite_target = std::move(default_output); // Provides context for kDefault tasks
    submission.tasks = planned_composition_tasks;
    return submission;
}
```

### 3.5 Upgrading the `Renderer` to a Global Compositor

The `Renderer` strips its scalar bottleneck and handles grouped collation across *all* active modules/pipelines during `OnCompositing`.

**`src/Oxygen/Renderer/Renderer.h`**:
```cpp
struct PendingComposition {
  CompositionSubmission submission;
  std::shared_ptr<graphics::Surface> default_surface;
};

std::mutex composition_mutex_;
std::vector<PendingComposition> pending_compositions_;
```

**`src/Oxygen/Renderer/Renderer.cpp` (`RegisterComposition`)**:
```cpp
auto Renderer::RegisterComposition(CompositionSubmission submission, std::shared_ptr<graphics::Surface> target_surface) -> void {
  std::lock_guard lock(composition_mutex_);
  // Support concurrent submissions from multi-viewport Editors or sibling Modules
  pending_compositions_.push_back({std::move(submission), std::move(target_surface)});
}
```

**`src/Oxygen/Renderer/Renderer.cpp` (`OnCompositing`)**:
The pipeline resolves layout globally, allowing independent modules to "piggy-back" on other framebuffers safely:
1.  **Extract:** Sweep `pending_compositions_` locally and clear the container.
2.  **Relational Binding:** Build lookup tables mapping active `ViewId`s to their known host `Framebuffer`s based on tasks marked `kDefault`.
3.  **Target Resolution:** Iterate over *all* `CompositingTask`s. Switch on `task.target.type`:
    *   `kDefault`: Binds to its native submission's `default_surface`/`composite_target`.
    *   `kPrimarySurface`: Queries `FrameContext` for `.front()` active surface.
    *   `kSpecificSurface`: Queries `FrameContext::GetSurface(std::get<SurfaceId>(task.target.target_id))` directly for stable resolution.
    *   `kRelatedView`: Queries relational binding table to map directly to the `Framebuffer` of the anchor `ViewId`.
4.  **Grouping:** Group resolved tasks by `graphics::Framebuffer*`.
5.  **Execution:** For each physical Framebuffer:
    *   Sort tasks by strict Z-Order.
    *   Call `TrackCompositionFramebuffer(...)`.
    *   Build and execute drawing commands for the collated task list.
    *   Mark mapped surfaces presentable in `FrameContext`.

## 4. No Holes Guarantee Validation

*   **Editor Multi-Viewport Capability**: A standalone editor module spawns an OS window, retrieves a `Surface` with a unique ID, and creates a `CompositionView` with `CompositionTarget::ToSurface(id)`. This routes cleanly past default submissions directly to the correct Swapchain backbuffer.
*   **Independent Module Adhesion (e.g., Minimaps)**: A minimap module executes its own render graph with `CompositionTarget::ToView(MainView)`. Its pipeline cannot determine the actual `Framebuffer`, yielding a null default output. However, because target resolution is deferred to the global `Renderer::OnCompositing` scope, the `Renderer` seamlessly queries the MainView's binding and injects the Minimap task securely into the Main Window's pass.
*   **Backward Compatibility**: All existing pipeline code emitting `CompositionTarget::ToDefault()` natively maps to the singular legacy code path without side effects.
