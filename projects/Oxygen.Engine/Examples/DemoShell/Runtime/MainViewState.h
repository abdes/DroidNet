//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Core/Types/View.h>
#include <Oxygen/Host/SceneStage.h>
#include <Oxygen/Scene/SceneNode.h>
#include <Oxygen/Vortex/CompositionView.h>

namespace oxygen::engine {
class FrameContext;
} // namespace oxygen::engine

namespace oxygen::vortex {
class Renderer;
} // namespace oxygen::vortex

namespace oxygen::examples {

//! The temporal state of a demo's main view.
/*!
 Exposure, the screen HZB and previous-frame history belong to one scene seen
 through one camera, not to the view's id. A view without a state is
 stateless: the renderer rebuilds its products every frame and keeps no
 history. A different scene or camera starts a new state and retires the old
 one first, so no history carries over to what the view now shows.
*/
class MainViewState final {
public:
  using Handle = vortex::CompositionView::ViewStateHandle;

  //! The view's state this frame, and whether this call started it.
  struct Session {
    Handle handle { vortex::CompositionView::kInvalidViewStateHandle };
    bool started = false;
  };

  //! The state of `view_id` showing `active_scene` through `camera`; a scene or
  //! camera other than the last one retires the previous state and starts a
  //! new one. The scene must be valid and the camera alive; a view without
  //! them is not built, and its state is reset.
  [[nodiscard]] auto For(engine::FrameContext& context,
    observer_ptr<vortex::Renderer> renderer, ViewId view_id,
    const host::ActiveScene& active_scene, const scene::SceneNode& camera)
    -> Session;

  //! Retires the current state, if any. Without a frame context (outside a
  //! frame), the renderer retires it between frames.
  auto Reset(observer_ptr<engine::FrameContext> context,
    observer_ptr<vortex::Renderer> renderer, ViewId view_id) -> void;

private:
  Handle handle_ { vortex::CompositionView::kInvalidViewStateHandle };
  host::ActiveScene scene_;
  scene::SceneNode camera_;
};

} // namespace oxygen::examples
