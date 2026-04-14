//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Profiling/ProfileScope.h>

#include <Oxygen/Profiling/Internal/ScopeNameFormatter.h>

namespace oxygen::profiling {

auto FormatScopeName(const ProfileScopeDesc& desc) -> std::string
{
  return internal::FormatScopeNameImpl(desc);
}

} // namespace oxygen::profiling
