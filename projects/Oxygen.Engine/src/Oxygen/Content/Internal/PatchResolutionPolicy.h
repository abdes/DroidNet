//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <Oxygen/Data/AssetKey.h>
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
  std::optional<data::SourceInstanceId> source_id {};
};

struct VirtualPathCollision final {
  data::SourceInstanceId winner_source_id {};
  data::SourceInstanceId masked_source_id {};
  data::AssetKey winner_key {};
  data::AssetKey masked_key {};
};

struct VirtualPathResolutionResult final {
  std::optional<data::AssetKey> asset_key {};
  KeyResolutionResult key_result {};
  std::vector<VirtualPathCollision> collisions {};
};

struct KeyResolutionCallbacks final {
  std::function<bool(
    data::SourceInstanceId source_id, const data::AssetKey& key)>
    source_has_asset {};
  std::function<bool(
    data::SourceInstanceId source_id, const data::AssetKey& key)>
    source_tombstones_asset {};
};

struct VirtualPathResolutionCallbacks final {
  KeyResolutionCallbacks key_resolution {};
  std::function<std::optional<data::AssetKey>(
    data::SourceInstanceId source_id, std::string_view virtual_path)>
    resolve_virtual_path {};
};

[[nodiscard]] auto ResolveAssetKeyByPrecedence(
  std::span<const data::SourceInstanceId> source_ids, const data::AssetKey& key,
  const KeyResolutionCallbacks& callbacks) -> KeyResolutionResult;

[[nodiscard]] auto ResolveVirtualPathByPrecedence(
  std::span<const data::SourceInstanceId> source_ids,
  std::string_view virtual_path,
  const VirtualPathResolutionCallbacks& callbacks)
  -> VirtualPathResolutionResult;

} // namespace oxygen::content::internal
