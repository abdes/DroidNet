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
#include <Oxygen/Core/Types/ResolvedView.h>
#include <Oxygen/Graphics/Common/CommandRecorder.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/Vortex/CompositionView.h>

namespace oxygen {
class Graphics;
namespace graphics {
  class Framebuffer;
} // namespace graphics
} // namespace oxygen

namespace oxygen::vortex {
struct RenderContext;
} // namespace oxygen::vortex

namespace oxygen::vortex::internal {

class CompositionViewImpl;

class ViewLifecycleService {
public:
  using UpsertPublishedViewCallback
    = std::function<ViewId(engine::FrameContext&, ViewId, engine::ViewContext,
      std::optional<ShadingMode>, CompositionView::ViewStateHandle)>;
  using ResolvePublishedViewCallback = std::function<ViewId(ViewId)>;
  using PruneStalePublishedViewsCallback
    = std::function<std::vector<ViewId>(engine::FrameContext&)>;
  using RenderViewCoroutine = std::function<co::Co<>(
    ViewId, const vortex::RenderContext&, graphics::CommandRecorder&)>;
  using RegisterViewGraphCallback
    = std::function<void(ViewId, const RenderViewCoroutine&, ResolvedView)>;

  ViewLifecycleService(UpsertPublishedViewCallback upsert_published_view,
    ResolvePublishedViewCallback resolve_published_view,
    PruneStalePublishedViewsCallback prune_stale_published_views,
    RegisterViewGraphCallback register_view_graph,
    RenderViewCoroutine render_view_coroutine);
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

  UpsertPublishedViewCallback upsert_published_view_;
  ResolvePublishedViewCallback resolve_published_view_;
  PruneStalePublishedViewsCallback prune_stale_published_views_;
  RegisterViewGraphCallback register_view_graph_;
  RenderViewCoroutine render_view_coroutine_;
  std::unique_ptr<State> state_;
};

} // namespace oxygen::vortex::internal
