//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma unmanaged

#include "pch.h"

#include <Commands/CreateSceneCommand.h>
#include <Commands/CreateViewCommand.h>
#include <Commands/DestroySceneCommand.h>
#include <Commands/DestroyViewCommand.h>
#include <Commands/HideViewCommand.h>
#include <Commands/SetViewCameraPresetCommand.h>
#include <Commands/ShowViewCommand.h>
#include <EditorModule/EditorCommand.h>
#include <EditorModule/EditorCompositor.h>
#include <EditorModule/EditorModule.h>
#include <EditorModule/EditorViewportNavigation.h>
#include <EditorModule/InputAccumulatorAdapter.h>
#include <EditorModule/NodeRegistry.h>
#include <EditorModule/SurfaceRegistry.h>

#include <Oxygen/Content/AssetLoader.h>
#include <Oxygen/Content/VirtualPathResolver.h>

namespace oxygen::interop::module {

  using namespace oxygen;
  using namespace oxygen::renderer;

  namespace {
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
    view_manager_ = std::make_unique<ViewManager>();
    input_accumulator_ = std::make_unique<InputAccumulator>();
    viewport_navigation_ = std::make_unique<EditorViewportNavigation>();
  }

  EditorModule::~EditorModule() { LOG_F(INFO, "EditorModule destroyed."); }

  auto EditorModule::OnAttached(observer_ptr<AsyncEngine> engine) noexcept
    -> bool {
    DCHECK_NOTNULL_F(engine);

    auto writer = std::make_unique<EditorInputWriter>(
      engine->GetPlatform().Input().ForWrite());
    input_accumulator_adapter_ =
      std::make_unique<InputAccumulatorAdapter>(std::move(writer));

    graphics_ = engine->GetGraphics();
    if (auto gfx = graphics_.lock()) {
      compositor_ =
        std::make_unique<EditorCompositor>(gfx, *view_manager_, *registry_);
    }

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

  auto EditorModule::OnFrameStart(engine::FrameContext& context) -> void {
    LOG_SCOPE_F(1, "EditorModule::OnFrameStart");
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

    if (roots_dirty_) {
      std::lock_guard lock(roots_mutex_);
      if (!asset_loader_.get()->IsRunning()) {
        LOG_F(INFO,
          "Deferring cooked-roots sync: AssetLoader not activated yet");
        return;
      }
      LOG_F(INFO, "Syncing {} cooked roots to AssetLoader and PathResolver", mounted_roots_.size());
      asset_loader_.get()->ClearMounts();
      path_resolver_->ClearMounts();
      for (const auto& root : mounted_roots_) {
        asset_loader_.get()->AddLooseCookedRoot(root);
        path_resolver_->AddLooseCookedRoot(root);
      }
      roots_dirty_ = false;
    }

    // Begin frame for the ViewManager: make the transient FrameContext
    // available so FrameStart commands (executed later in this method)
    // can perform immediate registration via ViewManager::CreateViewAsync.
    view_manager_->OnFrameStart(context);

    ProcessSurfaceRegistrations();
    ProcessSurfaceDestructions();
    auto surfaces = ProcessResizeRequests();
    SyncSurfacesWithFrameContext(context, surfaces);

    // Drain and dispatch input from the accumulator to the engine's input
    // system.
    for (auto* view : view_manager_->GetAllViews()) {
      const auto view_id = view->GetViewId();
      auto batch = input_accumulator_->Drain(view_id);

      UpdateViewRoutingFromInputBatch(view_id, batch);

      const bool has_mouse = (batch.mouse_delta.dx != 0.0F) || (batch.mouse_delta.dy != 0.0F);
      const bool has_wheel = (batch.scroll_delta.dx != 0.0F) || (batch.scroll_delta.dy != 0.0F);
      const bool has_keys = !batch.key_events.empty();
      const bool has_buttons = !batch.button_events.empty();
      if (has_mouse || has_wheel || has_keys || has_buttons) {
        LOG_F(INFO,
          "EditorModule input: draining+dispatching view={} mouse(dx={},dy={}) wheel(dx={},dy={}) keys={} buttons={} pos(x={},y={})",
          view_id.get(),
          batch.mouse_delta.dx, batch.mouse_delta.dy,
          batch.scroll_delta.dx, batch.scroll_delta.dy,
          batch.key_events.size(), batch.button_events.size(),
          batch.last_position.x, batch.last_position.y);
      }

      input_accumulator_adapter_->DispatchForView(view_id, batch);
    }

    // After surface handling, execute frame-start commands related to views
    // with a strict ordering to avoid race conditions: destroy -> create -> rest.
    CommandContext cmd_ctx{
      .Scene = observer_ptr{scene_.get()},
      .AssetLoader = observer_ptr{asset_loader_.get()},
      .PathResolver = observer_ptr{path_resolver_.get()}
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
          try {
            cmd->Execute(cmd_ctx);
          } catch (const std::exception& e) {
            LOG_F(ERROR, "Exception executing DestroyViewCommand: {}", e.what());
          } catch (...) {
            LOG_F(ERROR, "Unknown exception executing DestroyViewCommand");
          }
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
          try {
            cmd->Execute(cmd_ctx);
          } catch (const std::exception& e) {
            LOG_F(ERROR, "Exception executing CreateViewCommand: {}", e.what());
          } catch (...) {
            LOG_F(ERROR, "Unknown exception executing CreateViewCommand");
          }
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
          try {
            cmd->Execute(cmd_ctx);
          } catch (const std::exception& e) {
            LOG_F(ERROR, "Exception executing FrameStart command: {}", e.what());
          } catch (...) {
            LOG_F(ERROR, "Unknown exception executing FrameStart command");
          }
        }
      });

