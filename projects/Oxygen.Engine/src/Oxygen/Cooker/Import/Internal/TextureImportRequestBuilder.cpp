//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <filesystem>
#include <optional>
#include <vector>

#include <Oxygen/Cooker/Import/ImportDiagnostics.h>
#include <Oxygen/Cooker/Import/ImportOptions.h>
#include <Oxygen/Cooker/Import/ImportRequest.h>
#include <Oxygen/Cooker/Import/Internal/TextureImportRequestBuilder.h>
#include <Oxygen/Cooker/Import/Internal/Utils/ImportSettingsUtils.h>
#include <Oxygen/Cooker/Import/Internal/Utils/RequestPreparation.h>
#include <Oxygen/Cooker/Import/TextureImportSettings.h>

namespace oxygen::content::import {

auto TextureImportSettings::Prepare(
  std::vector<ImportDiagnostic>& diagnostics) const
  -> std::optional<ImportRequest>
{
  const auto& settings = *this;
  ImportRequest request {};
  request.source_path = settings.source_path;
  request.asset_virtual_path = settings.virtual_path;
  internal::ApplyJobName(settings.job_name, request);

  auto& tuning = request.options.texture_tuning;

  if (!internal::MapSettingsToTuning(settings, tuning, diagnostics)) {
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
          .depth_slice = mapping.slice, }, });
    }
  }

  return request;
}

namespace internal {
  auto BuildTextureRequest(const TextureImportSettings& settings,
    std::vector<ImportDiagnostic>& diagnostics) -> std::optional<ImportRequest>
  {
    auto request = settings.Prepare(diagnostics);
    if (!request.has_value()
      || !RequireCookedRoot(settings.cooked_root, *request, diagnostics)) {
      return std::nullopt;
    }
    return request;
  }
} // namespace internal
} // namespace oxygen::content::import
