//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstddef>
#include <source_location>
#include <span>
#include <string_view>

#include <Oxygen/Tracy/api_export.h>

namespace oxygen::tracy::cpu {

OXGN_TRACY_NDAPI auto BeginZone(std::span<std::byte> storage,
  std::source_location callsite, std::string_view name, uint32_t color_rgb24)
  -> bool;

OXGN_TRACY_API auto EndZone(std::span<const std::byte> storage) -> void;

} // namespace oxygen::tracy::cpu
