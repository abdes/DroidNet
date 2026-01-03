//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <bit>
#include <memory>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Content/Internal/DependencyCollector.h>
#include <Oxygen/Content/Internal/ResourceRef.h>
#include <Oxygen/Content/LoaderFunctions.h>
#include <Oxygen/Content/Loaders/Helpers.h>
#include <Oxygen/Data/MaterialAsset.h>
#include <Oxygen/Data/ShaderReference.h>
#include <Oxygen/Data/TextureResource.h>
#include <Oxygen/Data/Unorm16.h>
#include <Oxygen/Serio/Reader.h>
#include <Oxygen/Serio/Stream.h>

namespace oxygen::content::loaders {

//! Loader for material assets.
inline auto LoadMaterialAsset(LoaderContext context)
  -> std::unique_ptr<data::MaterialAsset>
{
  LOG_SCOPE_FUNCTION(INFO);
  LOG_F(2, "offline mode   : {}", context.work_offline ? "yes" : "no");

  DCHECK_NOTNULL_F(context.desc_reader, "expecting desc_reader not to be null");
  auto& reader = *context.desc_reader;

  using data::ShaderReference;
  using data::Unorm16;
  using data::pak::kMaxNameSize;
  using data::pak::MaterialAssetDesc;
  using data::pak::ResourceIndexT;
  using data::pak::ShaderReferenceDesc;
  using oxygen::ShaderType;

  auto check_result = [](auto&& result, const char* field) {
    if (!result) {
      LOG_F(
        INFO, "-failed- on {}: {}", field, result.error().message().c_str());
      throw std::runtime_error(
        fmt::format("error reading material asset ({}): {}", field,
          result.error().message()));
    }
  };

  auto pack = reader.ScopedAlignment(1);

  // Read MaterialAssetDesc field-by-field to avoid
  // std::has_unique_object_representations issues with float fields
  MaterialAssetDesc desc;

  {
    LOG_SCOPE_F(INFO, "Header");
    LoadAssetHeader(reader, desc.header);
  }

  // -- Read MaterialAssetDesc specific fields

  auto material_domain_result = reader.ReadInto<uint8_t>(desc.material_domain);
  check_result(material_domain_result, "MaterialAssetDesc.material_domain");

  auto flags_result = reader.ReadInto<uint32_t>(desc.flags);
  check_result(flags_result, "MaterialAssetDesc.flags");

  auto shader_stages_result = reader.ReadInto<uint32_t>(desc.shader_stages);
  check_result(shader_stages_result, "MaterialAssetDesc.shader_stages");

  // ReadInto float arrays (these are the problematic fields for unique object
  // representation)
  for (float& i : desc.base_color) {
    auto base_color_result = reader.ReadInto<float>(i);
    check_result(base_color_result, "MaterialAssetDesc.base_color");
  }

  auto normal_scale_result = reader.ReadInto<float>(desc.normal_scale);
  check_result(normal_scale_result, "MaterialAssetDesc.normal_scale");

  auto metalness_result = ReadUnorm16(reader, desc.metalness);
  check_result(metalness_result, "MaterialAssetDesc.metalness");

  auto roughness_result = ReadUnorm16(reader, desc.roughness);
  check_result(roughness_result, "MaterialAssetDesc.roughness");

  auto ambient_occlusion_result = ReadUnorm16(reader, desc.ambient_occlusion);
  check_result(ambient_occlusion_result, "MaterialAssetDesc.ambient_occlusion");

  // ReadInto texture resource indices
  auto base_color_texture_result
    = reader.ReadInto<ResourceIndexT>(desc.base_color_texture);
  check_result(
    base_color_texture_result, "MaterialAssetDesc.base_color_texture");

  auto normal_texture_result
    = reader.ReadInto<ResourceIndexT>(desc.normal_texture);
  check_result(normal_texture_result, "MaterialAssetDesc.normal_texture");

  auto metallic_texture_result
    = reader.ReadInto<ResourceIndexT>(desc.metallic_texture);
  check_result(metallic_texture_result, "MaterialAssetDesc.metallic_texture");

  auto roughness_texture_result
    = reader.ReadInto<ResourceIndexT>(desc.roughness_texture);
  check_result(roughness_texture_result, "MaterialAssetDesc.roughness_texture");

  auto ambient_occlusion_texture_result
    = reader.ReadInto<ResourceIndexT>(desc.ambient_occlusion_texture);
  check_result(ambient_occlusion_texture_result,
    "MaterialAssetDesc.ambient_occlusion_texture");

  auto emissive_texture_result
    = reader.ReadInto<ResourceIndexT>(desc.emissive_texture);
  check_result(emissive_texture_result, "MaterialAssetDesc.emissive_texture");

  auto specular_texture_result
    = reader.ReadInto<ResourceIndexT>(desc.specular_texture);
  check_result(specular_texture_result, "MaterialAssetDesc.specular_texture");

  auto sheen_color_texture_result
    = reader.ReadInto<ResourceIndexT>(desc.sheen_color_texture);
  check_result(
    sheen_color_texture_result, "MaterialAssetDesc.sheen_color_texture");

  auto clearcoat_texture_result
    = reader.ReadInto<ResourceIndexT>(desc.clearcoat_texture);
  check_result(clearcoat_texture_result, "MaterialAssetDesc.clearcoat_texture");

  auto clearcoat_normal_texture_result
    = reader.ReadInto<ResourceIndexT>(desc.clearcoat_normal_texture);
  check_result(clearcoat_normal_texture_result,
    "MaterialAssetDesc.clearcoat_normal_texture");

  auto transmission_texture_result
    = reader.ReadInto<ResourceIndexT>(desc.transmission_texture);
  check_result(
    transmission_texture_result, "MaterialAssetDesc.transmission_texture");

  auto thickness_texture_result
    = reader.ReadInto<ResourceIndexT>(desc.thickness_texture);
  check_result(thickness_texture_result, "MaterialAssetDesc.thickness_texture");

  for (auto& i : desc.emissive_factor) {
    auto emissive_factor_result = ReadHalfFloat(reader, i);
    check_result(emissive_factor_result, "MaterialAssetDesc.emissive_factor");
  }

  auto alpha_cutoff_result = ReadUnorm16(reader, desc.alpha_cutoff);
  check_result(alpha_cutoff_result, "MaterialAssetDesc.alpha_cutoff");

  auto ior_result = reader.ReadInto<float>(desc.ior);
  check_result(ior_result, "MaterialAssetDesc.ior");

  auto specular_factor_result = ReadUnorm16(reader, desc.specular_factor);
  check_result(specular_factor_result, "MaterialAssetDesc.specular_factor");

  for (auto& i : desc.sheen_color_factor) {
    auto sheen_color_factor_result = ReadHalfFloat(reader, i);
    check_result(
      sheen_color_factor_result, "MaterialAssetDesc.sheen_color_factor");
  }

  auto clearcoat_factor_result = ReadUnorm16(reader, desc.clearcoat_factor);
  check_result(clearcoat_factor_result, "MaterialAssetDesc.clearcoat_factor");

  auto clearcoat_roughness_result
    = ReadUnorm16(reader, desc.clearcoat_roughness);
  check_result(
    clearcoat_roughness_result, "MaterialAssetDesc.clearcoat_roughness");

  auto transmission_factor_result
    = ReadUnorm16(reader, desc.transmission_factor);
  check_result(
    transmission_factor_result, "MaterialAssetDesc.transmission_factor");

  auto thickness_factor_result = ReadUnorm16(reader, desc.thickness_factor);
  check_result(thickness_factor_result, "MaterialAssetDesc.thickness_factor");

  for (auto& i : desc.attenuation_color) {
    auto attenuation_color_result = ReadHalfFloat(reader, i);
    check_result(
      attenuation_color_result, "MaterialAssetDesc.attenuation_color");
  }

  auto attenuation_distance_result
    = reader.ReadInto<float>(desc.attenuation_distance);
  check_result(
    attenuation_distance_result, "MaterialAssetDesc.attenuation_distance");

  for (auto& i : desc.reserved) {
    auto reserved_result = reader.ReadInto<uint8_t>(i);
    check_result(reserved_result, "MaterialAssetDesc.reserved");
  }

  LOG_F(INFO, "material domain   : {}", desc.material_domain);
  LOG_F(INFO, "flags             : 0x{:08X}", desc.flags);
  LOG_F(INFO, "shader stages     : 0x{:08X}", desc.shader_stages);
  LOG_F(INFO, "base color        : [{:.2f}, {:.2f}, {:.2f}, {:.2f}]",
    desc.base_color[0], desc.base_color[1], desc.base_color[2],
    desc.base_color[3]);
  LOG_F(INFO, "normal scale      : {:.2f}", desc.normal_scale);
  LOG_F(INFO, "metalness         : {:.2f}", desc.metalness.ToFloat());
  LOG_F(INFO, "roughness         : {:.2f}", desc.roughness.ToFloat());
  LOG_F(INFO, "ambient occlusion : {:.2f}", desc.ambient_occlusion.ToFloat());
  LOG_F(INFO, "base color tex    : {}", desc.base_color_texture);
  LOG_F(INFO, "normal tex        : {}", desc.normal_texture);
  LOG_F(INFO, "metallic tex      : {}", desc.metallic_texture);
  LOG_F(INFO, "roughness tex     : {}", desc.roughness_texture);
  LOG_F(INFO, "ambient occ. tex  : {}", desc.ambient_occlusion_texture);
  LOG_F(INFO, "emissive tex     : {}", desc.emissive_texture);
  LOG_F(INFO, "specular tex     : {}", desc.specular_texture);
  LOG_F(INFO, "sheen color tex  : {}", desc.sheen_color_texture);
  LOG_F(INFO, "clearcoat tex    : {}", desc.clearcoat_texture);
  LOG_F(INFO, "clearcoat N tex  : {}", desc.clearcoat_normal_texture);
  LOG_F(INFO, "transmission tex : {}", desc.transmission_texture);
  LOG_F(INFO, "thickness tex    : {}", desc.thickness_texture);
  LOG_F(INFO, "alpha cutoff     : {:.3f}", desc.alpha_cutoff.ToFloat());
  LOG_F(INFO, "ior              : {:.3f}", desc.ior);
  LOG_F(INFO, "specular factor  : {:.3f}", desc.specular_factor.ToFloat());
  LOG_F(INFO, "clearcoat        : {:.3f}", desc.clearcoat_factor.ToFloat());
  LOG_F(INFO, "clearcoat rough. : {:.3f}", desc.clearcoat_roughness.ToFloat());
  LOG_F(INFO, "transmission     : {:.3f}", desc.transmission_factor.ToFloat());
  LOG_F(INFO, "thickness        : {:.3f}", desc.thickness_factor.ToFloat());
  LOG_F(INFO, "attenuation dist : {:.3f}", desc.attenuation_distance);

  // TODO: Implement shader refs as resources
  // Currently shader references are embedded data without asset keys
  // In the future, shaders should be separate assets/resources with proper keys
  //
  // Register asset dependencies for shader references
  // for (const auto& shader_ref : shader_refs) {
  //   oxygen::data::AssetKey shader_asset_key = shader_ref.GetAssetKey();
  //   LOG_F(2, "Registering asset dependency: shader = {}",
  //   nostd::to_string(shader_asset_key));
  //   loader.AddAssetDependency(current_asset_key, shader_asset_key);
  // }

  // Count set bits in shader_stages to determine number of shader references
  uint32_t shader_stage_bits = desc.shader_stages;
  size_t shader_count = std::popcount(shader_stage_bits);
  std::vector<ShaderReference> shader_refs;
  shader_refs.reserve(shader_count);
  LOG_F(INFO, "shader references : {}", shader_count);

  // For each set bit, read a ShaderReferenceDesc and construct a
  // ShaderReference
  for (uint32_t i = 0; i < 32; ++i) {
    if ((shader_stage_bits & (1u << i)) != 0) {
      auto shader_result = reader.Read<ShaderReferenceDesc>();
      check_result(shader_result, "ShaderReferenceDesc");
      auto stage = static_cast<ShaderType>(i);
      shader_refs.emplace_back(stage, shader_result.value());
      LOG_F(INFO, "   shader stage {} : {} (hash: 0x{:016X})", i,
        shader_refs.back().GetShaderUniqueId(),
        shader_refs.back().GetShaderSourceHash());
    }
  }

  if (!context.parse_only && !context.dependency_collector) {
    throw std::runtime_error(
      "MaterialAsset loader requires a dependency collector; "
      "non-parse-only loads must be orchestrated via async publish");
  }

  if (!context.parse_only) {
    using data::TextureResource;
    using data::pak::kNoResourceIndex;
    using data::pak::ResourceIndexT;

    auto collect_texture_ref = [&](const ResourceIndexT texture_index) {
      if (texture_index == kNoResourceIndex) {
        return;
      }

      internal::ResourceRef ref {
        .source = context.source_token,
        .resource_type_id = TextureResource::ClassTypeId(),
        .resource_index = texture_index,
      };
      context.dependency_collector->AddResourceDependency(ref);
    };

    collect_texture_ref(desc.base_color_texture);
    collect_texture_ref(desc.normal_texture);
    collect_texture_ref(desc.metallic_texture);
    collect_texture_ref(desc.roughness_texture);
    collect_texture_ref(desc.ambient_occlusion_texture);
    collect_texture_ref(desc.emissive_texture);
    collect_texture_ref(desc.specular_texture);
    collect_texture_ref(desc.sheen_color_texture);
    collect_texture_ref(desc.clearcoat_texture);
    collect_texture_ref(desc.clearcoat_normal_texture);
    collect_texture_ref(desc.transmission_texture);
    collect_texture_ref(desc.thickness_texture);
  }

  // Create the material asset with the loaded shader references and runtime
  // per-slot texture resource keys produced during loading.
  auto material_asset = std::make_unique<data::MaterialAsset>(
    context.current_asset_key, desc, std::move(shader_refs));

  return material_asset;
}

static_assert(oxygen::content::LoadFunction<decltype(LoadMaterialAsset)>);

} // namespace oxygen::content::loaders
