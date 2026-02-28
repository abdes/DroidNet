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
#include <Oxygen/Data/PakCatalog.h>
#include <Oxygen/Data/PatchManifest.h>
#include <Oxygen/Data/SourceKey.h>

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
  std::optional<uint16_t> source_id {};
};

struct VirtualPathCollision final {
  uint16_t winner_source_id { 0 };
  uint16_t masked_source_id { 0 };
  data::AssetKey winner_key {};
  data::AssetKey masked_key {};
};

struct VirtualPathResolutionResult final {
  std::optional<data::AssetKey> asset_key {};
  KeyResolutionResult key_result {};
  std::vector<VirtualPathCollision> collisions {};
};

struct KeyResolutionCallbacks final {
  std::function<bool(uint16_t source_id, const data::AssetKey& key)>
    source_has_asset {};
  std::function<bool(uint16_t source_id, const data::AssetKey& key)>
    source_tombstones_asset {};
};

struct VirtualPathResolutionCallbacks final {
  KeyResolutionCallbacks key_resolution {};
  std::function<std::optional<data::AssetKey>(
    uint16_t source_id, std::string_view virtual_path)>
    resolve_virtual_path {};
};

[[nodiscard]] auto ResolveAssetKeyByPrecedence(
  std::span<const uint16_t> source_ids, const data::AssetKey& key,
  const KeyResolutionCallbacks& callbacks) -> KeyResolutionResult;

[[nodiscard]] auto ResolveVirtualPathByPrecedence(
  std::span<const uint16_t> source_ids, std::string_view virtual_path,
  const VirtualPathResolutionCallbacks& callbacks)
  -> VirtualPathResolutionResult;

enum class PatchCompatibilityCode : uint8_t {
  kMissingBaseSourceKey = 0,
  kUnexpectedBaseSourceKey,
  kMissingBaseContentVersion,
  kUnexpectedBaseContentVersion,
  kMissingBaseCatalogDigest,
  kUnexpectedBaseCatalogDigest,
};

[[nodiscard]] auto to_string(PatchCompatibilityCode code) noexcept
  -> std::string_view;

struct PatchCompatibilityDiagnostic final {
  PatchCompatibilityCode code { PatchCompatibilityCode::kMissingBaseSourceKey };
  std::string message;
};

struct PatchCompatibilityResult final {
  bool compatible { true };
  std::vector<PatchCompatibilityDiagnostic> diagnostics {};
};

[[nodiscard]] auto ValidatePatchCompatibility(
  std::span<const data::SourceKey> mounted_base_source_keys,
  std::span<const data::PakCatalog> mounted_base_catalogs,
  const data::PatchManifest& manifest) -> PatchCompatibilityResult;

} // namespace oxygen::content::internal
