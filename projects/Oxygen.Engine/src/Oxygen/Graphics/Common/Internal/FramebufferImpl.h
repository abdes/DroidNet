//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <memory>
#include <span>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Graphics/Common/Framebuffer.h>

namespace oxygen {
class Graphics;
} // namespace oxygen::graphics

namespace oxygen::graphics::internal {

class FramebufferImpl final : public Framebuffer {
  using Base = Framebuffer;

public:
  FramebufferImpl(FramebufferDesc desc, std::weak_ptr<Graphics> gfx_weak);
  ~FramebufferImpl() override;

  OXYGEN_MAKE_NON_COPYABLE(FramebufferImpl)
  OXYGEN_DEFAULT_MOVABLE(FramebufferImpl)

  [[nodiscard]] auto GetDescriptor() const -> const FramebufferDesc& override
  {
    return desc_;
  }
  [[nodiscard]] auto GetFramebufferInfo() const
    -> const FramebufferInfo& override;

  // TODO: maybe this should go to the render pass?
  OXGN_GFX_API auto PrepareForRender(CommandRecorder& crecorder)
    -> void override;

  [[nodiscard]] auto GetRenderTargetViews() const
    -> std::span<const NativeObject> override
  {
    return std::span(rtvs_.data(), rtvs_.size());
  }

  [[nodiscard]] auto GetDepthStencilView() const -> NativeObject override
  {
    return dsv_;
  }

private:
  FramebufferDesc desc_;
  std::weak_ptr<Graphics> gfx_weak_;

  StaticVector<std::shared_ptr<Texture>, kMaxRenderTargets> textures_ {};
  StaticVector<NativeObject, kMaxRenderTargets> rtvs_ {};
  NativeObject dsv_ {};

  uint32_t rt_width_ { 0 };
  uint32_t rt_height_ { 0 };
};

}
