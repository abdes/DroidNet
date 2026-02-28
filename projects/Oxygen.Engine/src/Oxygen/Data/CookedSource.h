//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <filesystem>
#include <string_view>

#include <Oxygen/Data/api_export.h>

namespace oxygen::data {

enum class CookedSourceKind : uint8_t {
  kLooseCooked,
  kPak,
};

OXGN_DATA_NDAPI auto to_string(CookedSourceKind value) noexcept
  -> std::string_view;

struct CookedSource final {
  CookedSourceKind kind = CookedSourceKind::kLooseCooked;
  std::filesystem::path path;
};

} // namespace oxygen::data
