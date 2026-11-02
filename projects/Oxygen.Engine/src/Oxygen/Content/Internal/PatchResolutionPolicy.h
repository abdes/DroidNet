//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <Oxygen/Core/Types/AssetKey.h>
#include <Oxygen/Data/SourceOrigin.h>

namespace oxygen::content::internal {

enum class KeyResolutionStatus : uint8_t {
  kNotFound = 0,
  kFound,
  kTombstoned,
};

[[nodiscard]] auto to_string(KeyResolutionStatus status) noexcept
  -> std::string_view;

struct KeyResolutionResult final {
  KeyResolutionStatus status { KeyResolutionStatus::kNotFound };
  std::optional<data::SourceInstanceId> source_id;
};

struct VirtualPathCollision final {
  data::SourceInstanceId winner_source_id;
  data::SourceInstanceId masked_source_id;
  data::AssetKey winner_key;
  data::AssetKey masked_key;
};

struct VirtualPathResolutionResult final {
  std::optional<data::AssetKey> asset_key;
  KeyResolutionResult key_result {};
  std::vector<VirtualPathCollision> collisions;
};

struct KeyResolutionCallbacks final {
  std::function<bool(
    data::SourceInstanceId source_id, const data::AssetKey& key)>
    source_has_asset;
  std::function<bool(
    data::SourceInstanceId source_id, const data::AssetKey& key)>
    source_tombstones_asset;
};

struct VirtualPathResolutionCallbacks final {
  KeyResolutionCallbacks key_resolution {};
  std::function<std::optional<data::AssetKey>(
    data::SourceInstanceId source_id, std::string_view virtual_path)>
    resolve_virtual_path;
};

//! The layer that decides a key under the precedence rule.
template <typename Layer> struct PrecedenceMatch final {
  KeyResolutionStatus status { KeyResolutionStatus::kNotFound };
  //! The layer that holds or deletes the key; null when none does.
  const Layer* layer { nullptr };
};

//! The precedence rule of every key lookup: `layers` run lowest precedence
//! first and are searched from the newest. The first layer that `deletes` the
//! key ends the search as tombstoned, the first that `holds` it as found.
template <typename Layer, typename Deletes, typename Holds>
[[nodiscard]] auto ResolveByPrecedence(std::span<const Layer> layers,
  const Deletes& deletes, const Holds& holds) -> PrecedenceMatch<Layer>
{
  for (const auto& layer : std::views::reverse(layers)) {
    if (deletes(layer)) {
      return { .status = KeyResolutionStatus::kTombstoned, .layer = &layer };
    }
    if (holds(layer)) {
      return { .status = KeyResolutionStatus::kFound, .layer = &layer };
    }
  }
  return {};
}

[[nodiscard]] auto ResolveAssetKeyByPrecedence(
  std::span<const data::SourceInstanceId> source_ids, const data::AssetKey& key,
  const KeyResolutionCallbacks& callbacks) -> KeyResolutionResult;

[[nodiscard]] auto ResolveVirtualPathByPrecedence(
  std::span<const data::SourceInstanceId> source_ids,
  std::string_view virtual_path,
  const VirtualPathResolutionCallbacks& callbacks)
  -> VirtualPathResolutionResult;

} // namespace oxygen::content::internal
