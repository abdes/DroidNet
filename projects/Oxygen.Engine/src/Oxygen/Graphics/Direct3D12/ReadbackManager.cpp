//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <expected>
#include <span>

#include <d3d12.h>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Core/Detail/FormatUtils.h>
#include <Oxygen/Core/Types/ByteUnits.h>
#include <Oxygen/Graphics/Common/CommandQueue.h>
#include <Oxygen/Graphics/Common/CommandRecorder.h>
#include <Oxygen/Graphics/Common/ReadbackErrors.h>
#include <Oxygen/Graphics/Common/StagedReadbackManager.h>
#include <Oxygen/Graphics/Common/Texture.h>
#include <Oxygen/Graphics/Common/Types/QueueRole.h>
#include <Oxygen/Graphics/Common/Types/ResourceStates.h>
#include <Oxygen/Graphics/Direct3D12/CommandRecorder.h>
#include <Oxygen/Graphics/Direct3D12/Detail/FormatUtils.h>
#include <Oxygen/Graphics/Direct3D12/Detail/TextureReadback.h>
#include <Oxygen/Graphics/Direct3D12/Graphics.h>
#include <Oxygen/Graphics/Direct3D12/ReadbackManager.h>
#include <Oxygen/Graphics/Direct3D12/Texture.h>

namespace oxygen::graphics::d3d12 {

namespace {

  using graphics::ReadbackError;
  using graphics::TextureSlice;

  auto ComputeSubresourceIndex(
    const TextureDesc& desc, const TextureSlice& slice) -> UINT
  {
    return slice.mip_level + (slice.array_slice * desc.mip_levels);
  }

  auto ComputeReadbackSurfaceByteOffset(const TextureDesc& desc,
    const TextureSlice& slice,
    const detail::ReadbackSubresourceLayout& subresource_layout)
    -> std::expected<uint64_t, ReadbackError>
  {
    const auto& format_info = graphics::detail::GetFormatInfo(desc.format);
    const auto block_size
      = (std::max)(1U, static_cast<uint32_t>(format_info.block_size));

    if ((slice.x % block_size) != 0U || (slice.y % block_size) != 0U) {
      return std::unexpected(ReadbackError::kInvalidArgument);
    }

    const auto row_pitch = static_cast<uint64_t>(
      subresource_layout.placed_footprint.Footprint.RowPitch);
    const auto slice_pitch
      = row_pitch * static_cast<uint64_t>(subresource_layout.row_count);
    const auto block_x = static_cast<uint64_t>(slice.x / block_size);
    const auto block_y = static_cast<uint64_t>(slice.y / block_size);

    return subresource_layout.placed_footprint.Offset
      + (slice_pitch * static_cast<uint64_t>(slice.z)) + (row_pitch * block_y)
      + (block_x * static_cast<uint64_t>(format_info.bytes_per_block));
  }

