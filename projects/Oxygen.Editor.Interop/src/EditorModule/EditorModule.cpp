//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma unmanaged

#include <algorithm>
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Core/FrameContext.h>
#include <Oxygen/Core/Types/Frame.h>
#include <Oxygen/Graphics/Common/CommandRecorder.h>
#include <Oxygen/Graphics/Common/Framebuffer.h>
#include <Oxygen/Graphics/Common/Texture.h>
#include <Oxygen/Graphics/Common/Types/Color.h>
#include <Oxygen/Graphics/Common/Types/ResourceStates.h>
#include <Oxygen/Graphics/Common/Types/QueueRole.h>
#include <Oxygen/Renderer/Renderer.h>
#include <Oxygen/Renderer/SceneCameraViewResolver.h>
#include <Oxygen/Core/Types/ViewResolver.h>
#include <Oxygen/Scene/Camera/Perspective.h>
#include <Oxygen/Scene/Scene.h>
#include <Oxygen/Scene/SceneTraversal.h>

#include "EditorModule/EditorModule.h"
#include "EditorModule/NodeRegistry.h"
#include "EditorModule/RenderGraph.h"

namespace oxygen::interop::module {

  using namespace oxygen;
  using namespace oxygen::renderer;

  EditorModule::EditorModule(std::shared_ptr<SurfaceRegistry> registry)
    : registry_(std::move(registry)) {
    if (registry_ == nullptr) {
      LOG_F(ERROR, "EditorModule construction failed: surface registry is null!");
      throw std::invalid_argument(
        "EditorModule requires a non-null surface registry.");
    }
    view_manager_ = std::make_unique<ViewManager>();
  }

  EditorModule::~EditorModule() { LOG_F(INFO, "EditorModule destroyed."); }

  auto EditorModule::OnAttached(observer_ptr<AsyncEngine> engine) noexcept
    -> bool {
    graphics_ = engine->GetGraphics();
    if (auto gfx = graphics_.lock()) {
      compositor_ = std::make_unique<EditorCompositor>(gfx, *view_manager_);
    }
    // Keep a non-owning reference to the engine so we can access other
    // engine modules (renderer) during command recording.
    engine_ = engine;
    return true;
  }

  auto EditorModule::OnFrameStart(engine::FrameContext& context) -> void {
    LOG_SCOPE_F(1, "EditorModule::OnFrameStart");
    DCHECK_NOTNULL_F(registry_);

    // Process pending view operations
    if (view_manager_ && scene_) {
      view_manager_->OnFrameStart(*scene_, context);
    }

    ProcessSurfaceRegistrations();
    ProcessSurfaceDestructions();
    auto surfaces = ProcessResizeRequests();
    SyncSurfacesWithFrameContext(context, surfaces);

    // After scene creation/loading:
    if (scene_) {
      context.SetScene(observer_ptr{ scene_.get() });
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

        // Note: EditorView and EditorCompositor resources should be released or resized
        // in response to surface resize.
        if (compositor_) {
          compositor_->CleanupSurface(*surface);
        }

        surface->Resize();

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
    // Drain the command queue and execute all pending commands
    CommandContext cmd_context{ scene_.get() };
    command_queue_.Drain([&](std::unique_ptr<EditorCommand>& cmd) {
      if (cmd) {
        cmd->Execute(cmd_context);
      }
    });

    if (scene_ && !graphics_.expired() && view_manager_) {
      auto gfx = graphics_.lock();

      // Iterate over all registered views
      for (auto* view : view_manager_->GetAllRegisteredViews()) {
        if (!view) {
          continue;
        }

        // Prepare context for this view (no recorder in this phase)
        EditorViewContext view_ctx {
          .frame_context = context,
          .graphics = *gfx,
          .recorder = nullptr
        };

        view->SetRenderingContext(view_ctx);
        view->OnSceneMutation();
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
        // Iterate over all registered views and allow them to prepare for rendering
        for (auto* view : view_manager_->GetAllRegisteredViews()) {
          if (view) {
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

  auto EditorModule::CreateScene(std::string_view name) -> void {
    LOG_F(INFO, "EditorModule::CreateScene called: name='{}'", name);
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
    // Forward to view manager (thread-safe, queues into pending creates)
    if (view_manager_) {
      view_manager_->CreateViewAsync(std::move(config), std::move(callback));
    }
    else {
      // If our view manager is not available, invoke callback with failure
      if (callback) callback(false, kInvalidViewId);
    }
  }

  void EditorModule::Enqueue(std::unique_ptr<EditorCommand> cmd) {
    command_queue_.Enqueue(std::move(cmd));
  }

  auto EditorModule::SyncSurfacesWithFrameContext(
    engine::FrameContext& context,
    const std::vector<std::shared_ptr<graphics::Surface>>& surfaces)
      -> void {
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
      if (s) existing.insert(s.get());
    }

    // Add new surfaces
    for (const auto& surface : surfaces) {
      if (!existing.contains(surface.get())) {
        context.AddSurface(oxygen::observer_ptr<oxygen::graphics::Surface> { surface.get() });
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
