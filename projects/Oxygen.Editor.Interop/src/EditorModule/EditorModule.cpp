//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma unmanaged

#include "pch.h"

#include <algorithm>
#include <bit>
#include <tuple>

#include <EditorModule/SceneAssetRequests.h>

#include <Commands/CreateSceneCommand.h>
#include <Commands/CreateViewCommand.h>
#include <Commands/DestroySceneCommand.h>
#include <Commands/DestroyViewCommand.h>
#include <Commands/FrameViewCommand.h>
#include <Commands/HideViewCommand.h>
#include <Commands/SetGroundGridConfigCommand.h>
#include <Commands/SetViewCameraControlModeCommand.h>
#include <Commands/SetViewCameraMovementSpeedCommand.h>
#include <Commands/SetViewCameraPresetCommand.h>
#include <Commands/SetViewCameraSettingsCommand.h>
#include <Commands/SetViewRenderOptionsCommand.h>
#include <Commands/OrthographicCameraPropertyApplier.h>
#include <Commands/QueryViewCameraPoseCommand.h>
#include <Commands/QueryViewEditorCameraCommand.h>
#include <Commands/SetViewSceneCameraCommand.h>
#include <Commands/SetViewScenePilotCommand.h>
#include <Commands/ShowViewCommand.h>
#include <EditorModule/EditorCommand.h>
#include <EditorModule/EditorModule.h>
#include <EditorModule/EditorViewportNavigation.h>
#include <EditorModule/InputAccumulatorAdapter.h>
#include <EditorModule/NodeRegistry.h>
#include <EditorModule/SurfaceFramebuffers.h>
#include <EditorModule/SurfaceRegistry.h>
#include <EditorModule/ViewportInset.h>

#include <Oxygen/Content/AssetLoader.h>
#include <Oxygen/Content/ContentMounts.h>
#include <Oxygen/Content/OperationCancelledException.h>
#include <Oxygen/Content/VirtualPathResolver.h>
#include <Oxygen/Engine/IAsyncEngine.h>
#include <Oxygen/OxCo/TaskCancelledException.h>
#include <Oxygen/OxCo/ThreadPool.h>
#include <Oxygen/Platform/Platform.h>
#include <Oxygen/Scene/Environment/Fog.h>
#include <Oxygen/Scene/Environment/LocalFogVolume.h>
#include <Oxygen/Scene/Environment/PostProcessVolume.h>
#include <Oxygen/Scene/Environment/SceneEnvironment.h>
#include <Oxygen/Scene/Environment/SkyAtmosphere.h>
#include <Oxygen/Scene/SceneTraversal.h>
#include <Oxygen/Scene/Types/Traversal.h>
#include <Oxygen/Vortex/Renderer.h>
#include <Oxygen/Vortex/SceneCameraViewResolver.h>

namespace oxygen::interop::module {

  using namespace oxygen;

  namespace {
    using SurfaceOperation = std::pair<
      SurfaceRegistry::GuidKey,
      std::pair<std::shared_ptr<graphics::Surface>, std::function<void(bool)>>>;

    void ReleaseSurfacesForShutdown(
      std::vector<SurfaceOperation>& surfaces,
      const std::shared_ptr<Graphics>& graphics,
      const bool callback_result)
    {
      for (auto& entry : surfaces) {
        auto& surface = entry.second.first;
        auto& callback = entry.second.second;

        if (surface) {
          try {
            if (graphics) {
              graphics->RegisterDeferredRelease(std::move(surface));
            }
            else {
              surface.reset();
            }
          }
          catch (...) {
            surface.reset();
          }
        }

        if (callback) {
          try {
            callback(callback_result);
          }
          catch (...) {
            /* swallow shutdown callback failures */
          }
        }
      }

      surfaces.clear();
    }

    //! Whether the scene's height fog renders in the main pass, the rule the
    //! runtime shell applies.
    auto IsHeightFogRequested(const scene::Scene* scene) -> bool {
      if (scene == nullptr || scene->GetEnvironment() == nullptr) {
        return false;
      }
      const auto fog
        = scene->GetEnvironment()->TryGetSystem<scene::environment::Fog>();
      return fog && fog->IsEnabled() && fog->GetRenderInMainPass();
    }

    //! Whether any enabled local fog volume exists; the renderer gathers the
    //! volumes only for views that request local fog.
    auto HasEnabledLocalFog(const scene::Scene& scene) -> bool {
      bool found = false;
      std::ignore = scene.Traverse().Traverse(
        [&found](const scene::ConstVisitedNode& visited, const bool dry_run) {
          if (dry_run || visited.node_impl == nullptr
            || !visited.node_impl
                  ->HasComponent<scene::environment::LocalFogVolume>()) {
            return scene::VisitResult::kContinue;
          }
          found = visited.node_impl
                    ->GetComponent<scene::environment::LocalFogVolume>()
                    .IsEnabled();
          return found ? scene::VisitResult::kStop
                       : scene::VisitResult::kContinue;
        });
      return found;
    }

    //! Maps a pane's presentation onto the view it publishes. An inset is a
    //! camera preview: it always renders lit, without the ground grid.
    void ApplyRenderOptions(
      vortex::CompositionView& view, const EditorView& editor_view) {
      using Mask = vortex::CompositionView::ViewFeatureMask;
      const auto options = editor_view.IsInset()
        ? EditorViewRenderOptions { .show_grid = false }
        : editor_view.GetConfig().render_options;
      if (!options.show_grid) {
        view.feature_mask.bits = vortex::CompositionView::ViewFeatureBits {
          view.feature_mask.bits.get() & ~Mask::kGroundGrid,
        };
      }

      auto& settings = view.render_settings;
      switch (options.view_mode) {
      case EditorViewMode::kLit:
        break;
      case EditorViewMode::kUnlit:
        settings.shader_debug_mode = vortex::ShaderDebugMode::kBaseColor;
        break;
      case EditorViewMode::kWireframe:
        settings.render_mode = vortex::RenderMode::kWireframe;
        break;
      case EditorViewMode::kLitWireframe:
        settings.render_mode = vortex::RenderMode::kOverlayWireframe;
        break;
      case EditorViewMode::kDirectLighting:
        settings.shader_debug_mode
          = vortex::ShaderDebugMode::kDirectLightingOnly;
        break;
      case EditorViewMode::kIndirectLighting:
        settings.shader_debug_mode = vortex::ShaderDebugMode::kIblOnly;
        break;
      case EditorViewMode::kWorldNormals:
        settings.shader_debug_mode = vortex::ShaderDebugMode::kWorldNormals;
        break;
      case EditorViewMode::kRoughness:
        settings.shader_debug_mode = vortex::ShaderDebugMode::kRoughness;
        break;
      case EditorViewMode::kMetalness:
        settings.shader_debug_mode = vortex::ShaderDebugMode::kMetalness;
        break;
      case EditorViewMode::kLinearDepth:
        settings.shader_debug_mode = vortex::ShaderDebugMode::kSceneDepthLinear;
        break;
      case EditorViewMode::kShadowMask:
        settings.shader_debug_mode
          = vortex::ShaderDebugMode::kDirectionalShadowMask;
        break;
      }
    }

    [[nodiscard]] auto PackFrameStatistics(
      const EditorFrameStatistics& statistics) noexcept -> std::uint64_t {
      return (static_cast<std::uint64_t>(
                std::bit_cast<std::uint32_t>(statistics.frames_per_second))
               << 32U)
        | std::bit_cast<std::uint32_t>(statistics.frame_time_ms);
    }

    [[nodiscard]] auto UnpackFrameStatistics(const std::uint64_t packed) noexcept
      -> EditorFrameStatistics {
      return EditorFrameStatistics {
        .frames_per_second
        = std::bit_cast<float>(static_cast<std::uint32_t>(packed >> 32U)),
        .frame_time_ms
        = std::bit_cast<float>(static_cast<std::uint32_t>(packed)),
      };
    }

    class EditorInputWriter final : public IInputWriter {
    public:
      explicit EditorInputWriter(
        co::BroadcastChannel<platform::InputEvent>::Writer& writer)
        : writer_(writer) {
      }

      void WriteMouseMove(ViewId view, SubPixelMotion delta,
        SubPixelPosition position) override {
        const auto now =
          oxygen::time::PhysicalTime{ std::chrono::steady_clock::now() };
        if (!writer_.TrySend(std::make_shared<platform::MouseMotionEvent>(
          now, static_cast<platform::WindowIdType>(view.get()),
          position, delta))) {
          LOG_F(ERROR, "Failed to send MouseMotionEvent for view {}", view.get());
        }
      }

