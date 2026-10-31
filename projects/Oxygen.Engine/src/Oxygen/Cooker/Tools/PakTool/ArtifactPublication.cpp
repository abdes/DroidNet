//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Cooker/Cooked/CookerStorage.h>
#include <Oxygen/Cooker/Tools/PakTool/ArtifactPublication.h>

namespace oxygen::content::pak::tool {

namespace {

  enum class ArtifactKind : uint8_t {
    kPak,
    kCatalog,
    kManifest,
    kReport,
  };

  struct ArtifactContext {
    ArtifactKind kind;
    observer_ptr<const ArtifactPaths> paths;
    observer_ptr<ArtifactPublicationState> state;
    bool remove_stale_final_on_skip = false;
  };

  struct PublishedArtifact {
    ArtifactKind kind;
    observer_ptr<const ArtifactPaths> paths;
    observer_ptr<ArtifactPublicationState> state;
    bool had_backup = false;
  };

  [[nodiscard]] auto ToString(const ArtifactKind kind) -> const char*
  {
    switch (kind) {
    case ArtifactKind::kPak:
      return "pak";
    case ArtifactKind::kCatalog:
      return "catalog";
    case ArtifactKind::kManifest:
      return "manifest";
    case ArtifactKind::kReport:
      return "report";
    }
    return "artifact";
  }

  //! Why a publication failed: a stable diagnostic code, and a message.
  struct PublicationError {
    std::string_view code;
    std::string message;
  };

  auto SetError(ArtifactPublicationResult& result, PublicationError error)
    -> void
  {
    result.success = false;
    result.error_code = std::string(error.code);
    result.error_message = std::move(error.message);
  }

  [[nodiscard]] auto MakeSiblingPath(const std::filesystem::path& path,
    const std::string_view suffix) -> std::filesystem::path
  {
    auto staged_name = path.filename().string();
    staged_name.append(suffix);
    return path.parent_path() / staged_name;
  }

  auto PopulateState(ArtifactPublicationState& state,
    const ArtifactPaths& paths, const bool publish_requested) -> void
  {
    state.final_path = paths.final_path;
    state.staged_path = paths.staged_path;
    state.publish_requested = publish_requested;
  }

  [[nodiscard]] auto IsFile(const cooked::CookerStorage& storage,
    const std::filesystem::path& path) -> bool
  {
    return !path.empty() && storage.KindOf(path) == cooked::PathKind::kFile;
  }

  [[nodiscard]] auto Failure(const std::string_view what,
    const ArtifactKind kind, const std::error_code& error) -> std::string
  {
    return std::string(what) + " " + ToString(kind)
      + " artifact: " + error.message();
  }

  auto CleanupStagedFile(
    cooked::CookerStorage& storage, ArtifactPublicationState& state) -> void
  {
    if (!IsFile(storage, state.staged_path)) {
      state.cleaned_staged = true;
      return;
    }
    if (const auto removed = storage.Remove(state.staged_path);
      removed && !IsFile(storage, state.staged_path)) {
      state.cleaned_staged = true;
    }
  }

  auto AddIssue(ArtifactPublicationResult& result, const std::string_view code,
    std::string message, const std::filesystem::path& path) -> void
  {
    result.issues.push_back(ArtifactPublicationIssue {
      .code = std::string(code),
      .message = std::move(message) + " ('" + path.string() + "')",
    });
  }

  //! Removes the backup of `paths` once its final file exists again. A backup
  //! whose final file is gone is the only copy left, and stays.
  auto CleanupBackupFile(cooked::CookerStorage& storage,
    const ArtifactPaths& paths, const ArtifactKind kind,
    ArtifactPublicationResult& result) -> void
  {
    if (!IsFile(storage, paths.backup_path)
      || !IsFile(storage, paths.final_path)) {
      return;
    }
    if (const auto removed = storage.Remove(paths.backup_path); !removed) {
      AddIssue(result, "paktool.publish.cleanup_failed",
        Failure("Failed to remove the backup of the", kind, removed.error()),
        paths.backup_path);
    }
  }

