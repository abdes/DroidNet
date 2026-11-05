//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <nlohmann/json-schema.hpp>
#include <nlohmann/json.hpp>
#include <nlohmann/json_fwd.hpp>

#include <Oxygen/Cooker/Import/ImportCodes.h>
#include <Oxygen/Cooker/Import/ImportDiagnostics.h>
#include <Oxygen/Cooker/Import/Internal/ImportManifest_schema.h>
#include <Oxygen/Cooker/Import/Internal/MaterialSource.h>
#include <Oxygen/Cooker/Import/Internal/Utils/JsonSchemaValidation.h>
#include <Oxygen/Core/Types/ShaderRequestRules.h>
#include <Oxygen/Core/Types/ShaderType.h>
#include <Oxygen/Data/MaterialDomain.h>
#include <Oxygen/Data/PakFormat_render.h>

namespace oxygen::content::import {
namespace {
  using nlohmann::json_schema::json_validator;
  constexpr std::size_t kMaxShaderStages = 32;

  void AddError(std::vector<ImportDiagnostic>& diagnostics,
    const std::string_view source_id, std::string code, std::string message,
    std::string object_path = {}, std::string json_pointer = {})
  {
    diagnostics.push_back(ImportDiagnostic {
      .severity = ImportSeverity::kError,
      .code = std::move(code),
      .message = std::move(message),
      .source_path = std::string(source_id),
      .object_path = std::move(object_path),
      .json_pointer = std::move(json_pointer),
    });
  }

  constexpr std::array kTextureSlots {
    MaterialSource::TextureSlot {
      .name = "base_color",
      .binding = &MaterialTextureBindings::base_color,
    },
    MaterialSource::TextureSlot {
      .name = "normal",
      .binding = &MaterialTextureBindings::normal,
    },
    MaterialSource::TextureSlot {
      .name = "metallic",
      .binding = &MaterialTextureBindings::metallic,
    },
    MaterialSource::TextureSlot {
      .name = "roughness",
      .binding = &MaterialTextureBindings::roughness,
    },
    MaterialSource::TextureSlot {
      .name = "ambient_occlusion",
      .binding = &MaterialTextureBindings::ambient_occlusion,
    },
    MaterialSource::TextureSlot {
      .name = "emissive",
      .binding = &MaterialTextureBindings::emissive,
    },
    MaterialSource::TextureSlot {
      .name = "specular",
      .binding = &MaterialTextureBindings::specular,
    },
    MaterialSource::TextureSlot {
      .name = "sheen_color",
      .binding = &MaterialTextureBindings::sheen_color,
    },
    MaterialSource::TextureSlot {
      .name = "clearcoat",
      .binding = &MaterialTextureBindings::clearcoat,
    },
    MaterialSource::TextureSlot {
      .name = "clearcoat_normal",
      .binding = &MaterialTextureBindings::clearcoat_normal,
    },
    MaterialSource::TextureSlot {
      .name = "transmission",
      .binding = &MaterialTextureBindings::transmission,
    },
    MaterialSource::TextureSlot {
      .name = "thickness",
      .binding = &MaterialTextureBindings::thickness,
    },
  };

  auto GetMaterialDescriptorValidator() -> json_validator&
  {
    static auto validator = internal::MakeJsonSchemaValidator(
      nlohmann::json::parse(kMaterialDescriptorSchema));
    return validator;
  }

  auto ValidateDescriptorSchema(std::vector<ImportDiagnostic>& diagnostics,
    const std::filesystem::path& source_path,
    const nlohmann::json& descriptor_doc) -> bool
  {
    const auto config = internal::JsonSchemaValidationDiagnosticConfig {
      .validation_failed_code
      = import_codes::kMaterialDescriptorSchemaValidationFailed,
      .validation_failed_prefix = "Material descriptor validation failed: ",
      .validator_failure_code
      = import_codes::kMaterialDescriptorSchemaValidatorFailure,
      .validator_failure_prefix
      = "Material descriptor schema validator failed: ",
    };

    return internal::ValidateJsonSchemaWithDiagnostics(
      GetMaterialDescriptorValidator(), descriptor_doc, config,
      [&](const std::string_view code, const std::string& message,
        const std::string& json_pointer) -> void {
        AddError(diagnostics, source_path.string(), std::string(code), message,
          {}, json_pointer);
      });
  }

