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
#include <vector>

#include <Oxygen/Cooker/Cooked/CookerStorage.h>

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

//! A cleanup or a rollback step of a publication that failed: a backup that
//! could not be removed or restored, a final file that could not be removed.
//! The publication's own outcome stays in `error_code`.
struct ArtifactPublicationIssue {
  std::string code;
  std::string message;
};

struct ArtifactPublicationResult {
  bool success = false;
  std::string error_code;
  std::string error_message;
  //! Every cleanup or rollback step that failed, in order.
  std::vector<ArtifactPublicationIssue> issues;
  std::optional<std::chrono::microseconds> publish_duration;

  ArtifactPublicationState pak {};
  ArtifactPublicationState catalog {};
  std::optional<ArtifactPublicationState> manifest;
  std::optional<ArtifactPublicationState> report;
};

[[nodiscard]] auto MakeArtifactPublicationPlan(
  const std::filesystem::path& pak_path,
  const std::filesystem::path& catalog_path,
  const std::optional<std::filesystem::path>& manifest_path,
  const std::optional<std::filesystem::path>& report_path)
  -> ArtifactPublicationPlan;

//! Publishes the staged artifacts of `plan` that `intent` requests, through
//! `storage`, as one step: all of them, or none.
/*!
 Each staged artifact replaces its final file; a final file it replaces is
 kept as a backup until every requested artifact is published, and restored
 when a later one fails. Skipped artifacts lose their staged file, and their
 stale final file when `intent` suppresses it.
*/
[[nodiscard]] auto PublishArtifacts(const ArtifactPublicationPlan& plan,
  const ArtifactPublicationIntent& intent, cooked::CookerStorage& storage)
  -> ArtifactPublicationResult;

} // namespace oxygen::content::pak::tool
