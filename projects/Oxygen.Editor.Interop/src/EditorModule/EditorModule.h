//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once
#pragma managed(push, off)

#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <mutex>
#include <vector>

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Core/EngineModule.h>

// Forward declarations to avoid heavy includes in header when possible
namespace oxygen {
  class Graphics;

  namespace engine {
    class FrameContext; // used as reference in method signatures
    class InputSystem;
  } // namespace engine

  namespace scene {
    class Scene;
  } // namespace scene

  namespace content {
    class AssetLoader;
    class VirtualPathResolver;
  }

  // AsyncEngine lives in the root oxygen namespace
  class AsyncEngine;

} // namespace oxygen

namespace oxygen::interop::module {
  class EditorViewportNavigation;
  class SurfaceRegistry;
  class EditorCommand;
  class EditorCompositor;
} // namespace oxygen::interop::module

#include "EditorModule/InputAccumulator.h"
#include "EditorModule/ThreadSafeQueue.h"
#include "EditorModule/ViewManager.h"

namespace oxygen::interop::module {

  class InputAccumulatorAdapter;
  class RenderGraph;

  //! An engine module, that connects the editor to the Oxygen engine.
  /*!
   Because this is an engine modules, it is fully aware of the frame lifecycle,
   can execute certain actions on the engine thread and exactly at a specific
   phase. Avoids the needs to expose lower level primitives from the engine to
   do frame synchronization.

   Consitently with the Oxygen engine architecture, this module acts as an
   application module, owning the application specific logic and data, and the
   surfaces used for rendering and presentation.

   @note
   In this particular implementation, surface/swapchain management is delegated
   to a `SurfaceRegistry` instance, which acts as a thread-safe surface manager,
   with lazy creation, deferred destruction and reuse of surfaces between
   multiple viewports as needed. The module is still howver, the single point of
   contact between the editor and the engine when it comes to surface lifecycle.
  */
  class EditorModule final : public oxygen::engine::EngineModule {
    OXYGEN_TYPED(EditorModule)
  public:
    //! Constructs the editor module with the provided surface registry, which
    //! must not be `null`.
    /*!
     @throws std::invalid_argument if `registry` is `null`.
    */
    explicit EditorModule(std::shared_ptr<SurfaceRegistry> registry);

    ~EditorModule() override;

    [[nodiscard]] auto GetName() const noexcept -> std::string_view override {
      return "EditorModule";
    }

    [[nodiscard]] auto GetPriority() const noexcept
      -> oxygen::engine::ModulePriority override {
      return oxygen::engine::kModulePriorityHighest;
    }

    [[nodiscard]] auto GetSupportedPhases() const noexcept
      -> oxygen::engine::ModulePhaseMask override {
      return oxygen::engine::MakeModuleMask<
        oxygen::core::PhaseId::kFrameStart, oxygen::core::PhaseId::kPreRender,
        oxygen::core::PhaseId::kRender, oxygen::core::PhaseId::kCompositing,
        oxygen::core::PhaseId::kSceneMutation>();
    }

    auto OnAttached(oxygen::observer_ptr<oxygen::AsyncEngine> engine) noexcept
      -> bool override;
    auto OnFrameStart(oxygen::engine::FrameContext& context) -> void override;
    auto OnSceneMutation(oxygen::engine::FrameContext& context)
      -> oxygen::co::Co<> override;
    auto OnPreRender(oxygen::engine::FrameContext& context)
      -> oxygen::co::Co<> override;
    auto OnRender(oxygen::engine::FrameContext& context)
      -> oxygen::co::Co<> override;
    auto OnCompositing(oxygen::engine::FrameContext& context)
      -> oxygen::co::Co<> override;

    // Ensure framebuffers for all registered surfaces (creates depth textures
    // and one framebuffer per backbuffer slot). Mirrors
    // AppWindow::EnsureFramebuffers from the examples so editor behavior matches
    // the sample exactly.
    auto EnsureFramebuffers() -> bool;

    // Scene management API
    // Create scene and invoke optional completion callback on the engine thread
    auto CreateScene(std::string_view name, std::function<void(bool)> onComplete)
      -> void;
    void ApplyCreateScene(std::string_view name);

