//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <string>

#include <Oxygen/Content/ResourceKey.h>

namespace oxygen::content {

const ResourceKey ResourceKey::kPlaceholder { 0U };
const ResourceKey ResourceKey::kFallback { 0U };

auto to_string(const ResourceKey& key) -> std::string
{
  return "ResourceKey{" + std::to_string(key.get()) + "}";
}

} // namespace oxygen::content