      void WriteMouseWheel(ViewId view, SubPixelMotion delta,
        SubPixelPosition position) override {
        const auto now =
          oxygen::time::PhysicalTime{ std::chrono::steady_clock::now() };
        if (!writer_.TrySend(std::make_shared<platform::MouseWheelEvent>(
          now, static_cast<platform::WindowIdType>(view.get()),
          position, delta))) {
          LOG_F(ERROR, "Failed to send MouseWheelEvent for view {}", view.get());
        }
      }

      void WriteKey(ViewId view, EditorKeyEvent ev) override {
        if (!writer_.TrySend(std::make_shared<platform::KeyEvent>(
          ev.timestamp, static_cast<platform::WindowIdType>(view.get()),
          platform::input::KeyInfo(ev.key, ev.repeat),
          ev.pressed ? platform::ButtonState::kPressed
          : platform::ButtonState::kReleased))) {
          LOG_F(ERROR, "Failed to send KeyEvent for view {}", view.get());
        }
      }

      void WriteMouseButton(ViewId view, EditorButtonEvent ev) override {
        if (!writer_.TrySend(std::make_shared<platform::MouseButtonEvent>(
          ev.timestamp, static_cast<platform::WindowIdType>(view.get()),
          ev.position, ev.button,
          ev.pressed ? platform::ButtonState::kPressed
          : platform::ButtonState::kReleased))) {
          LOG_F(ERROR, "Failed to send MouseButtonEvent for view {}", view.get());
        }
      }

    private:
      co::BroadcastChannel<platform::InputEvent>::Writer& writer_;
    };
  } // namespace

  // Opaque token type used to keep an AsyncEngine module subscription alive
  // without exposing the subscription type in the public header.
  struct EditorModule::SubscriptionToken {
    explicit SubscriptionToken(oxygen::AsyncEngine::ModuleSubscription sub)
      : sub_(std::move(sub))
    {
    }

    oxygen::AsyncEngine::ModuleSubscription sub_;
  };

  EditorModule::EditorModule(std::shared_ptr<SurfaceRegistry> registry)
    : registry_(std::move(registry)) {
    if (registry_ == nullptr) {
      LOG_F(ERROR, "EditorModule construction failed: surface registry is null!");
      throw std::invalid_argument(
        "EditorModule requires a non-null surface registry.");
    }
    view_manager_ = std::make_unique<ViewManager>(*registry_);
    input_accumulator_ = std::make_unique<InputAccumulator>();
    viewport_navigation_ = std::make_unique<EditorViewportNavigation>();
  }

  EditorModule::~EditorModule() {
    CancelPendingPicks();
    asset_requests_.reset();
    LOG_F(INFO, "EditorModule destroying; releasing registered surfaces.");

    if (registry_) {
      auto graphics = graphics_.lock();

      auto pending_registrations = registry_->DrainPendingRegistrations();
      ReleaseSurfacesForShutdown(pending_registrations, graphics, false);

      registry_->Clear();
      auto pending_destructions = registry_->DrainPendingDestructions();
      ReleaseSurfacesForShutdown(pending_destructions, graphics, true);
    }

    LOG_F(INFO, "EditorModule destroyed.");
  }

  auto EditorModule::OnAttached(observer_ptr<IAsyncEngine> engine) noexcept
    -> bool {
    DCHECK_NOTNULL_F(engine);

    auto platform = engine->GetPlatformShared();
    DCHECK_NOTNULL_F(platform);
    auto writer = std::make_unique<EditorInputWriter>(
      platform->Input().ForWrite());
    input_accumulator_adapter_ =
      std::make_unique<InputAccumulatorAdapter>(std::move(writer));

    graphics_ = engine->GetGraphics();
    framebuffers_ = std::make_unique<SurfaceFramebuffers>(graphics_);

    // Keep a non-owning reference to the engine so we can access other
    // engine modules (renderer) during command recording.
    engine_ = engine;

    // IMPORTANT: Do not touch AssetLoader here.
    // EngineRunner registers EditorModule from the UI thread, but AssetLoader
    // enforces an owning-thread invariant. We acquire it lazily on the engine
    // thread in OnFrameStart.

    // InputSystem is registered by the engine interface layer during the
    // engine startup sequence. In the editor, EditorModule may be registered
    // earlier, so we subscribe and initialize bindings once InputSystem is
    // attached.
    auto sub = engine->SubscribeModuleAttached(
      [this](const ::oxygen::engine::ModuleEvent& ev) {
        if (input_bindings_initialized_) {
          return;
        }
        if (ev.type_id != oxygen::engine::InputSystem::ClassTypeId()) {
          return;
        }

        auto opt = engine_->GetModule<oxygen::engine::InputSystem>();
        if (!opt) {
          LOG_F(ERROR,
            "InputSystem attachment event received but module lookup failed");
          return;
        }
        input_bindings_initialized_ = InitInputBindings(opt->get());
      },
      /*replay_existing=*/true);
    input_system_subscription_token_
      = std::make_unique<SubscriptionToken>(std::move(sub));

    return true;
  }

  auto EditorModule::InitInputBindings(
    oxygen::engine::InputSystem& input_system) noexcept -> bool
  {
    if (!viewport_navigation_) {
      viewport_navigation_ = std::make_unique<EditorViewportNavigation>();
    }
    return viewport_navigation_->InitializeBindings(input_system);
  }

  auto EditorModule::OnFrameStart(observer_ptr<engine::FrameContext> context) -> void {
    LOG_SCOPE_F(1, "EditorModule::OnFrameStart");
    if (context == nullptr) {
      return;
    }
    DCHECK_NOTNULL_F(registry_);
    DCHECK_NOTNULL_F(view_manager_);

    if (!asset_loader_) {
      asset_loader_ = engine_->GetAssetLoader();
      DCHECK_NOTNULL_F(asset_loader_,
        "EditorModule requires AssetLoader - set config.enable_asset_loader = true");
      roots_dirty_ = true;
    }

    if (!path_resolver_) {
      LOG_F(INFO, "Initializing VirtualPathResolver on engine thread");
      path_resolver_ = std::make_unique<oxygen::content::VirtualPathResolver>();
      roots_dirty_ = true;
    }

    // Begin frame for the ViewManager: make the transient FrameContext
    // available so FrameStart commands (executed later in this method)
    // can perform immediate registration via ViewManager::CreateViewAsync.
    view_manager_->OnFrameStart(*context);

    // The frame timing still describes the previous, completed frame.
    frame_statistics_.store(PackFrameStatistics(EditorFrameStatistics {
      .frames_per_second = context->GetModuleTimingData().current_fps,
      .frame_time_ms = std::chrono::duration<float, std::milli>(
        context->GetFrameTiming().frame_duration)
                         .count(),
    }),
      std::memory_order_relaxed);

    ProcessSurfaceRegistrations();
    ProcessSurfaceDestructions();
    auto surfaces = ProcessResizeRequests();
    SyncSurfacesWithFrameContext(*context, surfaces);

    // Drain and dispatch input from the accumulator to the engine's input
    // system. The transform gizmo, then the scene helpers, take their drags
    // out first; a running helper drag keeps the pointer from the gizmo.
    transform_gizmo_.BeginFrame(scene_.get(), scene_generation_->load());
    {
      std::vector<UuidKey> selected;
      std::optional<UuidKey> active;
      {
        std::lock_guard lock(outline_mutex_);
        selected = outline_nodes_;
        active = outline_active_;
      }
      scene_helpers_.BeginFrame(
        scene_.get(), scene_generation_->load(), selected, active);
    }
    // Hover feedback draws only in the hovered view: when it moves, the view
    // it left and the view it entered both draw differently.
    const auto gizmo_hover = transform_gizmo_.GetHoverState();
    const auto helper_hover = scene_helpers_.GetHoverState();
    for (auto* view : view_manager_->GetAllViews()) {
      const auto view_id = view->GetViewId();
      auto batch = input_accumulator_->Drain(view_id);

      UpdateViewRoutingFromInputBatch(view_id, batch);
      if (scene_helpers_.IsDragging()) {
        scene_helpers_.ProcessInput(*view, batch, false);
        transform_gizmo_.ProcessInput(*view, batch);
      } else {
        transform_gizmo_.ProcessInput(*view, batch);
        scene_helpers_.ProcessInput(
          *view, batch, transform_gizmo_.IsHovering(view_id));
      }

      const bool has_mouse = (batch.mouse_delta.dx != 0.0F) || (batch.mouse_delta.dy != 0.0F);
      const bool has_wheel = (batch.scroll_delta.dx != 0.0F) || (batch.scroll_delta.dy != 0.0F);
      const bool has_keys = !batch.key_events.empty();
      const bool has_buttons = !batch.button_events.empty();
      if (has_mouse || has_wheel || has_keys || has_buttons) {
        // Hover feedback and navigation change only this pane.
        pane_policy_.Invalidate(view_id);
        DLOG_F(2,
          "EditorModule input: draining+dispatching view={} mouse(dx={},dy={}) wheel(dx={},dy={}) keys={} buttons={} pos(x={},y={})",
          view_id.get(),
          batch.mouse_delta.dx, batch.mouse_delta.dy,
          batch.scroll_delta.dx, batch.scroll_delta.dy,
          batch.key_events.size(), batch.button_events.size(),
          batch.last_position.x, batch.last_position.y);
      }

      input_accumulator_adapter_->DispatchForView(view_id, batch);
    }
    if (const auto hover = transform_gizmo_.GetHoverState();
      hover != gizmo_hover) {
      pane_policy_.Invalidate(gizmo_hover.view);
      pane_policy_.Invalidate(hover.view);
    }
    if (const auto hover = scene_helpers_.GetHoverState();
      hover != helper_hover) {
      pane_policy_.Invalidate(helper_hover.view);
      pane_policy_.Invalidate(hover.view);
    }

    // After surface handling, execute frame-start commands related to views
    // with a strict ordering to avoid race conditions: destroy -> create -> rest.
    CommandContext cmd_ctx{
      .FrameContext = observer_ptr { context.get() },
      .Scene = observer_ptr{scene_.get()},
      .AssetLoader = observer_ptr{asset_loader_.get()},
      .PathResolver = observer_ptr{path_resolver_.get()},
      .AssetRequests = observer_ptr{asset_requests_.get()}
    };

    // 1) Destroy view commands first
    command_queue_.DrainIf(
      [](const std::unique_ptr<EditorCommand>& cmd) {
        return cmd &&
          cmd->GetTargetPhase() == oxygen::core::PhaseId::kFrameStart &&
          dynamic_cast<const DestroyViewCommand*>(cmd.get()) != nullptr;
      },
      [&](std::unique_ptr<EditorCommand>& cmd) {
        if (cmd) {
          if (const auto* destroy_view =
                dynamic_cast<const DestroyViewCommand*>(cmd.get());
              destroy_view != nullptr) {
            RemovePublishedRuntimeViewForIntent(
              destroy_view->GetViewId(), context.get());
            pane_policy_.Forget(destroy_view->GetViewId());
          }
          RunCommand(*cmd, cmd_ctx, "DestroyViewCommand");
        }
      });

    // 2) Create view commands next
    command_queue_.DrainIf(
      [](const std::unique_ptr<EditorCommand>& cmd) {
        return cmd &&
          cmd->GetTargetPhase() == oxygen::core::PhaseId::kFrameStart &&
          dynamic_cast<const CreateViewCommand*>(cmd.get()) != nullptr;
      },
      [&](std::unique_ptr<EditorCommand>& cmd) {
        if (cmd) {
          RunCommand(*cmd, cmd_ctx, "CreateViewCommand");
        }
      });

    // 3) Run any remaining FrameStart commands
    command_queue_.DrainIf(
      [](const std::unique_ptr<EditorCommand>& cmd) {
        return cmd &&
          cmd->GetTargetPhase() == oxygen::core::PhaseId::kFrameStart;
      },
      [&](std::unique_ptr<EditorCommand>& cmd) {
        if (cmd) {
          RunCommand(*cmd, cmd_ctx, "FrameStart command");
        }
      });

    context->SetScene(observer_ptr{ scene_.get() });
    view_manager_->FinalizeViews();
  }

