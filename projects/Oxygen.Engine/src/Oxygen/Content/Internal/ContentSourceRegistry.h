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
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <Oxygen/Content/Internal/IContentSource.h>
#include <Oxygen/Data/AssetKey.h>
#include <Oxygen/Data/SourceOrigin.h>

namespace oxygen::content::internal {

class ContentSourceRegistry final {
public:
  [[nodiscard]] static auto AllocateSourceId() -> data::SourceInstanceId;

  enum class MountAction : uint8_t {
    kMounted,
    kRefreshed,
  };

  struct MountResult final {
    struct SourceKeyConflict final {
      data::SourceKey source_key {};
      data::SourceInstanceId existing_source_id {};
      std::string existing_mount_identity;
    };

    MountAction action { MountAction::kMounted };
    data::SourceInstanceId source_id {};
    size_t source_index { 0 };
    std::optional<SourceKeyConflict> source_key_conflict;
  };

  auto MountPak(std::filesystem::path normalized_path,
    std::shared_ptr<IContentSource> source) -> MountResult;

  auto MountLoose(std::string_view normalized_debug_name,
    std::shared_ptr<IContentSource> source) -> MountResult;

  auto MountGeneration(std::shared_ptr<IContentSource> source,
    std::optional<data::SourceKey> replaces) -> MountResult;
  auto RetireGeneration(data::SourceKey source_key) -> bool;

  [[nodiscard]] auto AcquireSource(data::SourceInstanceId source_id) const
    -> std::shared_ptr<IContentSource>;
  [[nodiscard]] auto FindSourceIdByKey(data::SourceKey source_key) const
    -> std::optional<data::SourceInstanceId>;
  [[nodiscard]] auto GetSourceKey(data::SourceInstanceId source_id) const
    -> std::optional<data::SourceKey>;
  [[nodiscard]] auto IsKnownSource(data::SourceInstanceId source_id) const
    -> bool;
  [[nodiscard]] auto FindPakId(const std::filesystem::path& path) const
    -> std::optional<data::SourceInstanceId>;

  auto Clear() -> void;
  [[nodiscard]] auto PruneExpiredSources()
    -> std::unordered_set<data::SourceInstanceId>;

  auto SetSourceTombstones(data::SourceInstanceId source_id,
    std::span<const data::AssetKey> tombstones) -> void;
  auto ClearSourceTombstones(data::SourceInstanceId source_id) -> void;
  [[nodiscard]] auto IsSourceTombstoningAsset(
    data::SourceInstanceId source_id, const data::AssetKey& key) const -> bool;

  auto FindSourceIndexById(data::SourceInstanceId source_id) const
    -> std::optional<size_t>;
  auto AssertStructuralConsistency(std::string_view context) const -> void;

  [[nodiscard]] auto Sources() const
    -> const std::vector<std::shared_ptr<IContentSource>>&
  {
    return sources_;
  }
  [[nodiscard]] auto SourceIds() const
    -> const std::vector<data::SourceInstanceId>&
  {
    return source_ids_;
  }
  [[nodiscard]] auto SourceIdToIndex() const
    -> const std::unordered_map<data::SourceInstanceId, size_t>&
  {
    return source_id_to_index_;
  }
  [[nodiscard]] auto PakPaths() const
    -> const std::vector<std::filesystem::path>&
  {
    return pak_paths_;
  }

private:
  enum class SourceKind : uint8_t { kPak, kLoose, kGeneration };

  struct SourceRecord {
    data::SourceKey key {};
    std::string mount_identity {};
    std::weak_ptr<IContentSource> source {};
    SourceKind kind { SourceKind::kLoose };
    bool readable = true;
  };

  auto InstallSource(std::shared_ptr<IContentSource> source,
    data::SourceInstanceId source_id, std::optional<size_t> replaced_index,
    SourceKind kind) -> MountResult;
  [[nodiscard]] auto FindSourceKeyConflict(
    data::SourceKey source_key, std::string_view mount_identity) const
    -> std::optional<MountResult::SourceKeyConflict>;
  auto RemoveActiveSource(size_t index) -> void;

  std::vector<std::shared_ptr<IContentSource>> sources_;
  std::vector<data::SourceInstanceId> source_ids_;
  std::unordered_map<data::SourceInstanceId, size_t> source_id_to_index_;
  std::unordered_map<data::SourceInstanceId, std::unordered_set<data::AssetKey>>
    tombstones_by_source_id_;
  std::unordered_map<data::SourceInstanceId, SourceRecord> records_;
  std::unordered_map<data::SourceKey, std::vector<data::SourceInstanceId>>
    source_key_to_ids_;
  std::vector<std::filesystem::path> pak_paths_;
  std::vector<data::SourceInstanceId> pak_source_ids_;
};

} // namespace oxygen::content::internal
