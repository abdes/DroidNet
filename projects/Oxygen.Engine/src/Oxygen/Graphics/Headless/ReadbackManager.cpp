//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <cstring>
#include <fmt/format.h>
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
#include <Oxygen/Graphics/Headless/Buffer.h>
#include <Oxygen/Graphics/Headless/Graphics.h>
#include <Oxygen/Graphics/Headless/ReadbackManager.h>
#include <Oxygen/Graphics/Headless/Texture.h>

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
using oxygen::graphics::ReadbackTicketId;
using oxygen::graphics::ResourceAccessMode;
using oxygen::graphics::ResourceStates;
using oxygen::graphics::TextureBufferCopyRegion;
using oxygen::graphics::TextureDesc;
using oxygen::graphics::TextureReadbackLayout;
using oxygen::graphics::TextureReadbackRequest;
using oxygen::graphics::TextureSlice;
using oxygen::graphics::detail::FormatInfo;
using oxygen::graphics::detail::GetFormatInfo;
using oxygen::graphics::headless::HeadlessReadbackManager;

namespace oxygen::graphics::headless {

namespace {

  struct SurfaceMappingInfo {
    const std::byte* data = nullptr;
    SizeBytes row_pitch {};
    SizeBytes slice_pitch {};
  };

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

  auto ValidateResolvedSlice(const TextureDesc& desc, const TextureSlice& slice)
    -> void
  {
    const auto mip_width = (std::max)(1u, desc.width >> slice.mip_level);
    const auto mip_height = (std::max)(1u, desc.height >> slice.mip_level);
    const auto mip_depth = desc.texture_type == TextureType::kTexture3D
      ? (std::max)(1u, desc.depth >> slice.mip_level)
      : 1u;

    CHECK_LE_F(static_cast<uint64_t>(slice.x) + slice.width,
      static_cast<uint64_t>(mip_width),
      "Texture readback width exceeds mip bounds: x={} width={} mip_width={}",
      slice.x, slice.width, mip_width);
    CHECK_LE_F(static_cast<uint64_t>(slice.y) + slice.height,
      static_cast<uint64_t>(mip_height),
      "Texture readback height exceeds mip bounds: y={} height={} "
      "mip_height={}",
      slice.y, slice.height, mip_height);
    CHECK_LE_F(static_cast<uint64_t>(slice.z) + slice.depth,
      static_cast<uint64_t>(mip_depth),
      "Texture readback depth exceeds mip bounds: z={} depth={} mip_depth={}",
      slice.z, slice.depth, mip_depth);
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

  auto ComputeHeadlessSurfaceMappingInfo(
    const Texture& surface, const TextureSlice& resolved_slice)
    -> std::expected<SurfaceMappingInfo, ReadbackError>
  {
    const auto& desc = surface.GetDescriptor();
    const auto& format_info = GetFormatInfo(desc.format);
    const auto& layout = surface.GetLayoutStrategy();

    const auto mip_width
      = (std::max)(1u, desc.width >> resolved_slice.mip_level);
    const auto mip_height
      = (std::max)(1u, desc.height >> resolved_slice.mip_level);
    const auto mip_depth = desc.texture_type == TextureType::kTexture3D
      ? (std::max)(1u, desc.depth >> resolved_slice.mip_level)
      : 1u;
    const auto mip_footprint = ComputeLinearTextureCopyFootprint(desc.format,
      { .width = mip_width, .height = mip_height, .depth = mip_depth });

    const auto base_offset = layout.ComputeSliceMipBaseOffset(
      desc, resolved_slice.array_slice, resolved_slice.mip_level);
    auto byte_offset = static_cast<uint64_t>(base_offset);

    if (format_info.block_size > 1) {
      const auto block_size = static_cast<uint32_t>(format_info.block_size);
      if ((resolved_slice.x % block_size) != 0u
        || (resolved_slice.y % block_size) != 0u) {
        return std::unexpected(ReadbackError::kInvalidArgument);
      }
      byte_offset += static_cast<uint64_t>(resolved_slice.z)
          * mip_footprint.slice_pitch.get()
        + static_cast<uint64_t>(resolved_slice.y / block_size)
          * mip_footprint.row_pitch.get()
        + static_cast<uint64_t>(resolved_slice.x / block_size)
          * static_cast<uint64_t>(format_info.bytes_per_block);
    } else {
      const auto bytes_per_pixel
        = static_cast<uint64_t>(format_info.bytes_per_block);
      byte_offset += static_cast<uint64_t>(resolved_slice.z)
          * mip_footprint.slice_pitch.get()
        + static_cast<uint64_t>(resolved_slice.y)
          * mip_footprint.row_pitch.get()
        + static_cast<uint64_t>(resolved_slice.x) * bytes_per_pixel;
    }

    const auto* backing = surface.GetBackingData();
    CHECK_NOTNULL_F(
      backing, "Headless readback surface must expose CPU backing data");
    return SurfaceMappingInfo {
      .data = backing + byte_offset,
      .row_pitch = mip_footprint.row_pitch,
      .slice_pitch = mip_footprint.slice_pitch,
    };
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
        "Failed to unregister headless readback-owned resource `{}` for "
        "`{}`: {}",
        resource->GetName(), debug_name, ex.what());
    }

