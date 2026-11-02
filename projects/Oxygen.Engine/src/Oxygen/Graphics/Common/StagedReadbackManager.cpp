//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <expected>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <fmt/format.h>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Core/Detail/FormatUtils.h>
#include <Oxygen/Core/Types/ByteUnits.h>
#include <Oxygen/Core/Types/Format.h>
#include <Oxygen/Core/Types/Frame.h>
#include <Oxygen/Core/Types/TextureType.h>
#include <Oxygen/Graphics/Common/BackendObject.h>
#include <Oxygen/Graphics/Common/Buffer.h>
#include <Oxygen/Graphics/Common/CommandQueue.h>
#include <Oxygen/Graphics/Common/CommandRecorder.h>
#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/Graphics/Common/ReadbackErrors.h>
#include <Oxygen/Graphics/Common/ReadbackTypes.h>
#include <Oxygen/Graphics/Common/ReadbackValidation.h>
#include <Oxygen/Graphics/Common/Registration.h>
#include <Oxygen/Graphics/Common/ResourceRegistry.h>
#include <Oxygen/Graphics/Common/StagedReadbackManager.h>
#include <Oxygen/Graphics/Common/SubmissionCallback.h>
#include <Oxygen/Graphics/Common/Texture.h>
#include <Oxygen/Graphics/Common/Types/ClearFlags.h>
#include <Oxygen/Graphics/Common/Types/QueueRole.h>
#include <Oxygen/Graphics/Common/Types/ResourceAccessMode.h>
#include <Oxygen/Graphics/Common/Types/ResourceStates.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/OxCo/Value.h>

namespace oxygen::graphics {

namespace {

  using detail::GetFormatInfo;

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
    const auto mip_width = MipExtent(desc.width, slice.mip_level);
    const auto mip_height = MipExtent(desc.height, slice.mip_level);
    const auto mip_depth = desc.texture_type == TextureType::kTexture3D
      ? MipExtent(desc.depth, slice.mip_level)
      : 1U;

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
    if (desc.format == Format::kUnknown || desc.is_typeless) {
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
    desc.use_clear_value = false;
    desc.initial_state = ResourceStates::kCopyDest;
    return desc;
  }

  //! The single-sampled texture a multisampled `source_desc` resolves into.
  auto MakeResolvedTextureDesc(const TextureDesc& source_desc) -> TextureDesc
  {
    auto desc = source_desc;
    desc.sample_count = 1;
    desc.sample_quality = 0;
    desc.texture_type = ResolveTextureTypeForReadback(source_desc);
    desc.is_shader_resource = false;
    desc.is_render_target = false;
    desc.is_uav = false;
    // Without the render target flag, an optimized clear value is invalid.
    desc.use_clear_value = false;
    desc.initial_state = ResourceStates::kResolveDest;
    desc.debug_name = source_desc.debug_name + "-resolve";
    return desc;
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

  auto ComputeMappedTextureByteCount(const TextureReadbackLayout& layout)
    -> SizeBytes
  {
    return SizeBytes { layout.slice_pitch.get()
      * static_cast<uint64_t>((std::max)(layout.depth, 1U)) };
  }

  //! The bytes of `mapped`, through its last slice.
  auto MappedTextureBytes(const MappedTextureReadback& mapped)
    -> std::span<const std::byte>
  {
    return { mapped.Data(),
      static_cast<std::size_t>(
        ComputeMappedTextureByteCount(mapped.Layout()).get()) };
  }

  auto CopyMappedTextureBytes(const MappedTextureReadback& mapped)
    -> OwnedTextureReadbackData
  {
    const auto bytes = MappedTextureBytes(mapped);

    OwnedTextureReadbackData result {};
    result.bytes.assign(bytes.begin(), bytes.end());
    result.layout = mapped.Layout();
    result.tightly_packed = false;
    return result;
  }

  auto TightPackMappedTextureBytes(const MappedTextureReadback& mapped)
    -> OwnedTextureReadbackData
  {
    const auto& layout = mapped.Layout();
    const auto tight = ComputeLinearTextureCopyFootprint(layout.format,
      {
        .width = layout.width,
        .height = layout.height,
        .depth = layout.depth,
      });

    OwnedTextureReadbackData result {};
    result.bytes.resize(static_cast<size_t>(tight.total_bytes.get()));
    result.layout = layout;
    result.layout.row_pitch = tight.row_pitch;
    result.layout.slice_pitch = tight.slice_pitch;
    result.tightly_packed = true;

    const auto src = MappedTextureBytes(mapped);
    const auto dst = std::span(result.bytes);
    const auto row_bytes = static_cast<size_t>(tight.row_pitch.get());
    for (uint32_t slice_index = 0; slice_index < tight.slice_count;
      ++slice_index) {
      for (uint32_t row_index = 0; row_index < tight.row_count; ++row_index) {
        const auto src_offset = (layout.slice_pitch.get() * slice_index)
          + (layout.row_pitch.get() * row_index);
        const auto dst_offset = (tight.slice_pitch.get() * slice_index)
          + (tight.row_pitch.get() * row_index);
        std::ranges::copy(src.subspan(src_offset, row_bytes),
          dst.subspan(dst_offset, row_bytes).begin());
      }
    }

    return result;
  }

  template <typename ResourceT>
  auto RetireReadbackResource(std::shared_ptr<ResourceT>& resource,
    RegistrationOwner& registration) noexcept -> void
  {
    registration = {};
    resource.reset();
  }

  //! Creates `T` for `manager` and hands it out under the backend owner.
  template <typename T, typename Interface>
  auto AdoptReadback(StagedReadbackManager& manager, oxygen::Graphics& graphics,
    const std::string_view debug_name) -> std::shared_ptr<Interface>
  {
    const auto admission = graphics.GetBackendLifetime()->AcquireOperation();
    auto owner = graphics.RetainBackendOwner();
    if (!owner) {
      throw std::logic_error("Readback requires a canonical backend owner");
    }
    auto readback = std::make_unique<T>(manager, debug_name);
    return AdoptBackendObject(
      static_cast<Interface*>(readback.release()),
      [](void* pointer) noexcept -> void {
        std::default_delete<T> {}(
          static_cast<T*>(static_cast<Interface*>(pointer)));
      },
      std::move(owner));
  }

} // namespace

//=== Readback Staging ===----------------------------------------------------//

//! The staging buffer, ticket and state of one readback, and the rules every
//! readback follows: it records one copy at a time into its staging buffer,
//! is ready once the copy's fence completes, and maps the staging buffer.
class ReadbackStaging final {
public:
  ReadbackStaging(StagedReadbackManager& manager, std::string_view debug_name)
    : manager_(&manager)
    , debug_name_(debug_name)
  {
  }

