//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Core/Types/View.h>
#include <Oxygen/Core/Types/ViewPort.h>
#include <Oxygen/Host/api_export.h>
#include <Oxygen/Vortex/CompositionView.h>
#include <Oxygen/Vortex/Renderer.h>

namespace oxygen {
class AsyncEngine;
class Graphics;
namespace engine {
  class FrameContext;
} // namespace engine
namespace graphics {
  class Framebuffer;
  class Surface;
} // namespace graphics
namespace scene {
  class SceneNode;
} // namespace scene
} // namespace oxygen

namespace oxygen::host {

class HostWindow;

//! Publishes a host's scene views to Vortex and composites them onto the
//! host window.
/*!
 Each frame the host calls `BeginFrame` at frame start, `Publish` at view
 publication and `Compose` at compositing. The publisher owns one scene and
 one composite framebuffer per published view, sized to the view's camera
 content rectangle, and retires them when the view disappears or resizes.

 Without a window (headless) views are still published; nothing is
 composited.
*/
class ViewPublisher final {
public:
  //! Called after the main view is published, with the viewport it covers.
  using MainViewReadyHandler = std::function<void(
    ViewId, const scene::SceneNode& camera, const ViewPort& viewport)>;
  //! Decides whether a view absent this frame keeps its renderer history.
  using RetainInactiveViewPredicate = std::function<bool(ViewId)>;
  //! Adds texture layers after the scene layers.
  using ExtraLayersHandler
    = std::function<void(vortex::Renderer::RuntimeCompositionInput&)>;

  //! `window` is null for a headless host; the engine and the window must
  //! outlive the publisher.
  OXGN_HOST_API ViewPublisher(observer_ptr<AsyncEngine> engine,
    std::weak_ptr<Graphics> gfx, observer_ptr<HostWindow> window);
  OXGN_HOST_API ~ViewPublisher();

  OXYGEN_MAKE_NON_COPYABLE(ViewPublisher)
  OXYGEN_MAKE_NON_MOVABLE(ViewPublisher)

  OXGN_HOST_API auto SetMainViewReadyHandler(MainViewReadyHandler handler)
    -> void;
  OXGN_HOST_API auto SetRetainInactiveViewPredicate(
    RetainInactiveViewPredicate predicate) -> void;

  //! Register the window surface, apply a pending resize, and drop the
  //! surface once the window is gone.
  OXGN_HOST_API auto BeginFrame(engine::FrameContext& context) -> void;

  //! Publish every scene view (a view with a camera) and release the views
  //! published before that are absent now. `views` is read during the call
  //! only.
  OXGN_HOST_API auto Publish(engine::FrameContext& context,
    std::span<const vortex::CompositionView> views,
    std::optional<ViewId> main_view) -> void;

  //! Composite the published views onto the window in z order.
  OXGN_HOST_API auto Compose(const ExtraLayersHandler& extra_layers = {})
    -> void;

  //! Remove every published view and release its framebuffers.
  OXGN_HOST_API auto Shutdown() -> void;

  //! A stable view id for a view name, unique within this publisher.
  OXGN_HOST_NDAPI auto GetOrCreateViewId(std::string_view name) -> ViewId;

private:
  struct SceneTarget {
    std::shared_ptr<graphics::Framebuffer> scene_framebuffer;
    std::shared_ptr<graphics::Framebuffer> composite_framebuffer;
    std::uint32_t width { 0 };
    std::uint32_t height { 0 };
  };

  struct Layer {
    ViewId view_id;
    vortex::CompositionView::ZOrder z_order;
    ViewPort viewport;
    float opacity { 1.0F };
  };

  [[nodiscard]] auto HasLiveWindow() const noexcept -> bool;
  [[nodiscard]] auto ResolveRenderer() const noexcept
    -> observer_ptr<vortex::Renderer>;
  [[nodiscard]] auto ResolveViewport(const vortex::CompositionView& view) const
    -> ViewPort;
  [[nodiscard]] auto Normalize(const vortex::CompositionView& source) const
    -> vortex::CompositionView;
  auto EnsureSceneTarget(ViewId view_id, std::uint32_t width,
    std::uint32_t height) -> observer_ptr<SceneTarget>;
  auto RetireSceneTarget(SceneTarget& target) -> void;
  auto ClearSceneTargets() -> void;
  auto ReleaseInactiveViews(observer_ptr<engine::FrameContext> context,
    std::span<const ViewId> retained) -> void;

  observer_ptr<AsyncEngine> engine_;
  std::weak_ptr<Graphics> gfx_weak_;
  observer_ptr<HostWindow> window_;
  MainViewReadyHandler main_view_ready_;
  RetainInactiveViewPredicate retain_inactive_view_;

  observer_ptr<graphics::Surface> last_surface_;
  std::map<std::string, ViewId, std::less<>> view_ids_;
  std::uint64_t next_view_id_;
  std::map<ViewId, SceneTarget> scene_targets_;
  std::vector<Layer> layers_;
};

} // namespace oxygen::host
