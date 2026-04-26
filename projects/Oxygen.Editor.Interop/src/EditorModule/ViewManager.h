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

#include <EditorModule/EditorView.h>

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
    using OnViewCreated = std::function<void(bool success, ViewId view_id)>;

    ViewManager();
    ~ViewManager();

    OXYGEN_MAKE_NON_COPYABLE(ViewManager)
      OXYGEN_MAKE_NON_MOVABLE(ViewManager)

      // Immediate: create a stable editor view intent. The Vortex renderer is
      // the only owner of FrameContext view publication; this manager keeps the
      // editor lifecycle id used as the runtime composition intent id.
      void CreateViewNow(EditorView::Config config, OnViewCreated callback);

    // Sync: Fire-and-forget operations
    void DestroyView(ViewId engine_id); // Destroys completely
    // Destroy all views immediately. Must be called on the engine thread
    // while holding the frame-phase invariants (no concurrent frame updates).
    void DestroyAllViews();
    bool RegisterView(
      ViewId view_id); // Marks visible for publication (returns false if bad ID)
    void UnregisterView(
      ViewId view_id); // Marks hidden for publication (keeps resources)

    // Engine thread hooks called by EditorModule for FrameStart processing.
    // Hooks used in the engine frame cycle.
    // OnFrameStart - called at the start of the frame and provides the
    // ViewManager a transient FrameContext and Scene so FrameStart commands
    // executed by EditorModule can perform immediate registrations.
    void OnFrameStart(engine::FrameContext& frame_ctx);

    // FinalizeViews - called by EditorModule after FrameStart command
    // processing completes. It only clears transient FrameStart state; runtime
    // view publication is handled by EditorModule::OnPublishViews.
    void FinalizeViews();

    // Called when a surface is resized to update dependent views
    void OnSurfaceResized(graphics::Surface* surface);

    // Accessors
    auto GetView(ViewId engine_id) -> EditorView*;
    auto GetAllViews() -> std::vector<EditorView*>; // All views
    auto GetAllRegisteredViews()
      -> std::vector<EditorView*>; // Only views in FrameContext

    //! Applies a camera view preset to a specific view.
    void SetCameraViewPreset(ViewId engine_id, CameraViewPreset preset);
    // Returns true if a transient frame context is currently set (OnFrameStart
    // has been called but FinalizeViews has not yet been called). Non-owning.
    bool HasActiveFrameContext() const;

  private:
    struct ViewEntry {
      std::unique_ptr<EditorView> view;
      bool is_registered; // Is in FrameContext
    };

    // No deferred creates are kept in the ViewManager; creation is driven
    // by commands executed during FrameStart and performed immediately via
    // CreateViewNow.


    mutable std::mutex mutex_;
    // pending_creates_ removed; EditorModule manages command queuing.
    observer_ptr<engine::FrameContext> active_frame_ctx_{}; // non-owning pointer
    std::unordered_map<ViewId, ViewEntry> views_;
    uint64_t next_view_id_ { 1U };
  };

} // namespace oxygen::interop::module

#pragma managed(pop)
