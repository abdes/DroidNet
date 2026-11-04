//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <optional>
#include <ranges>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <nlohmann/json-schema.hpp>
#include <nlohmann/json.hpp>
#include <nlohmann/json_fwd.hpp>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Cooker/Import/ImportDiagnostics.h>
#include <Oxygen/Cooker/Import/Internal/CollisionShapeSource.h>
#include <Oxygen/Cooker/Import/Internal/ImportManifest_schema.h>
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

  auto GetCollisionShapeDescriptorValidator() -> json_validator&
  {
    static auto validator
      = MakeJsonSchemaValidator(json::parse(kCollisionShapeDescriptorSchema));
    return validator;
  }

  auto ValidateDescriptorSchema(std::vector<ImportDiagnostic>& diagnostics,
    const std::filesystem::path& source_path, const json& descriptor_doc)
    -> bool
  {
    const auto config = JsonSchemaValidationDiagnosticConfig {
      .validation_failed_code = "physics.shape.schema_validation_failed",
      .validation_failed_prefix
      = "Collision shape descriptor validation failed: ",
      .validator_failure_code = "physics.shape.schema_validator_failure",
      .validator_failure_prefix
      = "Collision shape descriptor schema validator failed: ",
    };

    return ValidateJsonSchemaWithDiagnostics(
      GetCollisionShapeDescriptorValidator(), descriptor_doc, config,
      [&](const std::string_view code, const std::string& message,
        const std::string& json_pointer) -> void {
        AddError(
          diagnostics, source_path, std::string(code), message, json_pointer);
      });
  }

  //! Reads `field`, throwing when the schema contract does not hold.
  template <typename T>
  [[nodiscard]] auto Require(const json& doc, const std::string_view field) -> T
  {
    return doc.at(field).get<T>();
  }

  //! Reads `field` when present; the extraction only proves the contract.
  template <typename T>
  auto CheckOptional(const json& doc, const std::string_view field) -> void
  {
    if (doc.contains(field)) {
      static_cast<void>(Require<T>(doc, field));
    }
  }

  [[nodiscard]] auto IsAnalyticShapeType(const std::string_view type) -> bool
  {
    return type == "sphere" || type == "capsule" || type == "box"
      || type == "cylinder" || type == "cone" || type == "plane"
      || type == "world_boundary";
  }

  [[nodiscard]] auto IsKnownShapeType(const std::string_view type) -> bool
  {
    return IsAnalyticShapeType(type) || type == "convex_hull"
      || type == "triangle_mesh" || type == "height_field"
      || type == "compound";
  }

  [[nodiscard]] auto LimitsInverted(const std::array<float, 3>& limits_min,
    const std::array<float, 3>& limits_max) -> bool
  {
    return std::ranges::any_of(
      std::views::iota(std::size_t { 0 }, limits_min.size()),
      [&](const std::size_t axis) -> bool {
        return limits_min.at(axis) > limits_max.at(axis);
      });
  }

  //! Extracts the parameters of an analytic shape the way the cook reads them,
  //! throwing when the schema contract does not hold.
  /*!
   @return False when the world boundary limits are inverted.
  */
  [[nodiscard]] auto CheckAnalyticParameters(
    const json& doc, const std::string_view type) -> bool
  {
    if (type == "sphere") {
      static_cast<void>(Require<float>(doc, "radius"));
    } else if (type == "capsule" || type == "cylinder" || type == "cone") {
      static_cast<void>(Require<float>(doc, "radius"));
      static_cast<void>(Require<float>(doc, "half_height"));
    } else if (type == "box") {
      static_cast<void>(Require<std::array<float, 3>>(doc, "half_extents"));
    } else if (type == "plane") {
      static_cast<void>(Require<std::array<float, 3>>(doc, "normal"));
      static_cast<void>(Require<float>(doc, "distance"));
    } else if (type == "world_boundary") {
      const auto mode = Require<std::string>(doc, "boundary_mode");
      if (mode != "aabb_clamp" && mode != "plane_set") {
        throw std::invalid_argument("unsupported boundary_mode: " + mode);
      }
      return !LimitsInverted(Require<std::array<float, 3>>(doc, "limits_min"),
        Require<std::array<float, 3>>(doc, "limits_max"));
    }
    return true;
  }

  //! Extracts the fields every shape states, throwing when the schema
  //! contract does not hold.
  auto CheckPlacementFields(const json& doc) -> void
  {
    static_cast<void>(Require<std::array<float, 3>>(doc, "local_position"));
    static_cast<void>(Require<std::array<float, 4>>(doc, "local_rotation"));
    static_cast<void>(Require<std::array<float, 3>>(doc, "local_scale"));
  }

  //! Checks one compound child; reports every refusal it finds.
  auto CheckCompoundChild(const json& child_doc,
    const json::json_pointer& child_path,
    std::vector<ImportDiagnostic>& diagnostics,
    const std::filesystem::path& source_path) -> bool
  {
    auto type = std::string {};
    try {
      type = Require<std::string>(child_doc, "shape_type");
      if (!IsKnownShapeType(type)) {
        throw std::invalid_argument("unsupported shape_type: " + type);
      }
      CheckPlacementFields(child_doc);
    } catch (const std::exception& ex) {
      AddError(diagnostics, source_path,
        "physics.shape.schema_contract_mismatch",
        std::string("Compound child extraction failed after schema "
                    "validation: ")
          + ex.what(),
        child_path.to_string());
      return false;
    }

    // The schema's child shape types are the analytic ones.
    DCHECK_F(IsAnalyticShapeType(type), "unexpected child shape: {}", type);

    try {
      if (!CheckAnalyticParameters(child_doc, type)) {
        AddError(diagnostics, source_path,
          "physics.shape.world_boundary_limits_invalid",
          "Compound child limits_min must be <= limits_max for each axis",
          (child_path / "limits_min").to_string());
        return false;
      }
    } catch (const std::exception& ex) {
      AddError(diagnostics, source_path,
        "physics.shape.schema_contract_mismatch",
        std::string("Compound child parameter extraction failed after schema "
                    "validation: ")
          + ex.what(),
        child_path.to_string());
      return false;
    }
    return true;
  }

  //! Checks every compound child, reporting all that are refused.
  auto CheckCompoundChildren(const json& descriptor_doc,
    std::vector<ImportDiagnostic>& diagnostics,
    const std::filesystem::path& source_path) -> bool
  {
    if (!descriptor_doc.contains("children")) {
      return true;
    }
    const auto& children = descriptor_doc.at("children");
    auto valid = true;
    for (std::size_t i = 0; i < children.size(); ++i) {
      const auto child_path = json::json_pointer {} / "children" / i;
      valid &= CheckCompoundChild(
        children.at(i), child_path, diagnostics, source_path);
    }
    return valid;
  }

  //! Checks what the schema cannot say about a schema-valid document.
  auto CheckShape(const json& descriptor_doc,
    std::vector<ImportDiagnostic>& diagnostics,
    const std::filesystem::path& source_path) -> bool
  {
    auto type = std::string {};
    auto limits_valid = true;
    try {
      type = Require<std::string>(descriptor_doc, "shape_type");
      if (!IsKnownShapeType(type)) {
        throw std::invalid_argument("unsupported shape_type: " + type);
      }
      CheckOptional<std::array<float, 3>>(descriptor_doc, "local_position");
      CheckOptional<std::array<float, 4>>(descriptor_doc, "local_rotation");
      CheckOptional<std::array<float, 3>>(descriptor_doc, "local_scale");
      CheckOptional<bool>(descriptor_doc, "is_sensor");
      CheckOptional<std::uint64_t>(descriptor_doc, "collision_own_layer");
      CheckOptional<std::uint64_t>(descriptor_doc, "collision_target_layers");
      limits_valid = CheckAnalyticParameters(descriptor_doc, type);
      static_cast<void>(Require<std::string>(descriptor_doc, "material_ref"));
    } catch (const std::exception& ex) {
      AddError(diagnostics, source_path,
        "physics.shape.schema_contract_mismatch",
        std::string("Schema validated descriptor failed extraction: ")
          + ex.what());
      return false;
    }

    if (!limits_valid) {
      AddError(diagnostics, source_path,
        "physics.shape.world_boundary_limits_invalid",
        "limits_min must be <= limits_max for each axis", "/limits_min");
      return false;
    }
    if (type == "compound") {
      return CheckCompoundChildren(descriptor_doc, diagnostics, source_path);
    }
    return true;
  }

} // namespace

auto PrepareCollisionShapeDescriptor(const std::string_view bytes,
  const std::filesystem::path& source_path,
  std::vector<ImportDiagnostic>& diagnostics) -> std::optional<json>
{
  auto document = json {};
  try {
    document = json::parse(bytes);
  } catch (const json::exception& failure) {
    AddError(diagnostics, source_path, "physics.shape.request_invalid_json",
      std::string("Descriptor payload is invalid JSON: ") + failure.what());
    return std::nullopt;
  }
  if (!document.is_object()) {
    AddError(diagnostics, source_path, "physics.shape.request_invalid_json",
      "Descriptor payload must be a JSON object");
    return std::nullopt;
  }
  if (!ValidateDescriptorSchema(diagnostics, source_path, document)) {
    return std::nullopt;
  }
  if (!CheckShape(document, diagnostics, source_path)) {
    return std::nullopt;
  }
  return document;
}

} // namespace oxygen::content::import::internal
