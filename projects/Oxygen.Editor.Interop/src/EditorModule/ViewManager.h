//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause.
//===----------------------------------------------------------------------===//

#pragma once
#pragma managed(push, off)

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

      // Immediate: Create and register a view now using the active FrameContext.
      // This must be called while OnFrameStart has set a valid FrameContext.
      // If no active FrameContext exists this will fail and invoke the callback
      // with failure.
      void CreateViewNow(EditorView::Config config, OnViewCreated callback);

    // Sync: Fire-and-forget operations
    void DestroyView(ViewId engine_id); // Destroys completely
    bool RegisterView(
      ViewId engine_id); // Add to FrameContext (returns false if bad ID)
    void UnregisterView(
      ViewId engine_id); // Remove from FrameContext (keeps resources)

    // Engine thread hooks called by EditorModule for FrameStart processing.
    // Hooks used in the engine frame cycle.
    // OnFrameStart - called at the start of the frame and provides the
    // ViewManager a transient FrameContext and Scene so FrameStart commands
    // executed by EditorModule can perform immediate registrations.
    void OnFrameStart(scene::Scene& scene, engine::FrameContext& frame_ctx);

    // FinalizeViews - called by EditorModule after FrameStart command
    // processing completes. FinalizeViews performs per-view updates using the
    // previously-provided FrameContext and then clears the transient pointers.
    void FinalizeViews();

    // Called when a surface is resized to update dependent views
    void OnSurfaceResized(graphics::Surface* surface);

    // Accessors
    auto GetView(ViewId engine_id) -> EditorView*;
    auto GetAllViews() -> std::vector<EditorView*>; // All views
    auto GetAllRegisteredViews()
      -> std::vector<EditorView*>; // Only views in FrameContext

  private:
    struct ViewEntry {
      std::unique_ptr<EditorView> view;
      bool is_registered; // Is in FrameContext
    };

    // No deferred creates are kept in the ViewManager; creation is driven
    // by commands executed during FrameStart and performed immediately via
    // CreateViewNow.

    // Returns true if a transient frame context is currently set (OnFrameStart
    // has been called but FinalizeViews has not yet been called). Non-owning.
    bool HasActiveFrameContext() const;

    mutable std::mutex mutex_;
    // pending_creates_ removed; EditorModule manages command queuing.
    observer_ptr<engine::FrameContext> active_frame_ctx_{}; // non-owning pointer
    std::unordered_map<ViewId, ViewEntry> views_;
  };

} // namespace oxygen::interop::module

#pragma managed(pop)
