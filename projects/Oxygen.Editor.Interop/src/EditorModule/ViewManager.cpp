//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause.
//===----------------------------------------------------------------------===//

#pragma unmanaged

#include "pch.h"

#include <EditorModule/ViewManager.h>

namespace oxygen::interop::module {

  ViewManager::ViewManager() = default;
  ViewManager::~ViewManager() = default;

  void ViewManager::CreateViewNow(EditorView::Config config,
    OnViewCreated callback) {
    std::lock_guard<std::mutex> lock(mutex_);

    CHECK_NOTNULL_F(active_frame_ctx_.get());

    try {
      auto view = std::make_unique<EditorView>(config);
      // Resolve the scene from the active FrameContext rather than caching
      // it so we don't hold cross-frame references.
      auto scene = active_frame_ctx_->GetScene();
      if (!scene) {
        LOG_F(WARNING, "CreateViewNow: frame context has no scene");
        if (callback)
          callback(false, kInvalidViewId);
        return;
      }
      view->Initialize(*scene);

      engine::ViewContext vc{
          .view = View{},
          .metadata =
              engine::ViewMetadata{
                  .name = config.name,
                  .purpose = config.purpose,
              },
          .output = nullptr,
      };

      ViewId engine_id = active_frame_ctx_->RegisterView(std::move(vc));
      view->SetViewId(engine_id);

      views_[engine_id] = ViewEntry{ std::move(view), true };

      LOG_F(INFO, "CreateViewNow created view '{}' id {}", config.name,
        engine_id.get());

      if (callback)
        callback(true, engine_id);
    }
    catch (const std::exception& e) {
      LOG_F(ERROR, "CreateViewNow failed for '{}': {}", config.name, e.what());
      if (callback)
        callback(false, kInvalidViewId);
    }
    catch (...) {
      LOG_F(ERROR, "CreateViewNow failed for '{}': unknown error", config.name);
      if (callback)
        callback(false, kInvalidViewId);
    }
  }

