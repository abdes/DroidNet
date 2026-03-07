//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <cctype>
#include <unordered_map>
#include <vector>

#include <Oxygen/Cooker/Pak/PakCatalogIo.h>
#include <Oxygen/Cooker/Tools/PakTool/RequestPreparation.h>

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
    const std::string_view code_prefix, IRequestPreparationFileSystem& fs)
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
    if (!fs.Exists(path)) {
      return Result<void, RequestPreparationError>::Err(
        RequestPreparationError {
          .error_code = std::string(code_prefix) + ".missing",
          .error_message = "Input path does not exist.",
          .path = path,
        });
    }
    if (!fs.IsRegularFile(path)) {
      return Result<void, RequestPreparationError>::Err(
        RequestPreparationError {
          .error_code = std::string(code_prefix) + ".not_regular_file",
          .error_message = "Input path must be a regular file.",
          .path = path,
        });
    }
    return Result<void, RequestPreparationError>::Ok();
  }

  auto ValidateSourceInput(const data::CookedSource& source,
    IRequestPreparationFileSystem& fs) -> Result<void, RequestPreparationError>
  {
    if (source.path.empty()) {
      return Result<void, RequestPreparationError>::Err(
        RequestPreparationError {
          .error_code = "paktool.prepare.source_path_empty",
          .error_message = "Cooked source path must not be empty.",
          .path = source.path,
        });
    }
    if (!fs.Exists(source.path)) {
      return Result<void, RequestPreparationError>::Err(
        RequestPreparationError {
          .error_code = "paktool.prepare.source_missing",
          .error_message = "Cooked source path does not exist.",
          .path = source.path,
        });
    }

    if (source.kind == data::CookedSourceKind::kLooseCooked) {
      if (!fs.IsDirectory(source.path)) {
        return Result<void, RequestPreparationError>::Err(
          RequestPreparationError {
            .error_code = "paktool.prepare.loose_source_not_directory",
            .error_message = "Loose cooked source path must be a directory.",
            .path = source.path,
          });
      }
      return Result<void, RequestPreparationError>::Ok();
    }

    if (!fs.IsRegularFile(source.path)) {
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
    const std::string_view error_code, IRequestPreparationFileSystem& fs)
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
    if (fs.Exists(path) && fs.IsDirectory(path)) {
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

  [[nodiscard]] auto NormalizePathKey(const std::filesystem::path& path)
    -> std::string
  {
    auto key = path.lexically_normal().generic_string();
#ifdef _WIN32
    std::transform(
      key.begin(), key.end(), key.begin(), [](const unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
      });
#endif
    return key;
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

  auto EnsureParentDirectory(const std::filesystem::path& file_path,
    const std::string_view error_code, IRequestPreparationFileSystem& fs)
    -> Result<void, RequestPreparationError>
  {
    const auto parent = file_path.parent_path();
    if (parent.empty()) {
      return Result<void, RequestPreparationError>::Ok();
    }

    if (fs.Exists(parent)) {
      if (!fs.IsDirectory(parent)) {
        return Result<void, RequestPreparationError>::Err(
          RequestPreparationError {
            .error_code = std::string(error_code) + ".parent_not_directory",
            .error_message = "Output parent path must be a directory.",
            .path = parent,
          });
      }
      return Result<void, RequestPreparationError>::Ok();
    }

    const auto ec = fs.CreateDirectories(parent);
    if (ec) {
      return Result<void, RequestPreparationError>::Err(
        RequestPreparationError {
          .error_code = std::string(error_code) + ".create_parent_failed",
          .error_message = "Failed to create output parent directory.",
          .path = parent,
        });
    }
    return Result<void, RequestPreparationError>::Ok();
  }

} // namespace

auto RealRequestPreparationFileSystem::Exists(
  const std::filesystem::path& path) const -> bool
{
  return std::filesystem::exists(path);
}

auto RealRequestPreparationFileSystem::IsDirectory(
  const std::filesystem::path& path) const -> bool
{
  auto ec = std::error_code {};
  return std::filesystem::is_directory(path, ec) && !ec;
}

auto RealRequestPreparationFileSystem::IsRegularFile(
  const std::filesystem::path& path) const -> bool
{
  auto ec = std::error_code {};
  return std::filesystem::is_regular_file(path, ec) && !ec;
}

auto RealRequestPreparationFileSystem::CreateDirectories(
  const std::filesystem::path& path) -> std::error_code
{
  auto ec = std::error_code {};
  if (!path.empty()) {
    std::filesystem::create_directories(path, ec);
  }
  return ec;
}

auto PreparePakToolRequest(const pak::BuildMode mode,
  const PakToolCliOptions& options, IRequestPreparationFileSystem& fs)
  -> Result<PreparedPakToolRequest, RequestPreparationError>
{
  for (const auto& source : options.request.sources) {
    if (const auto validated = ValidateSourceInput(source, fs); !validated) {
      return Result<PreparedPakToolRequest, RequestPreparationError>::Err(
        validated.error());
    }
  }

  if (const auto validated = ValidateOutputFilePath(
        options.request.output_pak, "paktool.prepare.output_pak_path", fs);
    !validated) {
    return Result<PreparedPakToolRequest, RequestPreparationError>::Err(
      validated.error());
  }
  if (const auto validated
    = ValidateOutputFilePath(options.request.catalog_output,
      "paktool.prepare.catalog_output_path", fs);
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
          manifest_output, "paktool.prepare.manifest_output_path", fs);
      !validated) {
      return Result<PreparedPakToolRequest, RequestPreparationError>::Err(
        validated.error());
    }
  }
  if (!options.output.diagnostics_file.empty()) {
    if (const auto validated
      = ValidateOutputFilePath(options.output.diagnostics_file,
        "paktool.prepare.report_output_path", fs);
      !validated) {
      return Result<PreparedPakToolRequest, RequestPreparationError>::Err(
        validated.error());
    }
  }

  if (const auto ensured = EnsureParentDirectory(
        options.request.output_pak, "paktool.prepare.output_pak_parent", fs);
    !ensured) {
    return Result<PreparedPakToolRequest, RequestPreparationError>::Err(
      ensured.error());
  }
  if (const auto ensured = EnsureParentDirectory(options.request.catalog_output,
        "paktool.prepare.catalog_output_parent", fs);
    !ensured) {
    return Result<PreparedPakToolRequest, RequestPreparationError>::Err(
      ensured.error());
  }
  if (manifest_requested) {
    if (const auto ensured = EnsureParentDirectory(
          manifest_output, "paktool.prepare.manifest_output_parent", fs);
      !ensured) {
      return Result<PreparedPakToolRequest, RequestPreparationError>::Err(
        ensured.error());
    }
  }
  if (!options.output.diagnostics_file.empty()) {
    if (const auto ensured
      = EnsureParentDirectory(options.output.diagnostics_file,
        "paktool.prepare.report_output_parent", fs);
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

  auto base_catalogs = std::vector<data::PakCatalog> {};
  auto base_catalog_paths = std::vector<std::filesystem::path> {};
  if (mode == pak::BuildMode::kPatch) {
    if (options.patch.base_catalogs.empty()) {
      return MakeError("paktool.prepare.base_catalog_required",
        "Patch mode requires at least one --base-catalog.");
    }

    base_catalogs.reserve(options.patch.base_catalogs.size());
    base_catalog_paths.reserve(options.patch.base_catalogs.size());

    for (const auto& path : options.patch.base_catalogs) {
      if (const auto validated
        = ValidateFileInput(path, "paktool.prepare.base_catalog", fs);
        !validated) {
        return Result<PreparedPakToolRequest, RequestPreparationError>::Err(
          validated.error());
      }

      const auto read_result = pak::PakCatalogIo::Read(path);
      if (!read_result.has_value()) {
        return MakeError("paktool.prepare.base_catalog_invalid",
          "Failed to read canonical pak catalog sidecar.", path);
      }

      base_catalog_paths.push_back(path);
      base_catalogs.push_back(read_result.value());
    }
  }

  const auto publication_plan = MakeArtifactPublicationPlan(
    options.request.output_pak, options.request.catalog_output,
    manifest_requested ? std::optional<std::filesystem::path>(manifest_output)
                       : std::nullopt,
    options.output.diagnostics_file.empty()
      ? std::nullopt
      : std::optional<std::filesystem::path>(options.output.diagnostics_file));

  auto distinct_paths = std::vector<NamedPath> {};
  distinct_paths.reserve(
    options.request.sources.size() + base_catalog_paths.size() + 8U);
  for (const auto& source : options.request.sources) {
    distinct_paths.push_back(NamedPath {
      .label = source.kind == data::CookedSourceKind::kLooseCooked
        ? "loose-source"
        : "pak-source",
      .path = source.path,
    });
  }
  for (const auto& base_catalog_path : base_catalog_paths) {
    distinct_paths.push_back(NamedPath {
      .label = "base-catalog",
      .path = base_catalog_path,
    });
  }
  distinct_paths.push_back(NamedPath {
    .label = "output-pak", .path = publication_plan.pak.final_path });
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
  if (publication_plan.report.has_value()) {
    distinct_paths.push_back(NamedPath {
      .label = "diagnostics-file",
      .path = publication_plan.report->final_path,
    });
    distinct_paths.push_back(NamedPath {
      .label = "diagnostics-file.staged",
      .path = publication_plan.report->staged_path,
    });
    distinct_paths.push_back(NamedPath {
      .label = "diagnostics-file.backup",
      .path = publication_plan.report->backup_path,
    });
  }
  if (const auto validated = ValidateDistinctPaths(distinct_paths);
    !validated) {
    return Result<PreparedPakToolRequest, RequestPreparationError>::Err(
      validated.error());
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
  build_request.base_catalogs = std::move(base_catalogs);
  build_request.patch_compat.require_exact_base_set
    = !options.patch.allow_base_set_mismatch;
  build_request.patch_compat.require_content_version_match
    = !options.patch.allow_content_version_mismatch;
  build_request.patch_compat.require_base_source_key_match
    = !options.patch.allow_base_source_key_mismatch;
  build_request.patch_compat.require_catalog_digest_match
    = !options.patch.allow_catalog_digest_mismatch;
  build_request.options.deterministic = options.request.deterministic;
  build_request.options.embed_browse_index = options.request.embed_browse_index;
  build_request.options.compute_crc32 = options.request.compute_crc32;
  build_request.options.fail_on_warnings = options.request.fail_on_warnings;
  build_request.options.emit_manifest_in_full
    = (mode == pak::BuildMode::kFull) && manifest_requested;

  return Result<PreparedPakToolRequest, RequestPreparationError>::Ok(
    PreparedPakToolRequest {
      .build_request = build_request,
      .request_snapshot = PakToolRequestSnapshot {
        .request = build_request,
        .base_catalog_paths = std::move(base_catalog_paths),
      },
      .publication_plan = publication_plan,
    });
}

} // namespace oxygen::content::pak::tool
