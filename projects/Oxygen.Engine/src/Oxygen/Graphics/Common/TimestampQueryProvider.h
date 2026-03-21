//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <span>

#include <Oxygen/Graphics/Common/api_export.h>

namespace oxygen::graphics {

class CommandRecorder;

class TimestampQueryProvider {
public:
  OXGN_GFX_API virtual ~TimestampQueryProvider();

  virtual auto EnsureCapacity(uint32_t required_query_count) -> bool = 0;

  [[nodiscard]] virtual auto GetCapacity() const noexcept -> uint32_t = 0;

  virtual auto WriteTimestamp(
    CommandRecorder& recorder, uint32_t query_slot) -> bool
    = 0;

  virtual auto RecordResolve(
    CommandRecorder& recorder, uint32_t used_query_slots) -> bool
    = 0;

  [[nodiscard]] virtual auto GetResolvedTicks() const
    -> std::span<const uint64_t>
    = 0;
};

} // namespace oxygen::graphics