  auto ParseMaterialDomain(const std::string_view domain)
    -> std::optional<data::MaterialDomain>
  {
    using data::MaterialDomain;
    if (domain == "opaque") {
      return MaterialDomain::kOpaque;
    }
    if (domain == "alpha_blended") {
      return MaterialDomain::kAlphaBlended;
    }
    if (domain == "masked") {
      return MaterialDomain::kMasked;
    }
    if (domain == "decal") {
      return MaterialDomain::kDecal;
    }
    if (domain == "ui") {
      return MaterialDomain::kUserInterface;
    }
    if (domain == "post_process") {
      return MaterialDomain::kPostProcess;
    }
    return std::nullopt;
  }

  auto ParseAlphaMode(const std::string_view alpha_mode)
    -> std::optional<MaterialAlphaMode>
  {
    if (alpha_mode == "opaque") {
      return MaterialAlphaMode::kOpaque;
    }
    if (alpha_mode == "masked") {
      return MaterialAlphaMode::kMasked;
    }
    if (alpha_mode == "blended") {
      return MaterialAlphaMode::kBlended;
    }
    return std::nullopt;
  }

  auto ParseOrmPolicy(const std::string_view policy) -> std::optional<OrmPolicy>
  {
    if (policy == "auto") {
      return OrmPolicy::kAuto;
    }
    if (policy == "force_packed") {
      return OrmPolicy::kForcePacked;
    }
    if (policy == "force_separate") {
      return OrmPolicy::kForceSeparate;
    }
    return std::nullopt;
  }

  auto ParseOcclusionMode(const std::string_view mode)
    -> std::optional<AmbientOcclusionMode>
  {
    if (mode == "factor") {
      return AmbientOcclusionMode::kFactor;
    }
    if (mode == "strength") {
      return AmbientOcclusionMode::kStrength;
    }
    return std::nullopt;
  }

  auto ParseShaderType(const std::string_view stage) -> std::optional<uint8_t>
  {
    using oxygen::ShaderType;
    if (stage == "amplification") {
      return static_cast<uint8_t>(ShaderType::kAmplification);
    }
    if (stage == "mesh") {
      return static_cast<uint8_t>(ShaderType::kMesh);
    }
    if (stage == "vertex") {
      return static_cast<uint8_t>(ShaderType::kVertex);
    }
    if (stage == "hull") {
      return static_cast<uint8_t>(ShaderType::kHull);
    }
    if (stage == "domain") {
      return static_cast<uint8_t>(ShaderType::kDomain);
    }
    if (stage == "geometry") {
      return static_cast<uint8_t>(ShaderType::kGeometry);
    }
    if (stage == "pixel") {
      return static_cast<uint8_t>(ShaderType::kPixel);
    }
    if (stage == "compute") {
      return static_cast<uint8_t>(ShaderType::kCompute);
    }
    if (stage == "ray_gen") {
      return static_cast<uint8_t>(ShaderType::kRayGen);
    }
    if (stage == "intersection") {
      return static_cast<uint8_t>(ShaderType::kIntersection);
    }
    if (stage == "any_hit") {
      return static_cast<uint8_t>(ShaderType::kAnyHit);
    }
    if (stage == "closest_hit") {
      return static_cast<uint8_t>(ShaderType::kClosestHit);
    }
    if (stage == "miss") {
      return static_cast<uint8_t>(ShaderType::kMiss);
    }
    if (stage == "callable") {
      return static_cast<uint8_t>(ShaderType::kCallable);
    }
    return std::nullopt;
  }

  template <size_t N>
  auto ParseFloatArray(const nlohmann::json& value, std::array<float, N>& out)
    -> bool
  {
    if (!value.is_array() || value.size() != N) {
      return false;
    }
    for (size_t i = 0; i < N; ++i) {
      if (!value.at(i).is_number()) {
        return false;
      }
      out[i] = value.at(i).get<float>();
    }
    return true;
  }