  void EditorModule::UpdateViewRoutingFromInputBatch(ViewId view_id,
    const AccumulatedInput& batch) noexcept {
    const bool has_mouse = (batch.mouse_delta.dx != 0.0F) || (batch.mouse_delta.dy != 0.0F);
    const bool has_wheel = (batch.scroll_delta.dx != 0.0F) || (batch.scroll_delta.dy != 0.0F);
    const bool has_keys = !batch.key_events.empty();
    const bool has_buttons = !batch.button_events.empty();

    // Mouse motion and wheel identify the last-hovered view.
    if (has_mouse || has_wheel) {
      hover_view_id_ = view_id;
    }

    // Key and button events identify the focused (active) view.
    if (has_keys || has_buttons) {
      active_view_id_ = view_id;
    }
  }

  void EditorModule::ProcessSurfaceRegistrations() {
    DCHECK_NOTNULL_F(registry_);

    auto pending = registry_->DrainPendingRegistrations();
    if (pending.empty()) {
      return;
    }

    for (auto& entry : pending) {
      const auto& key = entry.first;
      auto& surface = entry.second.first;
      auto& cb = entry.second.second;

      CHECK_NOTNULL_F(surface);
      try {
        DLOG_F(INFO,
          "Processing pending surface registration for a surface (ptr={}).",
          fmt::ptr(surface.get()));

        registry_->CommitRegistration(key, surface);
        // A new swap chain holds no image until its panes render into it.
        InvalidatePanesOn(key);

        LOG_F(INFO, "Committed surface registration for surface ptr={}",
          fmt::ptr(surface.get()));
      }
      catch (...) {
        // Registration failed
      }

      if (cb) {
        try {
          cb(true);
        }
        catch (...) {
          /* swallow */
        }
      }
    }
  }

  void EditorModule::ProcessSurfaceDestructions() {
    if (graphics_.expired()) {
      DLOG_F(WARNING, "Graphics instance is expired; cannot process deferred "
        "surface destructions.");
      return;
    }
    auto gfx = graphics_.lock();

    auto pending = registry_->DrainPendingDestructions();
    if (pending.empty()) {
      return;
    }

    for (auto& entry : pending) {
      const auto& key = entry.first;
      auto& surface = entry.second.first;
      auto& cb = entry.second.second;

      CHECK_NOTNULL_F(surface);
      if (framebuffers_) {
        framebuffers_->ReleaseDeferred(key);
      }
      try {
        gfx->RegisterDeferredRelease(std::move(surface));
      }
      catch (...) {
      }

      if (cb) {
        try {
          cb(true);
        }
        catch (...) {
          /* swallow */
        }
      }
    }
  }

  auto EditorModule::ProcessResizeRequests()
    -> std::vector<std::shared_ptr<graphics::Surface>> {
    auto snapshot = registry_->SnapshotSurfaces();
    std::vector<std::shared_ptr<graphics::Surface>> surfaces;
    surfaces.reserve(snapshot.size());
    std::vector<std::pair<SurfaceRegistry::GuidKey, graphics::Surface*>> resizing;
    for (const auto& [key, surface] : snapshot) {
      CHECK_NOTNULL_F(surface);
      if (surface->ShouldResize()) {
        resizing.emplace_back(key, surface.get());
      }
      surfaces.emplace_back(surface);
    }

    if (resizing.empty()) {
      return surfaces;
    }

    // A swap chain cannot resize while the GPU still holds its backbuffers. One
    // flush covers every surface resized this frame (a splitter drag resizes
    // several panes together).
    if (const auto gfx = graphics_.lock()) {
      try {
        gfx->FlushCommandQueues();
      }
      catch (...) {
        DLOG_F(WARNING,
          "Graphics::FlushCommandQueues threw during pre-resize; continuing.");
      }
    }

    for (const auto& [key, surface] : resizing) {
      DLOG_F(INFO, "Applying resize for surface '{}'.", surface->GetName());
      if (framebuffers_) {
        framebuffers_->ReleaseForResize(key);
      }

      surface->Resize();
      view_manager_->OnSurfaceResized(key, *surface);
      // Resizing discards the swap chain's buffers and their image.
      InvalidatePanesOn(key);

      const bool ok = surface->GetCurrentBackBuffer() != nullptr;
      for (auto& callback : registry_->DrainResizeCallbacks(key)) {
        try {
          callback(ok);
        }
        catch (...) {
          /* swallow */
        }
      }
    }

    return surfaces;
  }

