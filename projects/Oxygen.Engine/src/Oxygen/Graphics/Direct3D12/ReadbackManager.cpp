//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <cmath>
#include <cstring>
#include <memory>
#include <string>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Core/Detail/FormatUtils.h>
#include <Oxygen/Graphics/Common/CommandQueue.h>
#include <Oxygen/Graphics/Common/CommandRecorder.h>
#include <Oxygen/Graphics/Common/ReadbackValidation.h>
#include <Oxygen/Graphics/Common/ResourceRegistry.h>
#include <Oxygen/Graphics/Common/Types/ResourceAccessMode.h>
#include <Oxygen/Graphics/Common/Types/ResourceStates.h>
#include <Oxygen/Graphics/Direct3D12/CommandRecorder.h>
#include <Oxygen/Graphics/Direct3D12/Detail/FormatUtils.h>
#include <Oxygen/Graphics/Direct3D12/Detail/TextureReadback.h>
#include <Oxygen/Graphics/Direct3D12/Graphics.h>
#include <Oxygen/Graphics/Direct3D12/ReadbackManager.h>
#include <Oxygen/Graphics/Direct3D12/Texture.h>

using oxygen::OffsetBytes;
using oxygen::SizeBytes;
using oxygen::TextureType;
using oxygen::graphics::Buffer;
using oxygen::graphics::BufferDesc;
using oxygen::graphics::BufferMemory;
using oxygen::graphics::BufferRange;
using oxygen::graphics::BufferUsage;
using oxygen::graphics::ClearFlags;
using oxygen::graphics::GpuBufferReadback;
using oxygen::graphics::GpuTextureReadback;
using oxygen::graphics::MappedBufferReadback;
using oxygen::graphics::MappedTextureReadback;
using oxygen::graphics::MsaaReadbackMode;
using oxygen::graphics::OwnedTextureReadbackData;
using oxygen::graphics::QueueRole;
using oxygen::graphics::ReadbackError;
using oxygen::graphics::ReadbackResult;
using oxygen::graphics::ReadbackState;
using oxygen::graphics::ReadbackSurfaceMapping;
using oxygen::graphics::ReadbackTicket;
using oxygen::graphics::ResourceAccessMode;
using oxygen::graphics::ResourceStates;
using oxygen::graphics::TextureBufferCopyRegion;
using oxygen::graphics::TextureDesc;
using oxygen::graphics::TextureReadbackLayout;
using oxygen::graphics::TextureReadbackRequest;
using oxygen::graphics::TextureSlice;
using oxygen::graphics::d3d12::D3D12ReadbackManager;
using oxygen::graphics::d3d12::detail::ComputeTextureToBufferCopyInfo;
using oxygen::graphics::d3d12::detail::GetDxgiFormatMapping;
using oxygen::graphics::detail::FormatInfo;
using oxygen::graphics::detail::GetFormatInfo;

namespace oxygen::graphics::d3d12 {

namespace {

  auto ResolveTextureTypeForReadback(const TextureDesc& desc) -> TextureType
  {
    switch (desc.texture_type) {
    case TextureType::kTexture2DMultiSample:
      return TextureType::kTexture2D;
    case TextureType::kTexture2DMultiSampleArray:
      return TextureType::kTexture2DArray;
    default:
      return desc.texture_type;
    }
  }

  auto MakeResolvedTextureDesc(const TextureDesc& source_desc) -> TextureDesc
  {
    auto desc = source_desc;
    desc.sample_count = 1;
    desc.sample_quality = 0;
    desc.texture_type = ResolveTextureTypeForReadback(source_desc);
    desc.is_shader_resource = false;
    desc.is_render_target = false;
    desc.is_uav = false;
    desc.initial_state = ResourceStates::kResolveDest;
    desc.debug_name = source_desc.debug_name + "-resolve";
    return desc;
  }

  auto ComputeSubresourceIndex(
    const TextureDesc& desc, const TextureSlice& slice) -> UINT
  {
    return slice.mip_level + (slice.array_slice * desc.mip_levels);
  }

  auto BuildTextureReadbackLayout(const TextureDesc& desc,
    const TextureSlice& slice, const TextureBufferCopyRegion& region,
    const ClearFlags aspects) -> TextureReadbackLayout
  {
    return TextureReadbackLayout {
      .format = desc.format,
      .texture_type = ResolveTextureTypeForReadback(desc),
      .width = slice.width,
      .height = slice.height,
      .depth = slice.depth,
      .row_pitch = region.buffer_row_pitch,
      .slice_pitch = region.buffer_slice_pitch,
      .mip_level = slice.mip_level,
      .array_slice = slice.array_slice,
      .aspects = aspects,
    };
  }

  auto ResolveTextureReadbackRegion(const TextureDesc& desc,
    const TextureSlice& slice) -> TextureBufferCopyRegion
  {
    static_cast<void>(desc);
    TextureBufferCopyRegion region {};
    region.buffer_offset = OffsetBytes { 0 };
    region.texture_slice = slice;
    return region;
  }

  auto MatchesResolveTextureDesc(const TextureDesc& lhs, const TextureDesc& rhs)
    -> bool
  {
    return lhs.width == rhs.width && lhs.height == rhs.height
      && lhs.depth == rhs.depth && lhs.array_size == rhs.array_size
      && lhs.mip_levels == rhs.mip_levels
      && lhs.sample_count == rhs.sample_count
      && lhs.sample_quality == rhs.sample_quality && lhs.format == rhs.format
      && lhs.texture_type == rhs.texture_type
      && lhs.is_shader_resource == rhs.is_shader_resource
      && lhs.is_render_target == rhs.is_render_target
      && lhs.is_uav == rhs.is_uav && lhs.is_typeless == rhs.is_typeless
      && lhs.initial_state == rhs.initial_state;
  }

  auto BuildReadbackSurfaceTextureDesc(TextureDesc desc)
    -> std::expected<TextureDesc, ReadbackError>
  {
    if (desc.format == oxygen::Format::kUnknown || desc.is_typeless) {
      return std::unexpected(ReadbackError::kUnsupportedFormat);
    }

    const auto& format_info = GetFormatInfo(desc.format);
    if (format_info.has_depth || format_info.has_stencil) {
      return std::unexpected(ReadbackError::kUnsupportedResource);
    }

    desc.cpu_access = ResourceAccessMode::kReadBack;
    desc.sample_count = 1;
    desc.sample_quality = 0;
    desc.texture_type = ResolveTextureTypeForReadback(desc);
    desc.is_shader_resource = false;
    desc.is_render_target = false;
    desc.is_uav = false;
    desc.initial_state = ResourceStates::kCopyDest;
    return desc;
  }