  auto RemoveStaleFinalIfRequested(cooked::CookerStorage& storage,
    const ArtifactContext& artifact, ArtifactPublicationResult& result) -> bool
  {
    if (!artifact.remove_stale_final_on_skip
      || !IsFile(storage, artifact.paths->final_path)) {
      return true;
    }

    if (const auto removed = storage.Remove(artifact.paths->final_path);
      !removed) {
      SetError(result,
        { .code = "paktool.publish.remove_stale_final_failed",
          .message = Failure(
            "Failed to remove stale final", artifact.kind, removed.error()) });
      return false;
    }

    artifact.state->removed_stale_final = true;
    return true;
  }

  //! Puts back what earlier publications of this run replaced: the backup
  //! where there was one, nothing where there was none.
  auto RestorePublishedArtifacts(std::vector<PublishedArtifact>& published,
    cooked::CookerStorage& storage, ArtifactPublicationResult& result) -> void
  {
    for (auto it = published.rbegin(); it != published.rend(); ++it) {
      const auto& artifact = *it;
      if (!artifact.had_backup) {
        if (const auto removed = storage.Remove(artifact.paths->final_path);
          !removed) {
          AddIssue(result, "paktool.publish.restore_failed",
            Failure("Failed to remove the newly published", artifact.kind,
              removed.error()),
            artifact.paths->final_path);
        }
        continue;
      }
      if (const auto restored
        = storage.Move(artifact.paths->backup_path, artifact.paths->final_path);
        !restored) {
        AddIssue(result, "paktool.publish.restore_failed",
          Failure("Failed to restore the backup of the", artifact.kind,
            restored.error()),
          artifact.paths->backup_path);
        continue;
      }
      artifact.state->restored_backup = true;
    }
  }