  auto EditorModule::OnSceneMutation(observer_ptr<engine::FrameContext> context) -> co::Co<> {
    if (context == nullptr) {
      co_return;
    }
    co_await ProcessContentPauseAsync(*context);
    const auto content_changed = co_await SynchronizeCookedRootsAsync();
    const auto renderer = engine_->GetModule<oxygen::vortex::Renderer>();
    // Drain only commands targeting SceneMutation. Leave other commands for
    // their appropriate phases so insertion order is preserved across phases.
    CommandContext cmd_context{
      .Scene = observer_ptr{scene_.get()},
      .AssetLoader = observer_ptr{asset_loader_.get()},
      .PathResolver = observer_ptr{path_resolver_.get()},
      .AssetRequests = observer_ptr{asset_requests_.get()},
      .Renderer = renderer ? observer_ptr{ &renderer->get() } : nullptr
    };
    command_queue_.DrainIf(
      [](const std::unique_ptr<EditorCommand>& cmd) {
        return cmd &&
          cmd->GetTargetPhase() == oxygen::core::PhaseId::kSceneMutation;
      },
      [&](std::unique_ptr<EditorCommand>& cmd) {
        if (cmd) {
          RunCommand(*cmd, cmd_context, "editor command");
        }
      });

    if (content_changed) {
      scene_changed_ = true;
    }
    if (scene_ && asset_requests_) {
      if (content_changed) {
        asset_requests_->Refresh(*scene_);
      }
      if (asset_requests_->Drain(*scene_)) {
        scene_changed_ = true;
      }
    }

    if (active_roots_completion_ && (!asset_requests_ || !asset_requests_->IsRefreshPending())) {
      auto complete = std::move(active_roots_completion_);
      const auto error = asset_requests_ ? asset_requests_->RefreshError() : std::string{};
      complete(error.empty(), error);
    }

    if (content_resume_completion_ && (!asset_requests_ || !asset_requests_->IsRefreshPending())) {
      auto complete = std::move(content_resume_completion_);
      const auto error = asset_requests_ ? asset_requests_->RefreshError() : std::string{};
      preview_paused_ = !error.empty();
      complete(error.empty(), error);
    }

    if (scene_ && !graphics_.expired() && view_manager_) {
      auto gfx = graphics_.lock();

      const auto input_blob = context->GetInputSnapshot();
      const auto input_snapshot = input_blob
        ? std::static_pointer_cast<const input::InputSnapshot>(input_blob)
        : std::shared_ptr<const input::InputSnapshot> {};

      float dt_seconds =
        std::chrono::duration<float>(context->GetGameDeltaTime().get()).count();
      if (dt_seconds <= 0.0f) {
        dt_seconds =
          std::chrono::duration<float>(context->GetFrameTiming().frame_duration)
            .count();
      }

      // Iterate over all registered views
      for (auto* view : view_manager_->GetAllRegisteredViews()) {
        if (!view) {
          continue;
        }

        // Prepare context for this view (no recorder in this phase)
        EditorViewContext view_ctx{
            .frame_context = *context, .graphics = *gfx, .recorder = nullptr };

        view->SetRenderingContext(view_ctx);
        view->OnSceneMutation();
        // A framing move runs before navigation, which can take it over.
        view->AdvanceFraming(dt_seconds);

        // A view looking through a scene camera navigates only while piloting
        // it. The editor camera is then the navigation proxy, and the scene
        // camera follows it below.
        const bool navigates
          = !view->IsViewingSceneCamera() || view->IsPilotingSceneCamera();
        if (input_snapshot && navigates) {
          const auto view_id = view->GetViewId();

          const auto active = (active_view_id_ != kInvalidViewId)
            ? active_view_id_
            : hover_view_id_;

          const auto hovered = hover_view_id_;

          // Piloting an orthographic camera: the wheel resizes it instead of
          // dollying the proxy.
          auto piloted = view->IsPilotingSceneCamera()
            ? view->GetRenderCameraNode()
            : scene::SceneNode {};
          const bool ortho_pilot = piloted.IsAlive()
            && piloted.GetCameraAs<scene::OrthographicCamera>().has_value();
          const auto apply_wheel = [&](glm::vec3& focus_point,
                                     float& ortho_half_height) {
            if (!ortho_pilot) {
              viewport_navigation_->ApplyWheelOnly(view->GetCameraNode(),
                *input_snapshot, view->GetCameraControlMode(), focus_point,
                ortho_half_height, view->GetCameraMovementSpeed(), dt_seconds);
              return;
            }
            auto& camera
              = piloted.GetCameraAs<scene::OrthographicCamera>()->get();
            auto projection = OrthographicCameraProjection::From(camera);
            float size = projection.orthographic_size;
            viewport_navigation_->ApplyWheelOnly(piloted, *input_snapshot,
              view->GetCameraControlMode(), focus_point, size,
              view->GetCameraMovementSpeed(), dt_seconds);
            if (size != projection.orthographic_size) {
              projection.orthographic_size = size;
              if (projection.IsValid()) {
                projection.ApplyTo(camera);
              }
            }
          };

          // Non-wheel navigation applies to the focused (active) viewport.
          if (active != kInvalidViewId && view_id == active) {
            auto focus_point = view->GetFocusPoint();
            auto ortho_half_height = view->GetOrthoHalfHeight();

            viewport_navigation_->ApplyNonWheel(
              view->GetCameraNode(),
              *input_snapshot,
              view->GetCameraControlMode(),
              focus_point,
              ortho_half_height,
              view->GetCameraMovementSpeed(),
              dt_seconds);
            // If the hovered view differs, keep wheel routing separate.
            if (hovered == kInvalidViewId || hovered == active) {
              apply_wheel(focus_point, ortho_half_height);
            }

            view->SetFocusPoint(focus_point);
            view->SetOrthoHalfHeight(ortho_half_height);
          }

          // Wheel navigation applies to the last-hovered viewport.
          if (hovered != kInvalidViewId && hovered != active && view_id == hovered) {
            auto focus_point = view->GetFocusPoint();
            auto ortho_half_height = view->GetOrthoHalfHeight();
            apply_wheel(focus_point, ortho_half_height);
            view->SetFocusPoint(focus_point);
            view->SetOrthoHalfHeight(ortho_half_height);
          }
        }
        view->SyncPilotedCamera();
        view->ClearPhaseRecorder();
      }
      view_manager_->UpdateCameraScene();
    }
    co_return;
  }

  auto EditorModule::OnPublishViews(observer_ptr<engine::FrameContext> context)
    -> co::Co<> {
    held_views_.clear();
    if (preview_paused_ || context == nullptr || engine_ == nullptr
      || !view_manager_) {
      CancelPendingPicks();
      co_return;
    }

    auto renderer_opt = engine_->GetModule<oxygen::vortex::Renderer>();
    const auto gfx = graphics_.lock();
    if (!renderer_opt.has_value() || !gfx) {
      CancelPendingPicks();
      co_return;
    }

    auto& renderer = renderer_opt->get();
    auto panes = CollectPanes(*context, renderer, *gfx);
    DecidePaneRenders(renderer, panes);

    const bool any_render = std::ranges::any_of(
      panes, [](const PaneCandidate& pane) { return pane.render; });
    const auto outline = any_render ? BuildSelectionOutline() : nullptr;
    std::unordered_set<ViewId> published_views;
    for (const auto& pane : panes) {
      const auto view_id = pane.view->GetViewId();
      if (!pane.render
        && renderer.HoldPublishedRuntimeView(*context, view_id)) {
        held_views_.insert(view_id);
        continue;
      }
      if (PublishPane(*context, renderer, *pane.view, outline)) {
        published_views.insert(view_id);
        pane_policy_.MarkRendered(view_id, pane.fingerprint);
      }
      else {
        pane_policy_.Forget(view_id);
      }
    }

    SubmitPendingPicks(renderer, published_views);
    co_return;
  }

  void EditorModule::RunCommand(EditorCommand& command,
    CommandContext& context, const std::string_view label) {
    try {
      command.Execute(context);
    }
    catch (const std::exception& e) {
      LOG_F(ERROR, "Exception executing {}: {}", label, e.what());
    }
    catch (...) {
      LOG_F(ERROR, "Unknown exception executing {}", label);
    }

    // A failed command may still have applied part of its change.
    const auto invalidation = command.GetInvalidation();
    switch (invalidation.scope) {
    case CommandInvalidation::Scope::kNone:
      break;
    case CommandInvalidation::Scope::kView:
      pane_policy_.Invalidate(invalidation.view);
      break;
    case CommandInvalidation::Scope::kScene:
      scene_changed_ = true;
      break;
    }
  }

