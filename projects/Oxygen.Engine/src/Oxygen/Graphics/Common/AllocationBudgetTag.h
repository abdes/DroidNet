//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <memory>

namespace oxygen::graphics {

class AllocationBudget;
enum class AllocationCategory : std::uint8_t { kGeneral, kCompactIndices };

//! Applies to newly allocated backing. External native imports have no charge;
//! aliases of charged resources retain the original resource owner instead.
struct AllocationBudgetTag {
  std::shared_ptr<AllocationBudget> owner;
  AllocationCategory category { AllocationCategory::kGeneral };
};

} // namespace oxygen::graphics
