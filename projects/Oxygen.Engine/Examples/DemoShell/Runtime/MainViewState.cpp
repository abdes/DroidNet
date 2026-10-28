//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <atomic>

#include "DemoShell/Runtime/MainViewState.h"

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Core/FrameContext.h>
#include <Oxygen/Core/Types/View.h>
#include <Oxygen/Host/SceneStage.h>
#include <Oxygen/Vortex/CompositionView.h>
#include <Oxygen/Vortex/Renderer.h>

namespace oxygen::examples {

namespace {

  //! A state handle no other view of the process holds.
  auto AllocateHandle() -> MainViewState::Handle
  {
    static std::atomic_uint64_t next_handle { 1U };
    const auto handle = MainViewState::Handle {
      next_handle.fetch_add(1U, std::memory_order_relaxed),
    };
    CHECK_F(handle != vortex::CompositionView::kInvalidViewStateHandle,
      "Main view state identity exhausted");
    return handle;
  }

} // namespace

auto MainViewState::For(engine::FrameContext& context,
  const observer_ptr<vortex::Renderer> renderer, const ViewId view_id,
  const host::ActiveScene& active_scene, const scene::SceneNode& camera)
  -> Session
{
  CHECK_F(active_scene.IsValid() && camera.IsAlive(),
    "A main view state needs a scene and a live camera");
  const bool same_session
    = handle_ != vortex::CompositionView::kInvalidViewStateHandle
    && scene_.IsValid() && camera_.IsAlive()
    && camera_.GetHandle() == camera.GetHandle();
  if (same_session) {
    return { .handle = handle_, .started = false };
  }
  // Retire the old state before replacing it, so its exposure and history
  // cannot survive into another scene or camera under the same view id.
  Reset(observer_ptr { &context }, renderer, view_id);
  handle_ = AllocateHandle();
  scene_ = active_scene;
  camera_ = camera;
  return { .handle = handle_, .started = true };
}

auto MainViewState::Reset(const observer_ptr<engine::FrameContext> context,
  const observer_ptr<vortex::Renderer> renderer, const ViewId view_id) -> void
{
  if (handle_ != vortex::CompositionView::kInvalidViewStateHandle
    && renderer != nullptr) {
    if (context != nullptr) {
      renderer->RemovePublishedRuntimeView(*context, view_id);
    } else {
      renderer->RemovePublishedRuntimeView(view_id);
    }
  }
  handle_ = vortex::CompositionView::kInvalidViewStateHandle;
  scene_ = {};
  camera_ = {};
}

} // namespace oxygen::examples