  void ViewManager::DestroyView(ViewId engine_id) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = views_.find(engine_id);
    if (it != views_.end()) {
      it->second.view->ReleaseResources();
      views_.erase(it);
      LOG_F(INFO, "Destroyed view with id {}", engine_id.get());
    }
    else {
      LOG_F(WARNING, "Attempted to destroy invalid view id {}", engine_id.get());
    }
  }

  void ViewManager::DestroyAllViews() {
    std::lock_guard<std::mutex> lock(mutex_);
    // Release resources for all views and clear the container. This mirrors
    // the behaviour of DestroyView but does it in-batch while holding the
    // mutex to avoid re-entrancy.
    for (auto& [id, entry] : views_) {
      if (entry.view)
        entry.view->ReleaseResources();
    }
    views_.clear();
    LOG_F(INFO, "ViewManager: destroyed all views");
  }

  bool ViewManager::RegisterView(ViewId engine_id) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = views_.find(engine_id);
    if (it == views_.end()) {
      LOG_F(WARNING, "RegisterView failed - invalid view id {}", engine_id.get());
      return false;
    }

    it->second.is_registered = true;
    LOG_F(INFO, "Registered view {}", engine_id.get());
    return true;
  }

  void ViewManager::UnregisterView(ViewId engine_id) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = views_.find(engine_id);
    if (it != views_.end()) {
      it->second.is_registered = false;
      LOG_F(INFO, "Unregistered view {}", engine_id.get());
    }
  }

  void ViewManager::OnFrameStart(engine::FrameContext& frame_ctx) {
    DLOG_F(INFO, "ViewManager::OnFrameStart (frame_ctx={}, current_phase={})",
      fmt::ptr(&frame_ctx), static_cast<int>(frame_ctx.GetCurrentPhase()));
    active_frame_ctx_ = observer_ptr{ &frame_ctx };
  }

  auto ViewManager::GetView(ViewId engine_id) -> EditorView* {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = views_.find(engine_id);
    return (it != views_.end()) ? it->second.view.get() : nullptr;
  }

  void ViewManager::SetCameraViewPreset(ViewId engine_id,
    CameraViewPreset preset) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = views_.find(engine_id);
    if (it == views_.end() || !it->second.view) {
      LOG_F(WARNING, "SetCameraViewPreset: invalid view id {}", engine_id.get());
      return;
    }

    it->second.view->SetCameraViewPreset(preset);
  }

  auto ViewManager::GetAllViews() -> std::vector<EditorView*> {
    std::lock_guard<std::mutex> lock(mutex_);

    std::vector<EditorView*> result;
    result.reserve(views_.size());

    for (auto& [id, entry] : views_) {
      result.push_back(entry.view.get());
    }

    return result;
  }

  auto ViewManager::GetAllRegisteredViews() -> std::vector<EditorView*> {
    std::lock_guard<std::mutex> lock(mutex_);

    std::vector<EditorView*> result;
    result.reserve(views_.size());

    for (auto& [id, entry] : views_) {
      if (entry.is_registered) {
        result.push_back(entry.view.get());
      }
    }

    return result;
  }

  auto ViewManager::HasActiveFrameContext() const -> bool {
    return active_frame_ctx_.get() != nullptr;
  }

  void ViewManager::OnSurfaceResized(graphics::Surface* surface) {
    DLOG_SCOPE_FUNCTION(INFO);

    std::lock_guard<std::mutex> lock(mutex_);

    if (!surface) {
      return;
    }

    uint32_t width = surface->Width();
    uint32_t height = surface->Height();

    // Try to get more accurate dimensions from backbuffer if available
    auto back = surface->GetCurrentBackBuffer();
    if (back) {
      const auto& desc = back->GetDescriptor();
      if (desc.width > 0 && desc.height > 0) {
        width = desc.width;
        height = desc.height;
      }
    }

    LOG_F(INFO, "Surface {} resized to {}x{} (backbuffer={})", fmt::ptr(surface),
      width, height, back ? "yes" : "no");

    for (auto& [id, entry] : views_) {
      if (entry.view) {
        const auto& config = entry.view->GetConfig();
        if (config.compositing_target.has_value() &&
          config.compositing_target.value() == surface) {
          LOG_F(INFO, "Resizing view '{}' to {}x{}", config.name, width, height);
          entry.view->Resize(width, height);
        }
      }
    }
  }

  // EndFrame removed: FinalizeViews is used instead.

  void ViewManager::FinalizeViews() {
    // Update FrameContext for all registered views while frame_ctx pointer is
    // still valid. Use the transient pointer set in OnFrameStart.
    CHECK_NOTNULL_F(active_frame_ctx_.get());

    if (!active_frame_ctx_->GetScene()) {
      DLOG_F(WARNING, "FinalizeViews: active FrameContext has no scene; skipping UpdateView");
      // Clear transient pointers to denote end of frame processing window.
      active_frame_ctx_ = {};
      return;
    }

    std::lock_guard<std::mutex> lock(mutex_);

    size_t registered_count = 0;
    for (const auto& pair : views_) {
      if (pair.second.is_registered) {
        ++registered_count;
      }
    }
    DLOG_F(INFO, "ViewManager::FinalizeViews (registered_views={})", registered_count);

    for (auto& [id, entry] : views_) {
      if (entry.is_registered) {
        const float w = entry.view->GetWidth();
        const float h = entry.view->GetHeight();

        View view;
        view.viewport = ViewPort{
            .top_left_x = 0.0f,
            .top_left_y = 0.0f,
            .width = w,
            .height = h,
            .min_depth = 0.0f,
            .max_depth = 1.0f,
        };
        view.scissor = Scissors{
            .left = 0,
            .top = 0,
            .right = static_cast<int32_t>(w),
            .bottom = static_cast<int32_t>(h),
        };

        engine::ViewContext vc{
            .view = view,
            .metadata =
                engine::ViewMetadata{.name = entry.view->GetConfig().name,
                                     .purpose = entry.view->GetConfig().purpose},
            .output = nullptr,
        };

        active_frame_ctx_->UpdateView(id, std::move(vc));

        DLOG_F(INFO,
          "ViewManager::FinalizeViews updated view={} size={}x{} name='{}'",
          id.get(), w, h, entry.view->GetConfig().name);
      }
    }

    // Clear transient pointers to denote end of frame processing window.
    active_frame_ctx_ = {};
  }

} // namespace oxygen::interop::module
