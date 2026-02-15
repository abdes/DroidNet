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
#include <Oxygen/Core/Types/ViewPort.h>

#include "DemoShell/Runtime/Internal/ViewRenderPlan.h"

namespace oxygen::graphics {
class Texture;
} // namespace oxygen::graphics

namespace oxygen::examples::internal {

class CompositionViewImpl;

// Immutable frame snapshot item built by FramePlanBuilder and consumed by
// ForwardPipelineImpl during render callback resolution/execution.
// Ownership: CompositionViewImpl is owned by
// ViewLifecycleService.
class FrameViewPacket {
public:
  FrameViewPacket(
    observer_ptr<const CompositionViewImpl> view, ViewRenderPlan plan)
    : view_(view)
    , plan_(plan)
  {
    CHECK_NOTNULL_F(view_.get(), "FrameViewPacket requires non-null view");
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

  [[nodiscard]] auto HasCompositeTexture() const noexcept -> bool;
  [[nodiscard]] auto GetCompositeTexture() const
    -> std::shared_ptr<graphics::Texture>;
  [[nodiscard]] auto GetCompositeViewport() const noexcept -> ViewPort;
  [[nodiscard]] auto GetCompositeOpacity() const noexcept -> float;

private:
  observer_ptr<const CompositionViewImpl> view_;
  ViewRenderPlan plan_;
};

} // namespace oxygen::examples::internal