  [[nodiscard]] auto State() const noexcept -> ReadbackState { return state_; }
  [[nodiscard]] auto Ticket() const noexcept -> std::optional<ReadbackTicket>
  {
    return ticket_;
  }
  [[nodiscard]] auto Manager() const noexcept -> StagedReadbackManager&
  {
    return *manager_;
  }

  //! Refuses a new copy while one is pending or mapped, or off the manager's
  //! queue.
  auto BeginCopy(CommandRecorder& recorder) const
    -> std::expected<void, ReadbackError>
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
    return manager_->EnsureTrackedQueue(target_queue);
  }

  //! Makes the staging buffer hold `size`, retained and tracked by
  //! `recorder` as a copy destination.
  auto PrepareStaging(CommandRecorder& recorder, const SizeBytes size,
    const std::string_view suffix) -> std::expected<void, ReadbackError>
  {
    if (!EnsureStagingBuffer(size, suffix)) {
      return std::unexpected(ReadbackError::kBackendFailure);
    }
    if (!recorder.RetainRegistration(
          manager_->graphics_->GetResourceRegistry(), staging_registration_)) {
      return std::unexpected(ReadbackError::kBackendFailure);
    }
    CHECK_NOTNULL_F(staging_.get(),
      "Readback staging buffer must exist after successful allocation");
    return {};
  }

  [[nodiscard]] auto StagingBuffer() const -> Buffer& { return *staging_; }

  //! Records the queue signal of the copy just recorded and makes the copy of
  //! `size` bytes this readback's pending ticket; `on_cancel` runs when the
  //! manager cancels it.
  auto CommitCopy(CommandRecorder& recorder, const SizeBytes size,
    std::function<void()> on_cancel) -> ReadbackTicket
  {
    // The queue assigns the signal on submission: this recording may submit
    // after work recorded later.
    const auto signal = recorder.RecordAssignedQueueSignal();

    const auto ticket
      = manager_->tracker_.RegisterPendingSubmission(size, debug_name_);
    ticket_ = ticket;
    manager_->TrackCancellationHandler(ticket.id, std::move(on_cancel));
    recorder.OnSubmission([manager = manager_, ticket, signal](
                            const SubmissionOutcome outcome) -> void {
      if (outcome == SubmissionOutcome::kSubmitted) {
        manager->tracker_.MarkSubmitted(
          ticket.id, FenceValue { signal->Value() });
        return;
      }
      if (const auto cancelled = manager->Cancel(ticket);
        !cancelled.has_value()) {
        LOG_F(WARNING, "Readback ticket {} was not cancelled: {}",
          ticket.id.get(), cancelled.error());
      }
    });
    state_ = ReadbackState::kPending;
    last_error_.reset();
    return ticket;
  }

