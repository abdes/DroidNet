//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Core/Types/Frame.h>
#include <Oxygen/Core/Types/View.h>
#include <Oxygen/Graphics/Common/Texture.h>
#include <Oxygen/Vortex/api_export.h>

namespace oxygen::graphics {
class CommandRecorder;
} // namespace oxygen::graphics

namespace oxygen::vortex {

class Renderer;

//! Records the reduction of a depth rect into reversed-Z HZB pyramids.
/*!
 Writes the closest (max) and furthest (min) pyramids, either or both, in at
 most two compute dispatches: mips 0-6 per 64 x 64 tile, then mips 7-12 in one
 workgroup after a UAV barrier. Mip-0 texel `x` reduces the source pixels `2x`
 and `2x + 1` per axis, clamped to the source rect; each later mip reduces 2 x 2
 texels of the previous one.

 The builder owns the compute pipeline and the per-dispatch constants; the
 caller owns the pyramid textures, created from `MakeTextureDesc`.
*/
class HzbPyramidBuilder {
public:
  //! Largest mip count: an 8192 root, from D3D12's 16384 texture limit.
  static constexpr std::uint32_t kMaxMipCount = 13U;

  //! The depth rect to reduce, in texels of mip 0 of one array slice of
  //! `depth`.
  struct Source {
    observer_ptr<const graphics::Texture> depth;
    //! The slice of an array or cube texture; 0 for a plain 2D texture.
    std::uint32_t array_slice { 0U };
    std::uint32_t origin_x { 0U };
    std::uint32_t origin_y { 0U };
    std::uint32_t width { 0U };
    std::uint32_t height { 0U };
  };

  //! Pyramids to write, with the extent and mip count `MakeTextureDesc`
  //! derives from the source rect. A null pyramid is not built.
  struct Targets {
    observer_ptr<graphics::Texture> closest;
    observer_ptr<graphics::Texture> furthest;
  };

  //! Root extent for a source extent: `bit_ceil(extent) / 2`, at least 1.
  [[nodiscard]] OXGN_VRTX_API static auto ComputeRootExtent(
    std::uint32_t source_extent) -> std::uint32_t;

  //! Mips down to 2 texels on the longer root axis, at least 1.
  [[nodiscard]] OXGN_VRTX_API static auto ComputeMipCount(
    std::uint32_t root_width, std::uint32_t root_height) -> std::uint32_t;

  //! R32F pyramid texture, full chain, with shader-resource and UAV usage.
  [[nodiscard]] OXGN_VRTX_API static auto MakeTextureDesc(
    std::uint32_t source_width, std::uint32_t source_height,
    std::string debug_name) -> graphics::TextureDesc;

  OXGN_VRTX_API HzbPyramidBuilder(
    Renderer& renderer, std::string_view debug_name);
  OXGN_VRTX_API ~HzbPyramidBuilder();

  OXYGEN_MAKE_NON_COPYABLE(HzbPyramidBuilder)
  OXYGEN_MAKE_NON_MOVABLE(HzbPyramidBuilder)

  //! The frame a build records in, and the view it publishes constants for.
  struct BuildFrame {
    frame::SequenceNumber sequence { 0U };
    frame::Slot slot { frame::kInvalidSlot };
    ViewId view_id { kInvalidViewId };
  };

  //! Records the build.
  /*!
   Leaves the source depth and the targets in `kShaderResource`.

   @return False when a view or the constants could not be published; nothing
   is recorded then.
  */
  OXGN_VRTX_API auto Build(const BuildFrame& frame,
    graphics::CommandRecorder& recorder, const Source& source,
    const Targets& targets) -> bool;

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace oxygen::vortex
