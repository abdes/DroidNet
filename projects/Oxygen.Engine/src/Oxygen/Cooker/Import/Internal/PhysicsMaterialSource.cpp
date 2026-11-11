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

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Cooker/Import/ImportCodes.h>
#include <Oxygen/Cooker/Import/ImportDiagnostics.h>
#include <Oxygen/Cooker/Import/Internal/ImportManifest_schema.h>
#include <Oxygen/Cooker/Import/Internal/PhysicsMaterialSource.h>
#include <Oxygen/Cooker/Import/Internal/Utils/CookedNumbers.h>
#include <Oxygen/Cooker/Import/Internal/Utils/JsonSchemaValidation.h>
#include <Oxygen/Data/PakFormat_physics.h>

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
      .validation_failed_code
      = import_codes::kPhysicsMaterialSchemaValidationFailed,
      .validation_failed_prefix
      = "Physics material descriptor validation failed: ",
      .validator_failure_code
      = import_codes::kPhysicsMaterialSchemaValidatorFailure,
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

  [[nodiscard]] auto ToCombineMode(const std::string_view value)
    -> std::optional<data::pak::physics::PhysicsCombineMode>
  {
    using data::pak::physics::PhysicsCombineMode;
    if (value == "average") {
      return PhysicsCombineMode::kAverage;
    }
    if (value == "min") {
      return PhysicsCombineMode::kMin;
    }
    if (value == "max") {
      return PhysicsCombineMode::kMax;
    }
    if (value == "multiply") {
      return PhysicsCombineMode::kMultiply;
    }
    return std::nullopt;
  }

  //! Reads the schema-valid `document`, reporting each value a float cannot
  //! hold and a density that is not positive once narrowed.
  [[nodiscard]] auto ReadMaterial(const json& document,
    std::vector<ImportDiagnostic>& diagnostics,
    const std::filesystem::path& source_path)
    -> std::optional<PhysicsMaterialSource>
  {
    auto out = PhysicsMaterialSource {
      .name = document.value("name", std::string {}),
    };
    auto valid = true;
    const auto read_float = [&](const std::string_view member,
                              std::optional<float>& target) -> void {
      const auto found = document.find(member);
      if (found == document.end()) {
        return;
      }
      const auto pointer = "/" + std::string(member);
      const auto cooked = ToCookedFloat(found->get<double>());
      if (!cooked.has_value()) {
        AddError(diagnostics, source_path,
          std::string(import_codes::kPhysicsMaterialValueOutOfRange),
          std::string(member) + " = " + found->dump()
            + " does not fit a 32-bit float",
          pointer);
        valid = false;
        return;
      }
      target = cooked;
    };
    read_float("static_friction", out.static_friction);
    read_float("dynamic_friction", out.dynamic_friction);
    read_float("restitution", out.restitution);
    read_float("density", out.density);
    if (out.density.has_value() && !(*out.density > 0.0F)) {
      AddError(diagnostics, source_path,
        std::string(import_codes::kPhysicsMaterialValueOutOfRange),
        "density = " + document.at("density").dump()
          + " is not positive as a 32-bit float",
        "/density");
      valid = false;
    }
    const auto read_mode =
      [&](const std::string_view member,
        std::optional<data::pak::physics::PhysicsCombineMode>& target) -> void {
      const auto found = document.find(member);
      if (found == document.end()) {
        return;
      }
      target = ToCombineMode(found->get<std::string>());
      DCHECK_F(target.has_value(), "the schema lists every combine mode");
    };
    read_mode("combine_mode_friction", out.combine_mode_friction);
    read_mode("combine_mode_restitution", out.combine_mode_restitution);
    if (!valid) {
      return std::nullopt;
    }
    return out;
  }

} // namespace

auto PreparePhysicsMaterialDescriptor(const std::string_view bytes,
  const std::filesystem::path& source_path,
  std::vector<ImportDiagnostic>& diagnostics)
  -> std::optional<PhysicsMaterialSource>
{
  auto document = json {};
  try {
    document = json::parse(bytes);
  } catch (const json::exception& failure) {
    AddError(diagnostics, source_path,
      std::string(import_codes::kPhysicsMaterialRequestInvalidJson),
      std::string("Descriptor payload is invalid JSON: ") + failure.what());
    return std::nullopt;
  }
  if (!document.is_object()) {
    AddError(diagnostics, source_path,
      std::string(import_codes::kPhysicsMaterialRequestInvalidJson),
      "Descriptor payload must be a JSON object");
    return std::nullopt;
  }
  if (!ValidateDescriptorSchema(diagnostics, source_path, document)) {
    return std::nullopt;
  }
  return ReadMaterial(document, diagnostics, source_path);
}

} // namespace oxygen::content::import::internal
