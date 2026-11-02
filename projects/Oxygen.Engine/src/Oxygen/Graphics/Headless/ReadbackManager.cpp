//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <cstdint>
#include <expected>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Core/Detail/FormatUtils.h>
#include <Oxygen/Core/Types/ByteUnits.h>
#include <Oxygen/Core/Types/TextureType.h>
#include <Oxygen/Graphics/Common/CommandRecorder.h>
#include <Oxygen/Graphics/Common/ReadbackErrors.h>
#include <Oxygen/Graphics/Common/StagedReadbackManager.h>
#include <Oxygen/Graphics/Common/Texture.h>
#include <Oxygen/Graphics/Headless/Graphics.h>
#include <Oxygen/Graphics/Headless/ReadbackManager.h>
#include <Oxygen/Graphics/Headless/Texture.h>

namespace oxygen::graphics::headless {

HeadlessReadbackManager::HeadlessReadbackManager(Graphics& graphics)
  : StagedReadbackManager(graphics)
{
}

HeadlessReadbackManager::~HeadlessReadbackManager() = default;

//! Headless copies read the source as it is: it holds a single sample.
auto HeadlessReadbackManager::PrepareTextureReadbackCopy(
  graphics::CommandRecorder& recorder, const graphics::Texture& source,
  const graphics::TextureSlice& slice,
  graphics::TextureReadbackResolveTarget& /*resolve*/)
  -> std::expected<graphics::TextureReadbackCopy, graphics::ReadbackError>
{
  if (!recorder.IsResourceTracked(source)) {
    return std::unexpected(graphics::ReadbackError::kInvalidArgument);
  }

  const auto& desc = source.GetDescriptor();
  const auto region = ResolveTextureBufferCopyRegion(desc,
    graphics::TextureBufferCopyRegion {
      .texture_slice = slice,
    });
  return graphics::TextureReadbackCopy {
    .texture = observer_ptr<const graphics::Texture> { &source },
    .desc = desc,
    .recorded_region = region,
    .placed_region = region,
    .bytes = SizeBytes { region.buffer_slice_pitch.get()
      * static_cast<uint64_t>((std::max)(slice.depth, 1U)) },
  };
}

auto HeadlessReadbackManager::MapReadbackSurfaceBytes(
  graphics::Texture& surface, const graphics::TextureSlice& slice)
  -> std::expected<graphics::ReadbackSurfaceBytes, graphics::ReadbackError>
{
  if (surface.GetTypeId() != Texture::ClassTypeId()) {
    LOG_F(ERROR,
      "MapReadbackTextureSurface rejected unexpected texture type `{}` ({})",
      surface.GetName(), surface.GetTypeId());
    return std::unexpected(graphics::ReadbackError::kUnsupportedResource);
  }
  const auto& headless_surface = static_cast<const Texture&>(surface);
  const auto& desc = headless_surface.GetDescriptor();
  const auto& format_info = graphics::detail::GetFormatInfo(desc.format);

  const auto block_size
    = (std::max)(1U, static_cast<uint32_t>(format_info.block_size));
  if ((slice.x % block_size) != 0U || (slice.y % block_size) != 0U) {
    return std::unexpected(graphics::ReadbackError::kInvalidArgument);
  }

  const auto mip_footprint = ComputeLinearTextureCopyFootprint(desc.format,
    {
      .width = MipExtent(desc.width, slice.mip_level),
      .height = MipExtent(desc.height, slice.mip_level),
      .depth = desc.texture_type == TextureType::kTexture3D
        ? MipExtent(desc.depth, slice.mip_level)
        : 1U,
    });
  const auto base_offset
    = headless_surface.GetLayoutStrategy().ComputeSliceMipBaseOffset(
      desc, slice.array_slice, slice.mip_level);
  const auto byte_offset = static_cast<uint64_t>(base_offset)
    + (uint64_t { slice.z } * mip_footprint.slice_pitch.get())
    + (uint64_t { slice.y / block_size } * mip_footprint.row_pitch.get())
    + graphics::detail::RowBytes(desc.format, slice.x);

  const auto backing = headless_surface.GetBackingBytes();
  CHECK_F(
    !backing.empty(), "Headless readback surface must expose CPU backing data");
  return graphics::ReadbackSurfaceBytes {
    .data = backing.subspan(byte_offset).data(),
    .row_pitch = mip_footprint.row_pitch,
    .slice_pitch = mip_footprint.slice_pitch,
  };
}

//! The CPU backing stays readable: there is nothing to unmap.
auto HeadlessReadbackManager::UnmapReadbackSurfaceBytes(
  graphics::Texture& /*surface*/) -> void
{
}

} // namespace oxygen::graphics::headless
