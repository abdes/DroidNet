//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string_view>
#include <unordered_map>

#include <Oxygen/Data/AssetKey.h>

namespace oxygen::content::internal {

struct IndexedAssetIdentity final {
  uint64_t hash_key = 0;
  uint16_t source_id = 0;
};

class AssetIdentityIndex final {
public:
  auto Clear() -> void;

  auto Index(uint64_t hash_key, const data::AssetKey& key, uint16_t source_id)
    -> void;
  auto Unindex(uint64_t hash_key) -> void;

  auto ResolveIndexed(const data::AssetKey& key,
    std::optional<uint16_t> preferred_source_id,
    const std::unordered_map<uint16_t, size_t>& source_id_to_index) const
    -> std::optional<IndexedAssetIdentity>;

  [[nodiscard]] auto FindAssetKey(uint64_t hash_key) const
    -> const data::AssetKey*;
  [[nodiscard]] auto FindSourceId(uint64_t hash_key) const
    -> std::optional<uint16_t>;

  [[nodiscard]] auto AssetKeyByHash() const
    -> const std::unordered_map<uint64_t, data::AssetKey>&;
  [[nodiscard]] auto AssetSourceIdByHash() const
    -> const std::unordered_map<uint64_t, uint16_t>&;
  [[nodiscard]] auto AssetHashByKeyAndSource() const -> const
    std::unordered_map<data::AssetKey, std::unordered_map<uint16_t, uint64_t>>&;

  auto AssertConsistency(std::string_view context,
    const std::unordered_map<uint16_t, size_t>& source_id_to_index,
    const std::function<uint64_t(const data::AssetKey&, uint16_t)>&
      hash_asset_key_with_source) const -> void;

private:
  std::unordered_map<uint64_t, data::AssetKey> asset_key_by_hash_;
  std::unordered_map<uint64_t, uint16_t> asset_source_id_by_hash_;
  std::unordered_map<data::AssetKey, std::unordered_map<uint16_t, uint64_t>>
    asset_hash_by_key_and_source_;
};

} // namespace oxygen::content::internal
