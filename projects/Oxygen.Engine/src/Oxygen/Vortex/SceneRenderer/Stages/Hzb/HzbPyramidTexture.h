//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Core/Bindless/Types.h>
#include <Oxygen/Vortex/api_export.h>

namespace oxygen {
class Graphics;
} // namespace oxygen

namespace oxygen::graphics {
class Texture;
} // namespace oxygen::graphics

namespace oxygen::vortex {

//! A registered pyramid texture for one source extent, with its whole-chain
//! SRV.
/*!
 `Ensure` recreates the texture when the source extent changes. A replaced or
 destroyed texture is retired after the GPU is done with it.
*/
class HzbPyramidTexture {
public:
  OXGN_VRTX_API explicit HzbPyramidTexture(std::string debug_name);
  OXGN_VRTX_API ~HzbPyramidTexture();

  OXYGEN_MAKE_NON_COPYABLE(HzbPyramidTexture)
  OXYGEN_MAKE_NON_MOVABLE(HzbPyramidTexture)

  //! Holds a pyramid for a `source_width` x `source_height` source; false
  //! when it cannot be created.
  OXGN_VRTX_API auto Ensure(const std::shared_ptr<Graphics>& gfx,
    std::uint32_t source_width, std::uint32_t source_height) -> bool;

  [[nodiscard]] auto GetTexture() const noexcept
    -> const std::shared_ptr<graphics::Texture>&
  {
    return texture_;
  }

  [[nodiscard]] auto GetSrv() const noexcept -> ShaderVisibleIndex
  {
    return srv_;
  }

private:
  auto Retire() -> void;

  std::string debug_name_;
  std::weak_ptr<Graphics> gfx_;
  std::shared_ptr<graphics::Texture> texture_;
  ShaderVisibleIndex srv_ { kInvalidShaderVisibleIndex };
  std::uint32_t source_width_ { 0U };
  std::uint32_t source_height_ { 0U };
};

} // namespace oxygen::vortex
