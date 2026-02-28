//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <Oxygen/Content/Constants.h>
#include <Oxygen/Content/Internal/IContentSource.h>
#include <Oxygen/Content/SourceToken.h>
#include <Oxygen/Data/AssetKey.h>

namespace oxygen::content::internal {

class ContentSourceRegistry final {
public:
  enum class MountAction : uint8_t {
    kMounted,
    kRefreshed,
  };

  struct MountResult final {
    MountAction action { MountAction::kMounted };
    uint16_t source_id { 0 };
    size_t source_index { 0 };
  };

  auto MountPak(std::filesystem::path normalized_path,
    std::unique_ptr<IContentSource> source) -> MountResult;

  auto MountLoose(std::string_view normalized_debug_name,
    std::unique_ptr<IContentSource> source) -> MountResult;

  auto Clear() -> void;

  auto SetSourceTombstones(
    uint16_t source_id, std::span<const data::AssetKey> tombstones) -> void;
  auto ClearSourceTombstones(uint16_t source_id) -> void;
  [[nodiscard]] auto IsSourceTombstoningAsset(
    uint16_t source_id, const data::AssetKey& key) const -> bool;

  auto FindSourceIdByToken(SourceToken token) const -> std::optional<uint16_t>;
  auto FindSourceIndexById(uint16_t source_id) const -> std::optional<size_t>;
  auto AssertStructuralConsistency(std::string_view context) const -> void;

  [[nodiscard]] auto Sources() const
    -> const std::vector<std::unique_ptr<IContentSource>>&
  {
    return sources_;
  }
  [[nodiscard]] auto SourceIds() const -> const std::vector<uint16_t>&
  {
    return source_ids_;
  }
  [[nodiscard]] auto SourceIdToIndex() const
    -> const std::unordered_map<uint16_t, size_t>&
  {
    return source_id_to_index_;
  }
  [[nodiscard]] auto SourceTokens() const -> const std::vector<SourceToken>&
  {
    return source_tokens_;
  }
  [[nodiscard]] auto PakPaths() const
    -> const std::vector<std::filesystem::path>&
  {
    return pak_paths_;
  }

private:
  std::vector<std::unique_ptr<IContentSource>> sources_ {};
  std::vector<uint16_t> source_ids_ {};
  std::unordered_map<uint16_t, size_t> source_id_to_index_ {};
  std::vector<SourceToken> source_tokens_ {};
  std::unordered_map<SourceToken, uint16_t> token_to_source_id_ {};
  std::unordered_map<uint16_t, std::unordered_set<data::AssetKey>>
    tombstones_by_source_id_ {};
  uint32_t next_source_token_value_ = 1;
  uint16_t next_loose_source_id_ = constants::kLooseCookedSourceIdBase;
  std::vector<std::filesystem::path> pak_paths_ {};
};

} // namespace oxygen::content::internal
