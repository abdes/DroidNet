//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once
#pragma managed(push, off)

#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

#include <glm/vec3.hpp>

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Core/EngineModule.h>
#include <Oxygen/Vortex/Types/GroundGridConfig.h>
#include <Oxygen/Vortex/Types/ResidentContentRevision.h>
#include <Oxygen/Vortex/Types/ViewOutline.h>
#include <Oxygen/Vortex/Types/ViewPick.h>

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
    class IAssetLoader;
    class VirtualPathResolver;
  }
  class IAsyncEngine;

  // AsyncEngine lives in the root oxygen namespace
  class AsyncEngine;

} // namespace oxygen

namespace oxygen::interop::module {
  class EditorViewportNavigation;
  class SurfaceRegistry;
  class EditorCommand;
  struct CommandContext;
  class SceneAssetRequests;
  class SurfaceFramebuffers;
} // namespace oxygen::interop::module

#include "EditorModule/InputAccumulator.h"
#include "EditorModule/PaneRenderPolicy.h"
#include "EditorModule/SceneHelperController.h"
#include "EditorModule/ThreadSafeQueue.h"
#include "EditorModule/TransformGizmoController.h"
#include "EditorModule/ViewManager.h"

namespace oxygen::interop::module {

  class InputAccumulatorAdapter;

  //! One accepted physical source and its optional project-owned logical mount.
  struct CookedRootBinding {
    std::filesystem::path path {};
    std::optional<std::wstring> project_mount {};
  };

  using CookedRootSet = std::shared_ptr<const std::vector<CookedRootBinding>>;

  //! A scene node with visible pixels in a picked rectangle.
  struct EditorPickHit {
    UuidKey node {};
    //! Device depth of the node's nearest pixel.
    float depth { 0.0F };
    //! Geometry slot (submesh) under the pixel closest to the centre.
    std::uint32_t geometry_slot { 0U };
    //! Pixels from the rectangle centre to the node's closest pixel.
    float center_distance { 0.0F };
  };

  //! Nodes found by a viewport pick, closest to the rectangle centre first.
  struct EditorPickResult {
    std::vector<EditorPickHit> hits;
    //! World position under the first hit's chosen pixel.
    std::optional<glm::vec3> world_position;
  };

