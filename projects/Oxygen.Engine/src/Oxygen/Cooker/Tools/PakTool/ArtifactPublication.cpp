//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <array>
#include <system_error>
#include <utility>
#include <vector>

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
    const ArtifactPaths* paths = nullptr;
    ArtifactPublicationState* state = nullptr;
    bool remove_stale_final_on_skip = false;
  };

  struct PublishedArtifact {
    ArtifactKind kind;
    const ArtifactPaths* paths = nullptr;
    ArtifactPublicationState* state = nullptr;
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

  auto SetError(ArtifactPublicationResult& result, const std::string_view code,
    const std::string_view message) -> void
  {
    result.success = false;
    result.error_code = std::string(code);
    result.error_message = std::string(message);
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

  auto CleanupStagedFile(
    IArtifactFileSystem& fs, ArtifactPublicationState& state) -> void
  {
    if (state.staged_path.empty()) {
      state.cleaned_staged = true;
      return;
    }
    if (!fs.Exists(state.staged_path)) {
      state.cleaned_staged = true;
      return;
    }

    const auto ec = fs.RemoveFile(state.staged_path);
    if (!ec && !fs.Exists(state.staged_path)) {
      state.cleaned_staged = true;
    }
  }

  auto CleanupBackupFile(
    IArtifactFileSystem& fs, const std::filesystem::path& backup_path) -> void
  {
    if (backup_path.empty() || !fs.Exists(backup_path)) {
      return;
    }
    [[maybe_unused]] const auto ec = fs.RemoveFile(backup_path);
  }

  auto RemoveStaleFinalIfRequested(IArtifactFileSystem& fs,
    const ArtifactContext& artifact, ArtifactPublicationResult& result) -> bool
  {
    if (!artifact.remove_stale_final_on_skip
      || artifact.paths->final_path.empty()
      || !fs.Exists(artifact.paths->final_path)) {
      return true;
    }

    const auto ec = fs.RemoveFile(artifact.paths->final_path);
    if (ec) {
      SetError(result, "paktool.publish.remove_stale_final_failed",
        std::string("Failed to remove stale final ") + ToString(artifact.kind)
          + " artifact.");
      return false;
    }

    artifact.state->removed_stale_final = true;
    return true;
  }

  auto EnsureDirectories(const ArtifactContext& artifact,
    const ArtifactPublicationIntent& intent, IArtifactFileSystem& fs,
    ArtifactPublicationResult& result) -> bool
  {
    if (!intent.create_parent_directories) {
      return true;
    }

    const auto parent = artifact.paths->final_path.parent_path();
    if (parent.empty()) {
      return true;
    }

    const auto ec = fs.CreateDirectories(parent);
    if (ec) {
      SetError(result, "paktool.publish.create_directories_failed",
        std::string("Failed to create output directory for ")
          + ToString(artifact.kind) + " artifact.");
      return false;
    }
    return true;
  }

  auto RestorePublishedArtifacts(
    std::vector<PublishedArtifact>& published, IArtifactFileSystem& fs) -> void
  {
    for (auto it = published.rbegin(); it != published.rend(); ++it) {
      const auto& artifact = *it;
      if (fs.Exists(artifact.paths->final_path)) {
        [[maybe_unused]] const auto remove_ec
          = fs.RemoveFile(artifact.paths->final_path);
      }

      if (artifact.had_backup && fs.Exists(artifact.paths->backup_path)) {
        const auto ec
          = fs.Rename(artifact.paths->backup_path, artifact.paths->final_path);
        if (!ec) {
          artifact.state->restored_backup = true;
        }
      }
    }
  }

  auto PublishOneArtifact(const ArtifactContext& artifact,
    IArtifactFileSystem& fs, ArtifactPublicationResult& result,
    std::vector<PublishedArtifact>& published) -> bool
  {
    if (!fs.Exists(artifact.paths->staged_path)) {
      SetError(result, "paktool.publish.staged_missing",
        std::string("Missing staged ") + ToString(artifact.kind)
          + " artifact.");
      return false;
    }

    CleanupBackupFile(fs, artifact.paths->backup_path);

    auto had_backup = false;
    if (fs.Exists(artifact.paths->final_path)) {
      const auto backup_ec
        = fs.Rename(artifact.paths->final_path, artifact.paths->backup_path);
      if (backup_ec) {
        SetError(result, "paktool.publish.backup_rename_failed",
          std::string("Failed to move existing final ")
            + ToString(artifact.kind) + " artifact out of the way.");
        return false;
      }
      had_backup = true;
    }

    const auto publish_ec
      = fs.Rename(artifact.paths->staged_path, artifact.paths->final_path);
    if (publish_ec) {
      if (had_backup && fs.Exists(artifact.paths->backup_path)) {
        [[maybe_unused]] const auto restore_ec
          = fs.Rename(artifact.paths->backup_path, artifact.paths->final_path);
      }
      SetError(result, "paktool.publish.rename_failed",
        std::string("Failed to publish staged ") + ToString(artifact.kind)
          + " artifact.");
      return false;
    }

    artifact.state->published = true;
    artifact.state->cleaned_staged = !fs.Exists(artifact.paths->staged_path);
    published.push_back(PublishedArtifact {
      .kind = artifact.kind,
      .paths = artifact.paths,
      .state = artifact.state,
      .had_backup = had_backup,
    });
    return true;
  }

} // namespace

auto RealArtifactFileSystem::Exists(const std::filesystem::path& path) const
  -> bool
{
  return std::filesystem::exists(path);
}

auto RealArtifactFileSystem::CreateDirectories(
  const std::filesystem::path& path) -> std::error_code
{
  auto ec = std::error_code {};
  if (!path.empty()) {
    std::filesystem::create_directories(path, ec);
  }
  return ec;
}

auto RealArtifactFileSystem::RemoveFile(const std::filesystem::path& path)
  -> std::error_code
{
  auto ec = std::error_code {};
  if (!path.empty()) {
    std::filesystem::remove(path, ec);
  }
  return ec;
}

auto RealArtifactFileSystem::Rename(const std::filesystem::path& from,
  const std::filesystem::path& to) -> std::error_code
{
  auto ec = std::error_code {};
  std::filesystem::rename(from, to, ec);
  return ec;
}

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
  const ArtifactPublicationIntent& intent, IArtifactFileSystem& fs)
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
    .paths = &plan.pak,
    .state = &result.pak,
    .remove_stale_final_on_skip = false,
  });
  artifacts.push_back(ArtifactContext {
    .kind = ArtifactKind::kCatalog,
    .paths = &plan.catalog,
    .state = &result.catalog,
    .remove_stale_final_on_skip = intent.suppress_stale_catalog_on_skip,
  });
  if (plan.manifest.has_value() && result.manifest.has_value()) {
    artifacts.push_back(ArtifactContext {
      .kind = ArtifactKind::kManifest,
      .paths = &*plan.manifest,
      .state = &*result.manifest,
      .remove_stale_final_on_skip = intent.suppress_stale_manifest_on_skip,
    });
  }
  if (plan.report.has_value() && result.report.has_value()) {
    artifacts.push_back(ArtifactContext {
      .kind = ArtifactKind::kReport,
      .paths = &*plan.report,
      .state = &*result.report,
      .remove_stale_final_on_skip = false,
    });
  }

  for (const auto& artifact : artifacts) {
    if (!EnsureDirectories(artifact, intent, fs, result)) {
      result.publish_duration
        = std::chrono::duration_cast<std::chrono::microseconds>(
          std::chrono::steady_clock::now() - publish_start);
      return result;
    }
  }

  auto published = std::vector<PublishedArtifact> {};
  published.reserve(artifacts.size());

  for (const auto& artifact : artifacts) {
    if (!artifact.state->publish_requested) {
      if (!RemoveStaleFinalIfRequested(fs, artifact, result)) {
        for (const auto& published_artifact : published) {
          CleanupBackupFile(fs, published_artifact.paths->backup_path);
        }
        CleanupStagedFile(fs, *artifact.state);
        result.publish_duration
          = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - publish_start);
        return result;
      }

      CleanupStagedFile(fs, *artifact.state);
      continue;
    }

    if (!PublishOneArtifact(artifact, fs, result, published)) {
      RestorePublishedArtifacts(published, fs);

      for (const auto& published_artifact : published) {
        CleanupBackupFile(fs, published_artifact.paths->backup_path);
      }
      for (const auto& cleanup_artifact : artifacts) {
        CleanupStagedFile(fs, *cleanup_artifact.state);
        CleanupBackupFile(fs, cleanup_artifact.paths->backup_path);
      }

      result.publish_duration
        = std::chrono::duration_cast<std::chrono::microseconds>(
          std::chrono::steady_clock::now() - publish_start);
      return result;
    }
  }

  for (const auto& published_artifact : published) {
    CleanupBackupFile(fs, published_artifact.paths->backup_path);
  }

  result.success = true;
  result.publish_duration
    = std::chrono::duration_cast<std::chrono::microseconds>(
      std::chrono::steady_clock::now() - publish_start);
  return result;
}

} // namespace oxygen::content::pak::tool
