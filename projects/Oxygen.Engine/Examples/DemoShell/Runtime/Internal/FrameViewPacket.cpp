//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "DemoShell/Runtime/Internal/FrameViewPacket.h"

#include "DemoShell/Runtime/Internal/ForwardPipelineImpl.h"

namespace oxygen::examples::internal {

auto FrameViewPacket::HasCompositeTexture() const noexcept -> bool
{
  return View().sdr_texture != nullptr;
}

auto FrameViewPacket::GetCompositeTexture() const
  -> std::shared_ptr<graphics::Texture>
{
  return View().sdr_texture;
}

auto FrameViewPacket::GetCompositeViewport() const noexcept -> ViewPort
{
  return View().intent.view.viewport;
}

auto FrameViewPacket::GetCompositeOpacity() const noexcept -> float
{
  return View().intent.opacity;
}

} // namespace oxygen::examples::internal
