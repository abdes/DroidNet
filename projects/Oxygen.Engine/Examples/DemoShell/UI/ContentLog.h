//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <Oxygen/Cooker/Import/ImportDiagnostics.h>

namespace oxygen::examples::ui {

//! The codes the content panel logs its own operations with; an import adds
//! its `ImportDiagnostic` codes.
namespace log_codes {
  inline constexpr std::string_view kMountsApplied = "mount.applied";
  inline constexpr std::string_view kMountRefused
    = "mount.cooked_dependency_missing";
  inline constexpr std::string_view kMountSourceMissing
    = "mount.source_missing";
  inline constexpr std::string_view kMountSourceUnreadable
    = "mount.source_unreadable";
  inline constexpr std::string_view kMountFailed = "mount.failed";
  inline constexpr std::string_view kGenerationMountFailed
    = "mount.generation_failed";
  inline constexpr std::string_view kSceneLoaded = "scene.loaded";
  inline constexpr std::string_view kSceneLoadFailed = "scene.load_failed";
  inline constexpr std::string_view kSceneLoadCancelled
    = "scene.load_cancelled";
  inline constexpr std::string_view kImportPublished = "import.published";
  inline constexpr std::string_view kImportFailed = "import.failed";
  inline constexpr std::string_view kImportCancelled = "import.cancelled";
  inline constexpr std::string_view kImportRecordUnavailable
    = "import.record_unavailable";
  inline constexpr std::string_view kSourceFolderAdded
    = "import.source_folder_added";
  inline constexpr std::string_view kSourceFolderFailed
    = "import.source_folder_failed";
} // namespace log_codes

using LogSeverity = content::import::ImportSeverity;

//! One row of the Log tab.
struct ContentLogEntry final {
  //! Unique within the log; rows are selected by it.
  std::uint64_t id { 0 };
  LogSeverity severity { LogSeverity::kInfo };
  std::string code;
  //! The one-line message.
  std::string message;
  //! The full message, when the line shortens it.
  std::string details;
  std::chrono::system_clock::time_point time;
  //! The scene the operation loaded, by virtual path; empty when none.
  std::string scene;
  //! The asset or object concerned, such as an import's object path.
  std::string asset;
  //! The virtual or source path involved, such as a missing dependency.
  std::string path;
  //! The cooked sources searched, by name, top first.
  std::vector<std::string> searched;
  //! The cooked source of the mount list the row concerns.
  std::optional<std::filesystem::path> cooked_source;
};

//! Which rows the Log tab shows.
struct LogFilter final {
  bool errors { true };
  bool warnings { true };
  bool info { true };
  //! Matches the code, the paths or the message, ignoring case.
  std::string text;
};

//! The content panel's coded log: every mount, scene load and import, and the
//! diagnostics of each import.
class ContentLog final {
public:
  //! Adds `entry` stamped now; returns its id.
  auto Add(ContentLogEntry entry) -> std::uint64_t;

  //! Adds an import's diagnostic: its source and object paths become the
  //! row's path and asset.
  auto AddDiagnostic(const content::import::ImportDiagnostic& diagnostic)
    -> std::uint64_t;

  auto Clear() -> void;

  [[nodiscard]] auto Entries() const noexcept
    -> const std::vector<ContentLogEntry>&
  {
    return entries_;
  }

  [[nodiscard]] auto Find(std::uint64_t id) const -> const ContentLogEntry*;

  [[nodiscard]] auto Count(LogSeverity severity) const -> std::size_t;

  //! The errors added since the last `MarkSeen`.
  [[nodiscard]] auto UnseenErrors() const noexcept -> std::size_t
  {
    return unseen_errors_;
  }

  auto MarkSeen() noexcept -> void { unseen_errors_ = 0; }

  [[nodiscard]] static auto Matches(
    const ContentLogEntry& entry, const LogFilter& filter) -> bool;

  //! The rows `filter` shows, one per line, as Copy puts them on the
  //! clipboard.
  [[nodiscard]] auto Text(const LogFilter& filter) const -> std::string;

private:
  std::vector<ContentLogEntry> entries_;
  std::uint64_t next_id_ { 1 };
  std::size_t unseen_errors_ { 0 };
};

} // namespace oxygen::examples::ui
