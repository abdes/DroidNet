//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <filesystem>
#include <optional>
#include <ostream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include <Oxygen/Cooker/Import/ImportDiagnostics.h>
#include <Oxygen/Cooker/Import/ImportOptions.h>
#include <Oxygen/Cooker/Import/ImportRequest.h>
#include <Oxygen/Cooker/Import/Internal/PhysicsSidecarSource.h>
#include <Oxygen/Cooker/Import/Internal/Utils/DescriptorDocument.h>
#include <Oxygen/Cooker/Import/Internal/Utils/VirtualPathResolution.h>
#include <Oxygen/Cooker/Import/PhysicsImportRequestBuilder.h>
#include <Oxygen/Cooker/Import/PhysicsImportSettings.h>

namespace oxygen::content::import::internal {

namespace {

  //! Refuses a payload the sidecar document preparation refuses, and stores
  //! the accepted payload in its compact form.
  auto NormalizeInlineSidecarPayload(const std::string_view payload_text,
    const std::filesystem::path& source_path, std::string& normalized_payload,
    std::ostream& error_stream) -> bool
  {
    auto diagnostics = std::vector<ImportDiagnostic> {};
    if (!PreparePhysicsSidecarDocument(payload_text, source_path, diagnostics)
          .has_value()) {
      WriteDiagnostics(diagnostics, error_stream);
      return false;
    }

    normalized_payload = nlohmann::json::parse(payload_text).dump();
    return true;
  }

  struct BaseRequestSettings final {
    std::string_view source_path;
    std::string_view cooked_root;
    std::string_view job_name;
  };

  auto BuildBaseImportRequest(const BaseRequestSettings& settings,
    std::ostream& error_stream) -> std::optional<ImportRequest>
  {
    auto request = ImportRequest {};
    request.source_path = settings.source_path;

    if (!settings.cooked_root.empty()) {
      auto root = std::filesystem::path(settings.cooked_root);
      if (!root.is_absolute()) {
        error_stream << "ERROR: cooked root must be an absolute path\n";
        return std::nullopt;
      }
      request.cooked_root = root;
    }

    if (!settings.job_name.empty()) {
      request.job_name = std::string(settings.job_name);
    } else {
      const auto stem = request.source_path.stem().string();
      if (!stem.empty()) {
        request.job_name = stem;
      }
    }

    return request;
  }

} // namespace

auto BuildPhysicsSidecarRequest(const PhysicsSidecarImportSettings& settings,
  std::ostream& error_stream) -> std::optional<ImportRequest>
{
  const auto has_source = !settings.source_path.empty();
  const auto has_inline = !settings.inline_bindings_json.empty();
  if (has_source == has_inline) {
    error_stream << "ERROR: exactly one of source_path or inline_bindings_json "
                    "must be provided\n";
    return std::nullopt;
  }

  const auto source_path_for_request = has_source
    ? settings.source_path
    : std::string("inline://physics-sidecar");

  auto normalized_inline_bindings = std::string {};
  if (has_inline
    && !NormalizeInlineSidecarPayload(settings.inline_bindings_json,
      source_path_for_request, normalized_inline_bindings, error_stream)) {
    return std::nullopt;
  }

  auto request = BuildBaseImportRequest(
    {
      .source_path = source_path_for_request,
      .cooked_root = settings.cooked_root,
      .job_name = settings.job_name,
    },
    error_stream);
  if (!request.has_value()) {
    return std::nullopt;
  }

  if (settings.target_scene_virtual_path.empty()) {
    error_stream << "ERROR: target_scene_virtual_path is required\n";
    return std::nullopt;
  }
  if (!::oxygen::content::import::internal::IsCanonicalVirtualPath(
        settings.target_scene_virtual_path)) {
    error_stream << "ERROR: target_scene_virtual_path must be a canonical "
                    "virtual path\n";
    return std::nullopt;
  }

  request->physics = PhysicsImportSettings {
    .target_scene_virtual_path = settings.target_scene_virtual_path,
    .inline_bindings_json = std::move(normalized_inline_bindings),
  };
  request->options.physics.backend = settings.physics_backend;
  return request;
}

} // namespace oxygen::content::import::internal
