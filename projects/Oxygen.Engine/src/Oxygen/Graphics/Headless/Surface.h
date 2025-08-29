//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <string_view>

#include <Oxygen/Core/Types/Frame.h>
#include <Oxygen/Graphics/Common/Framebuffer.h>
#include <Oxygen/Graphics/Common/Surface.h>
#include <Oxygen/Graphics/Headless/api_export.h>
namespace oxygen::graphics::headless {

class HeadlessSurface final : public Surface {
public:
  OXGN_HDLS_API explicit HeadlessSurface(
    std::string_view name = "Headless Surface");
  OXGN_HDLS_API ~HeadlessSurface() override = default;

  OXGN_HDLS_API auto AttachRenderer(std::shared_ptr<RenderController> renderer)
    -> void override;
  OXGN_HDLS_API auto DetachRenderer() -> void override;
  OXGN_HDLS_API auto Resize() -> void override;
  // Headless-only helper to update the desired surface size. Use strong type
  // PixelExtent to avoid argument confusion between width and height.
  OXGN_HDLS_API auto SetSize(PixelExtent size) -> void;

  OXGN_HDLS_API auto GetCurrentBackBufferIndex() const -> uint32_t override;
  OXGN_HDLS_API auto GetCurrentBackBuffer() const
    -> std::shared_ptr<Texture> override;
  OXGN_HDLS_API auto GetBackBuffer(uint32_t index) const
    -> std::shared_ptr<Texture> override;

  OXGN_HDLS_API auto Present() const -> void override;

  OXGN_HDLS_NDAPI auto Width() const -> uint32_t override;
  OXGN_HDLS_NDAPI auto Height() const -> uint32_t override;

private:
  // Headless backbuffer storage (fixed-size, frames-in-flight)
  std::array<std::shared_ptr<Texture>,
    static_cast<size_t>(frame::kFramesInFlight.get())>
    backbuffers_ {};
  uint32_t width_ = 1;
  uint32_t height_ = 1;
  uint32_t current_index_ = 0;
};

} // namespace oxygen::graphics::headless
