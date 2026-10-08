//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once
#pragma managed(push, off)

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <vector>

#include <glm/vec2.hpp>

#include <Oxygen/Core/Types/View.h>
#include <Oxygen/Vortex/Types/ViewOverlay.h>

#include <EditorModule/InputAccumulator.h>
#include <EditorModule/NodeRegistry.h>
#include <EditorModule/SceneHelpers.h>
#include <EditorModule/TransformGizmo.h>

namespace oxygen::scene {
  class Scene;
} // namespace oxygen::scene

namespace oxygen::interop::module {

  class EditorView;

  //! What the editor shows a gizmo for; set from the UI thread.
  struct TransformGizmoSettings {
    TransformTool tool { TransformTool::kTranslate };
    TransformSpace space { TransformSpace::kWorld };
    TransformSnap snap {};
    //! The nodes the gizmo moves: the selection without locked nodes and
    //! without nodes whose ancestor is also selected.
    std::vector<UuidKey> targets;
    //! The node whose origin is the pivot; the last target when absent.
    std::optional<UuidKey> active;
    //! Physical pixels per device-independent pixel.
    float display_scale { 1.0F };
  };

  enum class TransformGizmoEventKind : std::uint8_t {
    //! The pointer entered or left the gizmo's handles in a view.
    kHover = 0,
    kBegin,
    kUpdate,
    kCommit,
    kCancel,
    //! An orientation triad axis was clicked: `handle` is kX, kY or kZ.
    kViewAxis,
  };

  //! A gizmo interaction the editor applies to the authored scene.
  /*!
   Scene helpers report through the same events: their hover, their handle
   drags, which edit one light's value, and orientation triad clicks.
  */
  struct TransformGizmoEvent {
    TransformGizmoEventKind kind { TransformGizmoEventKind::kHover };
    ViewId view { kInvalidViewId };
    TransformTool tool { TransformTool::kTranslate };
    GizmoHandle handle { GizmoHandle::kNone };
    //! The drag began with Alt held: it moves copies of the targets.
    bool duplicate { false };
    //! For kHover: the pointer is over a handle.
    bool hovering { false };
    //! False while the pointer asks for a result the nodes cannot represent.
    bool representable { true };
    //! For kUpdate and kCommit: every target's new local transform.
    std::vector<GizmoTargetResult> targets;
    GizmoReadout readout {};
    //! The pivot and the pointer in the view's pixels.
    std::optional<glm::vec2> pivot_pixel;
    glm::vec2 pointer_pixel { 0.0F };
    //! The event comes from a scene helper or the orientation triad.
    bool helper { false };
    //! For helper drags: the light edited, the handle and its value, in
    //! metres or radians.
    UuidKey node {};
    HelperHandle helper_handle { HelperHandle::kNone };
    float value { 0.0F };
  };

  //! Hit tests, drags and draws the transform gizmo of the selection.
  /*!
   The controller reads the selection's transforms from the scene and never
   writes them: drag results go to the listener, and the editor applies them
   through its authoring commands. A press on a handle is taken out of the
   view's input, so navigation and picking do not see the gizmo's drags.
  */
  class TransformGizmoController {
  public:
    using Listener = std::function<void(const TransformGizmoEvent&)>;

    //! Replaces the settings; a running drag with other targets or another
    //! tool is cancelled at the next frame. Callable from any thread.
    void SetSettings(TransformGizmoSettings settings);
    //! Cancels a running drag at the next frame. Callable from any thread.
    void RequestCancel() noexcept;
    //! Receives events on the engine thread. Callable from any thread; once it
    //! returns, the previous listener is no longer called.
    void SetListener(Listener listener);

    //! Applies requests made since the last frame; the engine thread calls it
    //! before input. A new scene generation cancels the drag.
    void BeginFrame(scene::Scene* scene, std::uint64_t scene_generation);
    //! Handles a view's input before navigation sees it, removing what the
    //! gizmo takes.
    void ProcessInput(EditorView& view, AccumulatedInput& input);
    //! Adds the gizmo's geometry for a view this frame to `overlay`.
    void BuildOverlay(EditorView& view, vortex::ViewOverlay& overlay);
    //! True while the pointer is over a gizmo handle in `view`.
    [[nodiscard]] auto IsHovering(ViewId view) const noexcept -> bool
    {
      return view == hover_view_ && hovered_ != GizmoHandle::kNone;
    }

    //! What a view shows, as the gizmo projects it; none for a view without
    //! a camera or extent.
    [[nodiscard]] static auto CameraOf(EditorView& view)
      -> std::optional<GizmoCamera>;

  private:
    struct Resolution {
      GizmoFrame frame;
      std::vector<GizmoTargetStart> targets;
    };

    [[nodiscard]] auto Resolve() const -> std::optional<Resolution>;
    void SetHover(ViewId view, GizmoHandle handle);
    void CancelDrag();
    void Emit(TransformGizmoEvent event);
    auto MakeDragEvent(TransformGizmoEventKind kind, const GizmoCamera& camera)
      const -> TransformGizmoEvent;

    std::mutex mutex_;
    TransformGizmoSettings requested_;
    bool settings_changed_ { false };
    //! Held while the listener runs, so replacing it waits for the call.
    std::mutex listener_mutex_;
    Listener listener_;
    std::atomic<bool> cancel_requested_ { false };

    // Engine thread only.
    TransformGizmoSettings settings_;
    scene::Scene* scene_ { nullptr };
    std::uint64_t scene_generation_ { 0U };
    ViewId hover_view_ { kInvalidViewId };
    GizmoHandle hovered_ { GizmoHandle::kNone };
    std::optional<GizmoDrag> drag_;
    ViewId drag_view_ { kInvalidViewId };
    bool drag_duplicate_ { false };
    bool control_down_ { false };
    bool alt_down_ { false };
    bool swallow_left_release_ { false };
    bool swallow_right_release_ { false };
  };

} // namespace oxygen::interop::module

#pragma managed(pop)
