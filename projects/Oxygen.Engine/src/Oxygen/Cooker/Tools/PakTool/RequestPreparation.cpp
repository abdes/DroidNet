//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <cctype>
#include <exception>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <unordered_map>
#include <utility>
#include <vector>

#include <Oxygen/Base/Result.h>
#include <Oxygen/Content/PakFile.h>
#include <Oxygen/Cooker/Cooked/CookerStorage.h>
#include <Oxygen/Cooker/Import/ImportDiagnostics.h>
#include <Oxygen/Cooker/Import/SourceMounts.h>
#include <Oxygen/Cooker/Pak/PakBuildRequest.h>
#include <Oxygen/Cooker/Pak/PakTextures.h>
#include <Oxygen/Cooker/Tools/PakTool/ArtifactPublication.h>
#include <Oxygen/Cooker/Tools/PakTool/PakToolOptions.h>
#include <Oxygen/Cooker/Tools/PakTool/RequestPreparation.h>
#include <Oxygen/Cooker/Tools/PakTool/RequestSnapshot.h>
#include <Oxygen/Data/CookedSource.h>
#include <Oxygen/Data/SourceKey.h>

namespace oxygen::content::pak::tool {

namespace {

  struct NamedPath {
    std::string label;
    std::filesystem::path path;
  };

  auto MakeError(std::string code, std::string message,
    const std::filesystem::path& path = {})
    -> Result<PreparedPakToolRequest, RequestPreparationError>
  {
    return Result<PreparedPakToolRequest, RequestPreparationError>::Err(
      RequestPreparationError {
        .error_code = std::move(code),
        .error_message = std::move(message),
        .path = path,
      });
  }

  auto ValidateFileInput(const std::filesystem::path& path,
    const std::string_view code_prefix,
    const cooked::CookerStorageReader& storage)
    -> Result<void, RequestPreparationError>
  {
    if (path.empty()) {
      return Result<void, RequestPreparationError>::Err(
        RequestPreparationError {
          .error_code = std::string(code_prefix) + ".path_empty",
          .error_message = "Input path must not be empty.",
          .path = path,
        });
    }
    if (storage.KindOf(path) == cooked::PathKind::kNone) {
      return Result<void, RequestPreparationError>::Err(
        RequestPreparationError {
          .error_code = std::string(code_prefix) + ".missing",
          .error_message = "Input path does not exist.",
          .path = path,
        });
    }
    if (storage.KindOf(path) != cooked::PathKind::kFile) {
      return Result<void, RequestPreparationError>::Err(
        RequestPreparationError {
          .error_code = std::string(code_prefix) + ".not_regular_file",
          .error_message = "Input path must be a regular file.",
          .path = path,
        });
    }
    return Result<void, RequestPreparationError>::Ok();
  }

  //! Reads the catalog and the browse index textures of each base or
  //! dependency pak a request names.
  auto ReadLayerPaks(const std::span<const std::filesystem::path> paths,
    const std::string_view role, const cooked::CookerStorageReader& storage)
    -> Result<std::vector<pak::PakDependency>, RequestPreparationError>
  {
    using ReadResult
      = Result<std::vector<pak::PakDependency>, RequestPreparationError>;
    const auto prefix = "paktool.prepare." + std::string(role);
    auto paks = std::vector<pak::PakDependency> {};
    paks.reserve(paths.size());
    for (const auto& path : paths) {
      if (const auto validated = ValidateFileInput(path, prefix, storage);
        !validated) {
        return ReadResult::Err(validated.error());
      }
      try {
        const content::PakFile archive(path);
        paks.push_back(pak::PakDependency {
          .catalog = archive.Catalog(),
          .textures = pak::ReadPakTextures(archive),
        });
      } catch (const std::exception& error) {
        return ReadResult::Err(RequestPreparationError {
          .error_code = prefix + "_invalid",
          .error_message = std::string("Could not read PAK: ") + error.what(),
          .path = path,
        });
      }
    }
    return ReadResult::Ok(std::move(paks));
  }

