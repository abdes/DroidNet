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

//! Where a build's artifacts go. The pak, its catalog and its manifest are
//! published together; the report is written after them, and says how that
//! went.
struct ArtifactPublicationPlan {
  ArtifactPaths pak;
  ArtifactPaths catalog;
  std::optional<ArtifactPaths> manifest;
  std::optional<std::filesystem::path> report_path;
};

struct ArtifactPublicationIntent {
  bool publish_pak = true;
  bool publish_catalog = true;
  bool publish_manifest = false;
};

struct ArtifactPublicationState {
  std::filesystem::path final_path;
  std::filesystem::path staged_path;
  bool publish_requested = false;
  //! The final file is the one this publication staged.
  bool published = false;
  bool cleaned_staged = false;
  //! A rollback put the replaced final file back.
  bool restored_backup = false;
  //! A rollback could not put the replaced final file back: it stays in the
  //! backup, the only copy left of it.
  bool kept_backup = false;
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
};

//! The final paths a command asks for; a manifest and a report are optional.
struct ArtifactFinalPaths {
  std::filesystem::path pak;
  std::filesystem::path catalog;
  std::optional<std::filesystem::path> manifest;
  std::optional<std::filesystem::path> report;
};

//! The publication plan of `final_paths`: each artifact is staged and backed
//! up beside its final path.
[[nodiscard]] auto MakeArtifactPublicationPlan(
  const ArtifactFinalPaths& final_paths) -> ArtifactPublicationPlan;

//! Publishes the staged artifacts of `plan` that `intent` requests, through
//! `storage`, as one step: all of them, or none.
/*!
 Each staged artifact replaces its final file; a final file it replaces is
 kept as a backup until every requested artifact is published, and restored
 when a later one fails. A backup that cannot be restored stays, and the
 failure is an issue of the result. Skipped artifacts lose their staged file
 and keep their final file: a failed build leaves the earlier release whole.
*/
[[nodiscard]] auto PublishArtifacts(const ArtifactPublicationPlan& plan,
  const ArtifactPublicationIntent& intent, cooked::CookerStorage& storage)
  -> ArtifactPublicationResult;

} // namespace oxygen::content::pak::tool
