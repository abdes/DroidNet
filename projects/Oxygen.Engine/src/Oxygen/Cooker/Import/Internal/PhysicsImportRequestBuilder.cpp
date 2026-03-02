//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <exception>
#include <filesystem>
#include <nlohmann/json.hpp>
#include <optional>
#include <string_view>

#include <Oxygen/Cooker/Import/Internal/Utils/VirtualPathResolution.h>
#include <Oxygen/Cooker/Import/PhysicsImportRequestBuilder.h>

namespace oxygen::content::import::internal {

namespace {

  auto NormalizeInlineSidecarPayload(const std::string_view payload_text,
    std::string& normalized_payload, std::ostream& error_stream) -> bool
  {
    using json = nlohmann::json;
    json parsed;
    try {
      parsed = json::parse(payload_text);
    } catch (const std::exception& ex) {
      error_stream << "ERROR: inline_bindings_json is not valid JSON: "
                   << ex.what() << "\n";
      return false;
    }

    if (!parsed.is_object()) {
      error_stream << "ERROR: inline_bindings_json must be a JSON object\n";
      return false;
    }

    json sidecar_doc {};
    if (parsed.contains("bindings")) {
      if (!parsed["bindings"].is_object()) {
        error_stream << "ERROR: inline_bindings_json field 'bindings' must be "
                        "an object\n";
        return false;
      }
      sidecar_doc = std::move(parsed);
    } else {
      sidecar_doc = json::object();
      sidecar_doc["bindings"] = std::move(parsed);
    }

    normalized_payload = sidecar_doc.dump();
    return true;
  }

  auto BuildBaseImportRequest(const std::string& source_path,
    const std::string& cooked_root, const std::string& job_name,
    const bool with_content_hashing, std::ostream& error_stream)
    -> std::optional<ImportRequest>
  {
    auto request = ImportRequest {};
    request.source_path = source_path;

    if (!cooked_root.empty()) {
      auto root = std::filesystem::path(cooked_root);
      if (!root.is_absolute()) {
        error_stream << "ERROR: cooked root must be an absolute path\n";
        return std::nullopt;
      }
      request.cooked_root = root;
    }

    if (!job_name.empty()) {
      request.job_name = job_name;
    } else {
      const auto stem = request.source_path.stem().string();
      if (!stem.empty()) {
        request.job_name = stem;
      }
    }

    request.options.with_content_hashing
      = EffectiveContentHashingEnabled(with_content_hashing);
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

  auto normalized_inline_bindings = std::string {};
  if (has_inline
    && !NormalizeInlineSidecarPayload(settings.inline_bindings_json,
      normalized_inline_bindings, error_stream)) {
    return std::nullopt;
  }

  const auto source_path_for_request = has_source
    ? settings.source_path
    : std::string("inline://physics-sidecar");

  auto request
    = BuildBaseImportRequest(source_path_for_request, settings.cooked_root,
      settings.job_name, settings.with_content_hashing, error_stream);
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
  return request;
}

} // namespace oxygen::content::import::internal