    context.SetScene(observer_ptr{ scene_.get() });
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
    for (const auto& pair : snapshot) {
      const auto& key = pair.first;
      const auto& surface = pair.second;

      CHECK_NOTNULL_F(surface);

      // If a resize was requested by the caller, apply it explicitly here
      if (surface->ShouldResize()) {
        DLOG_F(INFO, "Applying resize for a surface (ptr={}).",
          fmt::ptr(surface.get()));

        if (!graphics_.expired()) {
          auto gfx = graphics_.lock();
          try {
            gfx->Flush();
          }
          catch (...) {
            DLOG_F(WARNING,
              "Graphics::Flush threw during pre-resize; continuing.");
          }
        }

        // Note: EditorView and EditorCompositor resources should be released or
        // resized in response to surface resize.
        if (compositor_) {
          compositor_->CleanupSurface(*surface);
        }

        surface->Resize();

        if (view_manager_) {
          view_manager_->OnSurfaceResized(surface.get());
        }

        auto resize_callbacks = registry_->DrainResizeCallbacks(key);
        auto back = surface->GetCurrentBackBuffer();
        bool ok = (back != nullptr);
        for (auto& rcb : resize_callbacks) {
          try {
            rcb(ok);
          }
          catch (...) {
            /* swallow */
          }
        }
      }

      surfaces.emplace_back(surface);
    }

