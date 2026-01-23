//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <filesystem>
#include <optional>
#include <string>

#include <Oxygen/Content/Import/ImportOptions.h>
#include <Oxygen/Content/Import/Internal/TextureImportRequestBuilder.h>
#include <Oxygen/Content/Import/Internal/Utils/ImportSettingsUtils.h>
#include <Oxygen/Content/Import/TextureSourceAssembly.h>

namespace oxygen::content::import::internal {

auto BuildTextureRequest(const TextureImportSettings& settings,
  std::ostream& error_stream) -> std::optional<ImportRequest>
{
  ImportRequest request {};
  request.source_path = settings.source_path;

  if (settings.cooked_root.empty()) {
    error_stream << "ERROR: --output or --cooked-root is required\n";
    return std::nullopt;
  }

  if (!settings.cooked_root.empty()) {
    std::filesystem::path root(settings.cooked_root);
    if (!root.is_absolute()) {
      error_stream << "ERROR: cooked root must be an absolute path\n";
      return std::nullopt;
    }
    request.cooked_root = root;
  }
  if (!settings.job_name.empty()) {
    request.job_name = settings.job_name;
  } else {
    const auto stem = request.source_path.stem().string();
    if (!stem.empty()) {
      request.job_name = stem;
    }
  }

  request.options.with_content_hashing = settings.with_content_hashing;

  auto& tuning = request.options.texture_tuning;

  if (!MapSettingsToTuning(settings, tuning, error_stream)) {
    return std::nullopt;
  }

  if (!settings.sources.empty()) {
    std::filesystem::path root_path;
    if (!settings.source_path.empty()) {
      root_path = std::filesystem::path(settings.source_path).parent_path();
    }

    for (const auto& mapping : settings.sources) {
      auto path = std::filesystem::path(mapping.file);
      if (path.is_relative() && !root_path.empty()) {
        path = (root_path / path).lexically_normal();
      }
      request.additional_sources.push_back({ .path = path,
        .subresource = { .array_layer = mapping.layer,
          .mip_level = mapping.mip,
          .depth_slice = mapping.slice } });
    }
  }

  return request;
}

} // namespace oxygen::content::import::internal
