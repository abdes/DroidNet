//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Renderer/Upload/StagingProvider.h>

namespace oxygen::engine::upload {

StagingProvider::~StagingProvider()
{
  const auto& ps = GetStats();
  LOG_SCOPE_F(INFO, "Staging Provider");
  LOG_F(INFO, "total allocations : {}", ps.total_allocations);
  LOG_F(INFO, "allocations/frame : {}", ps.allocations_this_frame);
  LOG_F(INFO, "avg alloc size    : {} bytes", ps.avg_allocation_size);
  LOG_F(INFO, "buffer grown      : {} times", ps.buffer_growth_count);
  LOG_F(INFO, "buffer size       : {} bytes", ps.current_buffer_size);
  LOG_F(INFO, "map/unmap calls   : {}/{}", ps.map_calls, ps.unmap_calls);
  if (!ps.implementation_info.empty()) {
    LOG_F(INFO, "{}", ps.implementation_info);
  }
}

} // namespace oxygen::engine::upload
