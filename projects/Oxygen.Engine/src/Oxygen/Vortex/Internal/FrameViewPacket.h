//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <memory>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Core/Types/View.h>
#include <Oxygen/Core/Types/ViewPort.h>
#include <Oxygen/Vortex/SceneRenderer/Internal/ViewRenderPlan.h>

namespace oxygen::graphics {
class Texture;
} // namespace oxygen::graphics

namespace oxygen::vortex::internal {

class CompositionViewImpl;

class FrameViewPacket {
public:
  FrameViewPacket(observer_ptr<const CompositionViewImpl> view,
    ViewId published_view_id, ViewRenderPlan plan)
    : view_(view)
    , published_view_id_(published_view_id)
    , plan_(plan)
  {
    CHECK_NOTNULL_F(view_.get(), "FrameViewPacket requires non-null view");
    CHECK_F(published_view_id_ != kInvalidViewId,
      "FrameViewPacket requires a published runtime view id");
  }
  ~FrameViewPacket() = default;
  OXYGEN_DEFAULT_COPYABLE(FrameViewPacket)
  OXYGEN_DEFAULT_MOVABLE(FrameViewPacket)

  [[nodiscard]] auto View() const noexcept -> const CompositionViewImpl&
  {
    return *view_;
  }

  [[nodiscard]] auto Plan() const noexcept -> const ViewRenderPlan&
  {
    return plan_;
  }
  [[nodiscard]] auto PublishedViewId() const noexcept -> ViewId
  {
    return published_view_id_;
  }

  [[nodiscard]] auto HasCompositeTexture() const noexcept -> bool;
  [[nodiscard]] auto GetCompositeTexture() const
    -> std::shared_ptr<graphics::Texture>;
  [[nodiscard]] auto GetCompositeViewport() const noexcept -> ViewPort;
  [[nodiscard]] auto GetCompositeOpacity() const noexcept -> float;

private:
  observer_ptr<const CompositionViewImpl> view_;
  ViewId published_view_id_ { kInvalidViewId };
  ViewRenderPlan plan_;
};

} // namespace oxygen::vortex::internal
