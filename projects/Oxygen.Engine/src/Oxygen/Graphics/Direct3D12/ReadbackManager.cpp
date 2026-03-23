//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <string>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Graphics/Common/CommandQueue.h>
#include <Oxygen/Graphics/Common/CommandRecorder.h>
#include <Oxygen/Graphics/Common/ResourceRegistry.h>
#include <Oxygen/Graphics/Common/Types/ResourceStates.h>
#include <Oxygen/Graphics/Direct3D12/Graphics.h>
#include <Oxygen/Graphics/Direct3D12/ReadbackManager.h>

using oxygen::SizeBytes;
using oxygen::graphics::Buffer;
using oxygen::graphics::BufferDesc;
using oxygen::graphics::BufferMemory;
using oxygen::graphics::BufferRange;
using oxygen::graphics::BufferUsage;
using oxygen::graphics::GpuBufferReadback;
using oxygen::graphics::MappedBufferReadback;
using oxygen::graphics::ReadbackError;
using oxygen::graphics::ReadbackResult;
using oxygen::graphics::ReadbackState;
using oxygen::graphics::ReadbackTicket;
using oxygen::graphics::ResourceStates;
using oxygen::graphics::d3d12::D3D12ReadbackManager;

namespace oxygen::graphics::d3d12 {

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

  readback_buffer_ = manager_.graphics_.CreateBuffer(BufferDesc {
    .size_bytes = size_bytes,
    .usage = BufferUsage::kNone,
    .memory = BufferMemory::kReadBack,
    .debug_name = debug_name_ + "-staging",
  });
  if (readback_buffer_ == nullptr) {
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
  manager_.TrackOwner(ticket_->id, weak_from_this());
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
  ReleaseMapping();
  if (ticket_.has_value()) {
    manager_.UntrackOwner(ticket_->id);
  }
  ticket_.reset();
  last_error_.reset();
  resolved_range_ = {};
  state_ = ReadbackState::kIdle;
}

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
  std::string_view /*debug_name*/) -> std::shared_ptr<GpuTextureReadback>
{
  return {};
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

auto D3D12ReadbackManager::TrackOwner(const ReadbackTicketId id,
  const std::weak_ptr<D3D12BufferReadback>& owner) -> void
{
  std::lock_guard lock(mutex_);
  buffer_owners_[id] = owner;
}

auto D3D12ReadbackManager::UntrackOwner(const ReadbackTicketId id) -> void
{
  std::lock_guard lock(mutex_);
  buffer_owners_.erase(id);
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
  const auto cancelled = tracker_.Cancel(ticket.id);
  if (!cancelled.has_value()) {
    return std::unexpected(cancelled.error());
  }

  if (*cancelled) {
    std::weak_ptr<D3D12BufferReadback> owner;
    {
      std::lock_guard lock(mutex_);
      if (const auto it = buffer_owners_.find(ticket.id);
        it != buffer_owners_.end()) {
        owner = it->second;
      }
    }
    if (const auto locked = owner.lock()) {
      locked->OnManagerCancelled();
    }
  }
  return *cancelled;
}

auto D3D12ReadbackManager::ReadBufferNow(const Buffer& /*source*/,
  BufferRange /*range*/) -> std::expected<std::vector<std::byte>, ReadbackError>
{
  return std::unexpected(ReadbackError::kUnsupportedResource);
}

auto D3D12ReadbackManager::ReadTextureNow(const Texture& /*source*/,
  TextureReadbackRequest /*request*/, const bool /*tightly_pack*/)
  -> std::expected<OwnedTextureReadbackData, ReadbackError>
{
  return std::unexpected(ReadbackError::kUnsupportedResource);
}

auto D3D12ReadbackManager::CreateReadbackTextureSurface(
  const TextureDesc& /*desc*/)
  -> std::expected<std::shared_ptr<Texture>, ReadbackError>
{
  return std::unexpected(ReadbackError::kUnsupportedResource);
}

auto D3D12ReadbackManager::MapReadbackTextureSurface(
  Texture& /*surface*/, TextureSlice /*slice*/)
  -> std::expected<ReadbackSurfaceMapping, ReadbackError>
{
  return std::unexpected(ReadbackError::kUnsupportedResource);
}

auto D3D12ReadbackManager::UnmapReadbackTextureSurface(Texture& /*surface*/)
  -> void
{
}

auto D3D12ReadbackManager::OnFrameStart(const frame::Slot slot) -> void
{
  PumpCompletions();
  tracker_.OnFrameStart(slot);
}

auto D3D12ReadbackManager::Shutdown(const std::chrono::milliseconds timeout)
  -> std::expected<void, ReadbackError>
{
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
      return std::unexpected(ReadbackError::kBackendFailure);
    }
    tracker_.MarkFenceCompleted(last_fence);
  }
  return {};
}

} // namespace oxygen::graphics::d3d12
