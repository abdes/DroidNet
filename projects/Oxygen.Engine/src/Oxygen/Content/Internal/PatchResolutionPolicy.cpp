//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

#include <Oxygen/Content/Internal/PatchResolutionPolicy.h>
#include <Oxygen/Data/AssetKey.h>
#include <Oxygen/Data/SourceOrigin.h>

namespace oxygen::content::internal {

auto to_string(const KeyResolutionStatus status) noexcept -> std::string_view
{
  switch (status) {
  case KeyResolutionStatus::kNotFound:
    return "not_found";
  case KeyResolutionStatus::kFound:
    return "found";
  case KeyResolutionStatus::kTombstoned:
    return "tombstoned";
  }
  return "unknown";
}

auto ResolveAssetKeyByPrecedence(
  const std::span<const data::SourceInstanceId> source_ids,
  const data::AssetKey& key, const KeyResolutionCallbacks& callbacks)
  -> KeyResolutionResult
{
  if (!callbacks.source_has_asset || !callbacks.source_tombstones_asset) {
    return {};
  }

  for (const auto source_id : std::views::reverse(source_ids)) {
    if (callbacks.source_tombstones_asset(source_id, key)) {
      return {
        .status = KeyResolutionStatus::kTombstoned,
        .source_id = source_id,
      };
    }
    if (callbacks.source_has_asset(source_id, key)) {
      return {
        .status = KeyResolutionStatus::kFound,
        .source_id = source_id,
      };
    }
  }

  return {};
}

auto ResolveVirtualPathByPrecedence(
  const std::span<const data::SourceInstanceId> source_ids,
  const std::string_view virtual_path,
  const VirtualPathResolutionCallbacks& callbacks)
  -> VirtualPathResolutionResult
{
  if (!callbacks.resolve_virtual_path) {
    return {};
  }

  struct Winner {
    data::AssetKey key {};
    data::SourceInstanceId source {};
  };
  std::optional<Winner> winner;
  std::vector<VirtualPathCollision> collisions {};

  for (const auto source_id : std::views::reverse(source_ids)) {
    const auto candidate
      = callbacks.resolve_virtual_path(source_id, virtual_path);
    if (!candidate.has_value()) {
      continue;
    }
    if (!winner.has_value()) {
      winner = Winner { .key = *candidate, .source = source_id };
      continue;
    }
    if (*candidate != winner->key) {
      collisions.push_back({
        .winner_source_id = winner->source,
        .masked_source_id = source_id,
        .winner_key = winner->key,
        .masked_key = *candidate,
      });
    }
  }

  if (!winner.has_value()) {
    return {};
  }

  auto key_result = ResolveAssetKeyByPrecedence(
    source_ids, winner->key, callbacks.key_resolution);
  if (key_result.status != KeyResolutionStatus::kFound) {
    return {
      .asset_key = std::nullopt,
      .key_result = key_result,
      .collisions = std::move(collisions),
    };
  }

  return {
    .asset_key = winner->key,
    .key_result = key_result,
    .collisions = std::move(collisions),
  };
}

} // namespace oxygen::content::internal