  //! The rate and duration of the last completed engine frame.
  struct EditorFrameStatistics {
    float frames_per_second { 0.0F };
    float frame_time_ms { 0.0F };
  };

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
        oxygen::core::PhaseId::kPublishViews, oxygen::core::PhaseId::kRender,
        oxygen::core::PhaseId::kCompositing,
        oxygen::core::PhaseId::kSceneMutation>();
    }

    auto OnAttached(oxygen::observer_ptr<oxygen::IAsyncEngine> engine) noexcept
      -> bool override;
    auto OnFrameStart(oxygen::observer_ptr<oxygen::engine::FrameContext> context) -> void override;
    auto OnSceneMutation(oxygen::observer_ptr<oxygen::engine::FrameContext> context)
      -> oxygen::co::Co<> override;
    auto OnPreRender(oxygen::observer_ptr<oxygen::engine::FrameContext> context)
      -> oxygen::co::Co<> override;
    auto OnPublishViews(
      oxygen::observer_ptr<oxygen::engine::FrameContext> context)
      -> oxygen::co::Co<> override;
    auto OnRender(oxygen::observer_ptr<oxygen::engine::FrameContext> context)
      -> oxygen::co::Co<> override;
    auto OnCompositing(oxygen::observer_ptr<oxygen::engine::FrameContext> context)
      -> oxygen::co::Co<> override;

    // Scene management API
    // Create scene and invoke optional completion callback on the engine thread
    auto CreateScene(std::string_view name,
      std::function<void(bool, std::string)> onComplete)
      -> void;
    void ApplyCreateScene(std::string_view name);

    // Request scene destruction (thread-safe; enqueued to engine thread)
    void DestroyScene();
    void ApplyDestroyScene(
      bool destroy_views = true,
      oxygen::engine::FrameContext* frame_context = nullptr);

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

    //! Renders a view through the authored camera on a scene node, or through
    //! its editor camera when `camera_node_id` is empty.
    void SetViewSceneCamera(ViewId view_id,
      std::optional<UuidKey> camera_node_id);

    //! Starts or stops piloting the scene camera a view looks through.
    void SetViewScenePilot(ViewId view_id, bool pilot);

    //! Reports, on the engine thread, the pose that would place a scene node
    //! at a view's editor camera; no value when the view or node is missing.
    void QueryViewCameraPose(ViewId view_id, UuidKey node_id,
      std::function<void(std::optional<EditorCameraPose>)> callback);

    //! Reports, on the engine thread, a view's editor camera state; no value
    //! when the view is missing.
    void QueryViewEditorCamera(ViewId view_id,
      std::function<void(std::optional<EditorCameraState>)> callback);

    //! Set the editor camera navigation mode for a specific view.
    void SetViewCameraControlMode(
      ViewId view_id,
      EditorViewportCameraControlMode mode);

    void SetViewCameraMovementSpeed(
      ViewId view_id,
      float speed_units_per_second);

    //! Sets how a specific view presents the scene.
    void SetViewRenderOptions(
      ViewId view_id, const EditorViewRenderOptions& options);

    //! Picks the scene nodes with visible geometry or a light or camera icon
    //! inside a rectangle of a view, in pixels of the view's surface.
    /*!
     The callback runs exactly once, on the engine thread a few frames later:
     with the hits, or with no value when the view is not rendering, or when
     the scene was replaced before the result arrived.
    */
    void PickView(ViewId view_id, vortex::ViewPickRect rect,
      std::function<void(std::optional<EditorPickResult>)> callback);

    //! Frames scene nodes, or the whole scene when `nodes` is empty, in one
    //! view. The callback runs exactly once, on the engine thread.
    void FrameView(ViewId view_id, std::vector<UuidKey> nodes,
      std::function<void(EditorFramingOutcome)> callback);

    //! Outlines the selected scene nodes, and their descendants, in every
    //! editing view that shows the selection outline; `active` is drawn
    //! brighter. Callable from any thread.
    void SetSelectionOutline(
      std::vector<UuidKey> nodes, std::optional<UuidKey> active);

    //! Renders every visible pane each frame instead of only the panes whose
    //! content may have changed. Callable from any thread.
    void SetAlwaysRenderPanes(bool always_render) noexcept;

    //! Sets how every view draws the ground grid; applied at the next scene
    //! mutation phase, and every pane renders again.
    void SetGroundGridConfig(const vortex::GroundGridConfig& config);

    //! The selection's transform gizmo: its settings and event listener are
    //! callable from any thread.
    [[nodiscard]] auto GetTransformGizmo() noexcept -> TransformGizmoController&
    {
      return transform_gizmo_;
    }

    //! Light and camera icons, selected helpers and the orientation triad:
    //! their settings and event listener are callable from any thread.
    [[nodiscard]] auto GetSceneHelpers() noexcept -> SceneHelperController&
    {
      return scene_helpers_;
    }

    //! The last completed frame's rate and duration, readable from any thread.
    [[nodiscard]] auto GetFrameStatistics() const noexcept
      -> EditorFrameStatistics;

    void SetViewCameraSettings(
      ViewId view_id,
      float field_of_view_y_radians,
      float near_plane,
      float far_plane);

    //! Replace the complete loose-root set as one engine-thread refresh request.
    void ReplaceCookedRoots(std::vector<CookedRootBinding> roots,
      std::function<void(bool, std::string)> complete);

    void SetCookedContentPaused(bool paused,
      std::function<void(bool, std::string)> complete);

  private:
    struct SubscriptionToken;

    //! A visible pane that can render this frame, and whether it will.
    struct PaneCandidate {
      EditorView* view { nullptr };
      PaneFingerprint fingerprint {};
      bool render { false };
    };

    //! Runs a command, logging failures, and records what it invalidates.
    void RunCommand(EditorCommand& command, CommandContext& context,
      std::string_view label);
    //! The panes that can publish this frame; withdraws the others.
    auto CollectPanes(oxygen::engine::FrameContext& context,
      vortex::Renderer& renderer, oxygen::Graphics& graphics)
      -> std::vector<PaneCandidate>;
    //! Marks which panes render: a surface renders all its panes or none.
    void DecidePaneRenders(
      vortex::Renderer& renderer, std::vector<PaneCandidate>& panes);
    //! Publishes a pane for rendering; false when the renderer rejects it.
    auto PublishPane(oxygen::engine::FrameContext& context,
      vortex::Renderer& renderer, EditorView& view,
      const std::shared_ptr<const vortex::ViewOutline>& outline) -> bool;
    //! Makes every pane composed on the surface render this frame.
    void InvalidatePanesOn(const SurfaceRegistry::GuidKey& key);
    //! Withdraws a pane's publication so the renderer never prepares it
    //! without a camera.
    void WithdrawPane(oxygen::engine::FrameContext& context,
      vortex::Renderer& renderer, const EditorView& view,
      std::string_view reason);

    auto SynchronizeCookedRootsAsync() -> oxygen::co::Co<bool>;
    auto ProcessContentPauseAsync(oxygen::engine::FrameContext& frame_context) -> oxygen::co::Co<>;

    void UpdateViewRoutingFromInputBatch(ViewId view_id,
      const AccumulatedInput& batch) noexcept;
    void RemovePublishedRuntimeViewForIntent(
      oxygen::ViewId view_id,
      oxygen::engine::FrameContext* frame_context);

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

    struct PendingPick {
      ViewId view_id { kInvalidViewId };
      vortex::ViewPickRect rect {};
      std::function<void(std::optional<EditorPickResult>)> callback;
    };

    //! The selection outline for this frame's views, or null.
    auto BuildSelectionOutline() -> std::shared_ptr<const vortex::ViewOutline>;
    //! Hands queued picks to the renderer for the views published this frame;
    //! picks of other views complete with no value.
    void SubmitPendingPicks(vortex::Renderer& renderer,
      const std::unordered_set<ViewId>& published_views);
    //! Completes every queued pick with no value.
    void CancelPendingPicks() noexcept;

    std::shared_ptr<SurfaceRegistry> registry_;
    std::weak_ptr<oxygen::Graphics> graphics_;
    oxygen::observer_ptr<oxygen::IAsyncEngine> engine_{};

    std::shared_ptr<oxygen::scene::Scene> scene_;
    std::unique_ptr<SceneAssetRequests> asset_requests_;
    oxygen::observer_ptr<oxygen::content::IAssetLoader> asset_loader_{};
    std::unique_ptr<oxygen::content::VirtualPathResolver> path_resolver_;

    // Roots management for thread-safe AssetLoader initialization
    std::mutex roots_mutex_;
    CookedRootSet mounted_roots_;
    CookedRootSet active_roots_;
    std::function<void(bool, std::string)> pending_roots_completion_;
    std::function<void(bool, std::string)> active_roots_completion_;
    std::uint64_t roots_revision_ = 0;
    bool preview_paused_ = false;
    std::optional<bool> requested_content_pause_;
    std::function<void(bool, std::string)> content_pause_completion_;
    std::function<void(bool, std::string)> content_resume_completion_;
    std::atomic<bool> roots_dirty_{ false };

    std::chrono::steady_clock::time_point last_frame_time_{};
    //! The last completed frame's statistics, both floats packed in one word.
    std::atomic<std::uint64_t> frame_statistics_{ 0 };

    //! Changes whenever the authored scene is created or destroyed; a pick
    //! result for an earlier generation is stale. Shared with completions that
    //! may outlive the module.
    std::shared_ptr<std::atomic<std::uint64_t>> scene_generation_ {
      std::make_shared<std::atomic<std::uint64_t>>(0U)
    };

    std::mutex outline_mutex_;
    std::vector<UuidKey> outline_nodes_;
    std::optional<UuidKey> outline_active_;
    //! Advances with every selection outline change.
    std::atomic<std::uint64_t> outline_revision_ { 0U };
    std::uint64_t published_outline_revision_ { 0U };

    //! Which panes render; see PaneRenderPolicy. Engine thread only.
    PaneRenderPolicy pane_policy_;
    std::atomic<bool> always_render_panes_ { false };
    //! Set by anything that changes what every pane shows since the last
    //! publication: scene commands, completed loads, cooked content.
    bool scene_changed_ { true };
    std::optional<vortex::ResidentContentRevision> resident_revision_;
    //! Whether the scene has an enabled local fog volume; refreshed when the
    //! scene changes, so panes request local fog only when there is some.
    bool scene_has_local_fog_ { false };
    //! Panes kept without rendering this frame; their surfaces are neither
    //! composed nor presented.
    std::unordered_set<ViewId> held_views_;

    std::mutex picks_mutex_;
    std::vector<PendingPick> pending_picks_;

    TransformGizmoController transform_gizmo_;
    SceneHelperController scene_helpers_;

    // Command queue for scene mutations
    ThreadSafeQueue<std::unique_ptr<EditorCommand>> command_queue_;

    // New Architecture Components
    std::unique_ptr<ViewManager> view_manager_;
    std::unique_ptr<SurfaceFramebuffers> framebuffers_;
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