  auto PublishOneArtifact(const ArtifactContext& artifact,
    cooked::CookerStorage& storage, ArtifactPublicationResult& result,
    std::vector<PublishedArtifact>& published) -> bool
  {
    const auto& paths = *artifact.paths;
    if (!IsFile(storage, paths.staged_path)) {
      SetError(result,
        { .code = "paktool.publish.staged_missing",
          .message = std::string("Missing staged ") + ToString(artifact.kind)
            + " artifact." });
      return false;
    }

    // The backup replaces any left by an earlier run.
    auto had_backup = false;
    if (IsFile(storage, paths.final_path)) {
      if (const auto moved = storage.Move(paths.final_path, paths.backup_path);
        !moved) {
        SetError(result,
          { .code = "paktool.publish.backup_rename_failed",
            .message = Failure("Failed to move aside the existing final",
              artifact.kind, moved.error()) });
        return false;
      }
      had_backup = true;
    }

    if (const auto moved = storage.Move(paths.staged_path, paths.final_path);
      !moved) {
      if (had_backup) {
        if (const auto restored
          = storage.Move(paths.backup_path, paths.final_path);
          !restored) {
          AddIssue(result, "paktool.publish.restore_failed",
            Failure("Failed to restore the backup of the", artifact.kind,
              restored.error()),
            paths.backup_path);
        }
      }
      SetError(result,
        { .code = "paktool.publish.rename_failed",
          .message = Failure(
            "Failed to publish staged", artifact.kind, moved.error()) });
      return false;
    }

    artifact.state->published = true;
    artifact.state->cleaned_staged = !IsFile(storage, paths.staged_path);
    published.push_back(PublishedArtifact {
      .kind = artifact.kind,
      .paths = artifact.paths,
      .state = artifact.state,
      .had_backup = had_backup,
    });
    return true;
  }

} // namespace

auto MakeArtifactPublicationPlan(const std::filesystem::path& pak_path,
  const std::filesystem::path& catalog_path,
  const std::optional<std::filesystem::path>& manifest_path,
  const std::optional<std::filesystem::path>& report_path)
  -> ArtifactPublicationPlan
{
  auto plan = ArtifactPublicationPlan {
    .pak = ArtifactPaths {
      .final_path = pak_path,
      .staged_path = MakeSiblingPath(pak_path, ".staged"),
      .backup_path = MakeSiblingPath(pak_path, ".previous"),
    },
    .catalog = ArtifactPaths {
      .final_path = catalog_path,
      .staged_path = MakeSiblingPath(catalog_path, ".staged"),
      .backup_path = MakeSiblingPath(catalog_path, ".previous"),
    },
  };

  if (manifest_path.has_value()) {
    plan.manifest = ArtifactPaths {
      .final_path = *manifest_path,
      .staged_path = MakeSiblingPath(*manifest_path, ".staged"),
      .backup_path = MakeSiblingPath(*manifest_path, ".previous"),
    };
  }

  if (report_path.has_value()) {
    plan.report = ArtifactPaths {
      .final_path = *report_path,
      .staged_path = MakeSiblingPath(*report_path, ".staged"),
      .backup_path = MakeSiblingPath(*report_path, ".previous"),
    };
  }

  return plan;
}

auto PublishArtifacts(const ArtifactPublicationPlan& plan,
  const ArtifactPublicationIntent& intent, cooked::CookerStorage& storage)
  -> ArtifactPublicationResult
{
  const auto publish_start = std::chrono::steady_clock::now();

  auto result = ArtifactPublicationResult {};
  PopulateState(result.pak, plan.pak, intent.publish_pak);
  PopulateState(result.catalog, plan.catalog, intent.publish_catalog);

  if (plan.manifest.has_value()) {
    result.manifest = ArtifactPublicationState {};
    PopulateState(*result.manifest, *plan.manifest, intent.publish_manifest);
  }

  if (plan.report.has_value()) {
    result.report = ArtifactPublicationState {};
    PopulateState(*result.report, *plan.report, intent.publish_report);
  }

  auto artifacts = std::vector<ArtifactContext> {};
  artifacts.reserve(4);
  artifacts.push_back(ArtifactContext {
    .kind = ArtifactKind::kPak,
    .paths = make_observer(&plan.pak),
    .state = make_observer(&result.pak),
    .remove_stale_final_on_skip = false,
  });
  artifacts.push_back(ArtifactContext {
    .kind = ArtifactKind::kCatalog,
    .paths = make_observer(&plan.catalog),
    .state = make_observer(&result.catalog),
    .remove_stale_final_on_skip = intent.suppress_stale_catalog_on_skip,
  });
  if (plan.manifest.has_value() && result.manifest.has_value()) {
    artifacts.push_back(ArtifactContext {
      .kind = ArtifactKind::kManifest,
      .paths = make_observer(&*plan.manifest),
      .state = make_observer(&*result.manifest),
      .remove_stale_final_on_skip = intent.suppress_stale_manifest_on_skip,
    });
  }
  if (plan.report.has_value() && result.report.has_value()) {
    artifacts.push_back(ArtifactContext {
      .kind = ArtifactKind::kReport,
      .paths = make_observer(&*plan.report),
      .state = make_observer(&*result.report),
      .remove_stale_final_on_skip = false,
    });
  }

  auto published = std::vector<PublishedArtifact> {};
  published.reserve(artifacts.size());

  for (const auto& artifact : artifacts) {
    if (!artifact.state->publish_requested) {
      if (!RemoveStaleFinalIfRequested(storage, artifact, result)) {
        for (const auto& published_artifact : published) {
          CleanupBackupFile(storage, *published_artifact.paths,
            published_artifact.kind, result);
        }
        CleanupStagedFile(storage, *artifact.state);
        result.publish_duration
          = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - publish_start);
        return result;
      }

      CleanupStagedFile(storage, *artifact.state);
      continue;
    }

    if (!PublishOneArtifact(artifact, storage, result, published)) {
      RestorePublishedArtifacts(published, storage, result);

      for (const auto& published_artifact : published) {
        CleanupBackupFile(
          storage, *published_artifact.paths, published_artifact.kind, result);
      }
      for (const auto& cleanup_artifact : artifacts) {
        CleanupStagedFile(storage, *cleanup_artifact.state);
        CleanupBackupFile(
          storage, *cleanup_artifact.paths, cleanup_artifact.kind, result);
      }

      result.publish_duration
        = std::chrono::duration_cast<std::chrono::microseconds>(
          std::chrono::steady_clock::now() - publish_start);
      return result;
    }
  }

  for (const auto& published_artifact : published) {
    CleanupBackupFile(
      storage, *published_artifact.paths, published_artifact.kind, result);
  }

  result.success = true;
  result.publish_duration
    = std::chrono::duration_cast<std::chrono::microseconds>(
      std::chrono::steady_clock::now() - publish_start);
  return result;
}

} // namespace oxygen::content::pak::tool