    // Request scene destruction (thread-safe; enqueued to engine thread)
    void DestroyScene();
    void ApplyDestroyScene();

    //! Enqueues a command to be executed during the SceneMutation phase.
    void Enqueue(std::unique_ptr<EditorCommand> cmd);

    // Async view creation (exposed for interop layer)
    void CreateViewAsync(EditorView::Config config,
      ViewManager::OnViewCreated callback);

    // Access to module-owned InputAccumulator for interop clients. Returns a
    // non-owning pointer; lifetime is managed by the EditorModule instance.
    auto GetInputAccumulator() noexcept -> auto& { return *input_accumulator_; }

    // Destroy a previously created view. This forwards to the ViewManager
    // and is safe to call from interop. If the view id is invalid this is
    // a no-op.
    void DestroyView(ViewId view_id);
    // Visibility helpers - EditorModule acts as the choreographer for view
    // visibility changes (keeps the ViewManager API surface unchanged).
    void ShowView(ViewId view_id);
    void HideView(ViewId view_id);

    //! Set the camera view preset for a specific view.
    void SetViewCameraPreset(ViewId view_id, CameraViewPreset preset);

    //! Adds a loose cooked root to the virtual path resolver.
    void AddLooseCookedRoot(std::string_view path);

    //! Clears all mounted roots in the virtual path resolver.
    void ClearCookedRoots();

  private:
    struct SubscriptionToken;

    void UpdateViewRoutingFromInputBatch(ViewId view_id,
      const AccumulatedInput& batch) noexcept;

    void ProcessSurfaceRegistrations();
    void ProcessSurfaceDestructions();
    auto ProcessResizeRequests()
      -> std::vector<std::shared_ptr<oxygen::graphics::Surface>>;
    auto SyncSurfacesWithFrameContext(
      oxygen::engine::FrameContext& context,
      const std::vector<std::shared_ptr<oxygen::graphics::Surface>>& surfaces)
      -> void;

    auto InitInputBindings(oxygen::engine::InputSystem& input_system) noexcept
      -> bool;

    std::shared_ptr<SurfaceRegistry> registry_;
    std::weak_ptr<oxygen::Graphics> graphics_;
    oxygen::observer_ptr<oxygen::AsyncEngine> engine_{};

    std::shared_ptr<oxygen::scene::Scene> scene_;
    oxygen::observer_ptr<oxygen::content::AssetLoader> asset_loader_{};
    std::unique_ptr<oxygen::content::VirtualPathResolver> path_resolver_;

    // Roots management for thread-safe AssetLoader initialization
    std::mutex roots_mutex_;
    std::vector<std::string> mounted_roots_;
    std::atomic<bool> roots_dirty_{ false };

    std::chrono::steady_clock::time_point last_frame_time_{};

    // Command queue for scene mutations
    ThreadSafeQueue<std::unique_ptr<EditorCommand>> command_queue_;

    // New Architecture Components
    std::unique_ptr<ViewManager> view_manager_;
    std::unique_ptr<EditorCompositor> compositor_;
    std::unique_ptr<InputAccumulator> input_accumulator_;
    std::unique_ptr<InputAccumulatorAdapter> input_accumulator_adapter_;

    // Viewport navigation is composed from small, independent features.
    std::unique_ptr<EditorViewportNavigation> viewport_navigation_;

    // View routing: input is produced per view (window), but the current
    // InputSystem snapshot is global. We route editor navigation explicitly
    // using these ids:
    // - active_view_id_: keyboard and drag navigation (focused viewport)
    // - hover_view_id_: wheel navigation (last-hovered viewport)
    ViewId active_view_id_{ kInvalidViewId };
    ViewId hover_view_id_{ kInvalidViewId };

    // Input actions / mapping contexts (Phase 1: navigation only)
    bool input_bindings_initialized_ { false };
    std::unique_ptr<SubscriptionToken> input_system_subscription_token_ {};
  };

} // namespace oxygen::interop::module

#pragma managed(pop)