  auto ApplyMaterialInputsFromJson(
    const nlohmann::json& doc, MaterialInputs& inputs) -> bool
  {
    if (!doc.is_object()) {
      return false;
    }

    if (doc.contains("base_color")
      && !ParseFloatArray(doc.at("base_color"), inputs.base_color)) {
      return false;
    }
    if (doc.contains("normal_scale")) {
      inputs.normal_scale = doc.at("normal_scale").get<float>();
    }
    if (doc.contains("metalness")) {
      inputs.metalness = doc.at("metalness").get<float>();
    }
    if (doc.contains("roughness")) {
      inputs.roughness = doc.at("roughness").get<float>();
    }
    if (doc.contains("ambient_occlusion")) {
      inputs.ambient_occlusion = doc.at("ambient_occlusion").get<float>();
    }
    std::array emissive_color { 1.0F, 1.0F, 1.0F };
    if (doc.contains("emissive_color")
      && !ParseFloatArray(doc.at("emissive_color"), emissive_color)) {
      return false;
    }
    const auto emissive_intensity = doc.value("emissive_intensity", 0.0F);
    for (size_t channel = 0; channel < std::size(emissive_color); ++channel) {
      inputs.emissive_factor[channel]
        = emissive_color[channel] * emissive_intensity;
    }
    if (doc.contains("alpha_cutoff")) {
      inputs.alpha_cutoff = doc.at("alpha_cutoff").get<float>();
    }
    if (doc.contains("ior")) {
      inputs.ior = doc.at("ior").get<float>();
    }
    if (doc.contains("specular_factor")) {
      inputs.specular_factor = doc.at("specular_factor").get<float>();
    }
    if (doc.contains("sheen_color_factor")
      && !ParseFloatArray(
        doc.at("sheen_color_factor"), inputs.sheen_color_factor)) {
      return false;
    }
    if (doc.contains("clearcoat_factor")) {
      inputs.clearcoat_factor = doc.at("clearcoat_factor").get<float>();
    }
    if (doc.contains("clearcoat_roughness")) {
      inputs.clearcoat_roughness = doc.at("clearcoat_roughness").get<float>();
    }
    if (doc.contains("transmission_factor")) {
      inputs.transmission_factor = doc.at("transmission_factor").get<float>();
    }
    if (doc.contains("thickness_factor")) {
      inputs.thickness_factor = doc.at("thickness_factor").get<float>();
    }
    if (doc.contains("attenuation_color")
      && !ParseFloatArray(
        doc.at("attenuation_color"), inputs.attenuation_color)) {
      return false;
    }
    if (doc.contains("attenuation_distance")) {
      inputs.attenuation_distance = doc.at("attenuation_distance").get<float>();
    }
    if (doc.contains("double_sided")) {
      inputs.double_sided = doc.at("double_sided").get<bool>();
    }
    if (doc.contains("unlit")) {
      inputs.unlit = doc.at("unlit").get<bool>();
    }
    if (doc.contains("roughness_as_glossiness")) {
      inputs.roughness_as_glossiness
        = doc.at("roughness_as_glossiness").get<bool>();
    }

    return true;
  }

  auto ParseProceduralGrid(const nlohmann::json& doc)
    -> std::optional<MaterialProceduralGrid>
  {
    auto grid = MaterialProceduralGrid {};
    const auto colors_read = [&] -> bool {
      const auto read = [&doc](const char* key, std::array<float, 4>& color) {
        return !doc.contains(key) || ParseFloatArray(doc.at(key), color);
      };
      return read("minor_color", grid.minor_color)
        && read("major_color", grid.major_color)
        && read("axis_color_x", grid.axis_color_x)
        && read("axis_color_y", grid.axis_color_y)
        && read("origin_color", grid.origin_color);
    };
    if ((doc.contains("spacing")
          && !ParseFloatArray(doc.at("spacing"), grid.spacing))
      || !colors_read()) {
      return std::nullopt;
    }
    grid.major_every = doc.value("major_every", grid.major_every);
    grid.line_thickness = doc.value("line_thickness", grid.line_thickness);
    grid.major_thickness = doc.value("major_thickness", grid.major_thickness);
    grid.axis_thickness = doc.value("axis_thickness", grid.axis_thickness);
    grid.fade_start = doc.value("fade_start", grid.fade_start);
    grid.fade_end = doc.value("fade_end", grid.fade_end);
    return grid;
  }