    resource.reset();
  }

} // namespace

class HeadlessBufferReadback final
  : public GpuBufferReadback,
    public std::enable_shared_from_this<HeadlessBufferReadback> {
public:
  HeadlessBufferReadback(
    HeadlessReadbackManager& manager, std::string_view debug_name)
    : manager_(manager)
    , debug_name_(debug_name)
  {
  }

  ~HeadlessBufferReadback() override;

  auto EnqueueCopy(oxygen::graphics::CommandRecorder& recorder,
    const oxygen::graphics::Buffer& source, BufferRange range = {})
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

  HeadlessReadbackManager& manager_;
  std::string debug_name_;
  std::shared_ptr<Buffer> readback_buffer_ {};
  mutable std::optional<ReadbackTicket> ticket_ {};
  mutable std::optional<ReadbackError> last_error_ {};
  mutable ReadbackState state_ { ReadbackState::kIdle };
  BufferRange resolved_range_ {};
};

class HeadlessTextureReadback final
  : public GpuTextureReadback,
    public std::enable_shared_from_this<HeadlessTextureReadback> {
public:
  HeadlessTextureReadback(
    HeadlessReadbackManager& manager, std::string_view debug_name)
    : manager_(manager)
    , debug_name_(debug_name)
  {
  }

  ~HeadlessTextureReadback() override;

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
  auto EnsureReadbackBuffer(uint64_t size_bytes) -> bool;
  auto RefreshStateFromTracker() const -> std::expected<void, ReadbackError>;
  auto ReleaseMapping() -> void;

  HeadlessReadbackManager& manager_;
  std::string debug_name_;
  std::shared_ptr<Buffer> readback_buffer_ {};
  mutable std::optional<ReadbackTicket> ticket_ {};
  mutable std::optional<ReadbackError> last_error_ {};
  mutable ReadbackState state_ { ReadbackState::kIdle };
  TextureReadbackLayout layout_ {};
  SizeBytes mapped_size_ {};
};

auto HeadlessBufferReadback::EnsureReadbackBuffer(const uint64_t size_bytes)
  -> bool
{
  if (readback_buffer_ != nullptr
    && readback_buffer_->GetSize() >= size_bytes) {
    return true;
  }

  ReleaseMapping();
  TryUnregisterOwnedResource(manager_.graphics_, readback_buffer_, debug_name_);

  readback_buffer_ = std::static_pointer_cast<Buffer>(
    manager_.graphics_.CreateBuffer(BufferDesc {
      .size_bytes = size_bytes,
      .usage = BufferUsage::kNone,
      .memory = BufferMemory::kReadBack,
      .debug_name = debug_name_ + "-staging",
    }));
  if (readback_buffer_ == nullptr) {
    LOG_F(ERROR, "Failed to allocate headless readback buffer `{}` ({} bytes)",
      debug_name_, size_bytes);
    return false;
  }

  auto& registry = manager_.graphics_.GetResourceRegistry();
  if (!registry.Contains(*readback_buffer_)) {
    registry.Register(readback_buffer_);
  }
  return true;
}

auto HeadlessBufferReadback::RefreshStateFromTracker() const
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

