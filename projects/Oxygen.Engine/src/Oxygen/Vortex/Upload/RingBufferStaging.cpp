//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <cstdint>
#include <expected>
#include <memory>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Core/Types/Frame.h>
#include <Oxygen/Graphics/Common/Buffer.h>
#include <Oxygen/Graphics/Common/Detail/DeferredReclaimer.h>
#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/Graphics/Common/ResourceRegistry.h>
#include <Oxygen/Vortex/Upload/Errors.h>
#include <Oxygen/Vortex/Upload/RingBufferStaging.h>
#include <Oxygen/Vortex/Upload/Types.h>
#include <Oxygen/Vortex/Upload/UploaderTag.h>

using oxygen::graphics::BufferDesc;
using oxygen::graphics::BufferMemory;
using oxygen::graphics::BufferUsage;
using oxygen::vortex::upload::FenceValue;

namespace {

constexpr auto AlignUp(std::uint64_t v, std::uint64_t a) -> std::uint64_t
{
  return (v + (a - 1)) & ~(a - 1);
}

constexpr std::uint64_t kInitialBytesPerPartition = 10ULL * 1024ULL * 1024ULL;

constexpr std::uint32_t kIdleFramesBeforeShrink = 120U;

template <typename Resource>
auto RegisterResourceIfNeeded(const oxygen::observer_ptr<oxygen::Graphics> gfx,
  const std::shared_ptr<Resource>& resource) -> void
{
  if (gfx == nullptr || resource == nullptr) {
    return;
  }

  auto& registry = gfx->GetResourceRegistry();
  if (!registry.Contains(*resource)) {
    registry.Register(resource);
  }
}

template <typename Resource>
auto UnregisterResourceIfPresent(
  const oxygen::observer_ptr<oxygen::Graphics> gfx,
  const std::shared_ptr<Resource>& resource) -> void
{
  if (gfx == nullptr || resource == nullptr) {
    return;
  }

  auto& registry = gfx->GetResourceRegistry();
  if (registry.Contains(*resource)) {
    registry.UnRegisterResource(*resource);
  }
}

} // namespace

namespace oxygen::vortex::upload {

namespace {

