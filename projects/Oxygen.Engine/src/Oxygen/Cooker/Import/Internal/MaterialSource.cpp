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

#include <Oxygen/Cooker/Import/ImportDiagnostics.h>
#include <Oxygen/Cooker/Import/Internal/ImportManifest_schema.h>
#include <Oxygen/Cooker/Import/Internal/MaterialSource.h>
#include <Oxygen/Cooker/Import/Internal/Utils/JsonSchemaValidation.h>
#include <Oxygen/Core/Types/ShaderType.h>
#include <Oxygen/Data/MaterialDomain.h>
#include <Oxygen/Data/PakFormat_render.h>

namespace oxygen::content::import {
namespace {
  using nlohmann::json_schema::json_validator;
  constexpr std::size_t kMaxShaderStages = 32;

  void AddError(std::vector<ImportDiagnostic>& diagnostics,
    const std::string_view source_id, std::string code, std::string message,
    std::string object_path = {})
  {
    diagnostics.push_back(ImportDiagnostic {
      .severity = ImportSeverity::kError,
      .code = std::move(code),
      .message = std::move(message),
      .source_path = std::string(source_id),
      .object_path = std::move(object_path),
    });
  }

  constexpr std::array kTextureSlots {
    MaterialSource::TextureSlot {
      "base_color",
      &MaterialTextureBindings::base_color,
    },
    MaterialSource::TextureSlot { "normal", &MaterialTextureBindings::normal },
    MaterialSource::TextureSlot {
      "metallic", &MaterialTextureBindings::metallic },
    MaterialSource::TextureSlot {
      "roughness",
      &MaterialTextureBindings::roughness,
    },
    MaterialSource::TextureSlot {
      "ambient_occlusion",
      &MaterialTextureBindings::ambient_occlusion,
    },
    MaterialSource::TextureSlot {
      "emissive", &MaterialTextureBindings::emissive },
    MaterialSource::TextureSlot {
      "specular", &MaterialTextureBindings::specular },
    MaterialSource::TextureSlot {
      "sheen_color",
      &MaterialTextureBindings::sheen_color,
    },
    MaterialSource::TextureSlot {
      "clearcoat",
      &MaterialTextureBindings::clearcoat,
    },
    MaterialSource::TextureSlot {
      "clearcoat_normal",
      &MaterialTextureBindings::clearcoat_normal,
    },
    MaterialSource::TextureSlot {
      "transmission",
      &MaterialTextureBindings::transmission,
    },
    MaterialSource::TextureSlot {
      "thickness",
      &MaterialTextureBindings::thickness,
    },
  };

  auto GetMaterialDescriptorValidator() -> json_validator&
  {
    static auto validator = [] -> json_validator {
      auto out = json_validator {};
      out.set_root_schema(nlohmann::json::parse(kMaterialDescriptorSchema));
      return out;
    }();
    return validator;
  }