  auto ValidateSourceInput(const data::CookedSource& source,
    const cooked::CookerStorageReader& storage)
    -> Result<void, RequestPreparationError>
  {
    if (source.path.empty()) {
      return Result<void, RequestPreparationError>::Err(
        RequestPreparationError {
          .error_code = "paktool.prepare.source_path_empty",
          .error_message = "Cooked source path must not be empty.",
          .path = source.path,
        });
    }
    if (storage.KindOf(source.path) == cooked::PathKind::kNone) {
      return Result<void, RequestPreparationError>::Err(
        RequestPreparationError {
          .error_code = "paktool.prepare.source_missing",
          .error_message = "Cooked source path does not exist.",
          .path = source.path,
        });
    }

    if (source.kind == data::CookedSourceKind::kLooseCooked) {
      if (storage.KindOf(source.path) != cooked::PathKind::kFolder) {
        return Result<void, RequestPreparationError>::Err(
          RequestPreparationError {
            .error_code = "paktool.prepare.loose_source_not_directory",
            .error_message = "Loose cooked source path must be a directory.",
            .path = source.path,
          });
      }
      return Result<void, RequestPreparationError>::Ok();
    }

    if (storage.KindOf(source.path) != cooked::PathKind::kFile) {
      return Result<void, RequestPreparationError>::Err(
        RequestPreparationError {
          .error_code = "paktool.prepare.pak_source_not_regular_file",
          .error_message = "Pak source path must be a regular file.",
          .path = source.path,
        });
    }
    return Result<void, RequestPreparationError>::Ok();
  }

  auto ValidateOutputFilePath(const std::filesystem::path& path,
    const std::string_view error_code,
    const cooked::CookerStorageReader& storage)
    -> Result<void, RequestPreparationError>
  {
    if (path.empty()) {
      return Result<void, RequestPreparationError>::Err(
        RequestPreparationError {
          .error_code = std::string(error_code),
          .error_message = "Output path must not be empty.",
          .path = path,
        });
    }
    if (storage.KindOf(path) == cooked::PathKind::kFolder) {
      return Result<void, RequestPreparationError>::Err(
        RequestPreparationError {
          .error_code = std::string(error_code) + ".is_directory",
          .error_message
          = "Output path must reference a file, not a directory.",
          .path = path,
        });
    }
    return Result<void, RequestPreparationError>::Ok();
  }

  //! The file `path` names, whatever the spelling: relative to the working
  //! directory or absolute, through any link to a folder that exists.
  [[nodiscard]] auto NormalizePathKey(const std::filesystem::path& path)
    -> std::string
  {
    // A relative path stays relative through `weakly_canonical` when none of
    // it exists: it is made absolute first.
    auto error = std::error_code {};
    auto resolved = std::filesystem::absolute(path, error);
    if (error) {
      resolved = path;
    }
    auto canonical = std::filesystem::weakly_canonical(resolved, error);
    resolved = error ? resolved.lexically_normal() : std::move(canonical);
    auto key = resolved.generic_string();
#ifdef _WIN32
    std::ranges::transform(
      key, key.begin(), [](const unsigned char ch) -> char {
        return static_cast<char>(std::tolower(ch));
      });
#endif
    return key;
  }

