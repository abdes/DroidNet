//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <functional>
#include <memory>
#include <span>
#include <vector>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Core/FrameContext.h>
#include <Oxygen/Graphics/Common/CommandRecorder.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/Renderer/Pipeline/CompositionView.h>
#include <Oxygen/Renderer/Renderer.h>

namespace oxygen {
class Graphics;
namespace engine {
  class FrameContext;
  class Renderer;
} // namespace engine
namespace graphics {
  class Framebuffer;
} // namespace graphics
} // namespace oxygen

namespace oxygen::renderer::internal {

class CompositionViewImpl;

//! Keeps view runtime state coherent between DemoShell, FrameContext, and
//! Renderer.
/*!
 `ViewLifecycleService` owns the active `CompositionViewImpl` set and updates it
 from the per-frame `CompositionView` descriptors. It is responsible for keeping
 view resources, registration state, and ordering consistent over time.

 Contracts:
 - Inputs are the authoritative per-frame view descriptors and valid engine
   services (`FrameContext`, `Renderer`, `Graphics`).
 - It maintains exactly one lifecycle record per active view and preserves
   deterministic ordering (z-order, then submission order).
 - It ensures view runtime resources exist before registration/update.
 - It performs view registration updates in `FrameContext` and matching render
   callback registration in `Renderer`.
 - It removes stale views after inactivity and unregisters them from both
   `FrameContext` and `Renderer`.

 Out of scope:
 - render policy planning
 - pass execution
 - compositing task planning
*/
class ViewLifecycleService {
public:
  using RenderViewCoroutine = std::function<co::Co<>(
    ViewId, const engine::RenderContext&, graphics::CommandRecorder&)>;

  ViewLifecycleService(
    engine::Renderer& renderer, RenderViewCoroutine render_view_coroutine);
  ~ViewLifecycleService();

  OXYGEN_MAKE_NON_COPYABLE(ViewLifecycleService)
  OXYGEN_MAKE_NON_MOVABLE(ViewLifecycleService)

  void SyncActiveViews(engine::FrameContext& context,
    std::span<const CompositionView> view_descs,
    observer_ptr<graphics::Framebuffer> composite_target, Graphics& graphics);

  void PublishViews(engine::FrameContext& context);
  void RegisterRenderGraphs();

  void UnpublishStaleViews(engine::FrameContext& context);

  [[nodiscard]] auto GetOrderedActiveViews() const
    -> const std::vector<CompositionViewImpl*>&;

private:
  void RegisterViewRenderGraph(CompositionViewImpl& view);

  struct State;

  observer_ptr<engine::Renderer> renderer_;
  RenderViewCoroutine render_view_coroutine_;
  std::unique_ptr<State> state_;
};

} // namespace oxygen::renderer::internal
