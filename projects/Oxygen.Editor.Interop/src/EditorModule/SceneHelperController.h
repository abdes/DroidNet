//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once
#pragma managed(push, off)

#include <atomic>
#include <cstdint>
#include <mutex>
#include <optional>
#include <unordered_set>
#include <vector>

#include <Oxygen/Core/Types/View.h>
#include <Oxygen/Vortex/Types/ViewOverlay.h>
#include <Oxygen/Vortex/Types/ViewPick.h>

#include <EditorModule/InputAccumulator.h>
#include <EditorModule/NodeRegistry.h>
#include <EditorModule/SceneHelpers.h>
#include <EditorModule/TransformGizmoController.h>

namespace oxygen::scene {
  class Scene;
} // namespace oxygen::scene

namespace oxygen::interop::module {

  class EditorView;

  //! Workspace state the helpers follow; set from the UI thread.
  struct SceneHelperSettings {
    //! Workspace-hidden nodes: no icon, no helper, not pickable.
    std::vector<UuidKey> hidden;
    //! Locked nodes keep their helpers but their handles cannot be dragged.
    std::vector<UuidKey> locked;
    //! Physical pixels per device-independent pixel.
    float display_scale { 1.0F };
  };

  //! Light and camera icons, the selected nodes' helpers with their handles,
  //! and the orientation triad of every editing view.
  /*!
   Like the transform gizmo, the controller reads the scene and never writes
   it: a handle drag reports the light's new value, and a triad click the axis
   to look along, through the gizmo's event listener. Presses it takes are
   removed from the view's input. All of it is editor overlay, never authored
   or cooked.
  */
  class SceneHelperController {
  public:
    using Listener = TransformGizmoController::Listener;

    //! Replaces the settings. Callable from any thread.
    void SetSettings(SceneHelperSettings settings);
    //! Cancels a running handle drag at the next frame. Callable from any
    //! thread.
    void RequestCancel() noexcept;
    //! Receives events on the engine thread. Callable from any thread.
    void SetListener(Listener listener);

    //! Reads this frame's lights and cameras; the engine thread calls it
    //! before input. A new scene generation cancels a drag.
    void BeginFrame(scene::Scene* scene, std::uint64_t scene_generation,
      const std::vector<UuidKey>& selected, std::optional<UuidKey> active);
    [[nodiscard]] auto IsDragging() const noexcept -> bool
    {
      return drag_.has_value();
    }
    //! Handles a view's input, removing what the helpers take. While
    //! `gizmo_has_pointer`, the transform gizmo has precedence.
    void ProcessInput(
      EditorView& view, AccumulatedInput& input, bool gizmo_has_pointer);
    //! Adds the view's icons, helpers and triad to `overlay`.
    void BuildOverlay(EditorView& view, vortex::ViewOverlay& overlay);
    //! The icons a pick rectangle of the view covers.
    [[nodiscard]] auto PickIcons(EditorView& view,
      const vortex::ViewPickRect& rect) -> std::vector<HelperIconHit>;

  private:
    //! The nodes as a view sees them: without the camera it looks through,
    //! and with the selected cameras' viewing volumes for its extent.
    [[nodiscard]] auto NodesFor(EditorView& view) const
      -> std::vector<SceneHelperNode>;
    void CollectNodes();
    void SetHover(ViewId view, std::optional<HelperHandleHit> handle,
      TriadAxis triad);
    void CancelDrag();
    void Emit(TransformGizmoEvent event);
    auto MakeDragEvent(TransformGizmoEventKind kind) const
      -> TransformGizmoEvent;

    std::mutex mutex_;
    SceneHelperSettings requested_;
    bool settings_changed_ { false };
    std::mutex listener_mutex_;
    Listener listener_;
    std::atomic<bool> cancel_requested_ { false };

    // Engine thread only.
    scene::Scene* scene_ { nullptr };
    std::uint64_t scene_generation_ { 0U };
    float display_scale_ { 1.0F };
    std::unordered_set<UuidKey, UuidKeyHash> hidden_;
    std::unordered_set<UuidKey, UuidKeyHash> locked_;
    std::unordered_set<UuidKey, UuidKeyHash> selected_;
    std::optional<UuidKey> active_;
    std::vector<SceneHelperNode> nodes_;
    ViewId hover_view_ { kInvalidViewId };
    std::optional<HelperHandleHit> hovered_handle_;
    TriadAxis hovered_triad_ { TriadAxis::kNone };
    std::optional<HelperDrag> drag_;
    ViewId drag_view_ { kInvalidViewId };
    //! The triad axis a left press landed on, and in which view.
    TriadAxis pressed_triad_ { TriadAxis::kNone };
    ViewId pressed_view_ { kInvalidViewId };
    bool alt_down_ { false };
    bool swallow_right_release_ { false };
  };

} // namespace oxygen::interop::module

#pragma managed(pop)
