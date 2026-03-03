//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace oxygen::physics::test {

[[nodiscard]] auto MakeSoftBodySettingsBlob(uint32_t cluster_count = 4U)
  -> std::vector<uint8_t>;

[[nodiscard]] auto MakeVehicleConstraintSettingsBlob(size_t wheel_count)
  -> std::vector<uint8_t>;

} // namespace oxygen::physics::test