  auto ValidateResolvedSlice(const TextureDesc& desc, const TextureSlice& slice)
    -> void
  {
    static_cast<void>(desc);
    const auto mip_width = (std::max)(1u, desc.width >> slice.mip_level);
    const auto mip_height = (std::max)(1u, desc.height >> slice.mip_level);
    const auto mip_depth = desc.texture_type == TextureType::kTexture3D
      ? (std::max)(1u, desc.depth >> slice.mip_level)
      : 1u;

    CHECK_LE_F(static_cast<uint64_t>(slice.x) + slice.width,
      static_cast<uint64_t>(mip_width),
      "Texture readback surface width exceeds mip bounds: x={} width={} "
      "mip_width={}",
      slice.x, slice.width, mip_width);
    CHECK_LE_F(static_cast<uint64_t>(slice.y) + slice.height,
      static_cast<uint64_t>(mip_height),
      "Texture readback surface height exceeds mip bounds: y={} height={} "
      "mip_height={}",
      slice.y, slice.height, mip_height);
    CHECK_LE_F(static_cast<uint64_t>(slice.z) + slice.depth,
      static_cast<uint64_t>(mip_depth),
      "Texture readback surface depth exceeds mip bounds: z={} depth={} "
      "mip_depth={}",
      slice.z, slice.depth, mip_depth);
  }

