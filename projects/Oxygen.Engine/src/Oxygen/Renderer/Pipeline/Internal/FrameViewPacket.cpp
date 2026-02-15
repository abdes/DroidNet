//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Renderer/Pipeline/Internal/CompositionViewImpl.h>
#include <Oxygen/Renderer/Pipeline/Internal/FrameViewPacket.h>

namespace oxygen::renderer::internal {

auto FrameViewPacket::HasCompositeTexture() const noexcept -> bool
{
  return View().GetSdrTexture() != nullptr;
}

auto FrameViewPacket::GetCompositeTexture() const
  -> std::shared_ptr<graphics::Texture>
{
  return View().GetSdrTexture();
}

auto FrameViewPacket::GetCompositeViewport() const noexcept -> ViewPort
{
  return View().GetDescriptor().view.viewport;
}

auto FrameViewPacket::GetCompositeOpacity() const noexcept -> float
{
  return View().GetDescriptor().opacity;
}

} // namespace oxygen::renderer::internal
