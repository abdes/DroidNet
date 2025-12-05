//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause.
//===----------------------------------------------------------------------===//

#include "ViewManager.h"

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Core/FrameContext.h>
#include <Oxygen/Scene/Scene.h>

namespace oxygen::interop::module {

ViewManager::ViewManager() = default;
ViewManager::~ViewManager() = default;

void ViewManager::CreateViewAsync(EditorView::Config config, OnViewCreated callback)
{
  std::lock_guard<std::mutex> lock(mutex_);

  PendingCreate entry;
  entry.config = std::move(config);
  entry.callback = std::move(callback);

  pending_creates_.emplace_back(std::move(entry));

  LOG_F(INFO, "ViewManager: Queued view creation '{}'", entry.config.name);
}

void ViewManager::DestroyView(ViewId engine_id)
{
  std::lock_guard<std::mutex> lock(mutex_);

  auto it = views_.find(engine_id);
  if (it != views_.end()) {
    it->second.view->ReleaseResources();
    views_.erase(it);
    LOG_F(INFO, "ViewManager: Destroyed view with id {}", engine_id.get());
  } else {
    LOG_F(WARNING, "ViewManager: Attempted to destroy invalid view id {}", engine_id.get());
  }
}

bool ViewManager::RegisterView(ViewId engine_id)
{
  std::lock_guard<std::mutex> lock(mutex_);

  auto it = views_.find(engine_id);
  if (it == views_.end()) {
    LOG_F(WARNING, "ViewManager: RegisterView failed - invalid view id {}", engine_id.get());
    return false;
  }

  it->second.is_registered = true;
  LOG_F(INFO, "ViewManager: Registered view {}", engine_id.get());
  return true;
}

void ViewManager::UnregisterView(ViewId engine_id)
{
  std::lock_guard<std::mutex> lock(mutex_);

  auto it = views_.find(engine_id);
  if (it != views_.end()) {
    it->second.is_registered = false;
    LOG_F(INFO, "ViewManager: Unregistered view {}", engine_id.get());
  }
}

void ViewManager::OnFrameStart(scene::Scene& scene, engine::FrameContext& frame_ctx)
{
  // Process pending creates
  auto pending = DrainPendingCreates();

  for (auto& pc : pending) {
    try {
      // Create EditorView
      auto view = std::make_unique<EditorView>(pc.config);

      // Initialize with scene
      view->Initialize(scene);

      // Build ViewContext for FrameContext registration
      engine::ViewContext vc {
        .view = View {},  // Default view config
        .metadata = engine::ViewMetadata {
          .name = pc.config.name,
          .purpose = pc.config.purpose
        },
        .output = nullptr
      };

      // Register with FrameContext - this assigns the engine ViewId
      ViewId engine_id = frame_ctx.RegisterView(std::move(vc));

      // Store in views map
      views_[engine_id] = ViewEntry{ std::move(view), true };

      LOG_F(INFO, "ViewManager: Created view '{}' with engine id {}",
            pc.config.name, engine_id.get());

      // Invoke callback with success
      if (pc.callback) {
        pc.callback(true, engine_id);
      }

    } catch (const std::exception& e) {
      LOG_F(ERROR, "ViewManager: Failed to create view '{}': {}",
            pc.config.name, e.what());

      if (pc.callback) {
        pc.callback(false, kInvalidViewId);
      }
    } catch (...) {
      LOG_F(ERROR, "ViewManager: Failed to create view '{}': unknown error",
            pc.config.name);

      if (pc.callback) {
        pc.callback(false, kInvalidViewId);
      }
    }
  }

  // Update FrameContext for all registered views
  std::lock_guard<std::mutex> lock(mutex_);
  for (auto& [id, entry] : views_) {
    if (entry.is_registered) {
      engine::ViewContext vc {
        .view = View {},
        .metadata = engine::ViewMetadata {
          .name = entry.view->GetConfig().name,
          .purpose = entry.view->GetConfig().purpose
        },
        .output = nullptr
      };

      frame_ctx.UpdateView(id, std::move(vc));
    }
  }
}

auto ViewManager::GetView(ViewId engine_id) -> EditorView*
{
  std::lock_guard<std::mutex> lock(mutex_);

  auto it = views_.find(engine_id);
  return (it != views_.end()) ? it->second.view.get() : nullptr;
}

auto ViewManager::GetAllViews() -> std::vector<EditorView*>
{
  std::lock_guard<std::mutex> lock(mutex_);

  std::vector<EditorView*> result;
  result.reserve(views_.size());

  for (auto& [id, entry] : views_) {
    result.push_back(entry.view.get());
  }

  return result;
}

auto ViewManager::GetAllRegisteredViews() -> std::vector<EditorView*>
{
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

auto ViewManager::DrainPendingCreates() -> std::vector<PendingCreate>
{
  std::lock_guard<std::mutex> lock(mutex_);

  std::vector<PendingCreate> result;
  result.swap(pending_creates_);

  return result;
}

} // namespace oxygen::interop::module