  auto ComputeReadbackSurfaceByteOffset(const TextureDesc& desc,
    const TextureSlice& slice,
    const detail::ReadbackSubresourceLayout& subresource_layout)
    -> std::expected<uint64_t, ReadbackError>
  {
    const auto& format_info = GetFormatInfo(desc.format);
    const auto block_size
      = (std::max)(1u, static_cast<uint32_t>(format_info.block_size));

    if ((slice.x % block_size) != 0u || (slice.y % block_size) != 0u) {
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

  auto ComputeMappedTextureByteCount(const TextureReadbackLayout& layout)
    -> SizeBytes
  {
    return SizeBytes { layout.slice_pitch.get()
      * static_cast<uint64_t>((std::max)(layout.depth, 1U)) };
  }

  auto CopyMappedTextureBytes(const MappedTextureReadback& mapped)
    -> OwnedTextureReadbackData
  {
    const auto& layout = mapped.Layout();
    const auto byte_count = ComputeMappedTextureByteCount(layout);

    OwnedTextureReadbackData result {};
    result.bytes.resize(static_cast<size_t>(byte_count.get()));
    std::memcpy(result.bytes.data(), mapped.Data(), result.bytes.size());
    result.layout = layout;
    result.tightly_packed = false;
    return result;
  }

  auto TightPackMappedTextureBytes(const MappedTextureReadback& mapped)
    -> OwnedTextureReadbackData
  {
    const auto& layout = mapped.Layout();
    const auto tight = ComputeLinearTextureCopyFootprint(
      layout.format, { layout.width, layout.height, layout.depth });

    OwnedTextureReadbackData result {};
    result.bytes.resize(static_cast<size_t>(tight.total_bytes.get()));
    result.layout = layout;
    result.layout.row_pitch = tight.row_pitch;
    result.layout.slice_pitch = tight.slice_pitch;
    result.tightly_packed = true;

    const auto* src = mapped.Data();
    auto* dst = result.bytes.data();
    const auto row_bytes = static_cast<size_t>(tight.row_pitch.get());
    for (uint32_t slice_index = 0; slice_index < tight.slice_count;
      ++slice_index) {
      const auto* src_slice
        = src + (layout.slice_pitch.get() * static_cast<size_t>(slice_index));
      auto* dst_slice
        = dst + (tight.slice_pitch.get() * static_cast<size_t>(slice_index));
      for (uint32_t row_index = 0; row_index < tight.row_count; ++row_index) {
        std::memcpy(
          dst_slice + (tight.row_pitch.get() * static_cast<size_t>(row_index)),
          src_slice + (layout.row_pitch.get() * static_cast<size_t>(row_index)),
          row_bytes);
      }
    }

    return result;
  }

  template <typename ResourceT>
  auto TryUnregisterOwnedResource(Graphics& graphics,
    std::shared_ptr<ResourceT>& resource, const std::string_view debug_name)
    -> void
  {
    if (resource == nullptr) {
      return;
    }

    try {
      auto& registry = graphics.GetResourceRegistry();
      if (registry.Contains(*resource)) {
        registry.UnRegisterResource(*resource);
      }
    } catch (const std::exception& ex) {
      LOG_F(WARNING,
        "Failed to unregister readback-owned resource `{}` for `{}`: {}",
        resource->GetName(), debug_name, ex.what());
    }

    resource.reset();
  }

} // namespace

class D3D12BufferReadback final
  : public GpuBufferReadback,
    public std::enable_shared_from_this<D3D12BufferReadback> {
public:
  D3D12BufferReadback(
    D3D12ReadbackManager& manager, std::string_view debug_name)
    : manager_(manager)
    , debug_name_(debug_name)
  {
  }

  ~D3D12BufferReadback() override;

  auto EnqueueCopy(oxygen::graphics::CommandRecorder& recorder,
    const Buffer& source, BufferRange range = {})
    -> std::expected<ReadbackTicket, ReadbackError> override;

  [[nodiscard]] auto GetState() const noexcept -> ReadbackState override
  {
    return state_;
  }

  [[nodiscard]] auto Ticket() const noexcept
    -> std::optional<ReadbackTicket> override
  {
    return ticket_;
  }

  [[nodiscard]] auto IsReady() const
    -> std::expected<bool, ReadbackError> override;

  auto TryMap() -> std::expected<MappedBufferReadback, ReadbackError> override;
  auto MapNow() -> std::expected<MappedBufferReadback, ReadbackError> override;

  auto Cancel() -> std::expected<bool, ReadbackError> override;
  auto Reset() -> void override;

  auto OnManagerCancelled() -> void
  {
    if (state_ == ReadbackState::kPending) {
      last_error_ = ReadbackError::kCancelled;
      state_ = ReadbackState::kCancelled;
    }
  }

private:
  auto EnsureReadbackBuffer(uint64_t size_bytes) -> bool;
  auto RefreshStateFromTracker() const -> std::expected<void, ReadbackError>;
  auto ReleaseMapping() -> void;

  D3D12ReadbackManager& manager_;
  std::string debug_name_;
  std::shared_ptr<Buffer> readback_buffer_ {};
  mutable std::optional<ReadbackTicket> ticket_ {};
  mutable std::optional<ReadbackError> last_error_ {};
  mutable ReadbackState state_ { ReadbackState::kIdle };
  BufferRange resolved_range_ {};
};

auto D3D12BufferReadback::EnsureReadbackBuffer(const uint64_t size_bytes)
  -> bool
{
  if (readback_buffer_ != nullptr
    && readback_buffer_->GetSize() >= size_bytes) {
    return true;
  }

  ReleaseMapping();
  TryUnregisterOwnedResource(manager_.graphics_, readback_buffer_, debug_name_);

  readback_buffer_ = manager_.graphics_.CreateBuffer(BufferDesc {
    .size_bytes = size_bytes,
    .usage = BufferUsage::kNone,
    .memory = BufferMemory::kReadBack,
    .debug_name = debug_name_ + "-staging",
  });
  if (readback_buffer_ == nullptr) {
    LOG_F(ERROR, "Failed to allocate readback buffer `{}` ({} bytes)",
      debug_name_, size_bytes);
    return false;
  }

  auto& registry = manager_.graphics_.GetResourceRegistry();
  if (!registry.Contains(*readback_buffer_)) {
    registry.Register(readback_buffer_);
  }
  return true;
}

auto D3D12BufferReadback::RefreshStateFromTracker() const
  -> std::expected<void, ReadbackError>
{
  if (state_ != ReadbackState::kPending || !ticket_.has_value()) {
    return {};
  }

  manager_.PumpCompletions();
  const auto result = manager_.TryGetResult(ticket_->id);
  if (!result.has_value()) {
    return {};
  }

  if (result->error.has_value()) {
    last_error_ = result->error;
    state_ = result->error == ReadbackError::kCancelled
      ? ReadbackState::kCancelled
      : ReadbackState::kFailed;
    return std::unexpected(*result->error);
  }

  state_ = ReadbackState::kReady;
  last_error_.reset();
  return {};
}

auto D3D12BufferReadback::EnqueueCopy(
  oxygen::graphics::CommandRecorder& recorder, const Buffer& source,
  BufferRange range) -> std::expected<ReadbackTicket, ReadbackError>
{
  if (state_ == ReadbackState::kMapped) {
    return std::unexpected(ReadbackError::kAlreadyMapped);
  }
  if (state_ != ReadbackState::kIdle) {
    return std::unexpected(ReadbackError::kAlreadyPending);
  }

  const auto target_queue = recorder.GetTargetQueue();
  if (target_queue == nullptr) {
    return std::unexpected(ReadbackError::kQueueUnavailable);
  }
  if (const auto queue_result = manager_.EnsureTrackedQueue(target_queue);
    !queue_result.has_value()) {
    return std::unexpected(queue_result.error());
  }

  const auto resolved = range.Resolve(source.GetDescriptor());
  if (resolved.size_bytes == 0) {
    return std::unexpected(ReadbackError::kInvalidArgument);
  }
  if (!EnsureReadbackBuffer(resolved.size_bytes)) {
    return std::unexpected(ReadbackError::kBackendFailure);
  }
  CHECK_NOTNULL_F(readback_buffer_.get(),
    "Readback buffer must exist after successful allocation");
  if (!recorder.IsResourceTracked(source)) {
    return std::unexpected(ReadbackError::kInvalidArgument);
  }
  if (!recorder.IsResourceTracked(*readback_buffer_)) {
    recorder.BeginTrackingResourceState(
      *readback_buffer_, ResourceStates::kCopyDest);
  }

  recorder.RequireResourceState(source, ResourceStates::kCopySource);
  recorder.RequireResourceState(*readback_buffer_, ResourceStates::kCopyDest);
  recorder.FlushBarriers();
  recorder.CopyBuffer(*readback_buffer_, 0, source,
    static_cast<size_t>(resolved.offset_bytes),
    static_cast<size_t>(resolved.size_bytes));

  const auto fence = manager_.AllocateFence(target_queue);
  if (!fence.has_value()) {
    return std::unexpected(fence.error());
  }
  recorder.RecordQueueSignal(fence->get());

  resolved_range_ = resolved;
  ticket_ = manager_.tracker_.Register(
    *fence, SizeBytes { resolved.size_bytes }, debug_name_);
  manager_.TrackCancellationHandler(ticket_->id, [weak = weak_from_this()]() {
    if (const auto locked = weak.lock()) {
      locked->OnManagerCancelled();
    }
  });
  state_ = ReadbackState::kPending;
  last_error_.reset();
  return *ticket_;
}

auto D3D12BufferReadback::IsReady() const -> std::expected<bool, ReadbackError>
{
  if (state_ == ReadbackState::kMapped || state_ == ReadbackState::kReady) {
    return true;
  }
  if (state_ == ReadbackState::kCancelled) {
    return std::unexpected(ReadbackError::kCancelled);
  }
  if (state_ == ReadbackState::kFailed) {
    return std::unexpected(
      last_error_.value_or(ReadbackError::kBackendFailure));
  }
  if (state_ == ReadbackState::kIdle) {
    return false;
  }

  if (const auto refresh = RefreshStateFromTracker(); !refresh.has_value()) {
    return std::unexpected(refresh.error());
  }
  return state_ == ReadbackState::kReady;
}

auto D3D12BufferReadback::ReleaseMapping() -> void
{
  if (state_ == ReadbackState::kMapped && readback_buffer_ != nullptr
    && readback_buffer_->IsMapped()) {
    readback_buffer_->UnMap();
    state_ = ReadbackState::kReady;
  }
}

auto D3D12BufferReadback::TryMap()
  -> std::expected<MappedBufferReadback, ReadbackError>
{
  if (const auto refresh = RefreshStateFromTracker();
    !refresh.has_value() && refresh.error() != ReadbackError::kCancelled) {
    return std::unexpected(refresh.error());
  }

  if (state_ == ReadbackState::kPending || state_ == ReadbackState::kIdle) {
    return std::unexpected(ReadbackError::kNotReady);
  }
  if (state_ == ReadbackState::kCancelled) {
    return std::unexpected(ReadbackError::kCancelled);
  }
  if (state_ == ReadbackState::kFailed) {
    return std::unexpected(
      last_error_.value_or(ReadbackError::kBackendFailure));
  }
  if (state_ == ReadbackState::kMapped) {
    return std::unexpected(ReadbackError::kAlreadyMapped);
  }
  if (readback_buffer_ == nullptr) {
    return std::unexpected(ReadbackError::kBackendFailure);
  }

  auto* mapped = static_cast<const std::byte*>(
    readback_buffer_->Map(0, resolved_range_.size_bytes));
  CHECK_NOTNULL_F(mapped, "Readback buffer map returned null");
  state_ = ReadbackState::kMapped;
  auto guard = std::shared_ptr<void>(nullptr,
    [self = shared_from_this()](void*) mutable { self->ReleaseMapping(); });
  return MappedBufferReadback {
    std::move(guard),
    std::span<const std::byte>(
      mapped, static_cast<size_t>(resolved_range_.size_bytes)),
  };
}

auto D3D12BufferReadback::MapNow()
  -> std::expected<MappedBufferReadback, ReadbackError>
{
  if (!ticket_.has_value()) {
    return std::unexpected(ReadbackError::kNotReady);
  }
  if (state_ == ReadbackState::kPending) {
    const auto result = manager_.Await(*ticket_);
    if (!result.has_value()) {
      return std::unexpected(result.error());
    }
    if (result->error.has_value()) {
      last_error_ = result->error;
      state_ = result->error == ReadbackError::kCancelled
        ? ReadbackState::kCancelled
        : ReadbackState::kFailed;
      return std::unexpected(*result->error);
    }
    state_ = ReadbackState::kReady;
  }
  return TryMap();
}

auto D3D12BufferReadback::Cancel() -> std::expected<bool, ReadbackError>
{
  if (!ticket_.has_value()) {
    return false;
  }
  const auto cancelled = manager_.Cancel(*ticket_);
  if (!cancelled.has_value()) {
    return std::unexpected(cancelled.error());
  }
  if (*cancelled) {
    state_ = ReadbackState::kCancelled;
    last_error_ = ReadbackError::kCancelled;
  }
  return *cancelled;
}

auto D3D12BufferReadback::Reset() -> void
{
  static_cast<void>(RefreshStateFromTracker());
  ReleaseMapping();
  if (ticket_.has_value()) {
    manager_.UntrackCancellationHandler(ticket_->id);
  }
  if (state_ == ReadbackState::kPending) {
    LOG_F(WARNING,
      "Resetting D3D12 buffer readback `{}` while a copy is still pending; "
      "retaining staging buffer registration until completion",
      debug_name_);
  } else {
    TryUnregisterOwnedResource(
      manager_.graphics_, readback_buffer_, debug_name_);
  }
  ticket_.reset();
  last_error_.reset();
  resolved_range_ = {};
  state_ = ReadbackState::kIdle;
}

D3D12BufferReadback::~D3D12BufferReadback() { Reset(); }

class D3D12TextureReadback final
  : public GpuTextureReadback,
    public std::enable_shared_from_this<D3D12TextureReadback> {
public:
  D3D12TextureReadback(
    D3D12ReadbackManager& manager, std::string_view debug_name)
    : manager_(manager)
    , debug_name_(debug_name)
  {
  }

  ~D3D12TextureReadback() override;

  auto EnqueueCopy(oxygen::graphics::CommandRecorder& recorder,
    const oxygen::graphics::Texture& source,
    TextureReadbackRequest request = {})
    -> std::expected<ReadbackTicket, ReadbackError> override;

  [[nodiscard]] auto GetState() const noexcept -> ReadbackState override
  {
    return state_;
  }

  [[nodiscard]] auto Ticket() const noexcept
    -> std::optional<ReadbackTicket> override
  {
    return ticket_;
  }

  [[nodiscard]] auto IsReady() const
    -> std::expected<bool, ReadbackError> override;

  auto TryMap() -> std::expected<MappedTextureReadback, ReadbackError> override;
  auto MapNow() -> std::expected<MappedTextureReadback, ReadbackError> override;

  auto Cancel() -> std::expected<bool, ReadbackError> override;
  auto Reset() -> void override;

  auto OnManagerCancelled() -> void
  {
    if (state_ == ReadbackState::kPending) {
      last_error_ = ReadbackError::kCancelled;
      state_ = ReadbackState::kCancelled;
    }
  }

private:
  auto EnsureReadbackBuffer(SizeBytes size_bytes) -> bool;
  auto EnsureResolveTexture(const TextureDesc& source_desc)
    -> std::expected<void, ReadbackError>;
  auto RefreshStateFromTracker() const -> std::expected<void, ReadbackError>;
  auto ReleaseMapping() -> void;

  D3D12ReadbackManager& manager_;
  std::string debug_name_;
  std::shared_ptr<Buffer> readback_buffer_ {};
  std::shared_ptr<oxygen::graphics::Texture> resolve_texture_ {};
  TextureDesc resolve_texture_desc_ {};
  mutable std::optional<ReadbackTicket> ticket_ {};
  mutable std::optional<ReadbackError> last_error_ {};
  mutable ReadbackState state_ { ReadbackState::kIdle };
  TextureReadbackLayout layout_ {};
  SizeBytes mapped_size_ {};
};

auto D3D12TextureReadback::EnsureReadbackBuffer(const SizeBytes size_bytes)
  -> bool
{
  if (readback_buffer_ != nullptr
    && readback_buffer_->GetSize() >= size_bytes.get()) {
    return true;
  }

  ReleaseMapping();
  TryUnregisterOwnedResource(manager_.graphics_, readback_buffer_, debug_name_);

  readback_buffer_ = manager_.graphics_.CreateBuffer(BufferDesc {
    .size_bytes = size_bytes.get(),
    .usage = BufferUsage::kNone,
    .memory = BufferMemory::kReadBack,
    .debug_name = debug_name_ + "-texture-staging",
  });
  if (readback_buffer_ == nullptr) {
    LOG_F(ERROR, "Failed to allocate texture readback buffer `{}` ({} bytes)",
      debug_name_, size_bytes.get());
    return false;
  }

  auto& registry = manager_.graphics_.GetResourceRegistry();
  if (!registry.Contains(*readback_buffer_)) {
    registry.Register(readback_buffer_);
  }
  return true;
}

auto D3D12TextureReadback::EnsureResolveTexture(const TextureDesc& source_desc)
  -> std::expected<void, ReadbackError>
{
  const auto expected_desc = MakeResolvedTextureDesc(source_desc);
  if (resolve_texture_ != nullptr
    && MatchesResolveTextureDesc(resolve_texture_desc_, expected_desc)) {
    return {};
  }

  TryUnregisterOwnedResource(manager_.graphics_, resolve_texture_, debug_name_);

  resolve_texture_ = manager_.graphics_.CreateTexture(expected_desc);
  if (resolve_texture_ == nullptr) {
    LOG_F(ERROR, "Failed to allocate resolve texture `{}` for readback",
      expected_desc.debug_name);
    return std::unexpected(ReadbackError::kBackendFailure);
  }

  auto& registry = manager_.graphics_.GetResourceRegistry();
  if (!registry.Contains(*resolve_texture_)) {
    registry.Register(resolve_texture_);
  }

  resolve_texture_desc_ = expected_desc;
  return {};
}

auto D3D12TextureReadback::RefreshStateFromTracker() const
  -> std::expected<void, ReadbackError>
{
  if (state_ != ReadbackState::kPending || !ticket_.has_value()) {
    return {};
  }

  manager_.PumpCompletions();
  const auto result = manager_.TryGetResult(ticket_->id);
  if (!result.has_value()) {
    return {};
  }

  if (result->error.has_value()) {
    last_error_ = result->error;
    state_ = result->error == ReadbackError::kCancelled
      ? ReadbackState::kCancelled
      : ReadbackState::kFailed;
    return std::unexpected(*result->error);
  }

  state_ = ReadbackState::kReady;
  last_error_.reset();
  return {};
}

auto D3D12TextureReadback::EnqueueCopy(
  oxygen::graphics::CommandRecorder& recorder,
  const oxygen::graphics::Texture& source, TextureReadbackRequest request)
  -> std::expected<ReadbackTicket, ReadbackError>
{
  if (state_ == ReadbackState::kMapped) {
    return std::unexpected(ReadbackError::kAlreadyMapped);
  }
  if (state_ != ReadbackState::kIdle) {
    return std::unexpected(ReadbackError::kAlreadyPending);
  }

  const auto target_queue = recorder.GetTargetQueue();
  if (target_queue == nullptr) {
    return std::unexpected(ReadbackError::kQueueUnavailable);
  }
  if (const auto queue_result = manager_.EnsureTrackedQueue(target_queue);
    !queue_result.has_value()) {
    return std::unexpected(queue_result.error());
  }

  const auto& source_desc = source.GetDescriptor();
  if (const auto validation
    = ValidateTextureReadbackRequest(source_desc, request);
    !validation.has_value()) {
    return std::unexpected(validation.error());
  }

  // NOLINTBEGIN(cppcoreguidelines-pro-type-static-cast-downcast)
  const auto& source_texture = static_cast<const d3d12::Texture&>(source);
  auto& d3d12_recorder = static_cast<d3d12::CommandRecorder&>(recorder);
  // NOLINTEND(cppcoreguidelines-pro-type-static-cast-downcast)
  if (source_texture.IsReadbackSurface()) {
    return std::unexpected(ReadbackError::kUnsupportedResource);
  }

  auto resolved_slice = request.src_slice.Resolve(source_desc);
  auto copy_source = observer_ptr<const oxygen::graphics::Texture> { &source };
  auto copy_desc = source_desc;
  auto plane_count = source_texture.GetPlaneCount();

  if (source_desc.sample_count > 1) {
    if (target_queue->GetQueueRole() != QueueRole::kGraphics) {
      return std::unexpected(ReadbackError::kQueueUnavailable);
    }
    if (const auto resolve_result = EnsureResolveTexture(source_desc);
      !resolve_result.has_value()) {
      return std::unexpected(resolve_result.error());
    }
    CHECK_NOTNULL_F(resolve_texture_.get(),
      "Resolve texture must exist after successful allocation");
    if (!recorder.IsResourceTracked(source)) {
      return std::unexpected(ReadbackError::kInvalidArgument);
    }
    if (!recorder.IsResourceTracked(*resolve_texture_)) {
      recorder.BeginTrackingResourceState(
        *resolve_texture_, ResourceStates::kResolveDest);
    }

    recorder.RequireResourceState(source, ResourceStates::kResolveSource);
    recorder.RequireResourceState(
      *resolve_texture_, ResourceStates::kResolveDest);
    recorder.FlushBarriers();

    auto* command_list = d3d12_recorder.GetD3D12CommandList();
    auto* src_resource
      = source_texture.GetNativeResource()->AsPointer<ID3D12Resource>();
    auto* resolve_resource
      = resolve_texture_->GetNativeResource()->AsPointer<ID3D12Resource>();
    CHECK_NOTNULL_F(
      command_list, "D3D12 command list must exist for texture resolve");
    CHECK_NOTNULL_F(src_resource,
      "D3D12 source texture resource must exist for readback resolve");
    CHECK_NOTNULL_F(resolve_resource,
      "D3D12 resolve texture resource must exist for readback resolve");

    const auto subresource_index
      = ComputeSubresourceIndex(source_desc, resolved_slice);
    command_list->ResolveSubresource(resolve_resource, subresource_index,
      src_resource, subresource_index,
      GetDxgiFormatMapping(source_desc.format).rtv_format);

    copy_source = observer_ptr<const oxygen::graphics::Texture> {
      resolve_texture_.get()
    };
    copy_desc = resolve_texture_desc_;
    plane_count = 1U;
  } else if (!recorder.IsResourceTracked(source)) {
    return std::unexpected(ReadbackError::kInvalidArgument);
  }

  const auto copy_region
    = ResolveTextureReadbackRegion(copy_desc, resolved_slice);
  auto* copy_resource = const_cast<ID3D12Resource*>(
    copy_source->GetNativeResource()->AsPointer<ID3D12Resource>());
  CHECK_NOTNULL_F(
    copy_resource, "D3D12 texture resource must exist for readback copy");
  detail::TextureToBufferCopyInfo copy_info {};
  try {
    copy_info = ComputeTextureToBufferCopyInfo(
      copy_desc, copy_resource->GetDesc().Format, plane_count, copy_region);
  } catch (const std::exception&) {
    LOG_F(ERROR,
      "Failed to compute texture readback layout for `{}` (planes={})",
      debug_name_, plane_count);
    return std::unexpected(ReadbackError::kUnsupportedResource);
  }

  if (!EnsureReadbackBuffer(SizeBytes { copy_info.bytes_written })) {
    return std::unexpected(ReadbackError::kBackendFailure);
  }
  CHECK_NOTNULL_F(readback_buffer_.get(),
    "Texture readback buffer must exist after successful allocation");
  if (!recorder.IsResourceTracked(*readback_buffer_)) {
    recorder.BeginTrackingResourceState(
      *readback_buffer_, ResourceStates::kCopyDest);
  }

  recorder.RequireResourceState(*copy_source, ResourceStates::kCopySource);
  recorder.RequireResourceState(*readback_buffer_, ResourceStates::kCopyDest);
  recorder.FlushBarriers();
  recorder.CopyTextureToBuffer(*readback_buffer_, *copy_source, copy_region);

  const auto fence = manager_.AllocateFence(target_queue);
  if (!fence.has_value()) {
    return std::unexpected(fence.error());
  }
  recorder.RecordQueueSignal(fence->get());

  layout_ = BuildTextureReadbackLayout(
    copy_desc, resolved_slice, copy_info.resolved_region, request.aspects);
  mapped_size_ = SizeBytes { copy_info.bytes_written };
  ticket_ = manager_.tracker_.Register(*fence, mapped_size_, debug_name_);
  manager_.TrackCancellationHandler(ticket_->id, [weak = weak_from_this()]() {
    if (const auto locked = weak.lock()) {
      locked->OnManagerCancelled();
    }
  });
  state_ = ReadbackState::kPending;
  last_error_.reset();
  return *ticket_;
}

auto D3D12TextureReadback::IsReady() const -> std::expected<bool, ReadbackError>
{
  if (state_ == ReadbackState::kMapped || state_ == ReadbackState::kReady) {
    return true;
  }
  if (state_ == ReadbackState::kCancelled) {
    return std::unexpected(ReadbackError::kCancelled);
  }
  if (state_ == ReadbackState::kFailed) {
    return std::unexpected(
      last_error_.value_or(ReadbackError::kBackendFailure));
  }
  if (state_ == ReadbackState::kIdle) {
    return false;
  }

  if (const auto refresh = RefreshStateFromTracker(); !refresh.has_value()) {
    return std::unexpected(refresh.error());
  }
  return state_ == ReadbackState::kReady;
}

auto D3D12TextureReadback::ReleaseMapping() -> void
{
  if (state_ == ReadbackState::kMapped && readback_buffer_ != nullptr
    && readback_buffer_->IsMapped()) {
    readback_buffer_->UnMap();
    state_ = ReadbackState::kReady;
  }
}

auto D3D12TextureReadback::TryMap()
  -> std::expected<MappedTextureReadback, ReadbackError>
{
  if (const auto refresh = RefreshStateFromTracker();
    !refresh.has_value() && refresh.error() != ReadbackError::kCancelled) {
    return std::unexpected(refresh.error());
  }

  if (state_ == ReadbackState::kPending || state_ == ReadbackState::kIdle) {
    return std::unexpected(ReadbackError::kNotReady);
  }
  if (state_ == ReadbackState::kCancelled) {
    return std::unexpected(ReadbackError::kCancelled);
  }
  if (state_ == ReadbackState::kFailed) {
    return std::unexpected(
      last_error_.value_or(ReadbackError::kBackendFailure));
  }
  if (state_ == ReadbackState::kMapped) {
    return std::unexpected(ReadbackError::kAlreadyMapped);
  }
  if (readback_buffer_ == nullptr) {
    return std::unexpected(ReadbackError::kBackendFailure);
  }

  auto* mapped = static_cast<const std::byte*>(
    readback_buffer_->Map(0, mapped_size_.get()));
  CHECK_NOTNULL_F(mapped, "Texture readback buffer map returned null");
  state_ = ReadbackState::kMapped;
  auto guard = std::shared_ptr<void>(nullptr,
    [self = shared_from_this()](void*) mutable { self->ReleaseMapping(); });
  return MappedTextureReadback { std::move(guard), mapped, layout_ };
}

auto D3D12TextureReadback::MapNow()
  -> std::expected<MappedTextureReadback, ReadbackError>
{
  if (!ticket_.has_value()) {
    return std::unexpected(ReadbackError::kNotReady);
  }
  if (state_ == ReadbackState::kPending) {
    const auto result = manager_.Await(*ticket_);
    if (!result.has_value()) {
      return std::unexpected(result.error());
    }
    if (result->error.has_value()) {
      last_error_ = result->error;
      state_ = result->error == ReadbackError::kCancelled
        ? ReadbackState::kCancelled
        : ReadbackState::kFailed;
      return std::unexpected(*result->error);
    }
    state_ = ReadbackState::kReady;
  }
  return TryMap();
}

auto D3D12TextureReadback::Cancel() -> std::expected<bool, ReadbackError>
{
  if (!ticket_.has_value()) {
    return false;
  }
  const auto cancelled = manager_.Cancel(*ticket_);
  if (!cancelled.has_value()) {
    return std::unexpected(cancelled.error());
  }
  if (*cancelled) {
    state_ = ReadbackState::kCancelled;
    last_error_ = ReadbackError::kCancelled;
  }
  return *cancelled;
}

auto D3D12TextureReadback::Reset() -> void
{
  static_cast<void>(RefreshStateFromTracker());
  ReleaseMapping();
  if (ticket_.has_value()) {
    manager_.UntrackCancellationHandler(ticket_->id);
  }
  if (state_ == ReadbackState::kPending) {
    LOG_F(WARNING,
      "Resetting D3D12 texture readback `{}` while a copy is still pending; "
      "retaining staging resources until completion",
      debug_name_);
  } else {
    TryUnregisterOwnedResource(
      manager_.graphics_, readback_buffer_, debug_name_);
    TryUnregisterOwnedResource(
      manager_.graphics_, resolve_texture_, debug_name_);
  }
  ticket_.reset();
  last_error_.reset();
  layout_ = {};
  mapped_size_ = {};
  state_ = ReadbackState::kIdle;
}

D3D12TextureReadback::~D3D12TextureReadback() { Reset(); }

D3D12ReadbackManager::D3D12ReadbackManager(Graphics& graphics)
  : graphics_(graphics)
{
}

D3D12ReadbackManager::~D3D12ReadbackManager() = default;

auto D3D12ReadbackManager::CreateBufferReadback(
  const std::string_view debug_name) -> std::shared_ptr<GpuBufferReadback>
{
  return std::make_shared<D3D12BufferReadback>(*this, debug_name);
}

auto D3D12ReadbackManager::CreateTextureReadback(
  const std::string_view debug_name) -> std::shared_ptr<GpuTextureReadback>
{
  return std::make_shared<D3D12TextureReadback>(*this, debug_name);
}

auto D3D12ReadbackManager::EnsureTrackedQueue(
  const observer_ptr<graphics::CommandQueue> queue)
  -> std::expected<void, ReadbackError>
{
  if (queue == nullptr) {
    return std::unexpected(ReadbackError::kQueueUnavailable);
  }

  std::lock_guard lock(mutex_);
  if (shutdown_) {
    return std::unexpected(ReadbackError::kShutdown);
  }
  if (tracked_queue_ == nullptr) {
    tracked_queue_ = queue;
    next_fence_ = FenceValue { (
      std::max)(queue->GetCurrentValue(), queue->GetCompletedValue()) };
    return {};
  }
  if (tracked_queue_ != queue) {
    return std::unexpected(ReadbackError::kQueueUnavailable);
  }
  return {};
}

auto D3D12ReadbackManager::AllocateFence(
  const observer_ptr<graphics::CommandQueue> queue)
  -> std::expected<FenceValue, ReadbackError>
{
  if (const auto tracked = EnsureTrackedQueue(queue); !tracked.has_value()) {
    return std::unexpected(tracked.error());
  }

  std::lock_guard lock(mutex_);
  const auto current_fence = FenceValue { (
    std::max)(queue->GetCurrentValue(), queue->GetCompletedValue()) };
  next_fence_
    = FenceValue { (std::max)(next_fence_.get(), current_fence.get()) + 1 };
  return next_fence_;
}

auto D3D12ReadbackManager::PumpCompletions() -> void
{
  observer_ptr<graphics::CommandQueue> queue;
  {
    std::lock_guard lock(mutex_);
    queue = tracked_queue_;
  }
  if (queue != nullptr) {
    tracker_.MarkFenceCompleted(FenceValue { queue->GetCompletedValue() });
  }
}

auto D3D12ReadbackManager::TryGetResult(const ReadbackTicketId id) const
  -> std::optional<ReadbackResult>
{
  return tracker_.TryGetResult(id);
}

auto D3D12ReadbackManager::TrackCancellationHandler(
  const ReadbackTicketId id, std::function<void()> handler) -> void
{
  std::lock_guard lock(mutex_);
  cancellation_handlers_[id] = std::move(handler);
}

auto D3D12ReadbackManager::UntrackCancellationHandler(const ReadbackTicketId id)
  -> void
{
  std::lock_guard lock(mutex_);
  cancellation_handlers_.erase(id);
}

auto D3D12ReadbackManager::Await(const ReadbackTicket ticket)
  -> std::expected<ReadbackResult, ReadbackError>
{
  PumpCompletions();

  observer_ptr<graphics::CommandQueue> queue;
  {
    std::lock_guard lock(mutex_);
    queue = tracked_queue_;
  }
  if (queue != nullptr && queue->GetCompletedValue() < ticket.fence.get()) {
    try {
      queue->Wait(ticket.fence.get());
    } catch (...) {
      LOG_F(ERROR, "Readback await failed for ticket {} at fence {}",
        ticket.id.get(), ticket.fence.get());
      return std::unexpected(ReadbackError::kBackendFailure);
    }
    tracker_.MarkFenceCompleted(ticket.fence);
  }
  return tracker_.Await(ticket.id);
}

auto D3D12ReadbackManager::AwaitAsync(const ReadbackTicket ticket)
  -> co::Co<void>
{
  PumpCompletions();
  co_await Until(tracker_.CompletedFenceValue() >= ticket.fence);
  co_return;
}

auto D3D12ReadbackManager::Cancel(const ReadbackTicket ticket)
  -> std::expected<bool, ReadbackError>
{
  PumpCompletions();
  const auto cancelled = tracker_.Cancel(ticket.id);
  if (!cancelled.has_value()) {
    return std::unexpected(cancelled.error());
  }

  if (*cancelled) {
    std::function<void()> handler;
    {
      std::lock_guard lock(mutex_);
      if (const auto it = cancellation_handlers_.find(ticket.id);
        it != cancellation_handlers_.end()) {
        handler = it->second;
      }
    }
    if (handler) {
      handler();
    }
  }
  return *cancelled;
}

auto D3D12ReadbackManager::ReadBufferNow(const Buffer& source,
  BufferRange range) -> std::expected<std::vector<std::byte>, ReadbackError>
{
  LOG_SCOPE_F(1, "ReadBufferNow `{}`", source.GetName());

  auto readback = CreateBufferReadback("ReadBufferNow");
  CHECK_NOTNULL_F(readback.get());

  QueueRole queue_role = QueueRole::kGraphics;
  {
    std::lock_guard lock(mutex_);
    if (tracked_queue_ != nullptr) {
      queue_role = tracked_queue_->GetQueueRole();
    }
  }
  DLOG_F(2, "ReadBufferNow queue role: {}", queue_role);

  {
    auto recorder = graphics_.AcquireCommandRecorder(
      graphics_.QueueKeyFor(queue_role), "ReadBufferNow");
    CHECK_NOTNULL_F(recorder.get());
    recorder->BeginTrackingResourceState(source, ResourceStates::kCommon, true);

    const auto ticket = readback->EnqueueCopy(*recorder, source, range);
    if (!ticket.has_value()) {
      LOG_F(ERROR, "ReadBufferNow enqueue failed for `{}`: {}",
        source.GetName(), ticket.error());
      return std::unexpected(ticket.error());
    }
  }

  const auto mapped = readback->MapNow();
  if (!mapped.has_value()) {
    LOG_F(ERROR, "ReadBufferNow map failed for `{}`: {}", source.GetName(),
      mapped.error());
    return std::unexpected(mapped.error());
  }

  const auto bytes = mapped->Bytes();
  return std::vector<std::byte>(bytes.begin(), bytes.end());
}

auto D3D12ReadbackManager::ReadTextureNow(
  const oxygen::graphics::Texture& source, TextureReadbackRequest request,
  const bool tightly_pack)
  -> std::expected<OwnedTextureReadbackData, ReadbackError>
{
  LOG_SCOPE_F(1, "ReadTextureNow `{}`", source.GetName());

  auto readback = CreateTextureReadback("ReadTextureNow");
  CHECK_NOTNULL_F(readback.get());

  QueueRole queue_role = QueueRole::kGraphics;
  {
    std::lock_guard lock(mutex_);
    if (tracked_queue_ != nullptr) {
      queue_role = tracked_queue_->GetQueueRole();
    }
  }
  DLOG_F(2, "ReadTextureNow queue role: {}, tightly_pack={}", queue_role,
    tightly_pack);

  {
    auto recorder = graphics_.AcquireCommandRecorder(
      graphics_.QueueKeyFor(queue_role), "ReadTextureNow");
    CHECK_NOTNULL_F(recorder.get());
    recorder->BeginTrackingResourceState(
      source, source.GetDescriptor().initial_state, true);

    const auto ticket = readback->EnqueueCopy(*recorder, source, request);
    if (!ticket.has_value()) {
      LOG_F(ERROR, "ReadTextureNow enqueue failed for `{}`: {}",
        source.GetName(), ticket.error());
      return std::unexpected(ticket.error());
    }
  }

  const auto mapped = readback->MapNow();
  if (!mapped.has_value()) {
    LOG_F(ERROR, "ReadTextureNow map failed for `{}`: {}", source.GetName(),
      mapped.error());
    return std::unexpected(mapped.error());
  }

  if (tightly_pack) {
    return TightPackMappedTextureBytes(*mapped);
  }
  return CopyMappedTextureBytes(*mapped);
}

auto D3D12ReadbackManager::CreateReadbackTextureSurface(const TextureDesc& desc)
  -> std::expected<std::shared_ptr<oxygen::graphics::Texture>, ReadbackError>
{
  LOG_SCOPE_F(1, "CreateReadbackTextureSurface `{}`", desc.debug_name.c_str());

  const auto readback_desc = BuildReadbackSurfaceTextureDesc(desc);
  if (!readback_desc.has_value()) {
    LOG_F(ERROR, "CreateReadbackTextureSurface rejected `{}`: {}",
      desc.debug_name.c_str(), readback_desc.error());
    return std::unexpected(readback_desc.error());
  }

  auto surface = graphics_.CreateTexture(*readback_desc);
  if (surface == nullptr) {
    LOG_F(ERROR, "CreateReadbackTextureSurface failed for `{}`",
      desc.debug_name.c_str());
    return std::unexpected(ReadbackError::kBackendFailure);
  }

  CHECK_EQ_F(surface->GetTypeId(), Texture::ClassTypeId(),
    "D3D12 readback surface creation returned unexpected texture type: {}",
    surface->GetTypeId());
  const auto* d3d12_surface = static_cast<const Texture*>(surface.get());
  CHECK_F(d3d12_surface->IsReadbackSurface(),
    "D3D12 readback surface creation must return a readback surface");
  return surface;
}

auto D3D12ReadbackManager::MapReadbackTextureSurface(
  oxygen::graphics::Texture& surface, TextureSlice slice)
  -> std::expected<ReadbackSurfaceMapping, ReadbackError>
{
  LOG_SCOPE_F(1, "MapReadbackTextureSurface `{}`", surface.GetName());

  if (surface.GetTypeId() != Texture::ClassTypeId()) {
    LOG_F(ERROR,
      "MapReadbackTextureSurface rejected unexpected texture type `{}` ({})",
      surface.GetName(), surface.GetTypeId());
    return std::unexpected(ReadbackError::kUnsupportedResource);
  }
  auto* d3d12_surface = static_cast<Texture*>(&surface);
  if (!d3d12_surface->IsReadbackSurface()) {
    LOG_F(ERROR, "MapReadbackTextureSurface rejected non-readback texture `{}`",
      surface.GetName());
    return std::unexpected(ReadbackError::kUnsupportedResource);
  }
  if (d3d12_surface->IsReadbackSurfaceMapped()) {
    return std::unexpected(ReadbackError::kAlreadyMapped);
  }
  if (d3d12_surface->GetPlaneCount() != 1) {
    return std::unexpected(ReadbackError::kUnsupportedResource);
  }

  const auto& desc = d3d12_surface->GetDescriptor();
  const auto& format_info = GetFormatInfo(desc.format);
  if (format_info.has_depth || format_info.has_stencil) {
    return std::unexpected(ReadbackError::kUnsupportedResource);
  }

  const auto resolved_slice = slice.Resolve(desc);
  ValidateResolvedSlice(desc, resolved_slice);
  const auto subresource_index = ComputeSubresourceIndex(desc, resolved_slice);
  const auto& surface_layout = d3d12_surface->GetReadbackSurfaceLayout();
  CHECK_LT_F(subresource_index, surface_layout.subresources.size(),
    "Resolved readback surface subresource index {} out of bounds ({})",
    subresource_index, surface_layout.subresources.size());
  const auto& subresource_layout
    = surface_layout.subresources[subresource_index];

  const auto mapped_offset = ComputeReadbackSurfaceByteOffset(
    desc, resolved_slice, subresource_layout);
  if (!mapped_offset.has_value()) {
    return std::unexpected(mapped_offset.error());
  }

  auto* resource
    = d3d12_surface->GetNativeResource()->AsPointer<ID3D12Resource>();
  CHECK_NOTNULL_F(
    resource, "D3D12 readback surface must expose a valid native resource");

  const auto row_pitch
    = SizeBytes { subresource_layout.placed_footprint.Footprint.RowPitch };
  const auto slice_pitch
    = SizeBytes { static_cast<uint64_t>(
                    subresource_layout.placed_footprint.Footprint.RowPitch)
        * static_cast<uint64_t>(subresource_layout.row_count) };

  void* mapped_base = nullptr;
  const HRESULT hr = resource->Map(0, nullptr, &mapped_base);
  if (FAILED(hr)) {
    LOG_F(ERROR, "MapReadbackTextureSurface failed for `{}` with {:#010X}",
      surface.GetName(), hr);
    return std::unexpected(ReadbackError::kBackendFailure);
  }

  d3d12_surface->SetReadbackSurfaceMapped(true);
  auto* data
    = static_cast<const std::byte*>(mapped_base) + mapped_offset.value();
  return ReadbackSurfaceMapping {
    .data = data,
    .layout = TextureReadbackLayout {
      .format = desc.format,
      .texture_type = desc.texture_type,
      .width = resolved_slice.width,
      .height = resolved_slice.height,
      .depth = resolved_slice.depth,
      .row_pitch = row_pitch,
      .slice_pitch = slice_pitch,
      .mip_level = resolved_slice.mip_level,
      .array_slice = resolved_slice.array_slice,
      .aspects = ClearFlags::kColor,
    },
  };
}

auto D3D12ReadbackManager::UnmapReadbackTextureSurface(
  oxygen::graphics::Texture& surface) -> void
{
  if (surface.GetTypeId() != Texture::ClassTypeId()) {
    LOG_F(ERROR,
      "UnmapReadbackTextureSurface rejected unexpected texture type `{}` ({})",
      surface.GetName(), surface.GetTypeId());
    return;
  }
  auto* d3d12_surface = static_cast<Texture*>(&surface);
  if (!d3d12_surface->IsReadbackSurface()) {
    LOG_F(ERROR,
      "UnmapReadbackTextureSurface rejected non-readback texture `{}`",
      surface.GetName());
    return;
  }
  if (!d3d12_surface->IsReadbackSurfaceMapped()) {
    DLOG_F(2, "UnmapReadbackTextureSurface ignored unmapped surface `{}`",
      surface.GetName());
    return;
  }

  auto* resource
    = d3d12_surface->GetNativeResource()->AsPointer<ID3D12Resource>();
  CHECK_NOTNULL_F(
    resource, "D3D12 readback surface must expose a valid native resource");
  resource->Unmap(0, nullptr);
  d3d12_surface->SetReadbackSurfaceMapped(false);
}

auto D3D12ReadbackManager::OnFrameStart(const frame::Slot slot) -> void
{
  PumpCompletions();
  tracker_.OnFrameStart(slot);
}

auto D3D12ReadbackManager::Shutdown(const std::chrono::milliseconds timeout)
  -> std::expected<void, ReadbackError>
{
  LOG_SCOPE_FUNCTION(INFO);
  observer_ptr<graphics::CommandQueue> queue;
  FenceValue last_fence { 0 };
  {
    std::lock_guard lock(mutex_);
    shutdown_ = true;
    queue = tracked_queue_;
    last_fence = tracker_.LastRegisteredFence();
  }

  if (queue != nullptr && last_fence.get() > 0 && tracker_.HasPending()) {
    try {
      queue->Wait(last_fence.get(), timeout);
    } catch (...) {
      LOG_F(ERROR, "Readback shutdown wait failed at fence {} after {} ms",
        last_fence.get(), timeout.count());
      return std::unexpected(ReadbackError::kBackendFailure);
    }
    tracker_.MarkFenceCompleted(last_fence);
  }
  return {};
}

} // namespace oxygen::graphics::d3d12