auto HeadlessBufferReadback::EnqueueCopy(
  oxygen::graphics::CommandRecorder& recorder,
  const oxygen::graphics::Buffer& source, BufferRange range)
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

  const auto resolved = range.Resolve(source.GetDescriptor());
  if (resolved.size_bytes == 0) {
    return std::unexpected(ReadbackError::kInvalidArgument);
  }
  if (!EnsureReadbackBuffer(resolved.size_bytes)) {
    return std::unexpected(ReadbackError::kBackendFailure);
  }
  CHECK_NOTNULL_F(readback_buffer_.get(),
    "Headless readback buffer must exist after successful allocation");
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

auto HeadlessBufferReadback::IsReady() const
  -> std::expected<bool, ReadbackError>
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

auto HeadlessBufferReadback::ReleaseMapping() -> void
{
  if (state_ == ReadbackState::kMapped && readback_buffer_ != nullptr
    && readback_buffer_->IsMapped()) {
    readback_buffer_->UnMap();
    state_ = ReadbackState::kReady;
  }
}

auto HeadlessBufferReadback::TryMap()
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
  CHECK_NOTNULL_F(mapped, "Headless readback buffer map returned null");
  state_ = ReadbackState::kMapped;
  auto guard = std::shared_ptr<void>(nullptr,
    [self = shared_from_this()](void*) mutable { self->ReleaseMapping(); });
  return MappedBufferReadback {
    std::move(guard),
    std::span<const std::byte>(
      mapped, static_cast<size_t>(resolved_range_.size_bytes)),
  };
}

auto HeadlessBufferReadback::MapNow()
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

auto HeadlessBufferReadback::Cancel() -> std::expected<bool, ReadbackError>
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

