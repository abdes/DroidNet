//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <stdexcept>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Core/Types/TextureType.h>
#include <Oxygen/Graphics/Common/Texture.h>
#include <Oxygen/Graphics/Direct3D12/Detail/FormatUtils.h>
#include <Oxygen/Graphics/Direct3D12/Detail/TextureReadback.h>

using oxygen::graphics::detail::FormatInfo;
using oxygen::graphics::detail::GetFormatInfo;

namespace {

auto GetArrayLayerCount(const oxygen::graphics::TextureDesc& desc) -> UINT
{
  using oxygen::TextureType;

  switch (desc.texture_type) {
  case TextureType::kTexture1DArray:
  case TextureType::kTexture2DArray:
  case TextureType::kTextureCube:
  case TextureType::kTextureCubeArray:
  case TextureType::kTexture2DMultiSampleArray:
    return desc.array_size;
  case TextureType::kTexture1D:
  case TextureType::kTexture2D:
  case TextureType::kTexture2DMultiSample:
  case TextureType::kTexture3D:
    return 1;
  case TextureType::kUnknown:
    ABORT_F(
      "Invalid texture dimension: {}", nostd::to_string(desc.texture_type));
  }

  ABORT_F("Unhandled texture dimension");
}

auto ValidateResolvedSlice(const oxygen::graphics::TextureDesc& desc,
  const oxygen::graphics::TextureSlice& slice) -> void
{
  const auto mip_width = (std::max)(1u, desc.width >> slice.mip_level);
  const auto mip_height = (std::max)(1u, desc.height >> slice.mip_level);
  const auto mip_depth = desc.texture_type == oxygen::TextureType::kTexture3D
    ? (std::max)(1u, desc.depth >> slice.mip_level)
    : 1u;

  CHECK_LE_F(static_cast<uint64_t>(slice.x) + slice.width,
    static_cast<uint64_t>(mip_width),
    "Texture copy width exceeds mip bounds: x={} width={} mip_width={}",
    slice.x, slice.width, mip_width);
  CHECK_LE_F(static_cast<uint64_t>(slice.y) + slice.height,
    static_cast<uint64_t>(mip_height),
    "Texture copy height exceeds mip bounds: y={} height={} mip_height={}",
    slice.y, slice.height, mip_height);
  CHECK_LE_F(static_cast<uint64_t>(slice.z) + slice.depth,
    static_cast<uint64_t>(mip_depth),
    "Texture copy depth exceeds mip bounds: z={} depth={} mip_depth={}",
    slice.z, slice.depth, mip_depth);
}

} // namespace