  auto ValidateDescriptorSchema(std::vector<ImportDiagnostic>& diagnostics,
    const std::filesystem::path& source_path,
    const nlohmann::json& descriptor_doc) -> bool
  {
    const auto config = internal::JsonSchemaValidationDiagnosticConfig {
      .validation_failed_code = "material.descriptor.schema_validation_failed",
      .validation_failed_prefix = "Material descriptor validation failed: ",
      .validation_overflow_prefix = "Material descriptor validation emitted ",
      .validator_failure_code = "material.descriptor.schema_validator_failure",
      .validator_failure_prefix
      = "Material descriptor schema validator failed: ",
      .max_issues = 12,
    };

    return internal::ValidateJsonSchemaWithDiagnostics(
      GetMaterialDescriptorValidator(), descriptor_doc, config,
      [&](const std::string_view code, const std::string& message,
        const std::string& object_path) -> void {
        AddError(diagnostics, source_path.string(), std::string(code), message,
          object_path);
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
  auto ParseFloatArray(const nlohmann::json& value, float (&out)[N]) noexcept
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
    float emissive_color[3] { 1.0F, 1.0F, 1.0F };
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
    && metallic.uv_set == roughness.uv_set
    && std::ranges::equal(
      metallic.uv_transform.scale, roughness.uv_transform.scale)
    && std::ranges::equal(
      metallic.uv_transform.offset, roughness.uv_transform.offset)
    && metallic.uv_transform.rotation_radians
    == roughness.uv_transform.rotation_radians;
}

auto MaterialSource::Validate(const std::string_view source_id,
  std::vector<ImportDiagnostic>& diagnostics) const -> bool
{
  auto valid = true;
  const auto error = [&](std::string code, std::string message) {
    AddError(diagnostics, source_id, std::move(code), std::move(message), name);
    valid = false;
  };
  if (std::ranges::any_of(inputs.emissive_factor, [](float value) {
        return !std::isfinite(value) || value < 0.0F
          || value > data::pak::render::kMaxMaterialEmissiveFactor;
      })) {
    error("material.emissive_factor_range",
      "Emission must contain finite channels in [0,65504]");
  }
  if (orm_policy == OrmPolicy::kForcePacked && !CanPackOrm()) {
    error("material.orm_policy",
      "ForcePacked requires metallic/roughness to share source and UV");
  }
  if (shader_requests.size() > kMaxShaderStages) {
    error("material.shader_stage_count", "Shader stage count exceeds 32");
    return false;
  }
  std::array<bool, kMaxShaderStages> seen {};
  for (const auto& shader : shader_requests) {
    if (shader.shader_type == 0
      || shader.shader_type
        > static_cast<uint8_t>(ShaderType::kMaxShaderType)) {
      error("material.shader_stage_invalid", "Shader type is invalid");
      continue;
    }
    const auto index = static_cast<std::size_t>(shader.shader_type - 1U);
    if (index >= seen.size()) {
      error("material.shader_stage_invalid", "Shader type is out of range");
      continue;
    }
    if (seen.at(index)) {
      error("material.shader_stage_duplicate",
        "Shader type is duplicated in request list");
      continue;
    }
    if (shader.source_path.empty() || shader.entry_point.empty()) {
      error("material.shader_ref_invalid",
        "Shader source_path and entry_point must be set");
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
      "material.descriptor.request_invalid_json", failure.what());
    return std::nullopt;
  }
  if (!document.is_object()) {
    AddError(diagnostics, source_path.string(),
      "material.descriptor.request_invalid_json",
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
  if (document.contains("content_hashing")) {
    material.content_hashing = document.at("content_hashing").get<bool>();
  }
  if (document.contains("domain")) {
    const auto value
      = ParseMaterialDomain(document.at("domain").get<std::string>());
    if (!value) {
      AddError(diagnostics, source_path.string(),
        "material.descriptor.domain_invalid", "Material domain is invalid");
      return std::nullopt;
    }
    material.domain = *value;
  }
  if (document.contains("alpha_mode")) {
    const auto value
      = ParseAlphaMode(document.at("alpha_mode").get<std::string>());
    if (!value) {
      AddError(diagnostics, source_path.string(),
        "material.descriptor.alpha_mode_invalid",
        "Material alpha mode is invalid");
      return std::nullopt;
    }
    material.alpha_mode = *value;
  }
  if (document.contains("orm_policy")) {
    const auto value
      = ParseOrmPolicy(document.at("orm_policy").get<std::string>());
    if (!value) {
      AddError(diagnostics, source_path.string(),
        "material.descriptor.orm_policy_invalid",
        "Material ORM policy is invalid");
      return std::nullopt;
    }
    material.orm_policy = *value;
  }
  if (document.contains("parameters")
    && !ApplyMaterialInputsFromJson(
      document.at("parameters"), material.inputs)) {
    AddError(diagnostics, source_path.string(),
      "material.descriptor.parameters_invalid",
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
            "material.descriptor.texture_uv_invalid",
            "Texture UV transform must contain two-component scale and offset",
            "textures." + std::string(slot.name));
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
          "material.descriptor.shader_stage_invalid",
          "Shader stage is not recognized");
        return std::nullopt;
      }
      material.shader_requests.push_back(ShaderRequest {
        .shader_type = *stage,
        .source_path = shader.at("source_path").get<std::string>(),
        .entry_point = shader.at("entry_point").get<std::string>(),
        .defines = shader.value("defines", std::string {}),
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
