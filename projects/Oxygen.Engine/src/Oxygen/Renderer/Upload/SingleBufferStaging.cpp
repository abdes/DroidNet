//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Graphics/Common/Buffer.h>
#include <Oxygen/Graphics/Common/DeferredObjectRelease.h>
#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/Renderer/Upload/SingleBufferStaging.h>
#include <Oxygen/Renderer/Upload/Types.h>

namespace oxygen::engine::upload {

SingleBufferStaging::SingleBufferStaging(
  UploaderTag tag, observer_ptr<oxygen::Graphics> gfx, float slack)
  : StagingProvider(tag)
  , gfx_(gfx)
  , slack_(slack)
{
}

auto SingleBufferStaging::Allocate(SizeBytes size, std::string_view debug_name)
  -> std::expected<Allocation, UploadError>
{
  auto result = EnsureCapacity(size.get(), debug_name);
  if (!result) {
    return std::unexpected(result.error());
  }
  DCHECK_NOTNULL_F(buffer_);
  DCHECK_NOTNULL_F(mapped_ptr_);
  // Update telemetry
  UpdateAllocationStats(size);
  return Allocation(buffer_, OffsetBytes { 0 }, size, mapped_ptr_);
}

auto SingleBufferStaging::RetireCompleted(UploaderTag, FenceValue /*completed*/)
  -> void
{
  // Always using pinned mapping - nothing to do here
}

auto SingleBufferStaging::UpdateAllocationStats(SizeBytes size) noexcept -> void
{
  Stats().total_allocations++;
  Stats().total_bytes_allocated += size.get();
  Stats().allocations_this_frame++;

  // Update moving average (simple exponential moving average with alpha=0.1)
  DCHECK_F(size.get() < (std::numeric_limits<std::uint32_t>::max)());
  constexpr double alpha = 0.1;
  if (Stats().avg_allocation_size == 0) {
    Stats().avg_allocation_size = static_cast<std::uint32_t>(size.get());
  } else {
    const auto new_avg = alpha * static_cast<double>(size.get())
      + (1.0 - alpha) * static_cast<double>(Stats().avg_allocation_size);
    Stats().avg_allocation_size = static_cast<std::uint32_t>(new_avg);
  }
}

auto SingleBufferStaging::EnsureCapacity(
  uint64_t desired, std::string_view name) -> std::expected<void, UploadError>
{
  if (buffer_ && buffer_->GetSize() >= desired) {
    return {};
  }

  const uint64_t current = buffer_ ? buffer_->GetSize() : 0ull;
  const uint64_t grow
    = current > 0 ? static_cast<uint64_t>(current * (1.0 + slack_)) : desired;
  const uint64_t size_bytes = (std::max)(desired, grow);

  graphics::BufferDesc desc {
    .size_bytes = size_bytes,
    .usage = graphics::BufferUsage::kNone,
    .memory = graphics::BufferMemory::kUpload,
    .debug_name = std::string(name),
  };

  // We can UnMap the buffer immediately, but it cannot be released now.
  // Release must be deferred until frames are no longer using it.
  UnMap();
  // This will keep the buffer shared_ptr alive until it is time for it to be
  // destroyed.
  graphics::DeferredObjectRelease(buffer_, gfx_->GetDeferredReclaimer());
  UploadError error_code;
  try {
    buffer_ = gfx_->CreateBuffer(desc);
    Stats().buffer_growth_count++;
    Stats().current_buffer_size = buffer_->GetSize();
    auto map_result = Map(); // This may throw for now...
    if (map_result) {
      Stats().current_buffer_size = buffer_->GetSize();
      return {};
    }
    error_code = map_result.error();
  } catch (const std::exception& ex) {
    LOG_F(ERROR, "Staging buffer allocation failed ({}): {}", size_bytes,
      ex.what());
    error_code = UploadError::kStagingAllocFailed;
    // fall through to the cleanup code below
  }
  // discard any partially created resources
  buffer_ = nullptr;
  mapped_ptr_ = nullptr;
  return std::unexpected(error_code);
}

auto SingleBufferStaging::Map() -> std::expected<void, UploadError>
{
  DCHECK_NOTNULL_F(buffer_);
  DCHECK_F(!buffer_->IsMapped());
  DCHECK_F(mapped_ptr_ == nullptr);

  mapped_ptr_ = static_cast<std::byte*>(buffer_->Map());
  if (!mapped_ptr_) {
    return std::unexpected(UploadError::kStagingMapFailed);
  }
  Stats().map_calls++;
  return {};
}

auto SingleBufferStaging::UnMap() noexcept -> void
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

} // namespace oxygen::engine::upload
