//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstddef>
#include <span>
#include <vector>

#include <Oxygen/Vortex/Internal/FrameViewPacket.h>
#include <Oxygen/Vortex/api_export.h>

namespace oxygen::vortex::internal {

class AuxiliaryDependencyGraph {
public:
  struct Plan {
    std::vector<std::size_t> ordered_packet_indices {};
    std::vector<std::vector<AuxiliaryResolvedInput>> resolved_inputs_by_packet {};
  };

  [[nodiscard]] OXGN_VRTX_API static auto Build(
    std::span<const FrameViewPacket> packets) -> Plan;
};

} // namespace oxygen::vortex::internal
