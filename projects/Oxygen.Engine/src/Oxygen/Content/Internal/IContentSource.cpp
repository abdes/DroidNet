//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cstdint>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>

#include <Oxygen/Content/Internal/IContentSource.h>
#include <Oxygen/Core/Types/AssetKey.h>
#include <Oxygen/Data/AssetReferences.h>
#include <Oxygen/Data/PakFormatSerioLoaders.h> // IWYU pragma: keep
#include <Oxygen/Data/PakFormat_core.h>
#include <Oxygen/Data/PakFormat_physics.h>

namespace oxygen::content::internal {

auto IContentSource::FindTextureIndexByKey(const data::AssetKey& key) const
  -> std::optional<data::pak::core::ResourceIndexT>
{
  std::call_once(texture_key_once_, [this] -> void {
    for (auto& path : TextureVirtualPaths()) {
      const auto texture_key = data::TextureKeyFromVirtualPath(path);
      texture_paths_by_key_.emplace(texture_key, std::move(path));
    }
  });
  const auto found = texture_paths_by_key_.find(key);
  if (found == texture_paths_by_key_.end()) {
    return std::nullopt;
  }
  return FindTextureIndexByVirtualPath(found->second);
}

auto IContentSource::FindPhysicsResource(const data::AssetKey& key) const
  -> std::optional<data::pak::core::ResourceIndexT>
{
  std::call_once(physics_index_once_, [this] -> void {
    const auto* table = GetPhysicsTable();
    if (!table) {
      return;
    }
    const auto reader = CreatePhysicsTableReader();
    if (!reader) {
      throw std::runtime_error("Cannot read physics resource table");
    }
    std::unordered_map<data::AssetKey, data::pak::core::ResourceIndexT> index;
    const auto packed = reader->ScopedAlignment(1U);
    for (uint32_t i = 0; i < table->Size().get(); ++i) {
      const auto position = data::pak::core::ResourceIndexT { i };
      const auto offset = table->GetResourceOffset(position);
      if (!offset || !reader->Seek(*offset)) {
        throw std::runtime_error("Invalid physics resource table offset");
      }
      const auto descriptor
        = reader->Read<data::pak::physics::PhysicsResourceDesc>();
      if (!descriptor) {
        throw std::runtime_error("Cannot decode physics resource descriptor");
      }
      if (i == 0U) {
        if (!descriptor->resource_asset_key.IsNil()
          || descriptor->size_bytes != 0U) {
          throw std::runtime_error(
            "Physics resource row zero must be the absent sentinel");
        }
        continue;
      }
      if (descriptor->resource_asset_key.IsNil()) {
        throw std::runtime_error("Physics resource has no stable key");
      }
      if (!index.emplace(descriptor->resource_asset_key, position).second) {
        throw std::runtime_error("Duplicate physics resource key in source");
      }
    }
    physics_index_ = std::move(index);
  });
  const auto found = physics_index_.find(key);
  return found == physics_index_.end() ? std::nullopt
                                       : std::optional(found->second);
}

} // namespace oxygen::content::internal