auto HeadlessBufferReadback::Reset() -> void
{
  static_cast<void>(RefreshStateFromTracker());
  ReleaseMapping();
  if (ticket_.has_value()) {
    manager_.UntrackCancellationHandler(ticket_->id);
  }
  if (state_ == ReadbackState::kPending) {
    LOG_F(WARNING,
      "Resetting headless buffer readback `{}` while a copy is still "
      "pending; retaining staging buffer registration until completion",
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

HeadlessBufferReadback::~HeadlessBufferReadback() { Reset(); }

auto HeadlessTextureReadback::EnsureReadbackBuffer(const uint64_t size_bytes)
  -> bool
{
  if (readback_buffer_ != nullptr
    && readback_buffer_->GetSize() >= size_bytes) {
    return true;
  }

  ReleaseMapping();
  TryUnregisterOwnedResource(manager_.graphics_, readback_buffer_, debug_name_);

  readback_buffer_ = std::static_pointer_cast<Buffer>(
    manager_.graphics_.CreateBuffer(BufferDesc {
      .size_bytes = size_bytes,
      .usage = BufferUsage::kNone,
      .memory = BufferMemory::kReadBack,
      .debug_name = debug_name_ + "-staging",
    }));
  if (readback_buffer_ == nullptr) {
    LOG_F(ERROR,
      "Failed to allocate headless texture readback buffer `{}` ({} bytes)",
      debug_name_, size_bytes);
    return false;
  }

  auto& registry = manager_.graphics_.GetResourceRegistry();
  if (!registry.Contains(*readback_buffer_)) {
    registry.Register(readback_buffer_);
  }
  return true;
}

auto HeadlessTextureReadback::RefreshStateFromTracker() const
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

auto HeadlessTextureReadback::EnqueueCopy(
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
  if (!recorder.IsResourceTracked(source)) {
    return std::unexpected(ReadbackError::kInvalidArgument);
  }

  const auto resolved_slice = request.src_slice.Resolve(source_desc);
  ValidateResolvedSlice(source_desc, resolved_slice);
  const auto copy_region = ResolveTextureBufferCopyRegion(source_desc,
    TextureBufferCopyRegion {
      .texture_slice = resolved_slice,
    });
  const auto mapped_size = SizeBytes { copy_region.buffer_slice_pitch.get()
    * static_cast<uint64_t>((std::max)(resolved_slice.depth, 1U)) };

  if (!EnsureReadbackBuffer(mapped_size.get())) {
    return std::unexpected(ReadbackError::kBackendFailure);
  }
  CHECK_NOTNULL_F(readback_buffer_.get(),
    "Headless texture readback buffer must exist after successful allocation");
  if (!recorder.IsResourceTracked(*readback_buffer_)) {
    recorder.BeginTrackingResourceState(
      *readback_buffer_, ResourceStates::kCopyDest);
  }

  recorder.RequireResourceState(source, ResourceStates::kCopySource);
  recorder.RequireResourceState(*readback_buffer_, ResourceStates::kCopyDest);
  recorder.FlushBarriers();
  recorder.CopyTextureToBuffer(*readback_buffer_, source, copy_region);

  const auto fence = manager_.AllocateFence(target_queue);
  if (!fence.has_value()) {
    return std::unexpected(fence.error());
  }
  recorder.RecordQueueSignal(fence->get());

  layout_ = BuildTextureReadbackLayout(
    source_desc, resolved_slice, copy_region, request.aspects);
  mapped_size_ = mapped_size;
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

auto HeadlessTextureReadback::IsReady() const
  -> std::expected<bool, ReadbackError>
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

auto HeadlessTextureReadback::ReleaseMapping() -> void
{
  if (state_ == ReadbackState::kMapped && readback_buffer_ != nullptr
    && readback_buffer_->IsMapped()) {
    readback_buffer_->UnMap();
    state_ = ReadbackState::kReady;
  }
}

auto HeadlessTextureReadback::TryMap()
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
  CHECK_NOTNULL_F(mapped, "Headless texture readback buffer map returned null");
  state_ = ReadbackState::kMapped;
  auto guard = std::shared_ptr<void>(nullptr,
    [self = shared_from_this()](void*) mutable { self->ReleaseMapping(); });
  return MappedTextureReadback { std::move(guard), mapped, layout_ };
}

auto HeadlessTextureReadback::MapNow()
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

auto HeadlessTextureReadback::Cancel() -> std::expected<bool, ReadbackError>
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

auto HeadlessTextureReadback::Reset() -> void
{
  static_cast<void>(RefreshStateFromTracker());
  ReleaseMapping();
  if (ticket_.has_value()) {
    manager_.UntrackCancellationHandler(ticket_->id);
  }
  if (state_ == ReadbackState::kPending) {
    LOG_F(WARNING,
      "Resetting headless texture readback `{}` while a copy is still "
      "pending; retaining staging buffer registration until completion",
      debug_name_);
  } else {
    TryUnregisterOwnedResource(
      manager_.graphics_, readback_buffer_, debug_name_);
  }
  ticket_.reset();
  last_error_.reset();
  layout_ = {};
  mapped_size_ = {};
  state_ = ReadbackState::kIdle;
}

HeadlessTextureReadback::~HeadlessTextureReadback() { Reset(); }

HeadlessReadbackManager::HeadlessReadbackManager(Graphics& graphics)
  : graphics_(graphics)
{
}

HeadlessReadbackManager::~HeadlessReadbackManager() = default;

auto HeadlessReadbackManager::CreateBufferReadback(
  const std::string_view debug_name) -> std::shared_ptr<GpuBufferReadback>
{
  return std::make_shared<HeadlessBufferReadback>(*this, debug_name);
}

auto HeadlessReadbackManager::CreateTextureReadback(
  const std::string_view debug_name) -> std::shared_ptr<GpuTextureReadback>
{
  return std::make_shared<HeadlessTextureReadback>(*this, debug_name);
}

auto HeadlessReadbackManager::EnsureTrackedQueue(
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
    next_fence_ = graphics::FenceValue { (
      std::max)(queue->GetCurrentValue(), queue->GetCompletedValue()) };
    return {};
  }
  if (tracked_queue_ != queue) {
    return std::unexpected(ReadbackError::kQueueUnavailable);
  }
  return {};
}

auto HeadlessReadbackManager::AllocateFence(
  const observer_ptr<graphics::CommandQueue> queue)
  -> std::expected<graphics::FenceValue, ReadbackError>
{
  if (const auto tracked = EnsureTrackedQueue(queue); !tracked.has_value()) {
    return std::unexpected(tracked.error());
  }

  std::lock_guard lock(mutex_);
  const auto current_fence = graphics::FenceValue { (
    std::max)(queue->GetCurrentValue(), queue->GetCompletedValue()) };
  next_fence_ = graphics::FenceValue {
    (std::max)(next_fence_.get(), current_fence.get()) + 1
  };
  return next_fence_;
}

auto HeadlessReadbackManager::PumpCompletions() -> void
{
  observer_ptr<graphics::CommandQueue> queue;
  {
    std::lock_guard lock(mutex_);
    queue = tracked_queue_;
  }
  if (queue != nullptr) {
    tracker_.MarkFenceCompleted(
      graphics::FenceValue { queue->GetCompletedValue() });
  }
}

auto HeadlessReadbackManager::TryGetResult(const ReadbackTicketId id) const
  -> std::optional<ReadbackResult>
{
  return tracker_.TryGetResult(id);
}

auto HeadlessReadbackManager::TrackCancellationHandler(
  const ReadbackTicketId id, std::function<void()> handler) -> void
{
  std::lock_guard lock(mutex_);
  cancellation_handlers_[id] = std::move(handler);
}

auto HeadlessReadbackManager::UntrackCancellationHandler(
  const ReadbackTicketId id) -> void
{
  std::lock_guard lock(mutex_);
  cancellation_handlers_.erase(id);
}

auto HeadlessReadbackManager::Await(const ReadbackTicket ticket)
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
      LOG_F(ERROR, "Headless readback await failed for ticket {} at fence {}",
        ticket.id.get(), ticket.fence.get());
      return std::unexpected(ReadbackError::kBackendFailure);
    }
    tracker_.MarkFenceCompleted(ticket.fence);
  }
  return tracker_.Await(ticket.id);
}

