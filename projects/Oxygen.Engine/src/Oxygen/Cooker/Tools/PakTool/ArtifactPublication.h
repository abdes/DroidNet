//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <chrono>
#include <filesystem>
#include <optional>
#include <string>

namespace oxygen::content::pak::tool {

struct ArtifactPaths {
  std::filesystem::path final_path;
  std::filesystem::path staged_path;
  std::filesystem::path backup_path;
};

struct ArtifactPublicationPlan {
  ArtifactPaths pak;
  ArtifactPaths catalog;
  std::optional<ArtifactPaths> manifest;
  std::optional<ArtifactPaths> report;
};

struct ArtifactPublicationIntent {
  bool create_parent_directories = true;
  bool publish_pak = true;
  bool publish_catalog = true;
  bool publish_manifest = false;
  bool publish_report = false;
  bool suppress_stale_catalog_on_skip = false;
  bool suppress_stale_manifest_on_skip = false;
};

struct ArtifactPublicationState {
  std::filesystem::path final_path;
  std::filesystem::path staged_path;
  bool publish_requested = false;
  bool published = false;
  bool cleaned_staged = false;
  bool removed_stale_final = false;
  bool restored_backup = false;
};

struct ArtifactPublicationResult {
  bool success = false;
  std::string error_code;
  std::string error_message;
  std::optional<std::chrono::microseconds> publish_duration;

  ArtifactPublicationState pak {};
  ArtifactPublicationState catalog {};
  std::optional<ArtifactPublicationState> manifest;
  std::optional<ArtifactPublicationState> report;
};

class IArtifactFileSystem {
public:
  virtual ~IArtifactFileSystem() = default;

  [[nodiscard]] virtual auto Exists(const std::filesystem::path& path) const
    -> bool
    = 0;
  virtual auto CreateDirectories(const std::filesystem::path& path)
    -> std::error_code
    = 0;
  virtual auto RemoveFile(const std::filesystem::path& path) -> std::error_code
    = 0;
  virtual auto Rename(const std::filesystem::path& from,
    const std::filesystem::path& to) -> std::error_code
    = 0;
};

class RealArtifactFileSystem final : public IArtifactFileSystem {
public:
  [[nodiscard]] auto Exists(const std::filesystem::path& path) const
    -> bool override;
  auto CreateDirectories(const std::filesystem::path& path)
    -> std::error_code override;
  auto RemoveFile(const std::filesystem::path& path)
    -> std::error_code override;
  auto Rename(const std::filesystem::path& from,
    const std::filesystem::path& to) -> std::error_code override;
};

[[nodiscard]] auto MakeArtifactPublicationPlan(
  const std::filesystem::path& pak_path,
  const std::filesystem::path& catalog_path,
  const std::optional<std::filesystem::path>& manifest_path,
  const std::optional<std::filesystem::path>& report_path)
  -> ArtifactPublicationPlan;

[[nodiscard]] auto PublishArtifacts(const ArtifactPublicationPlan& plan,
  const ArtifactPublicationIntent& intent, IArtifactFileSystem& fs)
  -> ArtifactPublicationResult;

} // namespace oxygen::content::pak::tool