  auto DeferUnregisterAndReleaseBuffer(const observer_ptr<Graphics> gfx,
    std::shared_ptr<graphics::Buffer>& buffer) -> void
  {
    if (gfx == nullptr || buffer == nullptr) {
      buffer.reset();
      return;
    }

    auto old_buffer = std::move(buffer);
    // The buffer object must stay alive until deferred reclamation runs, but
    // the registry entry does not. Unregister now while Graphics composition
    // state is unquestionably alive, then defer only the final shared_ptr drop.
    UnregisterResourceIfPresent(gfx, old_buffer);
    auto& reclaimer = gfx->GetDeferredReclaimer();
    reclaimer.RegisterDeferredAction(
      [old_buffer = std::move(old_buffer)]() mutable -> void {
        old_buffer.reset();
      });
  }

} // namespace

auto RingBufferStaging::Allocate(SizeBytes size, std::string_view debug_name)
  -> std::expected<Allocation, UploadError>
{
  const auto bytes = size.get();
  if (bytes == 0) {
    return std::unexpected(UploadError::kInvalidRequest);
  }

  const auto aligned = AlignUp(bytes, alignment_);
  auto ensure = EnsureCapacity(aligned, debug_name);
  if (!ensure) {
    return std::unexpected(ensure.error());
  }
  if (!buffer_ || (mapped_ptr_ == nullptr)) {
    return std::unexpected(UploadError::kStagingAllocFailed);
  }

  auto& head = heads_[active_partition_];
  const auto partition_base
    = static_cast<std::uint64_t>(active_partition_) * capacity_per_partition_;
  const auto offset = partition_base + head;
  head += aligned;

  Allocation out(
    buffer_, OffsetBytes { offset }, SizeBytes { bytes }, mapped_ptr_ + offset);

  // Record that this partition observed the current retire counter at the
  // time of allocation. If we later reuse this partition without retire_count_
  // increasing, SetActivePartition will log a warning.
  if (partition_last_seen_retire_count_.size() < heads_.size()) {
    partition_last_seen_retire_count_.assign(heads_.size(), 0ULL);
  }
  partition_last_seen_retire_count_[active_partition_] = retire_count_;

  // Update telemetry
  Stats().total_allocations++;
  Stats().total_bytes_allocated += bytes;
  Stats().allocations_this_frame++;

  // Update moving average (simple exponential moving average with alpha=0.1)
  constexpr double alpha = 0.1;
  if (Stats().avg_allocation_size == 0) {
    Stats().avg_allocation_size = static_cast<std::uint32_t>(bytes);
  } else {
    const auto new_avg = (alpha * static_cast<double>(bytes))
      + ((1.0 - alpha) * static_cast<double>(Stats().avg_allocation_size));
    Stats().avg_allocation_size = static_cast<std::uint32_t>(new_avg);
  }

  return out;
}

auto RingBufferStaging::RetireCompleted(
  UploaderTag /*tag*/, FenceValue completed) -> void
{
  // Only bump when the completed fence actually advances, to avoid false
  // positives in the partition reuse warning.
  if (completed > last_completed_fence_) {
    last_completed_fence_ = completed;
    ++retire_count_;
  }
}

// Notify of frame slot change without RTTI
auto RingBufferStaging::OnFrameStart(UploaderTag /*tag*/, frame::Slot slot)
  -> void
{
  OnFrameStartInternal(slot);
}

auto RingBufferStaging::OnFrameStartInternal(const frame::Slot slot) -> void
{
  // Observe whether the previous frame used the staging buffer.
  if (Stats().allocations_this_frame == 0U) {
    ++consecutive_idle_frames_;
  } else {
    consecutive_idle_frames_ = 0U;
  }

  SetActivePartition(slot);
  Stats().active_partition = active_partition_;
  Stats().partitions_count = partitions_count_;
  Stats().allocations_this_frame = 0; // Reset frame counter

  MaybeShrinkAfterIdle("RingBufferStaging.IdleTrim");
}

auto RingBufferStaging::MaybeShrinkAfterIdle(const std::string_view debug_name)
  -> void
{
  // Only trim after sustained idleness, and only if the buffer is meaningfully
  // above the baseline. This avoids shrink/grow thrash.
  if (consecutive_idle_frames_ < kIdleFramesBeforeShrink) {
    return;
  }
  if (capacity_per_partition_ <= 2ULL * kInitialBytesPerPartition) {
    return;
  }
  if (TrimExcessCapacity(debug_name)) {
    consecutive_idle_frames_ = 0U;
  }
}

auto RingBufferStaging::RecreateBuffer(
  const std::uint64_t aligned_per_partition, const std::string_view debug_name)
  -> std::expected<void, UploadError>
{
  const auto total_capacity
    = aligned_per_partition * static_cast<std::uint64_t>(partitions_count_);

  BufferDesc desc;
  desc.size_bytes = total_capacity;
  desc.usage = BufferUsage::kNone;
  desc.memory = BufferMemory::kUpload;
  desc.debug_name = debug_name_;

  UnMap();
  DeferUnregisterAndReleaseBuffer(gfx_, buffer_);

  UploadError error_code { UploadError::kStagingAllocFailed };
  try {
    buffer_ = gfx_->CreateBuffer(desc);
    RegisterResourceIfNeeded(gfx_, buffer_);
    auto map_result = Map();
    if (map_result) {
      capacity_per_partition_ = aligned_per_partition;
      capacity_ = total_capacity;
      Stats().current_buffer_size = buffer_->GetSize();
      Stats().max_buffer_size
        = (std::max)(Stats().max_buffer_size, Stats().current_buffer_size);

      // Reset partition bookkeeping on buffer recreation.
      std::ranges::fill(heads_, 0ULL);
      partition_last_seen_retire_count_.assign(heads_.size(), retire_count_);
      LOG_F(INFO,
        "Recreated staging buffer '{}' (trigger='{}') total={} "
        "per_partition={} partitions={}",
        debug_name_, debug_name, capacity_, capacity_per_partition_,
        partitions_count_.get());
      return {};
    }
    error_code = map_result.error();
  } catch (const std::exception& ex) {
    LOG_F(ERROR,
      "RingBufferStaging buffer recreate failed '{}' (trigger='{}' total={}): "
      "{}",
      debug_name_, debug_name, total_capacity, ex.what());
    error_code = UploadError::kStagingAllocFailed;
  }

  buffer_ = nullptr;
  mapped_ptr_ = nullptr;
  return std::unexpected(error_code);
}

// Select active partition (frame slot) and reset its bump pointer.
inline auto RingBufferStaging::SetActivePartition(frame::Slot slot) noexcept
  -> void
{
  if (slot >= partitions_count_) {
    return;
  }
  active_partition_ = slot;

  // Optional guard: if we are cycling back to this partition and have not
  // observed any retirement since it was last used, log a warning. We still
  // overwrite as designed; this is a diagnostic only.
  static constexpr bool kWarnOnPartitionReuseWithoutRetire = true;
  if (kWarnOnPartitionReuseWithoutRetire) {
    const auto last_seen = partition_last_seen_retire_count_.empty()
      ? 0ULL
      : partition_last_seen_retire_count_[active_partition_];
    const bool had_prior_allocations = !heads_.empty()
      && heads_[active_partition_] > 0ULL && capacity_per_partition_ > 0ULL;
    if (had_prior_allocations && last_seen == retire_count_) {
      LOG_F(WARNING,
        "Reusing staging buffer '{}' partition {} without observed "
        "retirement; overwriting staging data. head={} cap_per_partition={} "
        "retire_count={} last_seen_retire_count={} last_completed_fence={}",
        debug_name_, active_partition_,
        heads_.empty() ? 0ULL : heads_[active_partition_],
        capacity_per_partition_, retire_count_, last_seen,
        last_completed_fence_.get());
    }
  }

  // When we cycle back to this partition, all GPU work for it has completed
  // so we can safely reclaim the space by resetting the head
  heads_[active_partition_] = 0ULL;
}

auto RingBufferStaging::EnsureCapacity(std::uint64_t required,
  std::string_view debug_name) -> std::expected<void, UploadError>
{
  const auto head = heads_.empty() ? 0ULL : heads_[active_partition_];
  if (capacity_per_partition_ >= head + required && buffer_) {
    Stats().current_buffer_size = buffer_->GetSize();
    Stats().max_buffer_size
      = (std::max)(Stats().max_buffer_size, Stats().current_buffer_size);
    return {};
  }

  const auto current = capacity_per_partition_;
  const auto baseline = current > 0 ? current : kInitialBytesPerPartition;
  const auto grow = current > 0
    ? static_cast<std::uint64_t>(current * (1.0 + static_cast<double>(slack_)))
    : baseline;
  // Grow to fit the current bump head plus the new allocation.
  // Using only `required` here can under-size the new partition capacity,
  // which may cause repeated growth and (worse) allow an allocation that
  // would overflow the active partition.
  const auto needed = head + required;
  const auto new_per_partition = (std::max)(needed, grow);
  const auto aligned_per_partition = AlignUp(new_per_partition, alignment_);
  const auto total_capacity
    = aligned_per_partition * static_cast<std::uint64_t>(partitions_count_);

  BufferDesc desc;
  desc.size_bytes = total_capacity;
  desc.usage = BufferUsage::kNone;
  desc.memory = BufferMemory::kUpload;
  desc.debug_name = debug_name_;

  // We can UnMap the buffer immediately, but it cannot be released now.
  // Release must be deferred until frames are no longer using it.
  UnMap();
  // Keep the previous buffer alive until it is safe, then unregister it from
  // the registry before dropping the final reference.
  DeferUnregisterAndReleaseBuffer(gfx_, buffer_);
  // Now, safe to re-assign
  UploadError error_code { UploadError::kStagingAllocFailed };
  try {
    buffer_ = gfx_->CreateBuffer(desc);
    RegisterResourceIfNeeded(gfx_, buffer_);
    Stats().buffer_growth_count++;
    auto map_result = Map(); // This may throw for now...
    if (map_result) {
      capacity_per_partition_ = aligned_per_partition;
      capacity_ = total_capacity;
      Stats().current_buffer_size = buffer_->GetSize();
      Stats().max_buffer_size
        = (std::max)(Stats().max_buffer_size, Stats().current_buffer_size);

      LOG_F(INFO,
        "Grew staging buffer '{}' (trigger='{}') total={} per_partition={} "
        "partitions={} head={} required={}",
        debug_name_, debug_name, capacity_, capacity_per_partition_,
        partitions_count_.get(), head, required);
      return {};
    }
    error_code = map_result.error();
  } catch (const std::exception& ex) {
    LOG_F(ERROR, "Allocation failed '{}' (trigger='{}' total={}): {}",
      debug_name_, debug_name, total_capacity, ex.what());
    error_code = UploadError::kStagingAllocFailed;
    // fall through to the cleanup code below
  }
  buffer_ = nullptr;
  mapped_ptr_ = nullptr;
  return std::unexpected(error_code);
}

RingBufferStaging::~RingBufferStaging()
{
  // Ensure implementation-specific stats (partition/capacity) are populated
  // before the base StagingProvider destructor logs telemetry.
  if (buffer_) {
    Stats().current_buffer_size = buffer_->GetSize();
    Stats().max_buffer_size
      = (std::max)(Stats().max_buffer_size, Stats().current_buffer_size);
  }
  Stats().active_partition = active_partition_;
  Stats().partitions_count = partitions_count_;
  FinalizeStats();

  if (buffer_ && buffer_->IsMapped()) {
    buffer_->UnMap();
    Stats().unmap_calls++;
  }
  if (buffer_) {
    gfx_->GetResourceRegistry().UnRegisterResource(*buffer_);
    buffer_.reset();
  }
  mapped_ptr_ = nullptr;
}

// Notify of frame slot change without RTTI
auto RingBufferStaging::OnFrameStart(
  InlineCoordinatorTag /*tag*/, frame::Slot slot) -> void
{
  OnFrameStartInternal(slot);
}

auto RingBufferStaging::TrimExcessCapacity(const std::string_view debug_name)
  -> bool
{
  if (capacity_per_partition_ <= kInitialBytesPerPartition) {
    return false;
  }

  const auto aligned_per_partition
    = AlignUp(kInitialBytesPerPartition, alignment_);
  const auto old_total_capacity = capacity_;
  const auto old_per_partition = capacity_per_partition_;

  const auto result = RecreateBuffer(aligned_per_partition, debug_name);
  if (!result) {
    const auto error_code
      = oxygen::vortex::upload::make_error_code(result.error());
    LOG_F(WARNING, "Explicit trim failed: {}", error_code.message());
    return false;
  }

  LOG_F(INFO,
    "Trimmed upload buffer '{}': total {} -> {} bytes, per-partition {} -> {} "
    "bytes",
    debug_name, old_total_capacity, capacity_, old_per_partition,
    capacity_per_partition_);
  return true;
}

auto RingBufferStaging::FinalizeStats() -> void
{
  // Add partition utilization info
  const auto partition_used = heads_.empty() ? 0ULL : heads_[active_partition_];
  Stats().implementation_info = "RingBuffer: Partition "
    + std::to_string(active_partition_) + "/"
    + std::to_string(partitions_count_.get()) + ", "
    + std::to_string(partition_used) + "/"
    + std::to_string(capacity_per_partition_) + " bytes used";
}

auto RingBufferStaging::Map() -> std::expected<void, UploadError>
{
  DCHECK_NOTNULL_F(buffer_);
  DCHECK_F(!buffer_->IsMapped());
  DCHECK_F(mapped_ptr_ == nullptr);

  mapped_ptr_ = static_cast<std::byte*>(buffer_->Map());
  if (mapped_ptr_ == nullptr) {
    return std::unexpected(UploadError::kStagingMapFailed);
  }
  Stats().map_calls++;
  return {};
}

auto RingBufferStaging::UnMap() noexcept -> void
{
  // This call is idempotent and may be made even if the buffer is not yet
  // created or not mapped.
  if (!buffer_ || !buffer_->IsMapped()) {
    return;
  }
  DCHECK_NOTNULL_F(mapped_ptr_);
  buffer_->UnMap();
  mapped_ptr_ = nullptr;
  Stats().unmap_calls++;
}

} // namespace oxygen::vortex::upload
