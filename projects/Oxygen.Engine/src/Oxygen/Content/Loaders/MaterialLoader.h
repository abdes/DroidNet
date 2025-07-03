//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <memory>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/NoStd.h>
#include <Oxygen/Base/Reader.h>
#include <Oxygen/Base/Stream.h>
#include <Oxygen/Content/Loaders/Helpers.h>
#include <Oxygen/Data/MaterialAsset.h>
#include <Oxygen/Data/ShaderReference.h>

namespace oxygen::content::loaders {

//! Loader for material assets.
template <oxygen::serio::Stream S>
auto LoadMaterialAsset(oxygen::serio::Reader<S> reader)
  -> std::unique_ptr<data::MaterialAsset>
{
  LOG_SCOPE_FUNCTION(INFO);

  using oxygen::ShaderType;
  using oxygen::data::ShaderReference;
  using oxygen::data::pak::kMaxNameSize;
  using oxygen::data::pak::MaterialAssetDesc;
  using oxygen::data::pak::ResourceIndexT;
  using oxygen::data::pak::ShaderReferenceDesc;

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
    desc.header = LoadAssetHeader(reader);
  }

  // -- Read MaterialAssetDesc specific fields

  auto material_domain_result = reader.read<uint8_t>();
  check_result(material_domain_result, "MaterialAssetDesc.material_domain");
  desc.material_domain = material_domain_result.value();

  auto flags_result = reader.read<uint32_t>();
  check_result(flags_result, "MaterialAssetDesc.flags");
  desc.flags = flags_result.value();

  auto shader_stages_result = reader.read<uint32_t>();
  check_result(shader_stages_result, "MaterialAssetDesc.shader_stages");
  desc.shader_stages = shader_stages_result.value();

  // Read float arrays (these are the problematic fields for unique object
  // representation)
  for (int i = 0; i < 4; ++i) {
    auto base_color_result = reader.read<float>();
    check_result(base_color_result, "MaterialAssetDesc.base_color");
    desc.base_color[i] = base_color_result.value();
  }

  auto normal_scale_result = reader.read<float>();
  check_result(normal_scale_result, "MaterialAssetDesc.normal_scale");
  desc.normal_scale = normal_scale_result.value();

  auto metalness_result = reader.read<float>();
  check_result(metalness_result, "MaterialAssetDesc.metalness");
  desc.metalness = metalness_result.value();

  auto roughness_result = reader.read<float>();
  check_result(roughness_result, "MaterialAssetDesc.roughness");
  desc.roughness = roughness_result.value();

  auto ambient_occlusion_result = reader.read<float>();
  check_result(ambient_occlusion_result, "MaterialAssetDesc.ambient_occlusion");
  desc.ambient_occlusion = ambient_occlusion_result.value();

  // Read texture resource indices
  auto base_color_texture_result = reader.read<ResourceIndexT>();
  check_result(
    base_color_texture_result, "MaterialAssetDesc.base_color_texture");
  desc.base_color_texture = base_color_texture_result.value();

  auto normal_texture_result = reader.read<ResourceIndexT>();
  check_result(normal_texture_result, "MaterialAssetDesc.normal_texture");
  desc.normal_texture = normal_texture_result.value();

  auto metallic_texture_result = reader.read<ResourceIndexT>();
  check_result(metallic_texture_result, "MaterialAssetDesc.metallic_texture");
  desc.metallic_texture = metallic_texture_result.value();

  auto roughness_texture_result = reader.read<ResourceIndexT>();
  check_result(roughness_texture_result, "MaterialAssetDesc.roughness_texture");
  desc.roughness_texture = roughness_texture_result.value();

  auto ambient_occlusion_texture_result = reader.read<ResourceIndexT>();
  check_result(ambient_occlusion_texture_result,
    "MaterialAssetDesc.ambient_occlusion_texture");
  desc.ambient_occlusion_texture = ambient_occlusion_texture_result.value();

  // Skip reserved texture indices array
  auto skip_result = reader.forward(sizeof(desc.reserved_textures));
  check_result(skip_result, "MaterialAssetDesc.reserved_textures (skip)");

  // Skip reserved bytes
  skip_result = reader.forward(sizeof(desc.reserved));
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
      auto shader_result = reader.read<ShaderReferenceDesc>();
      check_result(shader_result, "ShaderReferenceDesc");
      ShaderType stage = static_cast<ShaderType>(i);
      shader_refs.emplace_back(stage, shader_result.value());
      LOG_F(INFO, "  shader stage {}: {} (hash: 0x{:016X})", i,
        shader_refs.back().GetShaderUniqueId(),
        shader_refs.back().GetShaderSourceHash());
    }
  }

  return std::make_unique<data::MaterialAsset>(desc, std::move(shader_refs));
}

} // namespace oxygen::content::loaders