  auto OnManagerCancelled() -> void
  {
    if (state_ == ReadbackState::kPending) {
      last_error_ = ReadbackError::kCancelled;
      state_ = ReadbackState::kCancelled;
    }
  }

  auto IsReady() const -> std::expected<bool, ReadbackError>
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

    if (const auto refresh = Refresh(); !refresh.has_value()) {
      return std::unexpected(refresh.error());
    }
    return state_ == ReadbackState::kReady;
  }

  //! Maps the first `size` bytes of the staging buffer of a ready readback.
  auto Map(const SizeBytes size)
    -> std::expected<const std::byte*, ReadbackError>
  {
    if (const auto refresh = Refresh();
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
    if (staging_ == nullptr) {
      return std::unexpected(ReadbackError::kBackendFailure);
    }

    const auto* mapped
      = static_cast<const std::byte*>(staging_->Map(0, size.get()));
    CHECK_NOTNULL_F(mapped, "Readback staging buffer map returned null");
    state_ = ReadbackState::kMapped;
    return mapped;
  }

  //! Waits for a pending copy to complete.
  auto AwaitCompletion() -> std::expected<void, ReadbackError>
  {
    if (!ticket_.has_value()) {
      return std::unexpected(ReadbackError::kNotReady);
    }
    if (state_ != ReadbackState::kPending) {
      return {};
    }
    const auto result = manager_->Await(*ticket_);
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
    return {};
  }

  auto ReleaseMapping() -> void
  {
    if (state_ == ReadbackState::kMapped && staging_ != nullptr
      && staging_->IsMapped()) {
      staging_->UnMap();
      state_ = ReadbackState::kReady;
    }
  }

  auto Cancel() -> std::expected<bool, ReadbackError>
  {
    if (!ticket_.has_value()) {
      return false;
    }
    const auto cancelled = manager_->Cancel(*ticket_);
    if (!cancelled.has_value()) {
      return std::unexpected(cancelled.error());
    }
    if (*cancelled) {
      state_ = ReadbackState::kCancelled;
      last_error_ = ReadbackError::kCancelled;
    }
    return *cancelled;
  }

  //! Returns a completed, unmapped readback to Idle, keeping its staging.
  auto ResetForReuse() -> std::expected<void, ReadbackError>
  {
    if (const auto refresh = Refresh(); !refresh.has_value()) {
      return std::unexpected(refresh.error());
    }
    if (const auto ready
      = detail::ValidateBufferReadbackReuseState(state_, last_error_);
      !ready.has_value()) {
      return std::unexpected(ready.error());
    }
    if (!ticket_.has_value() || staging_ == nullptr) {
      return std::unexpected(ReadbackError::kBackendFailure);
    }
    ForgetTicket();
    return {};
  }

  //! Returns the readback to Idle and releases its staging.
  auto Reset() -> void
  {
    if (const auto refresh = Refresh(); !refresh.has_value()) {
      DLOG_F(2, "Readback `{}` resets after {}", debug_name_, refresh.error());
    }
    ReleaseMapping();
    ForgetTicket();
    RetireReadbackResource(staging_, staging_registration_);
  }

private:
  auto EnsureStagingBuffer(const SizeBytes size, const std::string_view suffix)
    -> bool
  {
    if (staging_ != nullptr && staging_->GetSize() >= size.get()) {
      return true;
    }

    ReleaseMapping();
    RetireReadbackResource(staging_, staging_registration_);

    auto& graphics = *manager_->graphics_;
    staging_ = graphics.CreateBuffer(BufferDesc {
      .size_bytes = size.get(),
      .usage = BufferUsage::kNone,
      .memory = BufferMemory::kReadBack,
      .debug_name = fmt::format("{}-{}", debug_name_, suffix),
    });
    if (staging_ == nullptr) {
      LOG_F(ERROR, "Failed to allocate readback buffer `{}` ({} bytes)",
        debug_name_, size.get());
      return false;
    }

    auto lease = graphics.GetResourceRegistry().RegisterManaged(staging_);
    if (!lease) {
      return false;
    }
    staging_registration_ = lease->AllocationOwner();
    return true;
  }

  //! Takes the outcome of a pending copy whose fence has completed.
  auto Refresh() const -> std::expected<void, ReadbackError>
  {
    if (state_ != ReadbackState::kPending || !ticket_.has_value()) {
      return {};
    }

    manager_->PumpCompletions();
    const auto result = manager_->TryGetResult(ticket_->id);
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

  auto ForgetTicket() -> void
  {
    if (ticket_.has_value()) {
      manager_->UntrackCancellationHandler(ticket_->id);
      manager_->ForgetTicket(ticket_->id);
    }
    ticket_.reset();
    last_error_.reset();
    state_ = ReadbackState::kIdle;
  }

  observer_ptr<StagedReadbackManager> manager_;
  std::string debug_name_;
  std::shared_ptr<Buffer> staging_;
  RegistrationOwner staging_registration_;
  mutable std::optional<ReadbackTicket> ticket_;
  mutable std::optional<ReadbackError> last_error_;
  mutable ReadbackState state_ { ReadbackState::kIdle };
};

//=== Buffer Readback ===-----------------------------------------------------//

class StagedBufferReadback final : public GpuBufferReadback {
public:
  StagedBufferReadback(
    StagedReadbackManager& manager, const std::string_view debug_name)
    : staging_(manager, debug_name)
  {
  }

  ~StagedBufferReadback() override { staging_.Reset(); }

  OXYGEN_MAKE_NON_COPYABLE(StagedBufferReadback)
  OXYGEN_MAKE_NON_MOVABLE(StagedBufferReadback)

  [[nodiscard]] auto GetState() const noexcept -> ReadbackState override
  {
    return staging_.State();
  }

  [[nodiscard]] auto Ticket() const noexcept
    -> std::optional<ReadbackTicket> override
  {
    return staging_.Ticket();
  }

  [[nodiscard]] auto IsReady() const
    -> std::expected<bool, ReadbackError> override
  {
    return staging_.IsReady();
  }

  auto TryMap() -> std::expected<MappedBufferReadback, ReadbackError> override
  {
    const auto mapped = staging_.Map(SizeBytes { resolved_range_.size_bytes });
    if (!mapped.has_value()) {
      return std::unexpected(mapped.error());
    }
    return MappedBufferReadback {
      MappingGuard(),
      std::span<const std::byte>(
        *mapped, static_cast<size_t>(resolved_range_.size_bytes)),
    };
  }

  auto MapNow() -> std::expected<MappedBufferReadback, ReadbackError> override
  {
    if (const auto completed = staging_.AwaitCompletion();
      !completed.has_value()) {
      return std::unexpected(completed.error());
    }
    return TryMap();
  }

  auto Cancel() -> std::expected<bool, ReadbackError> override
  {
    return staging_.Cancel();
  }

  auto Reset() -> void override
  {
    staging_.Reset();
    resolved_range_ = {};
  }

  auto ResetForReuse() -> std::expected<void, ReadbackError> override
  {
    if (const auto reset = staging_.ResetForReuse(); !reset.has_value()) {
      return reset;
    }
    resolved_range_ = {};
    return {};
  }

protected:
  auto DoEnqueueCopy(
    CommandRecorder& recorder, const Buffer& source, const BufferRange range)
    -> std::expected<ReadbackTicket, ReadbackError> override
  {
    if (const auto begun = staging_.BeginCopy(recorder); !begun.has_value()) {
      return std::unexpected(begun.error());
    }

    const auto resolved = range.Resolve(source.GetDescriptor());
    if (resolved.size_bytes == 0) {
      return std::unexpected(ReadbackError::kInvalidArgument);
    }
    if (const auto staged = staging_.PrepareStaging(
          recorder, SizeBytes { resolved.size_bytes }, "staging");
      !staged.has_value()) {
      return std::unexpected(staged.error());
    }
    if (!recorder.IsResourceTracked(source)) {
      return std::unexpected(ReadbackError::kInvalidArgument);
    }
    auto& staging = staging_.StagingBuffer();
    if (!recorder.IsResourceTracked(staging)) {
      recorder.BeginTrackingResourceState(staging, ResourceStates::kCopyDest);
    }

    recorder.RequireResourceState(source, ResourceStates::kCopySource);
    recorder.RequireResourceState(staging, ResourceStates::kCopyDest);
    recorder.FlushBarriers();
    recorder.CopyBuffer(staging, 0, source,
      static_cast<size_t>(resolved.offset_bytes),
      static_cast<size_t>(resolved.size_bytes));

    resolved_range_ = resolved;
    return staging_.CommitCopy(
      recorder, SizeBytes { resolved.size_bytes }, [weak = WeakSelf()] -> void {
        if (const auto locked = weak.lock()) {
          locked->staging_.OnManagerCancelled();
        }
      });
  }

private:
  auto WeakSelf() -> std::weak_ptr<StagedBufferReadback>
  {
    return std::static_pointer_cast<StagedBufferReadback>(shared_from_this());
  }

  //! Keeps this readback alive and mapped while the mapping is held.
  auto MappingGuard() -> std::shared_ptr<void>
  {
    return AdoptBackendObject(
      static_cast<void*>(this),
      [](void* pointer) noexcept -> void {
        static_cast<StagedBufferReadback*>(pointer)->staging_.ReleaseMapping();
      },
      shared_from_this());
  }

  ReadbackStaging staging_;
  BufferRange resolved_range_;
};

//=== Texture Readback ===----------------------------------------------------//

class StagedTextureReadback final : public GpuTextureReadback {
public:
  StagedTextureReadback(
    StagedReadbackManager& manager, const std::string_view debug_name)
    : staging_(manager, debug_name)
  {
  }

  ~StagedTextureReadback() override { Reset(); }

  OXYGEN_MAKE_NON_COPYABLE(StagedTextureReadback)
  OXYGEN_MAKE_NON_MOVABLE(StagedTextureReadback)

  [[nodiscard]] auto GetState() const noexcept -> ReadbackState override
  {
    return staging_.State();
  }

  [[nodiscard]] auto Ticket() const noexcept
    -> std::optional<ReadbackTicket> override
  {
    return staging_.Ticket();
  }

  [[nodiscard]] auto IsReady() const
    -> std::expected<bool, ReadbackError> override
  {
    return staging_.IsReady();
  }

  auto TryMap() -> std::expected<MappedTextureReadback, ReadbackError> override
  {
    const auto mapped = staging_.Map(mapped_size_);
    if (!mapped.has_value()) {
      return std::unexpected(mapped.error());
    }
    return MappedTextureReadback { MappingGuard(), *mapped, layout_ };
  }

  auto MapNow() -> std::expected<MappedTextureReadback, ReadbackError> override
  {
    if (const auto completed = staging_.AwaitCompletion();
      !completed.has_value()) {
      return std::unexpected(completed.error());
    }
    return TryMap();
  }

  auto Cancel() -> std::expected<bool, ReadbackError> override
  {
    return staging_.Cancel();
  }

  auto Reset() -> void override
  {
    staging_.Reset();
    RetireReadbackResource(resolve_.texture, resolve_.registration);
    resolve_.desc = {};
    layout_ = {};
    mapped_size_ = {};
  }

protected:
  auto DoEnqueueCopy(CommandRecorder& recorder, const Texture& source,
    const TextureReadbackRequest& request)
    -> std::expected<ReadbackTicket, ReadbackError> override
  {
    if (const auto begun = staging_.BeginCopy(recorder); !begun.has_value()) {
      return std::unexpected(begun.error());
    }

    const auto& source_desc = source.GetDescriptor();
    if (const auto validation
      = ValidateTextureReadbackRequest(source_desc, request);
      !validation.has_value()) {
      return std::unexpected(validation.error());
    }
    const auto resolved_slice = request.src_slice.Resolve(source_desc);
    ValidateResolvedSlice(source_desc, resolved_slice);

    const auto copy = staging_.Manager().PrepareTextureReadbackCopy(
      recorder, source, resolved_slice, resolve_);
    if (!copy.has_value()) {
      return std::unexpected(copy.error());
    }
    if (const auto staged
      = staging_.PrepareStaging(recorder, copy->bytes, "texture-staging");
      !staged.has_value()) {
      return std::unexpected(staged.error());
    }
    auto& staging = staging_.StagingBuffer();
    if (!recorder.IsResourceTracked(staging)) {
      recorder.BeginTrackingResourceState(staging, ResourceStates::kCopyDest);
    }

    recorder.RequireResourceState(*copy->texture, ResourceStates::kCopySource);
    recorder.RequireResourceState(staging, ResourceStates::kCopyDest);
    recorder.FlushBarriers();
    recorder.CopyTextureToBuffer(
      staging, *copy->texture, copy->recorded_region);

    layout_ = BuildTextureReadbackLayout(
      copy->desc, resolved_slice, copy->placed_region, request.aspects);
    mapped_size_ = copy->bytes;
    return staging_.CommitCopy(
      recorder, mapped_size_, [weak = WeakSelf()] -> void {
        if (const auto locked = weak.lock()) {
          locked->staging_.OnManagerCancelled();
        }
      });
  }

private:
  auto WeakSelf() -> std::weak_ptr<StagedTextureReadback>
  {
    return std::static_pointer_cast<StagedTextureReadback>(shared_from_this());
  }

  //! Keeps this readback alive and mapped while the mapping is held.
  auto MappingGuard() -> std::shared_ptr<void>
  {
    return AdoptBackendObject(
      static_cast<void*>(this),
      [](void* pointer) noexcept -> void {
        static_cast<StagedTextureReadback*>(pointer)->staging_.ReleaseMapping();
      },
      shared_from_this());
  }

  ReadbackStaging staging_;
  TextureReadbackResolveTarget resolve_;
  TextureReadbackLayout layout_ {};
  SizeBytes mapped_size_;
};

//=== Manager ===-------------------------------------------------------------//

StagedReadbackManager::StagedReadbackManager(oxygen::Graphics& graphics)
  : graphics_(&graphics)
{
}

StagedReadbackManager::~StagedReadbackManager() = default;

auto StagedReadbackManager::CreateBufferReadback(
  const std::string_view debug_name) -> std::shared_ptr<GpuBufferReadback>
{
  return AdoptReadback<StagedBufferReadback, GpuBufferReadback>(
    *this, *graphics_, debug_name);
}

auto StagedReadbackManager::CreateTextureReadback(
  const std::string_view debug_name) -> std::shared_ptr<GpuTextureReadback>
{
  return AdoptReadback<StagedTextureReadback, GpuTextureReadback>(
    *this, *graphics_, debug_name);
}

auto StagedReadbackManager::EnsureResolveTarget(
  TextureReadbackResolveTarget& target, const TextureDesc& source_desc)
  -> std::expected<void, ReadbackError>
{
  const auto expected_desc = MakeResolvedTextureDesc(source_desc);
  if (target.texture != nullptr
    && MatchesResolveTextureDesc(target.desc, expected_desc)) {
    return {};
  }

  RetireReadbackResource(target.texture, target.registration);

  target.texture = graphics_->CreateTexture(expected_desc);
  if (target.texture == nullptr) {
    LOG_F(ERROR, "Failed to allocate resolve texture `{}` for readback",
      expected_desc.debug_name);
    return std::unexpected(ReadbackError::kBackendFailure);
  }

  auto lease = graphics_->GetResourceRegistry().RegisterManaged(target.texture);
  if (!lease) {
    return std::unexpected(ReadbackError::kBackendFailure);
  }
  target.registration = lease->AllocationOwner();
  target.desc = expected_desc;
  return {};
}

auto StagedReadbackManager::EnsureTrackedQueue(
  const observer_ptr<CommandQueue> queue) -> std::expected<void, ReadbackError>
{
  if (queue == nullptr) {
    return std::unexpected(ReadbackError::kQueueUnavailable);
  }

  std::scoped_lock lock(mutex_);
  if (shutdown_) {
    return std::unexpected(ReadbackError::kShutdown);
  }
  if (tracked_queue_ == nullptr) {
    tracked_queue_ = queue;
    return {};
  }
  if (tracked_queue_ != queue) {
    return std::unexpected(ReadbackError::kQueueUnavailable);
  }
  return {};
}

auto StagedReadbackManager::PumpCompletions() -> void
{
  observer_ptr<CommandQueue> queue;
  {
    std::scoped_lock lock(mutex_);
    queue = tracked_queue_;
  }
  if (queue != nullptr) {
    tracker_.MarkFenceCompleted(FenceValue { queue->GetCompletedValue() });
  }
}

auto StagedReadbackManager::TryGetResult(const ReadbackTicketId id) const
  -> std::optional<ReadbackResult>
{
  return tracker_.TryGetResult(id);
}

auto StagedReadbackManager::TrackCancellationHandler(
  const ReadbackTicketId id, std::function<void()> handler) -> void
{
  std::scoped_lock lock(mutex_);
  cancellation_handlers_[id] = std::move(handler);
}

auto StagedReadbackManager::UntrackCancellationHandler(
  const ReadbackTicketId id) -> void
{
  std::scoped_lock lock(mutex_);
  cancellation_handlers_.erase(id);
}

auto StagedReadbackManager::ForgetTicket(const ReadbackTicketId id) -> void
{
  const auto forgotten = tracker_.Forget(id);
  if (!forgotten.has_value()
    && forgotten.error() != ReadbackError::kTicketNotFound) {
    LOG_F(WARNING, "Failed to forget readback ticket {}: {}", id.get(),
      make_error_code(forgotten.error()).message());
  }
}

auto StagedReadbackManager::TrackedQueueRole() const -> QueueRole
{
  std::scoped_lock lock(mutex_);
  return tracked_queue_ != nullptr ? tracked_queue_->GetQueueRole()
                                   : QueueRole::kGraphics;
}

auto StagedReadbackManager::Await(const ReadbackTicket ticket)
  -> std::expected<ReadbackResult, ReadbackError>
{
  PumpCompletions();

  if (const auto ready = tracker_.TryGetResult(ticket.id); ready.has_value()) {
    return *ready;
  }

  if (tracker_.IsSubmissionPending(ticket.id)) {
    LOG_F(ERROR,
      "Readback await would deadlock: its copy has not been submitted. Submit "
      "the command recorder before awaiting the ticket.");
    return std::unexpected(ReadbackError::kWouldDeadlock);
  }

  const auto tracked = tracker_.FindTicket(ticket.id);
  if (!tracked.has_value()) {
    return std::unexpected(ReadbackError::kTicketNotFound);
  }
  // The caller's copy predates submission; the tracker holds the fence.
  const auto fence = tracked->fence;

  observer_ptr<CommandQueue> queue;
  bool shutdown = false;
  {
    std::scoped_lock lock(mutex_);
    queue = tracked_queue_;
    shutdown = shutdown_;
  }

  if (shutdown) {
    LOG_F(WARNING,
      "Readback await rejected for ticket {} at fence {} because the readback "
      "manager is shutting down",
      ticket.id.get(), fence.get());
    return std::unexpected(ReadbackError::kShutdown);
  }
  if (queue == nullptr) {
    LOG_F(WARNING,
      "Readback await would deadlock for ticket {} at fence {} because no "
      "tracked queue is available",
      ticket.id.get(), fence.get());
    return std::unexpected(ReadbackError::kWouldDeadlock);
  }

  const auto current = FenceValue { queue->GetCurrentValue() };
  const auto completed = FenceValue { queue->GetCompletedValue() };
  if (current < fence) {
    LOG_F(WARNING,
      "Readback await would deadlock for ticket {} at fence {} on queue `{}` "
      "because the signal has not been submitted yet (queue current={} "
      "completed={}). Submit the command recorder before awaiting the ticket.",
      ticket.id.get(), fence.get(), queue->GetName(), current.get(),
      completed.get());
    return std::unexpected(ReadbackError::kWouldDeadlock);
  }

  if (completed < fence) {
    try {
      queue->Wait(fence.get());
    } catch (...) {
      LOG_F(ERROR, "Readback await failed for ticket {} at fence {}",
        ticket.id.get(), fence.get());
      return std::unexpected(ReadbackError::kBackendFailure);
    }
    tracker_.MarkFenceCompleted(fence);
  } else {
    // The queue may complete the fence after the initial PumpCompletions()
    // call but before we reach this branch. Mark that completion explicitly
    // so tracker_.Await() does not sleep forever on an already-completed
    // ticket.
    tracker_.MarkFenceCompleted(completed);
  }
  return tracker_.Await(ticket.id);
}

auto StagedReadbackManager::AwaitAsync(const ReadbackTicket ticket)
  -> co::Co<void>
{
  PumpCompletions();
  if (tracker_.IsSubmissionPending(ticket.id)) {
    throw std::logic_error(
      "Cannot await a readback whose recording has not submitted");
  }
  const auto tracked = tracker_.FindTicket(ticket.id);
  if (!tracked.has_value()) {
    throw std::logic_error("Readback ticket was released");
  }
  co_await Until(tracker_.CompletedFenceValue() >= tracked->fence);
  co_return;
}

auto StagedReadbackManager::Cancel(const ReadbackTicket ticket)
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
      std::scoped_lock lock(mutex_);
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

auto StagedReadbackManager::DoReadBufferNow(
  const Buffer& source, const BufferRange range)
  -> std::expected<std::vector<std::byte>, ReadbackError>
{
  DLOG_SCOPE_F(1, "ReadBufferNow `{}`", source.GetName());

  auto readback = CreateBufferReadback("ReadBufferNow");
  CHECK_NOTNULL_F(readback.get());

  const auto queue_role = TrackedQueueRole();
  DLOG_F(2, "ReadBufferNow queue role: {}", queue_role);

  {
    auto recorder = graphics_->AcquireCommandRecorder(
      graphics_->QueueKeyFor(queue_role), "ReadBufferNow");
    CHECK_F(static_cast<bool>(recorder));
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

auto StagedReadbackManager::DoReadTextureNow(const Texture& source,
  const TextureReadbackRequest& request, const bool tightly_pack)
  -> std::expected<OwnedTextureReadbackData, ReadbackError>
{
  DLOG_SCOPE_F(1, "ReadTextureNow `{}`", source.GetName());

  auto readback = CreateTextureReadback("ReadTextureNow");
  CHECK_NOTNULL_F(readback.get());

  const auto queue_role = TrackedQueueRole();
  DLOG_F(2, "ReadTextureNow queue role: {}, tightly_pack={}", queue_role,
    tightly_pack);

  {
    auto recorder = graphics_->AcquireCommandRecorder(
      graphics_->QueueKeyFor(queue_role), "ReadTextureNow");
    CHECK_F(static_cast<bool>(recorder));
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

auto StagedReadbackManager::CreateReadbackTextureSurface(
  const TextureDesc& desc)
  -> std::expected<std::shared_ptr<Texture>, ReadbackError>
{
  DLOG_SCOPE_F(1,
    fmt::format("CreateReadbackTextureSurface `{}`", desc.debug_name).c_str());

  const auto readback_desc = BuildReadbackSurfaceTextureDesc(desc);
  if (!readback_desc.has_value()) {
    LOG_F(ERROR, "CreateReadbackTextureSurface rejected `{}`: {}",
      desc.debug_name.c_str(), readback_desc.error());
    return std::unexpected(readback_desc.error());
  }

  auto surface = graphics_->CreateTexture(*readback_desc);
  if (surface == nullptr) {
    LOG_F(ERROR, "CreateReadbackTextureSurface failed for `{}`",
      desc.debug_name.c_str());
    return std::unexpected(ReadbackError::kBackendFailure);
  }

  CHECK_F(surface->IsReadbackSurface(),
    "Readback surface creation must return a readback surface");
  return surface;
}

auto StagedReadbackManager::MapReadbackTextureSurface(
  Texture& surface, const TextureSlice slice)
  -> std::expected<ReadbackSurfaceMapping, ReadbackError>
{
  DLOG_SCOPE_F(1,
    fmt::format("MapReadbackTextureSurface `{}`", surface.GetName()).c_str());

  if (!surface.IsReadbackSurface()) {
    LOG_F(ERROR, "MapReadbackTextureSurface rejected non-readback texture `{}`",
      surface.GetName());
    return std::unexpected(ReadbackError::kUnsupportedResource);
  }
  if (surface.IsReadbackSurfaceMapped()) {
    return std::unexpected(ReadbackError::kAlreadyMapped);
  }

  const auto& desc = surface.GetDescriptor();
  const auto& format_info = GetFormatInfo(desc.format);
  if (format_info.has_depth || format_info.has_stencil) {
    return std::unexpected(ReadbackError::kUnsupportedResource);
  }

  const auto resolved_slice = slice.Resolve(desc);
  ValidateResolvedSlice(desc, resolved_slice);
  const auto bytes = MapReadbackSurfaceBytes(surface, resolved_slice);
  if (!bytes.has_value()) {
    return std::unexpected(bytes.error());
  }

  surface.readback_surface_mapped_ = true;
  return ReadbackSurfaceMapping {
    .data = bytes->data,
    .layout = TextureReadbackLayout {
      .format = desc.format,
      .texture_type = desc.texture_type,
      .width = resolved_slice.width,
      .height = resolved_slice.height,
      .depth = resolved_slice.depth,
      .row_pitch = bytes->row_pitch,
      .slice_pitch = bytes->slice_pitch,
      .mip_level = resolved_slice.mip_level,
      .array_slice = resolved_slice.array_slice,
      .aspects = ClearFlags::kColor,
    },
  };
}

auto StagedReadbackManager::UnmapReadbackTextureSurface(Texture& surface)
  -> void
{
  if (!surface.IsReadbackSurface()) {
    LOG_F(ERROR,
      "UnmapReadbackTextureSurface rejected non-readback texture `{}`",
      surface.GetName());
    return;
  }
  if (!surface.IsReadbackSurfaceMapped()) {
    DLOG_F(2, "UnmapReadbackTextureSurface ignored unmapped surface `{}`",
      surface.GetName());
    return;
  }

  UnmapReadbackSurfaceBytes(surface);
  surface.readback_surface_mapped_ = false;
}

auto StagedReadbackManager::OnFrameStart(const frame::Slot slot) -> void
{
  PumpCompletions();
  tracker_.OnFrameStart(slot);
}

auto StagedReadbackManager::Shutdown(const std::chrono::milliseconds timeout)
  -> std::expected<void, ReadbackError>
{
  LOG_SCOPE_FUNCTION(INFO);
  observer_ptr<CommandQueue> queue;
  {
    std::scoped_lock lock(mutex_);
    shutdown_ = true;
    queue = tracked_queue_;
  }

  // Resolve unissued copies without waiting on values that were never emitted.
  // Move callbacks out before invoking them: late owner release can re-enter
  // us.
  for (;;) {
    std::function<void()> cancel;
    ReadbackTicketId id { 0 };
    {
      std::scoped_lock lock(mutex_);
      const auto pending = std::ranges::find_if(
        cancellation_handlers_, [&](const auto& item) -> bool {
          return tracker_.IsSubmissionPending(item.first);
        });
      if (pending == cancellation_handlers_.end()) {
        break;
      }
      id = pending->first;
      cancel = std::move(pending->second);
      cancellation_handlers_.erase(pending);
    }
    if (const auto cancelled = tracker_.Cancel(id); !cancelled.has_value()) {
      LOG_F(WARNING, "Readback ticket {} was not cancelled at shutdown: {}",
        id.get(), cancelled.error());
    }
    if (cancel) {
      cancel();
    }
  }
  tracker_.CancelPendingSubmissions();
  const auto last_fence = tracker_.LastPendingSubmittedFence();

  if (queue != nullptr && last_fence.get() > 0 && tracker_.HasPending()) {
    try {
      queue->Wait(last_fence.get(), timeout);
    } catch (...) {
      LOG_F(ERROR, "Readback shutdown wait failed at fence {} after {} ms",
        last_fence.get(), timeout.count());
      return std::unexpected(ReadbackError::kBackendFailure);
    }
    tracker_.MarkFenceCompleted(FenceValue { queue->GetCompletedValue() });
  }
  return {};
}

} // namespace oxygen::graphics
