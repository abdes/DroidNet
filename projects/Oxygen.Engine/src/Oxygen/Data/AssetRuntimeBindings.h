//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <memory>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Composition/Object.h>
#include <Oxygen/Data/AssetKey.h>

namespace oxygen::content {
class ResourceKey;
}

namespace oxygen::data {

class Asset;

//! Immutable, resolved dependencies retained for the decoded asset's lifetime.
//! Content builds these bindings before publication; Data has no cache policy.
class AssetRuntimeBindings : public Object {
public:
  AssetRuntimeBindings() = default;
  ~AssetRuntimeBindings() override = default;
  OXYGEN_MAKE_NON_COPYABLE(AssetRuntimeBindings)
  OXYGEN_MAKE_NON_MOVABLE(AssetRuntimeBindings)

  [[nodiscard]] virtual auto FindAsset(const AssetKey& key) const noexcept
    -> std::shared_ptr<const Asset> = 0;
  [[nodiscard]] virtual auto FindResource(
    const content::ResourceKey& key) const noexcept
    -> std::shared_ptr<const Object> = 0;
};

} // namespace oxygen::data
