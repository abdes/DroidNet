//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <nlohmann/json-schema.hpp>
#include <nlohmann/json.hpp>
#include <nlohmann/json_fwd.hpp>

#include <Oxygen/Cooker/Import/ImportDiagnostics.h>
#include <Oxygen/Cooker/Import/Internal/ImportManifest_schema.h>
#include <Oxygen/Cooker/Import/Internal/PhysicsMaterialSource.h>
#include <Oxygen/Cooker/Import/Internal/Utils/JsonSchemaValidation.h>

namespace oxygen::content::import::internal {

namespace {

  using nlohmann::json;
  using nlohmann::json_schema::json_validator;

  auto AddError(std::vector<ImportDiagnostic>& diagnostics,
    const std::filesystem::path& source_path, std::string code,
    std::string message, std::string json_pointer = {}) -> void
  {
    diagnostics.push_back(ImportDiagnostic {
      .severity = ImportSeverity::kError,
      .code = std::move(code),
      .message = std::move(message),
      .source_path = source_path.string(),
      .json_pointer = std::move(json_pointer),
    });
  }

  auto GetPhysicsMaterialDescriptorValidator() -> json_validator&
  {
    static auto validator
      = MakeJsonSchemaValidator(json::parse(kPhysicsMaterialDescriptorSchema));
    return validator;
  }

  auto ValidateDescriptorSchema(std::vector<ImportDiagnostic>& diagnostics,
    const std::filesystem::path& source_path, const json& descriptor_doc)
    -> bool
  {
    const auto config = JsonSchemaValidationDiagnosticConfig {
      .validation_failed_code = "physics.material.schema_validation_failed",
      .validation_failed_prefix
      = "Physics material descriptor validation failed: ",
      .validator_failure_code = "physics.material.schema_validator_failure",
      .validator_failure_prefix
      = "Physics material descriptor schema validator failed: ",
    };

    return ValidateJsonSchemaWithDiagnostics(
      GetPhysicsMaterialDescriptorValidator(), descriptor_doc, config,
      [&](const std::string_view code, const std::string& message,
        const std::string& json_pointer) -> void {
        AddError(
          diagnostics, source_path, std::string(code), message, json_pointer);
      });
  }

} // namespace

auto PreparePhysicsMaterialDescriptor(const std::string_view bytes,
  const std::filesystem::path& source_path,
  std::vector<ImportDiagnostic>& diagnostics) -> std::optional<json>
{
  auto document = json {};
  try {
    document = json::parse(bytes);
  } catch (const json::exception& failure) {
    AddError(diagnostics, source_path, "physics.material.request_invalid_json",
      std::string("Descriptor payload is invalid JSON: ") + failure.what());
    return std::nullopt;
  }
  if (!document.is_object()) {
    AddError(diagnostics, source_path, "physics.material.request_invalid_json",
      "Descriptor payload must be a JSON object");
    return std::nullopt;
  }
  if (!ValidateDescriptorSchema(diagnostics, source_path, document)) {
    return std::nullopt;
  }
  return document;
}

} // namespace oxygen::content::import::internal