    return surfaces;
  }

  auto EditorModule::OnSceneMutation(engine::FrameContext& context) -> co::Co<> {
    // Drain only commands targeting SceneMutation. Leave other commands for
    // their appropriate phases so insertion order is preserved across phases.
    CommandContext cmd_context{
      .Scene = observer_ptr{scene_.get()},
      .AssetLoader = observer_ptr{asset_loader_.get()},
      .PathResolver = observer_ptr{path_resolver_.get()}
    };
    command_queue_.DrainIf(
      [](const std::unique_ptr<EditorCommand>& cmd) {
        return cmd &&
          cmd->GetTargetPhase() == oxygen::core::PhaseId::kSceneMutation;
      },
      [&](std::unique_ptr<EditorCommand>& cmd) {
        if (cmd) {
          try {
            cmd->Execute(cmd_context);
          } catch (const std::exception& e) {
            LOG_F(ERROR, "Exception executing editor command: {}", e.what());
          } catch (...) {
            LOG_F(ERROR, "Unknown exception executing editor command");
          }
        }
      });

    if (scene_ && !graphics_.expired() && view_manager_) {
      auto gfx = graphics_.lock();

      const auto input_blob = context.GetInputSnapshot();
      const auto input_snapshot = input_blob
        ? std::static_pointer_cast<const input::InputSnapshot>(input_blob)
        : std::shared_ptr<const input::InputSnapshot> {};

      float dt_seconds =
        std::chrono::duration<float>(context.GetGameDeltaTime().get()).count();
      if (dt_seconds <= 0.0f) {
        dt_seconds =
          std::chrono::duration<float>(context.GetFrameTiming().frameDuration)
            .count();
      }

      // Iterate over all registered views
      for (auto* view : view_manager_->GetAllRegisteredViews()) {
        if (!view) {
          continue;
        }

        // Prepare context for this view (no recorder in this phase)
        EditorViewContext view_ctx{
            .frame_context = context, .graphics = *gfx, .recorder = nullptr };

        view->SetRenderingContext(view_ctx);
        view->OnSceneMutation();

        if (input_snapshot) {
          const auto view_id = view->GetViewId();

          const auto active = (active_view_id_ != kInvalidViewId)
            ? active_view_id_
            : hover_view_id_;

          const auto hovered = hover_view_id_;

          // Non-wheel navigation applies to the focused (active) viewport.
          if (active != kInvalidViewId && view_id == active) {
            auto focus_point = view->GetFocusPoint();
            auto ortho_half_height = view->GetOrthoHalfHeight();

            // If the hovered view differs, keep wheel routing separate.
            if (hovered != kInvalidViewId && hovered != active) {
              viewport_navigation_->ApplyNonWheel(view->GetCameraNode(),
                *input_snapshot, focus_point, ortho_half_height, dt_seconds);
            }
            else {
              viewport_navigation_->Apply(view->GetCameraNode(),
                *input_snapshot, focus_point, ortho_half_height, dt_seconds);
            }

            view->SetFocusPoint(focus_point);
            view->SetOrthoHalfHeight(ortho_half_height);
          }

          // Wheel navigation applies to the last-hovered viewport.
          if (hovered != kInvalidViewId && hovered != active && view_id == hovered) {
            auto focus_point = view->GetFocusPoint();
            auto ortho_half_height = view->GetOrthoHalfHeight();
            viewport_navigation_->ApplyWheelOnly(view->GetCameraNode(),
              *input_snapshot, focus_point, ortho_half_height, dt_seconds);
            view->SetFocusPoint(focus_point);
            view->SetOrthoHalfHeight(ortho_half_height);
          }
        }
        view->ClearPhaseRecorder();
      }
    }
    co_return;
  }

  auto EditorModule::OnPreRender(engine::FrameContext& context) -> co::Co<> {
    // Ensure framebuffers are created for all surfaces
    EnsureFramebuffers();

    if (engine_ && view_manager_) {
      auto renderer_opt = engine_->GetModule<oxygen::engine::Renderer>();
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
                .frame_context = context, .graphics = *gfx, .recorder = nullptr };
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

  auto EditorModule::OnRender(engine::FrameContext& context) -> co::Co<> {
    // Rendering is handled by the Renderer module via registered views.
    // EditorModule participates in OnCompositing to blit results to surfaces.
    co_return;
  }

  auto EditorModule::OnCompositing(engine::FrameContext& context) -> co::Co<> {
    if (!compositor_) {
      co_return;
    }

    // Delegate all compositing logic to the compositor
    compositor_->OnCompositing();

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
        ApplyDestroyScene();
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
    scene_ = std::make_shared<oxygen::scene::Scene>(std::string(name));
  }

  auto EditorModule::EnsureFramebuffers() -> bool {
    auto snapshot = registry_->SnapshotSurfaces();
    for (const auto& p : snapshot) {
      const auto& surface = p.second;
      if (surface) {
        compositor_->EnsureFramebuffersForSurface(*surface);
      }
    }
    return true;
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
    // OnPreRender). This avoids use-after-free when the UI requests
    // destruction from a different thread.
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

  void EditorModule::AddLooseCookedRoot(std::string_view path) {
    LOG_F(INFO, "EditorModule::AddLooseCookedRoot: registering root '{}'",
      std::string(path));
    try {
      std::lock_guard lock(roots_mutex_);
      mounted_roots_.push_back(std::string(path));
      roots_dirty_ = true;
    } catch (const std::exception& e) {
      LOG_F(ERROR, "Failed to add loose cooked root '{}': {}", std::string(path),
        e.what());
    }
  }

  void EditorModule::ClearCookedRoots() {
    LOG_F(INFO, "EditorModule::ClearCookedRoots: clearing all mounted roots");
    try {
      std::lock_guard lock(roots_mutex_);
      mounted_roots_.clear();
      roots_dirty_ = true;
    } catch (const std::exception& e) {
      LOG_F(ERROR, "Failed to clear cooked roots: {}", e.what());
    }
  }

  void EditorModule::ApplyDestroyScene() {
    LOG_F(INFO, "EditorModule::ApplyDestroyScene: destroying current scene");
    // Ensure all views are destroyed/cleaned up before releasing the scene.
    if (view_manager_) {
      try {
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
    scene_.reset();
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

    // Mark all as presentable
    // We need to find indices again because additions happened at the end
    current_surfaces = context.GetSurfaces();
    for (size_t i = 0; i < current_surfaces.size(); ++i) {
      if (current_surfaces[i] && desired.contains(current_surfaces[i].get())) {
        context.SetSurfacePresentable(i, true);
      }
    }
  }

} // namespace oxygen::interop::module