  /*!
   Resolves the source mounts that seal external scripts: the `--mount`
   options, the project's `.mounts.json` hierarchy and `--mounts`, as
   ImportTool resolves them. Without any of these options there are none.
  */
  auto ResolveSourceMountFolders(const PakToolRequestOptions& options)
    -> Result<SourceMountFolders, RequestPreparationError>
  {
    using import::MountDeclaration;
    using import::MountKind;
    using import::MountState;

    auto folders = SourceMountFolders {};
    if (options.source_mounts.empty() && options.mounts_file.empty()
      && options.project_root.empty()) {
      return Result<SourceMountFolders, RequestPreparationError>::Ok(
        std::move(folders));
    }

    auto declarations = std::vector<MountDeclaration> {};
    for (const auto& mount : options.source_mounts) {
      const auto equals = mount.find('=');
      if (equals == std::string::npos || equals == 0
        || equals + 1 == mount.size()) {
        return Result<SourceMountFolders, RequestPreparationError>::Err(
          RequestPreparationError {
            .error_code = "paktool.prepare.source_mount_invalid",
            .error_message = "--mount '" + mount + "' is not <Name>=<dir>",
            .path = {},
          });
      }
      declarations.push_back({
        .name = mount.substr(0, equals),
        .kind = MountKind::kSource,
        .path = mount.substr(equals + 1),
      });
    }

    const auto working_directory = std::filesystem::current_path();
    const auto table = import::ResolveMountTable({
      .project_root
      = options.project_root.empty() ? working_directory : options.project_root,
      .declarations = std::move(declarations),
      .form = import::MountDeclarationForm::kCommandLine,
      .declared_in = "--mount",
      .working_directory = working_directory,
      .mounts_option = options.mounts_file.empty()
        ? std::nullopt
        : std::optional { options.mounts_file },
      .owner = import::MountTableOwner::kProject,
    });
    if (!table.IsValid()) {
      auto message = std::string { "The source mounts are invalid:" };
      for (const auto& diagnostic : table.diagnostics) {
        if (diagnostic.severity == import::ImportSeverity::kError) {
          message += "\n  " + diagnostic.code + ": " + diagnostic.message;
        }
      }
      return Result<SourceMountFolders, RequestPreparationError>::Err(
        RequestPreparationError {
          .error_code = "paktool.prepare.source_mounts_invalid",
          .error_message = std::move(message),
          .path = table.project_root,
        });
    }

    for (const auto& mount : table.mounts) {
      if (mount.kind == MountKind::kSource
        && mount.state == MountState::kBound) {
        folders.emplace(mount.name, mount.folder);
      }
    }
    return Result<SourceMountFolders, RequestPreparationError>::Ok(
      std::move(folders));
  }

  auto ValidateDistinctPaths(const std::vector<NamedPath>& paths)
    -> Result<void, RequestPreparationError>
  {
    auto seen = std::unordered_map<std::string, NamedPath> {};
    seen.reserve(paths.size());

    for (const auto& named_path : paths) {
      if (named_path.path.empty()) {
        continue;
      }

      const auto key = NormalizePathKey(named_path.path);
      if (const auto it = seen.find(key); it != seen.end()) {
        return Result<void, RequestPreparationError>::Err(
          RequestPreparationError {
            .error_code = "paktool.prepare.path_conflict",
            .error_message = "Tool paths must be distinct: '" + named_path.label
              + "' conflicts with '" + it->second.label + "'.",
            .path = named_path.path,
          });
      }

      seen.emplace(key, named_path);
    }

    return Result<void, RequestPreparationError>::Ok();
  }