  //! Records the resolve of the multisampled `source` into `target`.
  auto RecordResolve(CommandRecorder& recorder, const Texture& source,
    const graphics::Texture& target, const TextureSlice& slice) -> void
  {
    recorder.RequireResourceState(source, ResourceStates::kResolveSource);
    recorder.RequireResourceState(target, ResourceStates::kResolveDest);
    recorder.FlushBarriers();

    auto* command_list = recorder.GetD3D12CommandList();
    auto* src_resource
      = source.GetNativeResource()->AsPointer<ID3D12Resource>();
    auto* resolve_resource
      = target.GetNativeResource()->AsPointer<ID3D12Resource>();
    CHECK_NOTNULL_F(
      command_list, "D3D12 command list must exist for texture resolve");
    CHECK_NOTNULL_F(src_resource,
      "D3D12 source texture resource must exist for readback resolve");
    CHECK_NOTNULL_F(resolve_resource,
      "D3D12 resolve texture resource must exist for readback resolve");

    const auto& source_desc = source.GetDescriptor();
    const auto subresource_index = ComputeSubresourceIndex(source_desc, slice);
    command_list->ResolveSubresource(resolve_resource, subresource_index,
      src_resource, subresource_index,
      detail::GetDxgiFormatMapping(source_desc.format).rtv_format);
  }

} // namespace

D3D12ReadbackManager::D3D12ReadbackManager(Graphics& graphics)
  : StagedReadbackManager(graphics)
{
}

D3D12ReadbackManager::~D3D12ReadbackManager() = default;

//! A multisampled source resolves into `resolve` first, on the graphics
//! queue; the copy covers every plane of a planar format.
auto D3D12ReadbackManager::PrepareTextureReadbackCopy(
  graphics::CommandRecorder& recorder, const graphics::Texture& source,
  const TextureSlice& slice, graphics::TextureReadbackResolveTarget& resolve)
  -> std::expected<graphics::TextureReadbackCopy, ReadbackError>
{
  if (source.GetTypeId() != Texture::ClassTypeId()) {
    return std::unexpected(ReadbackError::kUnsupportedResource);
  }
  const auto& source_texture = static_cast<const Texture&>(source);
  auto& d3d12_recorder = static_cast<CommandRecorder&>(recorder);
  if (source_texture.IsReadbackSurface()) {
    return std::unexpected(ReadbackError::kUnsupportedResource);
  }

  const auto& source_desc = source.GetDescriptor();
  auto copy_source = observer_ptr<const graphics::Texture> { &source };
  auto copy_desc = source_desc;
  auto plane_count = source_texture.GetPlaneCount();

  if (source_desc.sample_count > 1) {
    if (recorder.GetTargetQueue()->GetQueueRole() != QueueRole::kGraphics) {
      return std::unexpected(ReadbackError::kQueueUnavailable);
    }
    if (const auto ensured = EnsureResolveTarget(resolve, source_desc);
      !ensured.has_value()) {
      return std::unexpected(ensured.error());
    }
    if (!recorder.RetainRegistration(
          GetGraphics().GetResourceRegistry(), resolve.registration)) {
      return std::unexpected(ReadbackError::kBackendFailure);
    }
    CHECK_NOTNULL_F(resolve.texture.get(),
      "Resolve texture must exist after successful allocation");
    if (!recorder.IsResourceTracked(source)) {
      return std::unexpected(ReadbackError::kInvalidArgument);
    }
    if (!recorder.IsResourceTracked(*resolve.texture)) {
      recorder.BeginTrackingResourceState(
        *resolve.texture, ResourceStates::kResolveDest);
    }
    RecordResolve(d3d12_recorder, source_texture, *resolve.texture, slice);

    copy_source
      = observer_ptr<const graphics::Texture> { resolve.texture.get() };
    copy_desc = resolve.desc;
    plane_count = 1U;
  } else if (!recorder.IsResourceTracked(source)) {
    return std::unexpected(ReadbackError::kInvalidArgument);
  }

  const auto copy_region = graphics::TextureBufferCopyRegion {
    .buffer_offset = OffsetBytes { 0 },
    .texture_slice = slice,
  };
  auto* copy_resource
    = copy_source->GetNativeResource()->AsPointer<ID3D12Resource>();
  CHECK_NOTNULL_F(
    copy_resource, "D3D12 texture resource must exist for readback copy");
  detail::TextureToBufferCopyInfo copy_info {};
  try {
    copy_info = detail::ComputeTextureToBufferCopyInfo(
      copy_desc, copy_resource->GetDesc().Format, plane_count, copy_region);
  } catch (const std::exception&) {
    LOG_F(ERROR,
      "Failed to compute texture readback layout for `{}` (planes={})",
      source.GetName(), plane_count);
    return std::unexpected(ReadbackError::kUnsupportedResource);
  }

  return graphics::TextureReadbackCopy {
    .texture = copy_source,
    .desc = copy_desc,
    .recorded_region = copy_region,
    .placed_region = copy_info.resolved_region,
    .bytes = SizeBytes { copy_info.bytes_written },
  };
}

auto D3D12ReadbackManager::MapReadbackSurfaceBytes(
  graphics::Texture& surface, const TextureSlice& slice)
  -> std::expected<graphics::ReadbackSurfaceBytes, ReadbackError>
{
  if (surface.GetTypeId() != Texture::ClassTypeId()) {
    LOG_F(ERROR,
      "MapReadbackTextureSurface rejected unexpected texture type `{}` ({})",
      surface.GetName(), surface.GetTypeId());
    return std::unexpected(ReadbackError::kUnsupportedResource);
  }
  const auto& d3d12_surface = static_cast<const Texture&>(surface);
  if (d3d12_surface.GetPlaneCount() != 1) {
    return std::unexpected(ReadbackError::kUnsupportedResource);
  }

  const auto& desc = d3d12_surface.GetDescriptor();
  const auto subresource_index = ComputeSubresourceIndex(desc, slice);
  const auto subresources
    = std::span(d3d12_surface.GetReadbackSurfaceLayout().subresources);
  CHECK_LT_F(subresource_index, subresources.size(),
    "Resolved readback surface subresource index {} out of bounds ({})",
    subresource_index, subresources.size());
  const auto& subresource_layout = subresources[subresource_index];

  const auto mapped_offset
    = ComputeReadbackSurfaceByteOffset(desc, slice, subresource_layout);
  if (!mapped_offset.has_value()) {
    return std::unexpected(mapped_offset.error());
  }

  auto* resource
    = d3d12_surface.GetNativeResource()->AsPointer<ID3D12Resource>();
  CHECK_NOTNULL_F(
    resource, "D3D12 readback surface must expose a valid native resource");

  const auto row_pitch
    = uint64_t { subresource_layout.placed_footprint.Footprint.RowPitch };
  void* mapped_base = nullptr;
  if (const HRESULT hr = resource->Map(0, nullptr, &mapped_base); FAILED(hr)) {
    LOG_F(ERROR, "MapReadbackTextureSurface failed for `{}` with {:#010X}",
      surface.GetName(), hr);
    return std::unexpected(ReadbackError::kBackendFailure);
  }

  // The resource maps whole: the slice lies at its offset in it.
  const auto mapped = std::span(static_cast<const std::byte*>(mapped_base),
    static_cast<std::size_t>(
      d3d12_surface.GetReadbackSurfaceLayout().total_bytes));
  return graphics::ReadbackSurfaceBytes {
    .data = mapped.subspan(*mapped_offset).data(),
    .row_pitch = SizeBytes { row_pitch },
    .slice_pitch
    = SizeBytes { row_pitch * uint64_t { subresource_layout.row_count } },
  };
}

auto D3D12ReadbackManager::UnmapReadbackSurfaceBytes(graphics::Texture& surface)
  -> void
{
  if (surface.GetTypeId() != Texture::ClassTypeId()) {
    LOG_F(ERROR,
      "UnmapReadbackTextureSurface rejected unexpected texture type `{}` ({})",
      surface.GetName(), surface.GetTypeId());
    return;
  }
  auto* resource = static_cast<const Texture&>(surface)
                     .GetNativeResource()
                     ->AsPointer<ID3D12Resource>();
  CHECK_NOTNULL_F(
    resource, "D3D12 readback surface must expose a valid native resource");
  resource->Unmap(0, nullptr);
}

} // namespace oxygen::graphics::d3d12
