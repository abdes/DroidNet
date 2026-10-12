//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cstddef>
#include <cstdint>
#include <expected>
#include <limits>
#include <memory>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Core/Types/ByteUnits.h>
#include <Oxygen/Core/Types/Frame.h>
#include <Oxygen/Graphics/Common/Buffer.h>
#include <Oxygen/Graphics/Common/CommandRecorder.h>
#include <Oxygen/Vortex/RendererTag.h>
#include <Oxygen/Vortex/Upload/Errors.h>
#include <Oxygen/Vortex/Upload/StagingProvider.h>

namespace oxygen::vortex::upload {
//! Construct a well-formed Allocation.
StagingProvider::Allocation::Allocation(std::shared_ptr<graphics::Buffer> buf,
  OffsetBytes offset, SizeBytes size, std::byte* ptr) noexcept
  : buffer(std::move(buf))
  , offset(offset)
  , size(size)
  , ptr(ptr)
{
  CHECK_NOTNULL_F(buffer);
  CHECK_NOTNULL_F(ptr);
  CHECK_F(size.get() > 0 && size.get() <= buffer->GetSize());
}

auto StagingProvider::Allocation::Subrange(
  const OffsetBytes sub_offset, const SizeBytes sub_size) && -> Allocation
{
  CHECK_LE_F(sub_offset.get(), size.get(),
    "Allocation subrange offset {} exceeds size {}", sub_offset.get(),
    size.get());
  CHECK_LE_F(sub_size.get(), size.get() - sub_offset.get(),
    "Allocation subrange [{}, +{}) exceeds size {}", sub_offset.get(),
    sub_size.get(), size.get());
  auto* const sub_ptr = std::span(ptr, static_cast<std::size_t>(size.get()))
                          .subspan(static_cast<std::size_t>(sub_offset.get()))
                          .data();
  return { std::move(buffer), OffsetBytes { offset.get() + sub_offset.get() },
    sub_size, sub_ptr };
}

auto StagingProvider::AllocateAligned(const SizeBytes size,
  const std::uint64_t alignment, const std::string_view debug_name)
  -> std::expected<Allocation, UploadError>
{
  if (alignment == 0U || size.get() == 0U
    || size.get() > std::numeric_limits<std::uint64_t>::max() - alignment) {
    return std::unexpected(UploadError::kInvalidRequest);
  }
  auto allocation
    = Allocate(SizeBytes { size.get() + (alignment - 1U) }, debug_name);
  if (!allocation) {
    return std::unexpected(allocation.error());
  }
  // Strides need not be powers of two, so align by remainder.
  const auto base = allocation->Offset().get();
  const auto remainder = base % alignment;
  const auto delta = remainder == 0U ? 0U : alignment - remainder;
  return std::move(*allocation).Subrange(OffsetBytes { delta }, size);
}

auto StagingProvider::AddFrameListener(std::weak_ptr<FrameListener> listener)
  -> void
{
  frame_listeners_.push_back(std::move(listener));
}

auto StagingProvider::NotifyFrameStart(vortex::RendererTag /*tag*/,
  const frame::SequenceNumber sequence, const frame::Slot slot) -> void
{
  std::erase_if(frame_listeners_, [&](const auto& weak) -> bool {
    const auto listener = weak.lock();
    if (!listener) {
      return true;
    }
    listener->OnStagingFrameStart(sequence, slot);
    return false;
  });
}

StagingProvider::~StagingProvider()
{
  const auto& ps = GetStats();
  LOG_SCOPE_F(INFO, "Staging Provider");
  LOG_F(INFO, "total allocations : {}", ps.total_allocations);
  LOG_F(INFO, "allocations/frame : {}", ps.allocations_this_frame);
  LOG_F(INFO, "avg alloc size    : {} bytes", ps.avg_allocation_size);
  LOG_F(INFO, "buffer grown      : {} times", ps.buffer_growth_count);
  LOG_F(INFO, "current size      : {} bytes", ps.current_buffer_size);
  LOG_F(INFO, "max size          : {} bytes", ps.max_buffer_size);
  LOG_F(INFO, "map/unmap calls   : {}/{}", ps.map_calls, ps.unmap_calls);
  LOG_F(INFO, "active partition  : {}/{}", ps.active_partition,
    ps.partitions_count);
  if (!ps.implementation_info.empty()) {
    LOG_F(INFO, "{}", ps.implementation_info);
  }
}

auto StagingProvider::Allocation::Attach(
  graphics::CommandRecorder& recorder) const -> void
{
  constexpr std::uint64_t kStagingUse = 0x55504C5354414745ULL;
  recorder.RetainOpaqueUse(buffer, kStagingUse);
}

} // namespace oxygen::vortex::upload