  auto RequireParentFolder(const std::filesystem::path& file_path,
    const std::string_view error_code,
    const cooked::CookerStorageReader& storage)
    -> Result<void, RequestPreparationError>
  {
    const auto parent = file_path.parent_path();
    if (parent.empty()) {
      return Result<void, RequestPreparationError>::Ok();
    }

    // A missing parent is created when the artifact is published.
    const auto kind = storage.KindOf(parent);
    if (kind != cooked::PathKind::kNone && kind != cooked::PathKind::kFolder) {
      return Result<void, RequestPreparationError>::Err(
        RequestPreparationError {
          .error_code = std::string(error_code) + ".parent_not_directory",
          .error_message = "Output parent path must be a directory.",
          .path = parent,
        });
    }
    return Result<void, RequestPreparationError>::Ok();
  }

} // namespace

auto PreparePakToolRequest(const pak::BuildMode mode,
  const PakToolCliOptions& options, const cooked::CookerStorageReader& storage)
  -> Result<PreparedPakToolRequest, RequestPreparationError>
{
  for (const auto& source : options.request.sources) {
    if (const auto validated = ValidateSourceInput(source, storage);
      !validated) {
      return Result<PreparedPakToolRequest, RequestPreparationError>::Err(
        validated.error());
    }
  }

  if (const auto validated = ValidateOutputFilePath(
        options.request.output_pak, "paktool.prepare.output_pak_path", storage);
    !validated) {
    return Result<PreparedPakToolRequest, RequestPreparationError>::Err(
      validated.error());
  }
  if (const auto validated
    = ValidateOutputFilePath(options.request.catalog_output,
      "paktool.prepare.catalog_output_path", storage);
    !validated) {
    return Result<PreparedPakToolRequest, RequestPreparationError>::Err(
      validated.error());
  }

  const auto manifest_output = mode == pak::BuildMode::kPatch
    ? options.patch.manifest_output
    : options.build.manifest_output;
  const auto manifest_requested = !manifest_output.empty();
  if (mode == pak::BuildMode::kPatch && !manifest_requested) {
    return MakeError("paktool.prepare.patch_manifest_output_required",
      "Patch mode requires --manifest-out.");
  }
  if (manifest_requested) {
    if (const auto validated = ValidateOutputFilePath(
          manifest_output, "paktool.prepare.manifest_output_path", storage);
      !validated) {
      return Result<PreparedPakToolRequest, RequestPreparationError>::Err(
        validated.error());
    }
  }
  if (!options.output.diagnostics_file.empty()) {
    if (const auto validated
      = ValidateOutputFilePath(options.output.diagnostics_file,
        "paktool.prepare.report_output_path", storage);
      !validated) {
      return Result<PreparedPakToolRequest, RequestPreparationError>::Err(
        validated.error());
    }
  }

  if (const auto ensured = RequireParentFolder(options.request.output_pak,
        "paktool.prepare.output_pak_parent", storage);
    !ensured) {
    return Result<PreparedPakToolRequest, RequestPreparationError>::Err(
      ensured.error());
  }
  if (const auto ensured = RequireParentFolder(options.request.catalog_output,
        "paktool.prepare.catalog_output_parent", storage);
    !ensured) {
    return Result<PreparedPakToolRequest, RequestPreparationError>::Err(
      ensured.error());
  }
  if (manifest_requested) {
    if (const auto ensured = RequireParentFolder(
          manifest_output, "paktool.prepare.manifest_output_parent", storage);
      !ensured) {
      return Result<PreparedPakToolRequest, RequestPreparationError>::Err(
        ensured.error());
    }
  }
  if (!options.output.diagnostics_file.empty()) {
    if (const auto ensured
      = RequireParentFolder(options.output.diagnostics_file,
        "paktool.prepare.report_output_parent", storage);
      !ensured) {
      return Result<PreparedPakToolRequest, RequestPreparationError>::Err(
        ensured.error());
    }
  }

  const auto source_key
    = data::SourceKey::FromString(options.request.source_key);
  if (!source_key.has_value()) {
    return MakeError("paktool.prepare.invalid_source_key",
      "source_key must be canonical lowercase UUIDv7 text.");
  }

  // Only a patch has bases, the exact stack its deltas apply to; the builder
  // rejects bases in a full build.
  const auto& base_pak_paths = options.patch.base_paks;
  if (mode == pak::BuildMode::kPatch && base_pak_paths.empty()) {
    return MakeError("paktool.prepare.base_pak_required",
      "Patch mode requires at least one --base-pak.");
  }
  auto base_paks = ReadLayerPaks(base_pak_paths, "base_pak", storage);
  if (!base_paks) {
    return Result<PreparedPakToolRequest, RequestPreparationError>::Err(
      base_paks.error());
  }
  const auto& dependency_pak_paths = options.request.dependency_paks;
  auto dependency_paks
    = ReadLayerPaks(dependency_pak_paths, "dependency_pak", storage);
  if (!dependency_paks) {
    return Result<PreparedPakToolRequest, RequestPreparationError>::Err(
      dependency_paks.error());
  }

  const auto publication_plan = MakeArtifactPublicationPlan({
    .pak = options.request.output_pak,
    .catalog = options.request.catalog_output,
    .manifest = manifest_requested
      ? std::optional<std::filesystem::path>(manifest_output)
      : std::nullopt,
    .report = options.output.diagnostics_file.empty()
      ? std::nullopt
      : std::optional<std::filesystem::path>(options.output.diagnostics_file),
  });

  auto distinct_paths = std::vector<NamedPath> {};
  distinct_paths.reserve(options.request.sources.size() + base_pak_paths.size()
    + dependency_pak_paths.size() + 8U);
  for (const auto& source : options.request.sources) {
    distinct_paths.push_back(NamedPath {
      .label = source.kind == data::CookedSourceKind::kLooseCooked
        ? "loose-source"
        : "pak-source",
      .path = source.path,
    });
  }
  for (const auto& base_catalog_path : base_pak_paths) {
    distinct_paths.push_back(NamedPath {
      .label = "base-pak",
      .path = base_catalog_path,
    });
  }
  for (const auto& dependency_path : dependency_pak_paths) {
    distinct_paths.push_back(NamedPath {
      .label = "dependency-pak",
      .path = dependency_path,
    });
  }
  distinct_paths.push_back(NamedPath {
    .label = "output-pak",
    .path = publication_plan.pak.final_path,
  });
  distinct_paths.push_back(NamedPath {
    .label = "output-pak.staged",
    .path = publication_plan.pak.staged_path,
  });
  distinct_paths.push_back(NamedPath {
    .label = "output-pak.backup",
    .path = publication_plan.pak.backup_path,
  });
  distinct_paths.push_back(NamedPath {
    .label = "catalog-out",
    .path = publication_plan.catalog.final_path,
  });
  distinct_paths.push_back(NamedPath {
    .label = "catalog-out.staged",
    .path = publication_plan.catalog.staged_path,
  });
  distinct_paths.push_back(NamedPath {
    .label = "catalog-out.backup",
    .path = publication_plan.catalog.backup_path,
  });
  if (publication_plan.manifest.has_value()) {
    distinct_paths.push_back(NamedPath {
      .label = "manifest-out",
      .path = publication_plan.manifest->final_path,
    });
    distinct_paths.push_back(NamedPath {
      .label = "manifest-out.staged",
      .path = publication_plan.manifest->staged_path,
    });
    distinct_paths.push_back(NamedPath {
      .label = "manifest-out.backup",
      .path = publication_plan.manifest->backup_path,
    });
  }
  if (publication_plan.report_path.has_value()) {
    distinct_paths.push_back(NamedPath {
      .label = "diagnostics-file",
      .path = *publication_plan.report_path,
    });
  }
  if (const auto validated = ValidateDistinctPaths(distinct_paths);
    !validated) {
    return Result<PreparedPakToolRequest, RequestPreparationError>::Err(
      validated.error());
  }

  auto source_mounts = ResolveSourceMountFolders(options.request);
  if (!source_mounts.has_value()) {
    return Result<PreparedPakToolRequest, RequestPreparationError>::Err(
      source_mounts.error());
  }

  auto build_request = pak::PakBuildRequest {};
  build_request.mode = mode;
  build_request.sources = options.request.sources;
  build_request.output_pak_path = publication_plan.pak.staged_path;
  build_request.output_manifest_path = publication_plan.manifest.has_value()
    ? publication_plan.manifest->staged_path
    : std::filesystem::path {};
  build_request.content_version = options.request.content_version;
  build_request.source_key = source_key.value();
  for (auto& base : *base_paks) {
    build_request.base_catalogs.push_back(std::move(base.catalog));
    build_request.base_textures.push_back(std::move(base.textures));
  }
  build_request.dependencies = std::move(*dependency_paks);
  build_request.options.compute_crc32 = options.request.compute_crc32;
  build_request.options.fail_on_warnings = options.request.fail_on_warnings;
  build_request.options.emit_manifest_in_full
    = (mode == pak::BuildMode::kFull) && manifest_requested;

  return Result<PreparedPakToolRequest, RequestPreparationError>::Ok(
    PreparedPakToolRequest {
      .build_request = build_request,
      .request_snapshot = PakToolRequestSnapshot {
        .request = build_request,
        .base_pak_paths = base_pak_paths,
        .dependency_pak_paths = dependency_pak_paths,
        .source_mounts = std::move(source_mounts.value()),
      },
      .publication_plan = publication_plan,
    });
}

} // namespace oxygen::content::pak::tool