  auto EditorModule::CollectPanes(engine::FrameContext& context,
    vortex::Renderer& renderer, Graphics& graphics)
    -> std::vector<PaneCandidate> {
    std::vector<PaneCandidate> panes;
    for (auto* view : view_manager_->GetAllViews()) {
      if (view == nullptr || view->GetViewId() == kInvalidViewId) {
        continue;
      }
      if (!view->IsVisible()) {
        WithdrawPane(context, renderer, *view, "hidden");
        continue;
      }

      const auto& config = view->GetConfig();
      if (!config.compositing_target.has_value()
        || !registry_->FindSurface(*config.compositing_target)) {
        WithdrawPane(context, renderer, *view, "no registered surface");
        continue;
      }

      // An inset with no room on its surface has no extent and renders nothing.
      const auto width = view->GetWidth();
      const auto height = view->GetHeight();
      if (width <= 0.0F || height <= 0.0F) {
        WithdrawPane(context, renderer, *view, "no extent");
        continue;
      }

      view->EnsureRenderTarget(graphics);
      const auto scene_fb = view->GetFramebuffer();
      auto camera_node = view->GetRenderCameraNode();
      if (!scene_fb || !camera_node.IsAlive()) {
        WithdrawPane(
          context, renderer, *view, scene_fb ? "no camera" : "no render target");
        continue;
      }

      const auto resolved = vortex::SceneCameraViewResolver {
        [camera_node](const ViewId& /*view_id*/) { return camera_node; },
      }(view->GetViewId());
      panes.push_back(PaneCandidate {
        .view = view,
        .fingerprint = PaneFingerprint {
          .view = resolved.ViewMatrix(),
          .projection = resolved.ProjectionMatrix(),
          .width = width,
          .height = height,
        },
        .render = false,
      });
    }
    return panes;
  }

  void EditorModule::DecidePaneRenders(
    vortex::Renderer& renderer, std::vector<PaneCandidate>& panes) {
    bool scene_changed = std::exchange(scene_changed_, false);
    if (scene_changed) {
      scene_has_local_fog_ = scene_ && HasEnabledLocalFog(*scene_);
    }

    // Streamed textures, uploaded geometry and refreshed probes change every
    // pane's image without any scene edit.
    const auto resident = renderer.GetResidentContentRevision();
    if (resident_revision_ != resident) {
      resident_revision_ = resident;
      scene_changed = true;
    }

    const auto outline_revision
      = outline_revision_.load(std::memory_order_acquire);
    if (outline_revision != published_outline_revision_) {
      published_outline_revision_ = outline_revision;
      scene_changed = true;
    }

    // A piloted scene camera moves with its pane; the other panes draw it.
    for (const auto& pane : panes) {
      if (pane.view->IsPilotingSceneCamera()
        && !pane_policy_.IsCurrent(pane.view->GetViewId(), pane.fingerprint)) {
        scene_changed = true;
      }
    }

    // A view whose image is still moving on its own, such as the ground grid
    // easing after the camera stops, renders until the renderer reports rest.
    for (const auto& pane : panes) {
      const auto published
        = renderer.ResolvePublishedRuntimeViewId(pane.view->GetViewId());
      if (const auto status = renderer.InspectViewRenderStatus(published);
        status.has_value() && status->settling) {
        pane_policy_.Invalidate(pane.view->GetViewId());
      }
    }

    pane_policy_.SetAlwaysRender(
      always_render_panes_.load(std::memory_order_relaxed));
    pane_policy_.BeginFrame(scene_changed);
    {
      std::lock_guard lock(picks_mutex_);
      for (const auto& pick : pending_picks_) {
        pane_policy_.Invalidate(pick.view_id);
      }
    }

    // Panes sharing a surface (a host and its inset) are composed together,
    // so the surface renders all of them or none.
    std::vector<SurfaceRegistry::GuidKey> rendering_surfaces;
    for (const auto& pane : panes) {
      const auto view_id = pane.view->GetViewId();
      const bool unpublished
        = renderer.ResolvePublishedRuntimeViewId(view_id) == kInvalidViewId;
      if (unpublished || pane_policy_.NeedsRender(view_id, pane.fingerprint)) {
        rendering_surfaces.push_back(*pane.view->GetConfig().compositing_target);
      }
    }
    for (auto& pane : panes) {
      pane.render = std::ranges::find(rendering_surfaces,
                      *pane.view->GetConfig().compositing_target)
        != rendering_surfaces.end();
    }
  }

  auto EditorModule::PublishPane(engine::FrameContext& context,
    vortex::Renderer& renderer, EditorView& view,
    const std::shared_ptr<const vortex::ViewOutline>& outline) -> bool {
    const auto width = view.GetWidth();
    const auto height = view.GetHeight();
    View view_desc {};
    view_desc.viewport = ViewPort {
      .top_left_x = 0.0F,
      .top_left_y = 0.0F,
      .width = width,
      .height = height,
      .min_depth = 0.0F,
      .max_depth = 1.0F,
    };
    view_desc.scissor = Scissors {
      .left = 0,
      .top = 0,
      .right = static_cast<int32_t>(width),
      .bottom = static_cast<int32_t>(height),
    };

    const auto& config = view.GetConfig();
    const auto scene_fb = view.GetFramebuffer();
    auto composition_view = vortex::CompositionView::ForScene(
      view.GetViewId(), view_desc, view.GetRenderCameraNode());
    composition_view.name = config.name;
    composition_view.clear_color = config.clear_color;
    composition_view.with_atmosphere = true;
    composition_view.with_height_fog = IsHeightFogRequested(scene_.get());
    composition_view.with_local_fog = scene_has_local_fog_;
    composition_view.shading_mode = vortex::ShadingMode::kDeferred;
    ApplyRenderOptions(composition_view, view);
    // A camera preview shows what the camera sees, without editor aids.
    if (!view.IsInset() && config.render_options.show_selection_outline) {
      composition_view.outline = outline;
    }
    // Helpers first: the gizmo draws over them.
    auto overlay = std::make_shared<vortex::ViewOverlay>();
    scene_helpers_.BuildOverlay(view, *overlay);
    transform_gizmo_.BuildOverlay(view, *overlay);
    if (!overlay->IsEmpty()) {
      composition_view.overlay = std::move(overlay);
    }

    const auto published_view_id = renderer.PublishRuntimeCompositionView(
      context,
      vortex::Renderer::RuntimeViewPublishInput {
        .composition_view = composition_view,
        .render_target = observer_ptr { scene_fb.get() },
        .composite_source = observer_ptr { scene_fb.get() },
      },
      vortex::ShadingMode::kDeferred);
    if (published_view_id == kInvalidViewId) {
      WithdrawPane(context, renderer, view, "publication rejected");
      return false;
    }
    DLOG_F(2, "OnPublishViews: view '{}' intent={} published={} extent={}x{}",
      view.GetName(), view.GetViewId().get(), published_view_id.get(), width,
      height);
    return true;
  }

  void EditorModule::InvalidatePanesOn(const SurfaceRegistry::GuidKey& key) {
    for (const auto* view : view_manager_->GetAllViews()) {
      if (view != nullptr && view->GetConfig().compositing_target == key) {
        pane_policy_.Invalidate(view->GetViewId());
      }
    }
  }

  void EditorModule::WithdrawPane(engine::FrameContext& context,
    vortex::Renderer& renderer, const EditorView& view,
    const std::string_view reason) {
    // A publication lasts until it is removed, but its resolved camera only
    // lasts one frame. A pane neither published nor held this frame is
    // withdrawn, or the renderer would prepare it without a camera and fail
    // the frame's views.
    pane_policy_.Forget(view.GetViewId());
    if (renderer.ResolvePublishedRuntimeViewId(view.GetViewId())
      == kInvalidViewId) {
      return;
    }
    LOG_F(INFO, "OnPublishViews: view '{}' withdrawn: {}", view.GetName(),
      reason);
    RemovePublishedRuntimeViewForIntent(view.GetViewId(), &context);
  }

  auto EditorModule::BuildSelectionOutline()
    -> std::shared_ptr<const vortex::ViewOutline> {
    std::vector<UuidKey> nodes;
    std::optional<UuidKey> active;
    {
      std::lock_guard lock(outline_mutex_);
      nodes = outline_nodes_;
      active = outline_active_;
    }
    if (!scene_ || (nodes.empty() && !active.has_value())) {
      return nullptr;
    }

    // Selecting a node outlines its whole subtree: a group shows what it
    // holds.
    const auto add_subtree = [this](const UuidKey& id,
                               std::vector<scene::NodeHandle>& out) {
      const auto handle = NodeRegistry::Lookup(id);
      if (!handle.has_value()) {
        return;
      }
      auto root = scene_->GetNode(*handle);
      if (!root.has_value() || !root->IsAlive()) {
        return;
      }
      std::vector<scene::SceneNode> pending { *root };
      while (!pending.empty()) {
        auto node = pending.back();
        pending.pop_back();
        out.push_back(node.GetHandle());
        for (auto child = node.GetFirstChild(); child.has_value();
          child = child->GetNextSibling()) {
          pending.push_back(*child);
        }
      }
    };

    auto outline = std::make_shared<vortex::ViewOutline>();
    for (const auto& id : nodes) {
      add_subtree(id, outline->nodes);
    }
    if (active.has_value()) {
      add_subtree(*active, outline->active_nodes);
    }
    if (outline->IsEmpty()) {
      return nullptr;
    }
    return outline;
  }

