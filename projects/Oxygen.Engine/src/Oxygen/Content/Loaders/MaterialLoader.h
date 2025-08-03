//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <memory>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Content/AssetLoader.h>
#include <Oxygen/Content/LoaderFunctions.h>
#include <Oxygen/Content/Loaders/Helpers.h>
#include <Oxygen/Data/MaterialAsset.h>
#include <Oxygen/Data/ShaderReference.h>
#include <Oxygen/Serio/Reader.h>
#include <Oxygen/Serio/Stream.h>

namespace oxygen::content::loaders {

//! Register dependencies for a material asset with the asset loader.
/*!
 Registers resource dependencies for textures and handles future shader
 dependencies.

 @param loader The asset loader to register dependencies with
 @param current_asset_key The asset key of the material being loaded
 @param desc The material asset descriptor containing dependency information
 */
inline auto RegisterMaterialDependencies(AssetLoader& loader,
  const data::AssetKey& current_asset_key,
  const data::pak::MaterialAssetDesc& desc) -> void
{
  using data::pak::ResourceIndexT;

  // Register resource dependencies for non-zero texture indices
  if (desc.base_color_texture != 0) {
    LOG_F(2, "Registering resource dependency: base_color_texture = {}",
      desc.base_color_texture);
    loader.AddResourceDependency(current_asset_key, desc.base_color_texture);
  }

  if (desc.normal_texture != 0) {
    LOG_F(2, "Registering resource dependency: normal_texture = {}",
      desc.normal_texture);
    loader.AddResourceDependency(current_asset_key, desc.normal_texture);
  }

  if (desc.metallic_texture != 0) {
    LOG_F(2, "Registering resource dependency: metallic_texture = {}",
      desc.metallic_texture);
    loader.AddResourceDependency(current_asset_key, desc.metallic_texture);
  }

  if (desc.roughness_texture != 0) {
    LOG_F(2, "Registering resource dependency: roughness_texture = {}",
      desc.roughness_texture);
    loader.AddResourceDependency(current_asset_key, desc.roughness_texture);
  }

  if (desc.ambient_occlusion_texture != 0) {
    LOG_F(2, "Registering resource dependency: ambient_occlusion_texture = {}",
      desc.ambient_occlusion_texture);
    loader.AddResourceDependency(
      current_asset_key, desc.ambient_occlusion_texture);
  }

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
}

//! Loader for material assets.
inline auto LoadMaterialAsset(LoaderContext context)
  -> std::unique_ptr<data::MaterialAsset>
{
  LOG_SCOPE_FUNCTION(INFO);
  LOG_F(2, "offline mode   : {}", context.offline ? "yes" : "no");

  DCHECK_NOTNULL_F(context.desc_reader, "expecting desc_reader not to be null");
  auto& reader = *context.desc_reader;
  auto& loader
    = *context.asset_loader; // Asset loaders always have non-null asset_loader

  using data::ShaderReference;
  using data::pak::kMaxNameSize;
  using data::pak::MaterialAssetDesc;
  using data::pak::ResourceIndexT;
  using data::pak::ShaderReferenceDesc;
  using oxygen::ShaderType;

  auto check_result = [](auto&& result, const char* field) {
    if (!result) {
      LOG_F(INFO, "-failed- on {}: {}", field, result.error().message());
      throw std::runtime_error(
        fmt::format("error reading material asset ({}): {}", field,
          result.error().message()));
    }
  };

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

  auto metalness_result = reader.ReadInto<float>(desc.metalness);
  check_result(metalness_result, "MaterialAssetDesc.metalness");

  auto roughness_result = reader.ReadInto<float>(desc.roughness);
  check_result(roughness_result, "MaterialAssetDesc.roughness");

  auto ambient_occlusion_result
    = reader.ReadInto<float>(desc.ambient_occlusion);
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

  // Skip reserved texture indices array
  auto skip_result = reader.Forward(sizeof(desc.reserved_textures));
  check_result(skip_result, "MaterialAssetDesc.reserved_textures (skip)");

  // Skip reserved bytes
  skip_result = reader.Forward(sizeof(desc.reserved));
  check_result(skip_result, "MaterialAssetDesc.reserved (skip)");

  LOG_F(INFO, "material domain   : {}", desc.material_domain);
  LOG_F(INFO, "flags             : 0x{:08X}", desc.flags);
  LOG_F(INFO, "shader stages     : 0x{:08X}", desc.shader_stages);
  LOG_F(INFO, "base color        : [{:.2f}, {:.2f}, {:.2f}, {:.2f}]",
    desc.base_color[0], desc.base_color[1], desc.base_color[2],
    desc.base_color[3]);
  LOG_F(INFO, "normal scale      : {:.2f}", desc.normal_scale);
  LOG_F(INFO, "metalness         : {:.2f}", desc.metalness);
  LOG_F(INFO, "roughness         : {:.2f}", desc.roughness);
  LOG_F(INFO, "ambient occlusion : {:.2f}", desc.ambient_occlusion);
  LOG_F(INFO, "base color tex    : {}", desc.base_color_texture);
  LOG_F(INFO, "normal tex        : {}", desc.normal_texture);
  LOG_F(INFO, "metallic tex      : {}", desc.metallic_texture);
  LOG_F(INFO, "roughness tex     : {}", desc.roughness_texture);
  LOG_F(INFO, "ambient occ. tex  : {}", desc.ambient_occlusion_texture);

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
      LOG_F(INFO, "  shader stage {}: {} (hash: 0x{:016X})", i,
        shader_refs.back().GetShaderUniqueId(),
        shader_refs.back().GetShaderSourceHash());
    }
  }

  RegisterMaterialDependencies(loader, context.current_asset_key, desc);

  return std::make_unique<data::MaterialAsset>(desc, std::move(shader_refs));
}

static_assert(oxygen::content::LoadFunction<decltype(LoadMaterialAsset)>);

//! Unload function for MaterialAsset.
inline auto UnloadMaterialAsset(std::shared_ptr<data::MaterialAsset> /*asset*/,
  AssetLoader& /*loader*/, bool /*offline*/) noexcept -> void
{
  // Nothing to do for a material asset, its dependency resources will do the
  // work when unloaded.
}

static_assert(oxygen::content::UnloadFunction<decltype(UnloadMaterialAsset),
  data::MaterialAsset>);

} // namespace oxygen::content::loaders
