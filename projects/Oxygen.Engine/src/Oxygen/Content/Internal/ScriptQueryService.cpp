//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <optional>

#include <Oxygen/Content/Internal/ScriptQueryService.h>
#include <Oxygen/Content/ResourceKey.h>
#include <Oxygen/Core/Types/AssetKey.h>
#include <Oxygen/Data/PakFormat_core.h>

namespace oxygen::content::internal {

auto ScriptQueryService::MakeScriptResourceKeyForAsset(
  const data::AssetKey& context_asset_key,
  const data::pak::core::ResourceIndexT resource_index,
  const Callbacks& callbacks) -> std::optional<ResourceKey>
{
  const auto source_id
    = callbacks.resolve_source_id_for_asset(context_asset_key);
  if (!source_id.has_value()) {
    return std::nullopt;
  }
  return callbacks.make_script_resource_key(*source_id, resource_index);
}

} // namespace oxygen::content::internal
