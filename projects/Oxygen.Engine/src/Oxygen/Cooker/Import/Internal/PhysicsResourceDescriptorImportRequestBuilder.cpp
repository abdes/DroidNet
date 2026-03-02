//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <filesystem>
#include <nlohmann/json-schema.hpp>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <string_view>

#include <Oxygen/Cooker/Import/Internal/ImportManifest_schema.h>
#include <Oxygen/Cooker/Import/Internal/Utils/DescriptorDocument.h>
#include <Oxygen/Cooker/Import/Internal/Utils/JsonSchemaValidation.h>
#include <Oxygen/Cooker/Import/PhysicsResourceDescriptorImportRequestBuilder.h>

namespace oxygen::content::import::internal {

namespace {

  using nlohmann::json;
  using nlohmann::json_schema::json_validator;

  auto GetPhysicsResourceDescriptorValidator() -> json_validator&
  {
    static auto validator = []() {
      auto out = json_validator {};
      out.set_root_schema(json::parse(kPhysicsResourceDescriptorSchema));
      return out;
    }();
    return validator;
  }

  auto ValidateDescriptorSchema(
    const json& descriptor_doc, std::ostream& error_stream) -> bool
  {
    const auto config = JsonSchemaValidationDiagnosticConfig {
      .validation_failed_code
      = "physics.resource.descriptor.schema_validation_failed",
      .validation_failed_prefix
      = "Physics resource descriptor validation failed: ",
      .validation_overflow_prefix
      = "Physics resource descriptor validation emitted ",
      .validator_failure_code
      = "physics.resource.descriptor.schema_validator_failure",
      .validator_failure_prefix
      = "Physics resource descriptor schema validator failed: ",
      .max_issues = 12,
    };

    return ValidateJsonSchemaWithDiagnostics(
      GetPhysicsResourceDescriptorValidator(), descriptor_doc, config,
      [&](const std::string_view code, const std::string& message,
        const std::string& object_path) {
        error_stream << "ERROR [" << code << "]: " << message;
        if (!object_path.empty()) {
          error_stream << " (" << object_path << ")";
        }
        error_stream << "\n";
      });
  }

} // namespace

auto BuildPhysicsResourceDescriptorRequest(
  const PhysicsResourceDescriptorImportSettings& settings,
  std::ostream& error_stream) -> std::optional<ImportRequest>
{
  if (settings.descriptor_path.empty()) {
    error_stream << "ERROR: descriptor_path is required\n";
    return std::nullopt;
  }

  const auto descriptor_path
    = std::filesystem::path(settings.descriptor_path).lexically_normal();
  const auto descriptor_doc = LoadDescriptorJsonObject(
    descriptor_path, "physics resource descriptor", error_stream);
  if (!descriptor_doc.has_value()) {
    return std::nullopt;
  }

  if (!ValidateDescriptorSchema(*descriptor_doc, error_stream)) {
    return std::nullopt;
  }

  auto request = ImportRequest {};
  request.source_path = descriptor_path;

  if (settings.cooked_root.empty()) {
    error_stream << "ERROR: --output or --cooked-root is required\n";
    return std::nullopt;
  }

  auto cooked_root = std::filesystem::path(settings.cooked_root);
  if (!cooked_root.is_absolute()) {
    error_stream << "ERROR: cooked root must be an absolute path\n";
    return std::nullopt;
  }
  request.cooked_root = std::move(cooked_root);

  if (!settings.job_name.empty()) {
    request.job_name = settings.job_name;
  } else if (descriptor_doc->contains("name")) {
    request.job_name = descriptor_doc->at("name").get<std::string>();
  } else {
    const auto stem = descriptor_path.stem().string();
    if (!stem.empty()) {
      request.job_name = stem;
    }
  }

  auto with_content_hashing = settings.with_content_hashing;
  if (descriptor_doc->contains("content_hashing")) {
    with_content_hashing = descriptor_doc->at("content_hashing").get<bool>();
  }
  request.options.with_content_hashing
    = EffectiveContentHashingEnabled(with_content_hashing);

  request.physics_resource_descriptor
    = ImportRequest::PhysicsResourceDescriptorPayload {
        .normalized_descriptor_json = descriptor_doc->dump(),
      };

  return request;
}

} // namespace oxygen::content::import::internal