  void EditorModule::SubmitPendingPicks(vortex::Renderer& renderer,
    const std::unordered_set<ViewId>& published_views) {
    std::vector<PendingPick> picks;
    {
      std::lock_guard lock(picks_mutex_);
      picks.swap(pending_picks_);
    }
    for (auto& pick : picks) {
      if (!published_views.contains(pick.view_id)) {
        pick.callback(std::nullopt);
        continue;
      }
      const auto generation = scene_generation_->load();
      // Icons are hit now, where the pointer saw them; they come first
      // when as close as geometry, because they draw over it.
      auto icons = std::vector<HelperIconHit> {};
      if (auto* view = view_manager_->GetView(pick.view_id)) {
        icons = scene_helpers_.PickIcons(*view, pick.rect);
      }
      auto completion = [scene_generation = scene_generation_, generation,
                          icons = std::move(icons),
                          callback = std::move(pick.callback)](
                          vortex::ViewPickResult result) {
        if (result.status != vortex::ViewPickResult::Status::kCompleted
          || scene_generation->load() != generation) {
          callback(std::nullopt);
          return;
        }
        EditorPickResult picked;
        picked.world_position = result.world_position;
        picked.hits.reserve(icons.size() + result.hits.size());
        for (const auto& icon : icons) {
          picked.hits.push_back(EditorPickHit {
            .node = icon.id,
            .depth = icon.depth,
            .geometry_slot = 0U,
            .center_distance = icon.center_distance,
          });
        }
        for (const auto& hit : result.hits) {
          // Nodes the editor did not create (none today) are not pickable.
          if (const auto id = NodeRegistry::ReverseLookup(hit.node)) {
            picked.hits.push_back(EditorPickHit {
              .node = *id,
              .depth = hit.depth,
              .geometry_slot = hit.submesh_index,
              .center_distance = hit.center_distance,
            });
          }
        }
        if (!icons.empty()) {
          // Closest to the centre first, a node hit by its icon and its
          // geometry listed once.
          std::ranges::stable_sort(picked.hits, {}, &EditorPickHit::center_distance);
          std::unordered_set<UuidKey, UuidKeyHash> seen;
          std::erase_if(picked.hits, [&](const EditorPickHit& hit) {
            return !seen.insert(hit.node).second;
          });
        }
        callback(std::move(picked));
      };
      renderer.RequestPublishedRuntimeViewPick(pick.view_id,
        std::make_shared<vortex::ViewPickRequest>(
          pick.rect, std::move(completion)));
    }
  }

  void EditorModule::CancelPendingPicks() noexcept {
    std::vector<PendingPick> picks;
    {
      std::lock_guard lock(picks_mutex_);
      picks.swap(pending_picks_);
    }
    for (auto& pick : picks) {
      try {
        pick.callback(std::nullopt);
      } catch (...) {
        LOG_F(ERROR, "EditorModule: pick callback failed");
      }
    }
  }

  void EditorModule::PickView(ViewId view_id, vortex::ViewPickRect rect,
    std::function<void(std::optional<EditorPickResult>)> callback) {
    if (view_id == kInvalidViewId || !view_manager_) {
      callback(std::nullopt);
      return;
    }
    std::lock_guard lock(picks_mutex_);
    pending_picks_.push_back(PendingPick {
      .view_id = view_id,
      .rect = rect,
      .callback = std::move(callback),
    });
  }

  void EditorModule::FrameView(ViewId view_id, std::vector<UuidKey> nodes,
    std::function<void(EditorFramingOutcome)> callback) {
    if (view_id == kInvalidViewId || !view_manager_) {
      callback(EditorFramingOutcome::kNoView);
      return;
    }
    command_queue_.Enqueue(std::make_unique<FrameViewCommand>(
      view_manager_.get(), view_id, std::move(nodes), std::move(callback)));
  }

  void EditorModule::SetSelectionOutline(
    std::vector<UuidKey> nodes, std::optional<UuidKey> active) {
    std::lock_guard lock(outline_mutex_);
    outline_nodes_ = std::move(nodes);
    outline_active_ = active;
    outline_revision_.fetch_add(1U, std::memory_order_release);
  }

  void EditorModule::SetAlwaysRenderPanes(const bool always_render) noexcept {
    always_render_panes_.store(always_render, std::memory_order_relaxed);
  }

  void EditorModule::SetGroundGridConfig(
    const vortex::GroundGridConfig& config) {
    command_queue_.Enqueue(std::make_unique<SetGroundGridConfigCommand>(config));
  }

  auto EditorModule::OnPreRender(observer_ptr<engine::FrameContext> context) -> co::Co<> {
    if (preview_paused_) {
      co_return;
    }
    if (context == nullptr) {
      co_return;
    }
    if (engine_ && view_manager_) {
      auto renderer_opt = engine_->GetModule<oxygen::vortex::Renderer>();
      if (renderer_opt.has_value()) {
        auto& renderer = renderer_opt->get();
        // Iterate over all registered views and allow them to prepare for
        // rendering. Provide a rendering context for each view (frame context +
        // graphics) so the view can update FrameContext outputs and prepare its
        // framebuffer.
        if (!graphics_.expired()) {
          auto gfx = graphics_.lock();
          for (auto* view : view_manager_->GetAllRegisteredViews()) {
            if (!view)
              continue;

            EditorViewContext view_ctx{
                .frame_context = *context, .graphics = *gfx, .recorder = nullptr };
            view->SetRenderingContext(view_ctx);
            co_await view->OnPreRender(renderer);
            // Clear the per-phase recorder/context pointer after PreRender
            view->ClearPhaseRecorder();
          }
        }
        else {
          // Fall back to calling OnPreRender without a graphics context if the
          // Graphics instance has expired. Views which require resources will
          // no-op in that case.
          for (auto* view : view_manager_->GetAllRegisteredViews()) {
            if (view)
              co_await view->OnPreRender(renderer);
          }
        }
      }
    }
    co_return;
  }

  auto EditorModule::OnRender(observer_ptr<engine::FrameContext> context) -> co::Co<> {
    (void)context;
    // Rendering is handled by the Renderer module via registered views.
    // EditorModule participates in OnCompositing to blit results to surfaces.
    co_return;
  }

  auto EditorModule::OnCompositing(observer_ptr<engine::FrameContext> context) -> co::Co<> {
    if (preview_paused_ || context == nullptr || engine_ == nullptr
      || !view_manager_ || !framebuffers_) {
      co_return;
    }

    auto renderer_opt = engine_->GetModule<oxygen::vortex::Renderer>();
    if (!renderer_opt.has_value()) {
      co_return;
    }

    auto& renderer = renderer_opt->get();
    const auto views = view_manager_->GetAllRegisteredViews();
    const auto is_composable = [&renderer](const EditorView* view) {
      return view != nullptr && view->IsVisible()
        && view->GetViewId() != kInvalidViewId
        && view->GetWidth() > 0.0F && view->GetHeight() > 0.0F
        && renderer.ResolvePublishedRuntimeViewId(view->GetViewId())
        != kInvalidViewId;
    };

    for (auto* view : views) {
      // A camera preview inset is composed as a layer of its host. A held
      // pane keeps the image its surface last presented.
      if (!is_composable(view) || view->IsInset()
        || held_views_.contains(view->GetViewId())) {
        continue;
      }

      const auto& config = view->GetConfig();
      if (!config.compositing_target.has_value()) {
        continue;
      }
      const auto& key = *config.compositing_target;
      auto target_surface = registry_->FindSurface(key);
      if (!target_surface) {
        continue;
      }

      auto target_fb = framebuffers_->GetCurrent(key, *target_surface);
      if (!target_fb) {
        DLOG_F(2, "OnCompositing: view '{}' has no target", view->GetName());
        continue;
      }

      const auto width = view->GetWidth();
      const auto height = view->GetHeight();
      std::vector<vortex::Renderer::RuntimeCompositionLayer> layers {
        vortex::Renderer::RuntimeCompositionLayer {
          .intent_view_id = view->GetViewId(),
          .viewport = ViewPort {
            .top_left_x = 0.0F,
            .top_left_y = 0.0F,
            .width = width,
            .height = height,
            .min_depth = 0.0F,
            .max_depth = 1.0F,
          },
          .opacity = 1.0F,
        },
      };

      // Later layers draw on top: the host's insets go over its image.
      if (const auto inset_viewport = ResolveInsetViewport(width, height)) {
        for (const auto* inset : views) {
          if (is_composable(inset) && inset->IsInset()
            && *inset->GetConfig().inset_host == view->GetViewId()) {
            layers.push_back(vortex::Renderer::RuntimeCompositionLayer {
              .intent_view_id = inset->GetViewId(),
              .viewport = *inset_viewport,
              .opacity = 1.0F,
            });
          }
        }
      }

      renderer.RegisterRuntimeComposition(
        vortex::Renderer::RuntimeCompositionInput {
          .layers = std::move(layers),
          .composite_target = std::move(target_fb),
          .target_surface = std::move(target_surface),
        });
    }

    co_return;
  }

