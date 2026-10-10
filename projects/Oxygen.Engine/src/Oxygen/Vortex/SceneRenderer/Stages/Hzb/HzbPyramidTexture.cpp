//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cstdint>
#include <memory>
#include <string>
#include <utility>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Core/Bindless/Types.h>
#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/Graphics/Common/ResourceRegistry.h>
#include <Oxygen/Vortex/Internal/TextureViews.h>
#include <Oxygen/Vortex/SceneRenderer/Stages/Hzb/HzbPyramidBuilder.h>
#include <Oxygen/Vortex/SceneRenderer/Stages/Hzb/HzbPyramidTexture.h>

namespace oxygen::vortex {

HzbPyramidTexture::HzbPyramidTexture(std::string debug_name)
  : debug_name_(std::move(debug_name))
{
}

HzbPyramidTexture::~HzbPyramidTexture() { Retire(); }

auto HzbPyramidTexture::Ensure(const std::shared_ptr<Graphics>& gfx,
  const std::uint32_t source_width, const std::uint32_t source_height) -> bool
{
  if (texture_ != nullptr && source_width_ == source_width
    && source_height_ == source_height) {
    return true;
  }
  Retire();
  gfx_ = gfx;
  auto texture = gfx->CreateTexture(HzbPyramidBuilder::MakeTextureDesc(
    source_width, source_height, debug_name_));
  if (texture == nullptr) {
    LOG_F(ERROR, "{}: pyramid creation failed", debug_name_);
    return false;
  }
  gfx->GetResourceRegistry().Register(texture);
  const auto srv = internal::EnsureTextureView(
    *gfx, *texture, internal::WholeTextureSrvDesc(*texture));
  texture_ = std::move(texture);
  if (!srv.IsValid()) {
    LOG_F(ERROR, "{}: pyramid SRV registration failed", debug_name_);
    Retire();
    return false;
  }
  srv_ = srv;
  source_width_ = source_width;
  source_height_ = source_height;
  return true;
}

auto HzbPyramidTexture::Retire() -> void
{
  srv_ = kInvalidShaderVisibleIndex;
  source_width_ = 0U;
  source_height_ = 0U;
  auto gfx = gfx_.lock();
  if (gfx == nullptr || texture_ == nullptr) {
    texture_.reset();
    return;
  }
  auto* registry = &gfx->GetResourceRegistry();
  gfx->GetDeferredReclaimer().RegisterDeferredAction(
    [registry, texture = std::move(texture_)] mutable -> void {
      if (registry->Contains(*texture)) {
        registry->UnRegisterResource(*texture);
      }
      texture.reset();
    });
}

} // namespace oxygen::vortex