namespace oxygen::graphics::d3d12::detail {

auto GetD3D12SubresourceCount(const TextureDesc& desc, const UINT plane_count)
  -> UINT
{
  return static_cast<UINT>(desc.mip_levels) * GetArrayLayerCount(desc)
    * (std::max)(1u, plane_count);
}

auto MakeTextureResourceDesc(const TextureDesc& desc) -> D3D12_RESOURCE_DESC
{
  using oxygen::TextureType;

  const auto& format_mapping = GetDxgiFormatMapping(desc.format);
  const FormatInfo& format_info = GetFormatInfo(desc.format);

  D3D12_RESOURCE_DESC resource_desc {};
  resource_desc.Width = desc.width;
  resource_desc.Height = desc.height;
  resource_desc.MipLevels = static_cast<UINT16>(desc.mip_levels);
  resource_desc.Format = desc.is_typeless ? format_mapping.resource_format
                                          : format_mapping.rtv_format;
  resource_desc.SampleDesc.Count = desc.sample_count;
  resource_desc.SampleDesc.Quality = desc.sample_quality;

  switch (desc.texture_type) {
  case TextureType::kTexture1D:
  case TextureType::kTexture1DArray:
    resource_desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE1D;
    resource_desc.DepthOrArraySize = static_cast<UINT16>(desc.array_size);
    break;
  case TextureType::kTexture2D:
  case TextureType::kTexture2DArray:
  case TextureType::kTextureCube:
  case TextureType::kTextureCubeArray:
  case TextureType::kTexture2DMultiSample:
  case TextureType::kTexture2DMultiSampleArray:
    resource_desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    resource_desc.DepthOrArraySize = static_cast<UINT16>(desc.array_size);
    break;
  case TextureType::kTexture3D:
    resource_desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE3D;
    resource_desc.DepthOrArraySize = static_cast<UINT16>(desc.depth);
    break;
  case TextureType::kUnknown:
    ABORT_F(
      "Invalid texture dimension: {}", nostd::to_string(desc.texture_type));
  }

  if (desc.is_render_target) {
    if (format_info.has_depth || format_info.has_stencil) {
      resource_desc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
    } else {
      resource_desc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    }
  }
  if (!desc.is_shader_resource
    && (resource_desc.Flags & D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL)
      == D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL) {
    resource_desc.Flags |= D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE;
  }
  if (desc.is_uav) {
    resource_desc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
  }

  return resource_desc;
}

auto ComputeReadbackSurfaceLayout(dx::IDevice& device,
  const D3D12_RESOURCE_DESC& texture_desc, const UINT subresource_count,
  const UINT plane_count) -> ReadbackSurfaceLayout
{
  ReadbackSurfaceLayout layout {};
  layout.plane_count = (std::max)(1u, plane_count);
  layout.subresource_count = subresource_count;
  layout.subresources.resize(subresource_count);

  std::vector<D3D12_PLACED_SUBRESOURCE_FOOTPRINT> footprints(subresource_count);
  std::vector<UINT> row_counts(subresource_count);
  std::vector<UINT64> row_sizes(subresource_count);
  UINT64 total_bytes = 0;
  device.GetCopyableFootprints(&texture_desc, 0, subresource_count, 0,
    footprints.data(), row_counts.data(), row_sizes.data(), &total_bytes);

  layout.total_bytes = total_bytes;
  for (UINT index = 0; index < subresource_count; ++index) {
    layout.subresources[index] = { .placed_footprint = footprints[index],
      .row_count = row_counts[index],
      .row_size_bytes = row_sizes[index] };
  }
  return layout;
}

auto ComputeTextureToBufferCopyInfo(const TextureDesc& desc,
  const DXGI_FORMAT resource_format, const uint8_t plane_count,
  const TextureBufferCopyRegion& region) -> TextureToBufferCopyInfo
{
  if (desc.sample_count != 1) {
    throw std::runtime_error("Direct3D12 CopyTextureToBuffer does not support "
                             "multisampled textures yet");
  }
  if (plane_count != 1) {
    throw std::runtime_error("Direct3D12 CopyTextureToBuffer does not support "
                             "multi-plane formats yet");
  }

  TextureToBufferCopyInfo info {};
  info.resolved_region.texture_slice = region.texture_slice.Resolve(desc);
  ValidateResolvedSlice(desc, info.resolved_region.texture_slice);

  const auto tight_footprint = ComputeLinearTextureCopyFootprint(
    desc, info.resolved_region.texture_slice, SizeBytes { 1 });
  const auto aligned_footprint = ComputeLinearTextureCopyFootprint(desc,
    info.resolved_region.texture_slice,
    SizeBytes { D3D12_TEXTURE_DATA_PITCH_ALIGNMENT });

  info.resolved_region.buffer_offset = region.buffer_offset;
  info.resolved_region.buffer_row_pitch = region.buffer_row_pitch.get() == 0
    ? aligned_footprint.row_pitch
    : region.buffer_row_pitch;
  CHECK_EQ_F(info.resolved_region.buffer_row_pitch.get()
      % D3D12_TEXTURE_DATA_PITCH_ALIGNMENT,
    0u, "D3D12 texture readback row pitch must be {}-byte aligned: {}",
    D3D12_TEXTURE_DATA_PITCH_ALIGNMENT,
    info.resolved_region.buffer_row_pitch.get());
  CHECK_GE_F(info.resolved_region.buffer_row_pitch.get(),
    tight_footprint.row_pitch.get(),
    "D3D12 texture readback row pitch must accommodate the copied row bytes: "
    "row_pitch={} tight_row_bytes={}",
    info.resolved_region.buffer_row_pitch.get(),
    tight_footprint.row_pitch.get());

  info.resolved_region.buffer_slice_pitch = region.buffer_slice_pitch.get() == 0
    ? SizeBytes { info.resolved_region.buffer_row_pitch.get()
        * static_cast<uint64_t>(aligned_footprint.row_count) }
    : region.buffer_slice_pitch;
  CHECK_GE_F(info.resolved_region.buffer_slice_pitch.get(),
    info.resolved_region.buffer_row_pitch.get()
      * static_cast<uint64_t>(aligned_footprint.row_count),
    "D3D12 texture readback slice pitch must accommodate every copied row: "
    "slice_pitch={} row_pitch={} row_count={}",
    info.resolved_region.buffer_slice_pitch.get(),
    info.resolved_region.buffer_row_pitch.get(), aligned_footprint.row_count);
  CHECK_EQ_F(info.resolved_region.buffer_offset.get()
      % D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT,
    0u, "D3D12 texture readback buffer offsets must be {}-byte aligned: {}",
    D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT,
    info.resolved_region.buffer_offset.get());

  const auto& slice = info.resolved_region.texture_slice;
  info.placed_footprint.Offset = info.resolved_region.buffer_offset.get();
  info.placed_footprint.Footprint.Format = resource_format;
  info.placed_footprint.Footprint.Width = slice.width;
  info.placed_footprint.Footprint.Height = slice.height;
  info.placed_footprint.Footprint.Depth = static_cast<UINT16>(slice.depth);
  info.placed_footprint.Footprint.RowPitch
    = static_cast<UINT>(info.resolved_region.buffer_row_pitch.get());
  info.source_box = { .left = slice.x,
    .top = slice.y,
    .front = slice.z,
    .right = slice.x + slice.width,
    .bottom = slice.y + slice.height,
    .back = slice.z + slice.depth };
  info.subresource_index
    = slice.mip_level + (slice.array_slice * desc.mip_levels);
  info.bytes_written = 0;
  if (slice.depth > 0) {
    info.bytes_written += info.resolved_region.buffer_slice_pitch.get()
      * static_cast<uint64_t>(slice.depth - 1);
    info.bytes_written += info.resolved_region.buffer_row_pitch.get()
      * static_cast<uint64_t>(
        aligned_footprint.row_count > 0 ? aligned_footprint.row_count - 1 : 0);
    info.bytes_written += tight_footprint.row_pitch.get();
  }

  return info;
}

} // namespace oxygen::graphics::d3d12::detail
