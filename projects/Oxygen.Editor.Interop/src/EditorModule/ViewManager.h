//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause.
//===----------------------------------------------------------------------===//

#pragma once

#include <functional>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Core/Types/View.h>

#include "EditorModule/EditorView.h"

namespace oxygen {
namespace engine {
class FrameContext;
}
namespace scene {
class Scene;
}
} // namespace oxygen

namespace oxygen::interop::module {

class ViewManager {
public:
  using OnViewCreated = std::function<void(bool success, ViewId engine_id)>;

  ViewManager();
  ~ViewManager();

  OXYGEN_MAKE_NON_COPYABLE(ViewManager)
  OXYGEN_MAKE_NON_MOVABLE(ViewManager)

  // Async: View creation (callback invoked on engine thread with
  // engine-assigned ViewId)
  void CreateViewAsync(EditorView::Config config, OnViewCreated callback);

  // Sync: Fire-and-forget operations
  void DestroyView(ViewId engine_id); // Destroys completely
  bool RegisterView(
      ViewId engine_id); // Add to FrameContext (returns false if bad ID)
  void UnregisterView(
      ViewId engine_id); // Remove from FrameContext (keeps resources)

  // Engine thread hook (called by EditorModule::OnFrameStart)
  void OnFrameStart(scene::Scene &scene, engine::FrameContext &frame_ctx);

  // Called when a surface is resized to update dependent views
  void OnSurfaceResized(graphics::Surface *surface);

  // Accessors
  auto GetView(ViewId engine_id) -> EditorView *;
  auto GetAllViews() -> std::vector<EditorView *>; // All views
  auto GetAllRegisteredViews()
      -> std::vector<EditorView *>; // Only views in FrameContext

private:
  struct PendingCreate {
    EditorView::Config config;
    OnViewCreated callback;
  };

  struct ViewEntry {
    std::unique_ptr<EditorView> view;
    bool is_registered; // Is in FrameContext
  };

  auto DrainPendingCreates() -> std::vector<PendingCreate>;

  mutable std::mutex mutex_;
  std::vector<PendingCreate> pending_creates_;
  std::unordered_map<ViewId, ViewEntry> views_;
};

} // namespace oxygen::interop::module
