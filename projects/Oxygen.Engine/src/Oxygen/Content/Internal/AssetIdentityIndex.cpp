//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Content/Internal/AssetIdentityIndex.h>

namespace oxygen::content::internal {

auto AssetIdentityIndex::Clear() -> void
{
  asset_key_by_hash_.clear();
  asset_source_id_by_hash_.clear();
  asset_hash_by_key_and_source_.clear();
}

auto AssetIdentityIndex::Index(const uint64_t hash_key,
  const data::AssetKey& key, const uint16_t source_id) -> void
{
  asset_key_by_hash_.insert_or_assign(hash_key, key);
  asset_source_id_by_hash_.insert_or_assign(hash_key, source_id);
  auto& by_source = asset_hash_by_key_and_source_[key];
  by_source.insert_or_assign(source_id, hash_key);
}

auto AssetIdentityIndex::Unindex(const uint64_t hash_key) -> void
{
  const auto key_it = asset_key_by_hash_.find(hash_key);
  if (key_it == asset_key_by_hash_.end()) {
    return;
  }
  const auto source_it = asset_source_id_by_hash_.find(hash_key);
  if (source_it == asset_source_id_by_hash_.end()) {
    asset_key_by_hash_.erase(key_it);
    return;
  }

  const auto key = key_it->second;
  const auto source_id = source_it->second;

  if (auto by_key_it = asset_hash_by_key_and_source_.find(key);
    by_key_it != asset_hash_by_key_and_source_.end()) {
    by_key_it->second.erase(source_id);
    if (by_key_it->second.empty()) {
      asset_hash_by_key_and_source_.erase(by_key_it);
    }
  }

  asset_key_by_hash_.erase(key_it);
  asset_source_id_by_hash_.erase(source_it);
}

auto AssetIdentityIndex::ResolveIndexed(const data::AssetKey& key,
  const std::optional<uint16_t> preferred_source_id,
  const std::unordered_map<uint16_t, size_t>& source_id_to_index) const
  -> std::optional<IndexedAssetIdentity>
{
  const auto by_key_it = asset_hash_by_key_and_source_.find(key);
  if (by_key_it == asset_hash_by_key_and_source_.end()) {
    return std::nullopt;
  }
  const auto& by_source = by_key_it->second;

  if (preferred_source_id.has_value()) {
    if (const auto source_it = by_source.find(*preferred_source_id);
      source_it != by_source.end()) {
      return IndexedAssetIdentity {
        .hash_key = source_it->second,
        .source_id = *preferred_source_id,
      };
    }
    return std::nullopt;
  }

  std::optional<IndexedAssetIdentity> best;
  size_t best_source_index = 0;
  for (const auto& [source_id, hash_key] : by_source) {
    const auto index_it = source_id_to_index.find(source_id);
    if (index_it == source_id_to_index.end()) {
      continue;
    }
    if (!best.has_value() || index_it->second >= best_source_index) {
      best = IndexedAssetIdentity {
        .hash_key = hash_key,
        .source_id = source_id,
      };
      best_source_index = index_it->second;
    }
  }
  return best;
}

auto AssetIdentityIndex::FindAssetKey(const uint64_t hash_key) const
  -> const data::AssetKey*
{
  if (const auto it = asset_key_by_hash_.find(hash_key);
    it != asset_key_by_hash_.end()) {
    return &it->second;
  }
  return nullptr;
}

auto AssetIdentityIndex::FindSourceId(const uint64_t hash_key) const
  -> std::optional<uint16_t>
{
  if (const auto it = asset_source_id_by_hash_.find(hash_key);
    it != asset_source_id_by_hash_.end()) {
    return it->second;
  }
  return std::nullopt;
}

auto AssetIdentityIndex::AssetKeyByHash() const
  -> const std::unordered_map<uint64_t, data::AssetKey>&
{
  return asset_key_by_hash_;
}

auto AssetIdentityIndex::AssetSourceIdByHash() const
  -> const std::unordered_map<uint64_t, uint16_t>&
{
  return asset_source_id_by_hash_;
}

auto AssetIdentityIndex::AssetHashByKeyAndSource() const -> const
  std::unordered_map<data::AssetKey, std::unordered_map<uint16_t, uint64_t>>&
{
  return asset_hash_by_key_and_source_;
}

auto AssetIdentityIndex::AssertConsistency(std::string_view context,
  const std::unordered_map<uint16_t, size_t>& source_id_to_index,
  const std::function<uint64_t(const data::AssetKey&, uint16_t)>&
    hash_asset_key_with_source) const -> void
{
#if !defined(NDEBUG)
  for (const auto& [asset_hash, source_id] : asset_source_id_by_hash_) {
    const auto source_it = source_id_to_index.find(source_id);
    if (source_it == source_id_to_index.end()) {
      LOG_F(ERROR,
        "[invariant:{}] asset hash maps to unknown source_id: hash=0x{:016x} "
        "source_id={}",
        context, asset_hash, source_id);
      continue;
    }

    const auto key_it = asset_key_by_hash_.find(asset_hash);
    if (key_it == asset_key_by_hash_.end()) {
      LOG_F(ERROR,
        "[invariant:{}] asset_source_id_by_hash missing matching asset key: "
        "hash=0x{:016x}",
        context, asset_hash);
      continue;
    }

    const auto expected_hash
      = hash_asset_key_with_source(key_it->second, source_id);
    if (expected_hash != asset_hash) {
      LOG_F(ERROR,
        "[invariant:{}] source-aware hash mismatch for asset key={}: "
        "stored=0x{:016x} expected=0x{:016x} source_id={}",
        context, data::to_string(key_it->second), asset_hash, expected_hash,
        source_id);
    }

    const auto reverse_key_it
      = asset_hash_by_key_and_source_.find(key_it->second);
    if (reverse_key_it == asset_hash_by_key_and_source_.end()) {
      LOG_F(ERROR, "[invariant:{}] reverse asset hash index missing key={}",
        context, data::to_string(key_it->second));
      continue;
    }
    const auto reverse_hash_it = reverse_key_it->second.find(source_id);
    if (reverse_hash_it == reverse_key_it->second.end()) {
      LOG_F(ERROR,
        "[invariant:{}] reverse asset hash index missing source mapping: "
        "key={} source_id={}",
        context, data::to_string(key_it->second), source_id);
      continue;
    }
    if (reverse_hash_it->second != asset_hash) {
      LOG_F(ERROR,
        "[invariant:{}] reverse asset hash index mismatch: key={} "
        "source_id={} forward=0x{:016x} reverse=0x{:016x}",
        context, data::to_string(key_it->second), source_id, asset_hash,
        reverse_hash_it->second);
    }
  }
#else
  static_cast<void>(context);
  static_cast<void>(source_id_to_index);
  static_cast<void>(hash_asset_key_with_source);
#endif
}

} // namespace oxygen::content::internal
