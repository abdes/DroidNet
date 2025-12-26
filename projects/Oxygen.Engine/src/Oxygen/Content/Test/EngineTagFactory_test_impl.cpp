//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/content/EngineTag.h>

#if defined(OXYGEN_ENGINE_TESTING)

namespace oxygen::content::internal {

auto EngineTagFactory::Get() noexcept -> EngineTag
{
  return EngineTag {};
}

} // namespace oxygen::content::internal

#endif  // OXYGEN_ENGINE_TESTING
