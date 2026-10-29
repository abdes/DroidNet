//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <fmt/format.h>

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Cooker/Import/ImportDiagnostics.h>
#include <Oxygen/Cooker/api_export.h>

namespace oxygen::content::import {

/*!
 A diagnostic of the source mount contract: its stable id, its severity and
 its message, defined once. The message is a format checked at compile time
 against `Args`, the facts each case supplies. The file and field a diagnostic
 is about are not part of the message: they are its `source_path` and
 `object_path`.
*/
template <typename... Args> struct MountCode final {
  std::string_view id;
  ImportSeverity severity = ImportSeverity::kError;
  fmt::format_string<Args...> message;
};

//! The diagnostic codes of the source mount contract.
namespace mount_codes {
  inline constexpr auto kNameInvalid
    = MountCode<std::string_view, std::string_view> {
        .id = "mount.name_invalid",
        .severity = ImportSeverity::kError,
        .message = "mount name '{}' is invalid: {}",
      };
  inline constexpr auto kNameDuplicate
    = MountCode<std::string_view, std::string_view> {
        .id = "mount.name_duplicate",
        .severity = ImportSeverity::kError,
        .message
        = "mount name '{}' duplicates '{}': names are unique, ignoring case",
      };
  inline constexpr auto kNameConflict = MountCode<std::string_view,
    std::string_view, std::string_view, std::string_view> {
    .id = "mount.name_conflict",
    .severity = ImportSeverity::kError,
    .message = "mount '{}' names '{}' here but '{}' in {}: one batch maps a "
               "name to one folder",
  };
  inline constexpr auto kAbsoluteCommitted = MountCode<std::string_view> {
    .id = "path.absolute_committed",
    .severity = ImportSeverity::kError,
    .message = "absolute path '{}': only .mounts.json may hold absolute paths",
  };
  inline constexpr auto kFolderOverlap = MountCode<std::string_view,
    std::string_view, std::string_view, std::string_view, std::string_view> {
    .id = "mount.folder_overlap",
    .severity = ImportSeverity::kError,
    .message = "mount '{}' ('{}') overlaps mount '{}' ('{}', from {}): a file "
               "would have two mount paths",
  };
  inline constexpr auto kFolderInOutput
    = MountCode<std::string_view, std::string_view> {
        .id = "mount.folder_in_output",
        .severity = ImportSeverity::kError,
        .message = "mount '{}' folder '{}' lies in an output folder (a "
                   "dot-named folder below the project root)",
      };
  inline constexpr auto
    kFolderMissing = MountCode<std::string_view, std::string_view> {
      .id = "mount.folder_missing",
      .severity = ImportSeverity::kWarning,
      .message
      = "mount '{}' folder '{}' does not exist; a source needed from it fails",
    };
  inline constexpr auto kPathKind = MountCode<std::string_view,
    std::string_view, std::string_view, std::string_view> {
    .id = "mount.path_kind",
    .severity = ImportSeverity::kError,
    .message = "{} mount '{}' names a {}: '{}'",
  };
  inline constexpr auto kParentIgnored = MountCode<std::string_view> {
    .id = "mount.parent_ignored",
    .severity = ImportSeverity::kWarning,
    .message = "a parent mounts file cannot retarget mount '{}', which the "
               "project commits a path for; the committed path is used",
  };
  inline constexpr auto kUnbound = MountCode<std::string_view> {
    .id = "mount.unbound",
    .severity = ImportSeverity::kWarning,
    .message = "placeholder mount '{}' is unbound: no mounts file binds it; a "
               "source needed from it fails",
  };
  inline constexpr auto kFileVersion = MountCode<std::string_view> {
    .id = "mount.file_version",
    .severity = ImportSeverity::kError,
    .message = "unsupported mounts file version {}; the file binds nothing",
  };
  inline constexpr auto kFileInvalid = MountCode<std::string_view> {
    .id = "mount.file_invalid",
    .severity = ImportSeverity::kError,
    .message = "invalid mounts file: {}; the file binds nothing",
  };
  inline constexpr auto kSourceOutsideMount = MountCode<std::string_view> {
    .id = "import.source_outside_mount",
    .severity = ImportSeverity::kError,
    .message = "source '{}' lies in no source mount: a recorded source is "
               "its mount path",
  };
  inline constexpr auto kSourceMountUnbound
    = MountCode<std::string_view, std::string_view> {
        .id = "import.mount_unbound",
        .severity = ImportSeverity::kError,
        .message = "source '{}' needs mount '{}', which no mounts file binds "
                   "on this machine",
      };
  inline constexpr auto kDerivedFromNotDerived = MountCode<std::string_view> {
    .id = "import.derived_from_not_derived",
    .severity = ImportSeverity::kError,
    .message = "source '{}' is not in an output folder: only a derived file "
               "names the source it derives from",
  };
  inline constexpr auto kDerivedFromInvalid = MountCode<std::string_view> {
    .id = "import.derived_from_invalid",
    .severity = ImportSeverity::kError,
    .message = "derived_from '{}' is not a mount path in a source mount",
  };
} // namespace mount_codes

//! The file name of a mounts file, at the project root or above it.
inline constexpr std::string_view kMountsFileName = ".mounts.json";

//! The mount name reserved for the engine content build.
inline constexpr std::string_view kEngineMountName = "Engine";

//! What a mount's path names.
enum class MountKind : uint8_t {
  //! A folder of source files.
  kSource = 0,
  //! A cooked folder.
  kCooked,
  //! A `.pak` file.
  kPak,
};

//! Whether a mount has a folder for this run.
enum class MountState : uint8_t {
  //! Its folder (or pak file) exists.
  kBound = 0,
  //! A placeholder nothing binds.
  kUnbound,
  //! Its folder (or pak file) does not exist.
  kMissing,
};

//! Which source gave a mount its folder.
enum class MountOrigin : uint8_t {
  kNone = 0,
  //! The mounts file named by `--mounts`.
  kMountsOption,
  //! A `.mounts.json`, the project's own or a parent one.
  kMountsFile,
  //! The committed declaration (manifest `mounts` or `--mount` options).
  kCommitted,
};

//! Whether the committed declaration names a mount.
enum class MountScope : uint8_t {
  //! Declared by the committed declaration, with a path or as a placeholder.
  kDeclared = 0,
  //! Added by a `.mounts.json` only: available on this machine alone.
  kLocal,
};

//! How the committed declaration states its paths.
enum class MountDeclarationForm : uint8_t {
  //! A committed file (an import manifest's `mounts`): relative paths only,
  //! against the project root.
  kCommittedFile = 0,
  //! `--mount <Name>=<dir>` options: absolute, or relative to the working
  //! directory.
  kCommandLine,
};

//! Whose mount table is resolved: only the engine content build may declare
//! the `Engine` mount.
enum class MountTableOwner : uint8_t {
  kProject = 0,
  kEngineContent,
};

[[nodiscard]] inline auto to_string(MountKind kind) -> std::string_view
{
  switch (kind) {
  case MountKind::kSource:
    return "Source";
  case MountKind::kCooked:
    return "Cooked";
  case MountKind::kPak:
    return "Pak";
  }
  return "Unknown";
}

[[nodiscard]] inline auto to_string(MountState state) -> std::string_view
{
  switch (state) {
  case MountState::kBound:
    return "Bound";
  case MountState::kUnbound:
    return "Unbound";
  case MountState::kMissing:
    return "Missing";
  }
  return "Unknown";
}

[[nodiscard]] inline auto to_string(MountOrigin origin) -> std::string_view
{
  switch (origin) {
  case MountOrigin::kNone:
    return "none";
  case MountOrigin::kMountsOption:
    return "--mounts";
  case MountOrigin::kMountsFile:
    return ".mounts.json";
  case MountOrigin::kCommitted:
    return "committed";
  }
  return "Unknown";
}

[[nodiscard]] inline auto to_string(MountScope scope) -> std::string_view
{
  switch (scope) {
  case MountScope::kDeclared:
    return "declared";
  case MountScope::kLocal:
    return "local";
  }
  return "Unknown";
}

//! One mount of the committed declaration.
struct MountDeclaration final {
  std::string name;
  MountKind kind = MountKind::kSource;
  //! The declared path; absent for a placeholder.
  std::optional<std::string> path;
};

//! The inputs of one mount table.
struct MountTableRequest final {
  //! The project root: the folder relative committed paths and the project's
  //! own `.mounts.json` belong to. Made absolute against `working_directory`.
  std::filesystem::path project_root;
  //! The committed declaration.
  std::vector<MountDeclaration> declarations;
  MountDeclarationForm form = MountDeclarationForm::kCommittedFile;
  //! The file or option that holds the declaration, named by diagnostics.
  std::string declared_in;
  //! The base of relative `--mount` paths and of a relative project root.
  std::filesystem::path working_directory;
  //! The mounts file named by `--mounts`, if any.
  std::optional<std::filesystem::path> mounts_option;
  MountTableOwner owner = MountTableOwner::kProject;
};

//! One mount of a resolved table. Calculated values are local facts of one
//! run: they are logged, never stored.
struct Mount final {
  std::string name;
  MountKind kind = MountKind::kSource;
  MountScope scope = MountScope::kDeclared;
  //! The committed path text; absent for a placeholder or a local mount.
  std::optional<std::string> declared_path;
  //! The canonical folder (or pak file); empty when unbound.
  std::filesystem::path folder;
  MountOrigin origin = MountOrigin::kNone;
  //! The mounts file that gave the folder; empty for other origins.
  std::filesystem::path origin_file;
  MountState state = MountState::kUnbound;
};

//! A path a committed file holds, and the field that holds it.
struct CommittedPath final {
  std::string_view text;
  //! The field, such as `jobs[0].source`, named by diagnostics.
  std::string_view field;
};

//! A source file located through a mount table.
struct MountedSource final {
  //! The canonical file.
  std::filesystem::path file;
  //! Its mount path, `/<Mount>/<path in mount>`: the source the loose cooked
  //! index records for what it produces.
  std::string mount_path;
};

//! A resolved mount table and every diagnostic its checks produced.
struct MountTable final {
  //! The canonical project root.
  std::filesystem::path project_root;
  //! The file or option that holds the committed declaration.
  std::string declared_in;
  std::vector<Mount> mounts;
  std::vector<ImportDiagnostic> diagnostics;