  auto EditorModule::CreateScene(std::string_view name,
    std::function<void(bool, std::string)> onComplete) -> void {
    LOG_F(INFO, "EditorModule::CreateScene called: name='{}'", name);
    // Marshal scene creation to the engine thread by enqueuing a command
    // that will execute during FrameStart. Provide onComplete callback so
    // callers can observe when the scene has been created.
    auto cmd = std::make_unique<CreateSceneCommand>(this, std::string(name),
      std::move(onComplete));
    Enqueue(std::move(cmd));
  }

  void EditorModule::ApplyCreateScene(std::string_view name) {
    LOG_F(INFO, "EditorModule::ApplyCreateScene: creating scene '{}'", name);
    if (scene_) {
      try {
        ApplyDestroyScene(false);
      } catch (const std::exception& e) {
        LOG_F(ERROR, "ApplyCreateScene: failed to destroy existing scene: {}",
          e.what());
        scene_.reset();
      } catch (...) {
        LOG_F(ERROR,
          "ApplyCreateScene: failed to destroy existing scene: unknown error");
        scene_.reset();
      }
    }
    asset_requests_.reset();
    scene_generation_->fetch_add(1U);
    scene_ = std::make_shared<oxygen::scene::Scene>(std::string(name), 1024U);

    asset_requests_ = std::make_unique<SceneAssetRequests>(
      *asset_loader_, *path_resolver_);
    asset_requests_->SetCookedRoots(active_roots_);

    auto environment = std::make_unique<oxygen::scene::SceneEnvironment>();
    (void)environment
      ->AddSystem<oxygen::scene::environment::SkyAtmosphere>();
    (void)environment
      ->AddSystem<oxygen::scene::environment::PostProcessVolume>();
    scene_->SetEnvironment(std::move(environment));
    if (view_manager_) {
      view_manager_->RetargetAllViews(*scene_);
    }
  }

  void EditorModule::CreateViewAsync(EditorView::Config config,
    ViewManager::OnViewCreated callback) {
    // Enqueue a frame-start command to unify creation through the command
    // system while keeping the public API stable for editor-facing callers.
    // The actual registration is performed immediately during FrameStart by
    // the ViewManager (OnFrameStart makes the FrameContext available).
    if (view_manager_) {
      auto cmd = std::make_unique<CreateViewCommand>(
        view_manager_.get(), std::move(config), std::move(callback));
      Enqueue(std::move(cmd));
    }
    else {
      if (callback)
        callback(false, kInvalidViewId);
    }
  }

  void EditorModule::DestroyScene() {
    // Enqueue a destruction command to ensure scene teardown happens on the
    // engine thread during SceneMutation phase.
    auto cmd = std::make_unique<DestroySceneCommand>(this);
    Enqueue(std::move(cmd));
  }

  void EditorModule::DestroyView(ViewId view_id) {
    if (!view_manager_)
      return;

    // Enqueue a destroy command so the actual destruction runs on the engine
    // thread and cannot race with frame-phase iteration (OnSceneMutation /
    // OnPreRender). Runtime publication cleanup happens in the same FrameStart
    // drain while the active FrameContext is available; doing it here would
    // leave stale FrameContext view entries pointing at released framebuffers.
    auto cmd = std::make_unique<DestroyViewCommand>(view_manager_.get(), view_id);
    Enqueue(std::move(cmd));
    LOG_F(INFO, "DestroyView: queued destroy request for view {}", view_id.get());
  }

  void EditorModule::ShowView(ViewId view_id) {
    if (!view_manager_)
      return;

    // Create a command that will execute on the engine thread during
    // OnSceneMutation. This ensures the operation is executed in-frame and
    // avoids immediate state transitions from off-thread callers.
    auto cmd = std::make_unique<ShowViewCommand>(view_manager_.get(), view_id);
    Enqueue(std::move(cmd));
    LOG_F(INFO, "ShowView: queued show request for view {}", view_id.get());
  }

  void EditorModule::HideView(ViewId view_id) {
    if (!view_manager_)
      return;

    auto cmd = std::make_unique<HideViewCommand>(view_manager_.get(), view_id);
    Enqueue(std::move(cmd));
    LOG_F(INFO, "HideView: queued hide request for view {}", view_id.get());
  }

  void EditorModule::Enqueue(std::unique_ptr<EditorCommand> cmd) {
    command_queue_.Enqueue(std::move(cmd));
  }

  void EditorModule::SetViewCameraPreset(ViewId view_id,
    CameraViewPreset preset) {
    if (view_id == kInvalidViewId || !view_manager_) {
      return;
    }

    auto cmd = std::make_unique<SetViewCameraPresetCommand>(
      view_manager_.get(), view_id, preset);
    command_queue_.Enqueue(std::move(cmd));
  }

  void EditorModule::SetViewSceneCamera(ViewId view_id,
    std::optional<UuidKey> camera_node_id) {
    if (view_id == kInvalidViewId || !view_manager_) {
      return;
    }

    auto cmd = std::make_unique<SetViewSceneCameraCommand>(
      view_manager_.get(), view_id, camera_node_id);
    command_queue_.Enqueue(std::move(cmd));
  }

  void EditorModule::SetViewScenePilot(ViewId view_id, bool pilot) {
    if (view_id == kInvalidViewId || !view_manager_) {
      return;
    }

    command_queue_.Enqueue(std::make_unique<SetViewScenePilotCommand>(
      view_manager_.get(), view_id, pilot));
  }

  void EditorModule::QueryViewCameraPose(ViewId view_id, UuidKey node_id,
    std::function<void(std::optional<EditorCameraPose>)> callback) {
    if (view_id == kInvalidViewId || !view_manager_) {
      callback(std::nullopt);
      return;
    }

    command_queue_.Enqueue(std::make_unique<QueryViewCameraPoseCommand>(
      view_manager_.get(), view_id, node_id, std::move(callback)));
  }

  void EditorModule::QueryViewEditorCamera(ViewId view_id,
    std::function<void(std::optional<EditorCameraState>)> callback) {
    if (view_id == kInvalidViewId || !view_manager_) {
      callback(std::nullopt);
      return;
    }

    command_queue_.Enqueue(std::make_unique<QueryViewEditorCameraCommand>(
      view_manager_.get(), view_id, std::move(callback)));
  }

  void EditorModule::SetViewCameraControlMode(
    ViewId view_id,
    EditorViewportCameraControlMode mode) {
    if (view_id == kInvalidViewId || !view_manager_) {
      return;
    }

    auto cmd = std::make_unique<SetViewCameraControlModeCommand>(
      view_manager_.get(), view_id, mode);
    command_queue_.Enqueue(std::move(cmd));
  }

  void EditorModule::SetViewCameraMovementSpeed(
    ViewId view_id,
    float speed_units_per_second) {
    if (view_id == kInvalidViewId || !view_manager_) {
      return;
    }

    auto cmd = std::make_unique<SetViewCameraMovementSpeedCommand>(
      view_manager_.get(), view_id, speed_units_per_second);
    command_queue_.Enqueue(std::move(cmd));
  }

  void EditorModule::SetViewRenderOptions(
    ViewId view_id, const EditorViewRenderOptions& options) {
    if (view_id == kInvalidViewId || !view_manager_) {
      return;
    }

    command_queue_.Enqueue(std::make_unique<SetViewRenderOptionsCommand>(
      view_manager_.get(), view_id, options));
  }

  auto EditorModule::GetFrameStatistics() const noexcept
    -> EditorFrameStatistics {
    return UnpackFrameStatistics(
      frame_statistics_.load(std::memory_order_relaxed));
  }

  void EditorModule::SetViewCameraSettings(
    ViewId view_id,
    float field_of_view_y_radians,
    float near_plane,
    float far_plane) {
    if (view_id == kInvalidViewId || !view_manager_) {
      return;
    }

    auto cmd = std::make_unique<SetViewCameraSettingsCommand>(
      view_manager_.get(), view_id, field_of_view_y_radians, near_plane,
      far_plane);
    command_queue_.Enqueue(std::move(cmd));
  }

  void EditorModule::SetCookedContentPaused(bool paused,
    std::function<void(bool, std::string)> complete) {
    std::function<void(bool, std::string)> superseded;
    {
      std::lock_guard lock(roots_mutex_);
      requested_content_pause_ = paused;
      superseded = std::move(content_pause_completion_);
      content_pause_completion_ = std::move(complete);
    }
  }

