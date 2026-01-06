//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Renderer/Upload/StagingProvider.h>

namespace oxygen::engine::upload {
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

} // namespace oxygen::engine::upload