  //! Replaces each value of `values` that is not finite by that of `defaults`;
  //! returns whether there was one.
  auto ReplaceNonFinite(const std::span<float> values,
    const std::span<const float> defaults) -> bool
  {
    auto replaced = false;
    for (std::size_t at = 0; at < values.size(); ++at) {
      if (!std::isfinite(values[at])) {
        values[at] = defaults[at];
        replaced = true;
      }
    }
    return replaced;
  }

  //! Replaces what is not finite in `material`'s inputs and UV transforms by
  //! its default; returns whether there was any. Emission has its own range.
  auto ReplaceNonFiniteInputs(MaterialSource& material) -> bool
  {
    static constexpr auto kScalars = std::array {
      &MaterialInputs::normal_scale,
      &MaterialInputs::metalness,
      &MaterialInputs::roughness,
      &MaterialInputs::ambient_occlusion,
      &MaterialInputs::alpha_cutoff,
      &MaterialInputs::ior,
      &MaterialInputs::specular_factor,
      &MaterialInputs::clearcoat_factor,
      &MaterialInputs::clearcoat_roughness,
      &MaterialInputs::transmission_factor,
      &MaterialInputs::thickness_factor,
      &MaterialInputs::attenuation_distance,
    };
    auto& inputs = material.inputs;
    const auto defaults = MaterialInputs {};
    auto replaced = false;
    for (const auto scalar : kScalars) {
      replaced = ReplaceNonFinite(std::span { &(inputs.*scalar), 1U },
                   std::span { &(defaults.*scalar), 1U })
        || replaced;
    }
    replaced
      = ReplaceNonFinite(inputs.base_color, defaults.base_color) || replaced;
    replaced
      = ReplaceNonFinite(inputs.sheen_color_factor, defaults.sheen_color_factor)
      || replaced;
    replaced
      = ReplaceNonFinite(inputs.attenuation_color, defaults.attenuation_color)
      || replaced;
    const auto identity = MaterialUvTransform {};
    for (const auto& slot : MaterialSource::TextureSlots()) {
      auto& transform = (material.textures.*slot.binding).uv_transform;
      replaced = ReplaceNonFinite(transform.scale, identity.scale) || replaced;
      replaced
        = ReplaceNonFinite(transform.offset, identity.offset) || replaced;
      replaced = ReplaceNonFinite(std::span { &transform.rotation_radians, 1U },
                   std::span { &identity.rotation_radians, 1U })
        || replaced;
    }
    return replaced;
  }

  [[nodiscard]] auto SameUvs(const MaterialTextureBinding& first,
    const MaterialTextureBinding& second) noexcept -> bool
  {
    return first.uv_set == second.uv_set
      && std::ranges::equal(first.uv_transform.scale, second.uv_transform.scale)
      && std::ranges::equal(
        first.uv_transform.offset, second.uv_transform.offset)
      && first.uv_transform.rotation_radians
      == second.uv_transform.rotation_radians;
  }