  auto EditorModule::ProcessContentPauseAsync(engine::FrameContext& frame_context) -> co::Co<> {
    std::optional<bool> requested;
    std::uint64_t revision = 0;
    std::function<void(bool, std::string)> complete;
    std::function<void(bool, std::string)> canceled_roots;
    {
      std::lock_guard lock(roots_mutex_);
      requested = std::exchange(requested_content_pause_, std::nullopt);
      if (!requested) {
        co_return;
      }
      complete = std::move(content_pause_completion_);
      revision = roots_revision_;
      if (*requested) {
        canceled_roots = std::move(pending_roots_completion_);
      }
    }
    // Releasing a replaced completion cancels its managed waiter outside the lock.
    canceled_roots = {};
    if (!*requested) {
      if (scene_ && asset_requests_) {
        asset_requests_->ResumeLoads(*scene_);
      }
      content_resume_completion_ = std::move(complete);
      co_return;
    }
    preview_paused_ = true;
    if (asset_requests_) {
      asset_requests_->SuspendLoads();
    }
    for (const auto* view : view_manager_->GetAllRegisteredViews()) {
      if (view) {
        RemovePublishedRuntimeViewForIntent(view->GetViewId(), &frame_context);
      }
    }
    try {
      if (asset_loader_) {
        co_await asset_loader_->WaitForPendingLoadsAsync();
        if (scene_ && asset_requests_) {
          asset_requests_->Drain(*scene_);
        }
      }
      {
        std::lock_guard lock(roots_mutex_);
        if (revision == roots_revision_) {
          roots_dirty_ = false;
        }
      }
      complete(true, {});
    } catch (const std::exception& error) {
      complete(false, error.what());
    }
  }

  auto EditorModule::SynchronizeCookedRootsAsync() -> co::Co<bool> {
    CookedRootSet roots;
    std::uint64_t revision = 0;
    std::function<void(bool, std::string)> complete;
    std::function<void(bool, std::string)> superseded;
    std::optional<content::MountRetirement> retirement;
    {
      std::lock_guard lock(roots_mutex_);
      if (!roots_dirty_ || !asset_loader_ || !path_resolver_) {
        co_return false;
      }
      roots = mounted_roots_;
      revision = roots_revision_;
      complete = std::move(pending_roots_completion_);
      roots_dirty_ = false;
    }
    try {
      // New scene requests wait until this mutation phase finishes. Source and
      // resolver preparation perform their filesystem work on the worker pool.
      co_await asset_loader_->WaitForPendingLoadsAsync();
      const auto platform = engine_->GetPlatformShared();
      if (!platform || !platform->HasThreads()) {
        throw std::logic_error("Content mount preparation requires platform workers");
      }
      auto next_resolver = co_await platform->Threads().Run([paths = roots] {
        auto next = std::make_unique<content::VirtualPathResolver>();
        if (paths) {
          for (const auto& binding : *paths) {
            next->AddLooseCookedRoot(binding.path);
          }
        }
        return next;
      });
      std::vector<std::filesystem::path> paths;
      if (roots) {
        paths.reserve(roots->size());
        for (const auto& root : *roots) {
          paths.push_back(root.path);
        }
      }
      auto prepared = co_await asset_loader_->PrepareLooseCookedRootsAsync(std::move(paths));
      co_await asset_loader_->WaitForPendingLoadsAsync();
      std::lock_guard lock(roots_mutex_);
      if (revision != roots_revision_) {
        throw content::OperationCancelledException("Cooked root request was superseded");
      }
      retirement.emplace(asset_loader_->CommitPreparedMounts(std::move(prepared)));
      path_resolver_->Swap(*next_resolver);
      active_roots_ = roots;
      if (asset_requests_) {
        asset_requests_->SetCookedRoots(active_roots_);
      }
      superseded = std::move(active_roots_completion_);
      active_roots_completion_ = std::move(complete);
    } catch (const co::TaskCancelledException&) {
      throw;
    } catch (const content::OperationCancelledException&) {
      co_return false;
    } catch (const std::exception& error) {
      if (complete) {
        complete(false, error.what());
      }
      LOG_F(ERROR, "Cooked roots could not be refreshed: {}", error.what());
      co_return false;
    }
    // Both owners are visible before any eviction subscriber can reload.
    retirement->Finish();
    co_return true;
  }

  void EditorModule::ReplaceCookedRoots(std::vector<CookedRootBinding> roots,
    std::function<void(bool, std::string)> complete)
  {
    auto bindings = std::make_shared<const std::vector<CookedRootBinding>>(
      std::move(roots));
    std::function<void(bool, std::string)> superseded;
    {
      std::lock_guard lock(roots_mutex_);
      mounted_roots_ = std::move(bindings);
      superseded = std::move(pending_roots_completion_);
      pending_roots_completion_ = std::move(complete);
      ++roots_revision_;
      roots_dirty_ = true;
    }
  }

  void EditorModule::ApplyDestroyScene(
    bool destroy_views,
    engine::FrameContext* frame_context) {
    LOG_F(INFO, "EditorModule::ApplyDestroyScene: destroying current scene");
    asset_requests_.reset();
    // Ensure all views are destroyed/cleaned up before releasing the scene.
    if (destroy_views && view_manager_) {
      try {
        for (auto* view : view_manager_->GetAllViews()) {
          if (view != nullptr && view->GetViewId() != kInvalidViewId) {
            RemovePublishedRuntimeViewForIntent(
              view->GetViewId(), frame_context);
          }
        }
        view_manager_->DestroyAllViews();
      } catch (const std::exception& e) {
        LOG_F(ERROR, "ApplyDestroyScene: DestroyAllViews failed: {}", e.what());
      } catch (...) {
        LOG_F(ERROR, "ApplyDestroyScene: DestroyAllViews failed: unknown");
      }
    }

    // Clear all node GUID to native handle mappings so they can be re-registered
    // if the same scene (or another scene using the same node IDs) is reloaded.
    try {
      NodeRegistry::ClearAll();
    } catch (const std::exception& e) {
      LOG_F(ERROR, "ApplyDestroyScene: NodeRegistry::ClearAll failed: {}",
        e.what());
    } catch (...) {
      LOG_F(ERROR, "ApplyDestroyScene: NodeRegistry::ClearAll failed: unknown");
    }

    // Reset scene after views have been released to avoid traversals seeing
    // an invalid scene during frame phases.
    scene_generation_->fetch_add(1U);
    scene_.reset();
  }

  void EditorModule::RemovePublishedRuntimeViewForIntent(
    ViewId view_id,
    engine::FrameContext* frame_context) {
    if (view_id == kInvalidViewId || engine_ == nullptr) {
      return;
    }

    auto renderer_opt = engine_->GetModule<oxygen::vortex::Renderer>();
    if (!renderer_opt.has_value()) {
      return;
    }

    auto& renderer = renderer_opt->get();
    if (frame_context != nullptr) {
      renderer.RemovePublishedRuntimeView(*frame_context, view_id);
    } else {
      renderer.RemovePublishedRuntimeView(view_id);
    }
  }

  auto EditorModule::SyncSurfacesWithFrameContext(
    engine::FrameContext& context,
    const std::vector<std::shared_ptr<graphics::Surface>>& surfaces) -> void {
    std::unordered_set<const graphics::Surface*> desired;
    desired.reserve(surfaces.size());
    for (const auto& surface : surfaces) {
      DCHECK_NOTNULL_F(surface);
      desired.insert(surface.get());
    }

    // Get current surfaces from context
    auto current_surfaces = context.GetSurfaces();

    // Identify surfaces to remove
    std::vector<size_t> removal_indices;
    for (size_t i = 0; i < current_surfaces.size(); ++i) {
      if (current_surfaces[i]) {
        if (!desired.contains(current_surfaces[i].get())) {
          removal_indices.push_back(i);
        }
      }
    }

    // Remove from back to front to preserve indices
    std::sort(removal_indices.rbegin(), removal_indices.rend());
    for (auto index : removal_indices) {
      context.RemoveSurfaceAt(index);
    }

    // Refresh current surfaces after removal
    current_surfaces = context.GetSurfaces();
    std::unordered_set<const graphics::Surface*> existing;
    for (const auto& s : current_surfaces) {
      if (s)
        existing.insert(s.get());
    }

    // Add new surfaces
    for (const auto& surface : surfaces) {
      if (!existing.contains(surface.get())) {
        context.AddSurface(
          oxygen::observer_ptr<oxygen::graphics::Surface>{surface.get()});
      }
    }

    // Presentation ownership belongs to the renderer. A surface becomes
    // presentable only after Vortex composites into that exact target in the
    // compositing phase.
  }

} // namespace oxygen::interop::module