auto HeadlessReadbackManager::AwaitAsync(const ReadbackTicket ticket)
  -> co::Co<void>
{
  PumpCompletions();
  co_await Until(tracker_.CompletedFenceValue() >= ticket.fence);
  co_return;
}

auto HeadlessReadbackManager::Cancel(const ReadbackTicket ticket)
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

auto HeadlessReadbackManager::ReadBufferNow(
  const oxygen::graphics::Buffer& source, BufferRange range)
  -> std::expected<std::vector<std::byte>, ReadbackError>
{
  DLOG_SCOPE_F(1, fmt::format("ReadBufferNow `{}`", source.GetName()).c_str());

  auto readback = CreateBufferReadback("ReadBufferNow");
  CHECK_NOTNULL_F(readback.get());

  QueueRole queue_role = QueueRole::kGraphics;
  {
    std::lock_guard lock(mutex_);
    if (tracked_queue_ != nullptr) {
      queue_role = tracked_queue_->GetQueueRole();
    }
  }
  DLOG_F(2, "Headless ReadBufferNow queue role: {}", queue_role);

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

auto HeadlessReadbackManager::ReadTextureNow(
  const oxygen::graphics::Texture& source, TextureReadbackRequest request,
  const bool tightly_pack)
  -> std::expected<OwnedTextureReadbackData, ReadbackError>
{
  DLOG_SCOPE_F(1, fmt::format("ReadTextureNow `{}`", source.GetName()).c_str());

  auto readback = CreateTextureReadback("ReadTextureNow");
  CHECK_NOTNULL_F(readback.get());

  QueueRole queue_role = QueueRole::kGraphics;
  {
    std::lock_guard lock(mutex_);
    if (tracked_queue_ != nullptr) {
      queue_role = tracked_queue_->GetQueueRole();
    }
  }
  DLOG_F(2, "Headless ReadTextureNow queue role: {}, tightly_pack={}",
    queue_role, tightly_pack);

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

auto HeadlessReadbackManager::CreateReadbackTextureSurface(
  const TextureDesc& desc)
  -> std::expected<std::shared_ptr<oxygen::graphics::Texture>, ReadbackError>
{
  DLOG_SCOPE_F(1,
    fmt::format("CreateReadbackTextureSurface `{}`", desc.debug_name).c_str());

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
    "Headless readback surface creation returned unexpected texture type: {}",
    surface->GetTypeId());
  const auto* headless_surface = static_cast<const Texture*>(surface.get());
  CHECK_F(headless_surface->IsReadbackSurface(),
    "Headless readback surface creation must return a readback surface");
  return surface;
}

auto HeadlessReadbackManager::MapReadbackTextureSurface(
  oxygen::graphics::Texture& surface, TextureSlice slice)
  -> std::expected<ReadbackSurfaceMapping, ReadbackError>
{
  DLOG_SCOPE_F(1,
    fmt::format("MapReadbackTextureSurface `{}`", surface.GetName()).c_str());

  if (surface.GetTypeId() != Texture::ClassTypeId()) {
    LOG_F(ERROR,
      "MapReadbackTextureSurface rejected unexpected texture type `{}` ({})",
      surface.GetName(), surface.GetTypeId());
    return std::unexpected(ReadbackError::kUnsupportedResource);
  }
  auto* headless_surface = static_cast<Texture*>(&surface);
  if (!headless_surface->IsReadbackSurface()) {
    LOG_F(ERROR, "MapReadbackTextureSurface rejected non-readback texture `{}`",
      surface.GetName());
    return std::unexpected(ReadbackError::kUnsupportedResource);
  }
  if (headless_surface->IsReadbackSurfaceMapped()) {
    return std::unexpected(ReadbackError::kAlreadyMapped);
  }

  const auto& desc = headless_surface->GetDescriptor();
  const auto& format_info = GetFormatInfo(desc.format);
  if (format_info.has_depth || format_info.has_stencil) {
    return std::unexpected(ReadbackError::kUnsupportedResource);
  }

  const auto resolved_slice = slice.Resolve(desc);
  ValidateResolvedSlice(desc, resolved_slice);
  const auto mapping
    = ComputeHeadlessSurfaceMappingInfo(*headless_surface, resolved_slice);
  if (!mapping.has_value()) {
    return std::unexpected(mapping.error());
  }

  headless_surface->SetReadbackSurfaceMapped(true);
  return ReadbackSurfaceMapping {
    .data = mapping->data,
    .layout = TextureReadbackLayout {
      .format = desc.format,
      .texture_type = desc.texture_type,
      .width = resolved_slice.width,
      .height = resolved_slice.height,
      .depth = resolved_slice.depth,
      .row_pitch = mapping->row_pitch,
      .slice_pitch = mapping->slice_pitch,
      .mip_level = resolved_slice.mip_level,
      .array_slice = resolved_slice.array_slice,
      .aspects = ClearFlags::kColor,
    },
  };
}

auto HeadlessReadbackManager::UnmapReadbackTextureSurface(
  oxygen::graphics::Texture& surface) -> void
{
  if (surface.GetTypeId() != Texture::ClassTypeId()) {
    LOG_F(ERROR,
      "UnmapReadbackTextureSurface rejected unexpected texture type `{}` ({})",
      surface.GetName(), surface.GetTypeId());
    return;
  }
  auto* headless_surface = static_cast<Texture*>(&surface);
  if (!headless_surface->IsReadbackSurface()) {
    LOG_F(ERROR,
      "UnmapReadbackTextureSurface rejected non-readback texture `{}`",
      surface.GetName());
    return;
  }
  if (!headless_surface->IsReadbackSurfaceMapped()) {
    DLOG_F(2, "UnmapReadbackTextureSurface ignored unmapped surface `{}`",
      surface.GetName());
    return;
  }

  headless_surface->SetReadbackSurfaceMapped(false);
}

auto HeadlessReadbackManager::OnFrameStart(const frame::Slot slot) -> void
{
  PumpCompletions();
  tracker_.OnFrameStart(slot);
}

auto HeadlessReadbackManager::Shutdown(const std::chrono::milliseconds timeout)
  -> std::expected<void, ReadbackError>
{
  LOG_SCOPE_FUNCTION(INFO);
  observer_ptr<graphics::CommandQueue> queue;
  graphics::FenceValue last_fence { 0 };
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
      LOG_F(ERROR,
        "Headless readback shutdown wait failed at fence {} after {} ms",
        last_fence.get(), timeout.count());
      return std::unexpected(ReadbackError::kBackendFailure);
    }
    tracker_.MarkFenceCompleted(last_fence);
  }
  return {};
}

} // namespace oxygen::graphics::headless