  //! Whether `grid` is one the renderer can draw: finite, with a spacing, and
  //! a fade that does not end before it starts.
  [[nodiscard]] auto IsDrawableGrid(const MaterialProceduralGrid& grid) -> bool
  {
    const auto finite = [](const std::span<const float> values) -> bool {
      return std::ranges::all_of(
        values, [](const float value) -> bool { return std::isfinite(value); });
    };
    const auto lengths = std::array {
      grid.line_thickness,
      grid.major_thickness,
      grid.axis_thickness,
      grid.fade_start,
      grid.fade_end,
    };
    const auto spaced = finite(grid.spacing) && grid.spacing[0] > 0.0F
      && grid.spacing[1] > 0.0F;
    const auto lengths_valid = finite(lengths)
      && std::ranges::all_of(
        lengths, [](const float value) -> bool { return value >= 0.0F; });
    const auto colors_finite = finite(grid.minor_color)
      && finite(grid.major_color) && finite(grid.axis_color_x)
      && finite(grid.axis_color_y) && finite(grid.origin_color);
    return spaced && lengths_valid && colors_finite && grid.major_every > 0U
      && grid.fade_end >= grid.fade_start;
  }

} // namespace

auto MaterialSource::TextureSlots() noexcept -> std::span<const TextureSlot>
{
  return kTextureSlots;
}

auto MaterialSource::CanPackOrm() const noexcept -> bool
{
  const auto& metallic = textures.metallic;
  const auto& roughness = textures.roughness;
  return metallic.assigned && roughness.assigned && !metallic.source_id.empty()
    && metallic.source_id == roughness.source_id
    && SameUvs(metallic, roughness);
}

auto MaterialSource::ReportDroppedUvs(const std::string_view source_id,
  std::vector<ImportDiagnostic>& diagnostics) const -> void
{
  const MaterialTextureBinding* first = nullptr;
  auto second_set = false;
  auto other_uvs = std::string {};
  for (const auto& slot : TextureSlots()) {
    const auto& binding = textures.*slot.binding;
    if (!binding.assigned) {
      continue;
    }
    second_set = second_set || binding.uv_set != 0U;
    if (first == nullptr) {
      first = &binding;
    } else if (!SameUvs(*first, binding)) {
      other_uvs += (other_uvs.empty() ? "" : ", ") + std::string(slot.name);
    }
  }
  const auto warn = [&](std::string message) -> void {
    diagnostics.push_back(ImportDiagnostic {
      .severity = ImportSeverity::kWarning,
      .code = std::string(import_codes::kMaterialUvDropped),
      .message = std::move(message),
      .source_path = std::string(source_id),
      .object_path = name,
    });
  };
  if (!other_uvs.empty()) {
    warn("A material has one UV transform, its first texture's; these use it "
         "instead of their own: "
      + other_uvs);
  }
  if (second_set) {
    warn("Vertices have one UV set; textures bound to another use it instead");
  }
}

auto MaterialSource::RepairModelInputs(const std::string_view source_id,
  std::vector<ImportDiagnostic>& diagnostics) -> void
{
  auto repaired = false;
  for (auto& value : inputs.emissive_factor) {
    const auto clamped = std::isfinite(value)
      ? std::clamp(value, 0.0F,
          static_cast<float>(data::pak::render::kMaxMaterialEmissiveFactor))
      : 0.0F;
    repaired = repaired || clamped != value;
    value = clamped;
  }
  if (repaired) {
    diagnostics.push_back(ImportDiagnostic {
      .severity = ImportSeverity::kWarning,
      .code = std::string(import_codes::kMaterialEmissiveFactorRange),
      .message = "Emission channels outside [0,65504] were clamped",
      .source_path = std::string(source_id),
      .object_path = name,
    });
  }
  if (ReplaceNonFiniteInputs(*this)) {
    diagnostics.push_back(ImportDiagnostic {
      .severity = ImportSeverity::kWarning,
      .code = std::string(import_codes::kMaterialInputNotFinite),
      .message = "Material values that are NaN or infinite take their defaults",
      .source_path = std::string(source_id),
      .object_path = name,
    });
  }
}

auto MaterialSource::Validate(const std::string_view source_id,
  std::vector<ImportDiagnostic>& diagnostics) const -> bool
{
  auto valid = true;
  const auto error = [&](std::string code, std::string message) -> void {
    AddError(diagnostics, source_id, std::move(code), std::move(message), name);
    valid = false;
  };
  if (std::ranges::any_of(inputs.emissive_factor, [](float value) -> bool {
        return !std::isfinite(value) || value < 0.0F
          || value > data::pak::render::kMaxMaterialEmissiveFactor;
      })) {
    error(std::string(import_codes::kMaterialEmissiveFactorRange),
      "Emission must contain finite channels in [0,65504]");
  }
  if (auto repaired = *this; ReplaceNonFiniteInputs(repaired)) {
    error(std::string(import_codes::kMaterialInputNotFinite),
      "Material values must be finite");
  }
  if (procedural_grid.has_value() && !IsDrawableGrid(*procedural_grid)) {
    error(std::string(import_codes::kMaterialGridInvalid),
      "A procedural grid needs a spacing above zero, at least one minor cell "
      "per major line, finite values, and a fade that ends after it starts");
  }
  if (orm_policy == OrmPolicy::kForcePacked && !CanPackOrm()) {
    error(std::string(import_codes::kMaterialOrmPolicy),
      "ForcePacked requires metallic/roughness to share source and UV");
  }
  if (shader_requests.size() > kMaxShaderStages) {
    error(std::string(import_codes::kMaterialShaderStageCount),
      "Shader stage count exceeds 32");
    return false;
  }
  std::array<bool, kMaxShaderStages> seen {};
  for (const auto& shader : shader_requests) {
    if (!IsShaderStage(shader.shader_type)) {
      error(std::string(import_codes::kMaterialShaderStageInvalid),
        "Shader type is invalid");
      continue;
    }
    const auto index = static_cast<std::size_t>(shader.shader_type - 1U);
    if (seen.at(index)) {
      error(std::string(import_codes::kMaterialShaderStageDuplicate),
        "Shader type is duplicated in request list");
      continue;
    }
    // The runtime reads only canonical paths and defines, and identifier
    // entry points.
    if (CanonicalShaderSourcePath(shader.source_path) != shader.source_path) {
      error(std::string(import_codes::kMaterialShaderRefInvalid),
        "Shader source_path must be a canonical path within the shaders "
        "directory");
      continue;
    }
    if (!IsShaderEntryPoint(shader.entry_point)) {
      error(std::string(import_codes::kMaterialShaderRefInvalid),
        "Shader entry_point must be an identifier");
      continue;
    }
    if (CanonicalShaderDefines(shader.defines) != shader.defines) {
      error(std::string(import_codes::kMaterialShaderRefInvalid),
        "Shader defines must be sorted, unique NAME or NAME=VALUE entries "
        "separated by commas");
      continue;
    }
    seen.at(index) = true;
  }
  return valid;
}

auto MaterialSource::FromDescriptor(const std::string_view bytes,
  const std::filesystem::path& source_path,
  const std::string_view name_override,
  std::vector<ImportDiagnostic>& diagnostics) -> std::optional<MaterialSource>
{
  nlohmann::json document;
  try {
    document = nlohmann::json::parse(bytes);
  } catch (const nlohmann::json::exception& failure) {
    AddError(diagnostics, source_path.string(),
      std::string(import_codes::kMaterialDescriptorRequestInvalidJson),
      failure.what());
    return std::nullopt;
  }
  if (!document.is_object()) {
    AddError(diagnostics, source_path.string(),
      std::string(import_codes::kMaterialDescriptorRequestInvalidJson),
      "Material descriptor must be a JSON object");
    return std::nullopt;
  }
  if (!ValidateDescriptorSchema(diagnostics, source_path, document)) {
    return std::nullopt;
  }
  MaterialSource material;
  material.name = !name_override.empty()
    ? std::string(name_override)
    : document.value("name", source_path.stem().string());
  if (material.name.empty()) {
    material.name = "Material";
  }
  material.storage_name = material.name;
  if (document.contains("domain")) {
    const auto value
      = ParseMaterialDomain(document.at("domain").get<std::string>());
    if (!value) {
      AddError(diagnostics, source_path.string(),
        std::string(import_codes::kMaterialDescriptorDomainInvalid),
        "Material domain is invalid");
      return std::nullopt;
    }
    material.domain = *value;
  }
  if (document.contains("alpha_mode")) {
    const auto value
      = ParseAlphaMode(document.at("alpha_mode").get<std::string>());
    if (!value) {
      AddError(diagnostics, source_path.string(),
        std::string(import_codes::kMaterialDescriptorAlphaModeInvalid),
        "Material alpha mode is invalid");
      return std::nullopt;
    }
    material.alpha_mode = *value;
  } else if (material.domain == data::MaterialDomain::kMasked) {
    material.alpha_mode = MaterialAlphaMode::kMasked;
  } else if (material.domain == data::MaterialDomain::kAlphaBlended) {
    material.alpha_mode = MaterialAlphaMode::kBlended;
  }
  if (document.contains("orm_policy")) {
    const auto value
      = ParseOrmPolicy(document.at("orm_policy").get<std::string>());
    if (!value) {
      AddError(diagnostics, source_path.string(),
        std::string(import_codes::kMaterialDescriptorOrmPolicyInvalid),
        "Material ORM policy is invalid");
      return std::nullopt;
    }
    material.orm_policy = *value;
  }
  if (document.contains("ambient_occlusion_mode")) {
    const auto value = ParseOcclusionMode(
      document.at("ambient_occlusion_mode").get<std::string>());
    if (!value) {
      AddError(diagnostics, source_path.string(),
        std::string(import_codes::kMaterialDescriptorParametersInvalid),
        "Material ambient occlusion mode is invalid");
      return std::nullopt;
    }
    material.occlusion_mode = *value;
  }
  if (document.contains("procedural_grid")) {
    material.procedural_grid
      = ParseProceduralGrid(document.at("procedural_grid"));
    if (!material.procedural_grid.has_value()) {
      AddError(diagnostics, source_path.string(),
        std::string(import_codes::kMaterialDescriptorParametersInvalid),
        "Material procedural grid has an invalid shape");
      return std::nullopt;
    }
  }
  if (document.contains("parameters")
    && !ApplyMaterialInputsFromJson(
      document.at("parameters"), material.inputs)) {
    AddError(diagnostics, source_path.string(),
      std::string(import_codes::kMaterialDescriptorParametersInvalid),
      "Material parameters payload has invalid shape");
    return std::nullopt;
  }
  if (document.contains("textures")) {
    const auto& textures = document.at("textures");
    for (const auto& slot : TextureSlots()) {
      if (!textures.contains(slot.name)) {
        continue;
      }
      const auto& source = textures.at(slot.name);
      auto& binding = material.textures.*slot.binding;
      binding.assigned = true;
      binding.source_id = source.at("virtual_path").get<std::string>();
      binding.uv_set = source.value("uv_set", uint8_t { 0 });
      if (source.contains("uv_transform")) {
        const auto& transform = source.at("uv_transform");
        if ((transform.contains("scale")
              && !ParseFloatArray(
                transform.at("scale"), binding.uv_transform.scale))
          || (transform.contains("offset")
            && !ParseFloatArray(
              transform.at("offset"), binding.uv_transform.offset))) {
          AddError(diagnostics, source_path.string(),
            std::string(import_codes::kMaterialDescriptorTextureUvInvalid),
            "Texture UV transform must contain two-component scale and offset",
            {},
            (nlohmann::json::json_pointer {} / "textures"
              / std::string(slot.name))
              .to_string());
          return std::nullopt;
        }
        binding.uv_transform.rotation_radians
          = transform.value("rotation_radians", 0.0F);
      }
    }
  }
  if (document.contains("shaders")) {
    for (const auto& shader : document.at("shaders")) {
      const auto stage = ParseShaderType(shader.at("stage").get<std::string>());
      if (!stage) {
        AddError(diagnostics, source_path.string(),
          std::string(import_codes::kMaterialDescriptorShaderStageInvalid),
          "Shader stage is not recognized");
        return std::nullopt;
      }
      // An authored path and defines are cooked in their canonical forms,
      // when they have them; Validate refuses those without.
      const auto shader_path = shader.at("source_path").get<std::string>();
      const auto defines = shader.value("defines", std::string {});
      material.shader_requests.push_back(ShaderRequest {
        .shader_type = *stage,
        .source_path
        = CanonicalShaderSourcePath(shader_path).value_or(shader_path),
        .entry_point = shader.at("entry_point").get<std::string>(),
        .defines = CanonicalShaderDefines(defines).value_or(defines),
        .shader_hash = shader.value("shader_hash", uint64_t { 0 }),
      });
    }
  }
  if (!material.Validate(source_path.string(), diagnostics)) {
    return std::nullopt;
  }
  return material;
}

} // namespace oxygen::content::import