  //! True when no diagnostic is an error.
  OXGN_COOK_NDAPI auto IsValid() const -> bool;

  //! The mount with exactly this name, or null.
  OXGN_COOK_NDAPI auto Find(std::string_view name) const
    -> observer_ptr<const Mount>;

  /*!
   Locates a file through the `Source` mounts: its mount path takes the
   mount's name and the file's canonical spelling below the mount's folder, so
   two spellings of one file give one mount path.

   @param file The file: absolute, or relative to the project root.
   @param findings Receives `import.source_outside_mount` when no `Source`
     mount holds the file.
   @return The located source, or nothing.
  */
  OXGN_COOK_NDAPI auto MountPathOf(const std::filesystem::path& file,
    std::vector<ImportDiagnostic>& findings) const
    -> std::optional<MountedSource>;

  //! True when `file` lies in an output folder: a dot-named folder below the
  //! project root.
  OXGN_COOK_NDAPI auto IsInOutputFolder(const std::filesystem::path& file) const
    -> bool;

  /*!
   Locates a source a committed file names: a mount path (`/Content/...`), or
   a path relative to the project root that lies in a `Source` mount.

   @param source The path and the field that holds it.
   @param findings Receives `path.absolute_committed` for an absolute path,
     `import.mount_unbound` for a mount with no folder and
     `import.source_outside_mount` for a source in no `Source` mount.
   @return The located source, or nothing.
  */
  OXGN_COOK_NDAPI auto LocateSource(
    const CommittedPath& source, std::vector<ImportDiagnostic>& findings) const
    -> std::optional<MountedSource>;
};

//! True when `name` follows the mount name rule of `oxygen.mounts.schema.json`.
OXGN_COOK_NDAPI auto IsValidMountName(std::string_view name) -> bool;

/*!
 Builds the mount table of one run and checks it.

 Each name takes its folder from the first source that provides it: the
 `--mounts` file, the project's own `.mounts.json`, the committed declaration,
 then parent `.mounts.json` files nearest first. The walk over mounts files
 stops after a file with `"root": true`. A parent file only binds placeholders
 and adds local mounts; naming a mount with a committed path is ignored with
 `mount.parent_ignored`.

 Folders are made canonical before they are checked. The overlap and output
 folder rules apply to `Source` mounts: cooked roots and paks live in output
 folders by design and are never read as sources.

 Every mount's folder and origin is logged.
*/
OXGN_COOK_NDAPI auto ResolveMountTable(const MountTableRequest& request)
  -> MountTable;

//! Checks that the tables of one batch never map one name to two folders;
//! reports each conflict with `mount.name_conflict`.
OXGN_COOK_NDAPI auto CheckMountNameConflicts(std::span<const MountTable> tables)
  -> std::vector<ImportDiagnostic>;

} // namespace oxygen::content::import
