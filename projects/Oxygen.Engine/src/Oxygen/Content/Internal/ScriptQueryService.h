//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <functional>
#include <memory>
#include <optional>

#include <Oxygen/Content/Internal/IContentSource.h>
#include <Oxygen/Content/ResourceKey.h>
#include <Oxygen/Data/AssetKey.h>
#include <Oxygen/Data/PakFormat.h>
#include <Oxygen/Data/ScriptResource.h>
#include <Oxygen/Data/SourceOrigin.h>

namespace oxygen::content::internal {

class ScriptQueryService final {
public:
  struct Callbacks final {
    std::function<std::optional<data::SourceInstanceId>(const data::AssetKey&)>
      resolve_source_id_for_asset;
    std::function<std::shared_ptr<const IContentSource>(data::SourceInstanceId)>
      resolve_source_for_id;
    std::function<ResourceKey(
      data::SourceInstanceId, data::pak::core::ResourceIndexT)>
      make_script_resource_key;
  };

  [[nodiscard]] auto MakeScriptResourceKeyForAsset(
    const data::AssetKey& context_asset_key,
    data::pak::core::ResourceIndexT resource_index, const Callbacks& callbacks)
    -> std::optional<ResourceKey>;

  [[nodiscard]] auto ReadScriptResourceForAsset(
    const data::AssetKey& context_asset_key,
    data::pak::core::ResourceIndexT resource_index,
    const Callbacks& callbacks) const
    -> std::shared_ptr<const data::ScriptResource>;
};

} // namespace oxygen::content::internal
