//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <string>
#include <string_view>

#include <Oxygen/Profiling/api_export.h>

namespace oxygen::profiling {
struct ProfileScopeDesc;
} // namespace oxygen::profiling

namespace oxygen::profiling::internal {

OXGN_PROF_NDAPI auto EscapeScopeVariableValue(std::string_view value)
  -> std::string;
OXGN_PROF_NDAPI auto FormatScopeNameImpl(const ProfileScopeDesc& desc)
  -> std::string;

} // namespace oxygen::profiling::internal
