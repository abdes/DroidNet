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

  // Read AssetHeader field-by-field
  auto asset_type_result = reader.read<uint8_t>();
  check_result(asset_type_result, "AssetHeader.asset_type");
  desc.header.asset_type = asset_type_result.value();

  // Read name as individual characters
  for (size_t i = 0; i < kMaxNameSize; ++i) {
    auto char_result = reader.read<char>();
    check_result(char_result, "AssetHeader.name");
    desc.header.name[i] = char_result.value();
  }

  auto version_result = reader.read<uint8_t>();
  check_result(version_result, "AssetHeader.version");
  desc.header.version = version_result.value();

  auto priority_result = reader.read<uint8_t>();
  check_result(priority_result, "AssetHeader.streaming_priority");
  desc.header.streaming_priority = priority_result.value();

  auto content_hash_result = reader.read<uint64_t>();
  check_result(content_hash_result, "AssetHeader.content_hash");
  desc.header.content_hash = content_hash_result.value();

  auto variant_flags_result = reader.read<uint32_t>();
  check_result(variant_flags_result, "AssetHeader.variant_flags");
  desc.header.variant_flags = variant_flags_result.value();

  // Skip AssetHeader reserved bytes
  for (size_t i = 0; i < 16; ++i) {
    auto reserved_result = reader.read<uint8_t>();
    check_result(reserved_result, "AssetHeader.reserved");
    desc.header.reserved[i] = reserved_result.value();
  }

  // Read MaterialAssetDesc specific fields
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

  // Read reserved texture indices array
  for (int i = 0; i < 8; ++i) {
    auto reserved_texture_result = reader.read<ResourceIndexT>();
    check_result(
      reserved_texture_result, "MaterialAssetDesc.reserved_textures");
    desc.reserved_textures[i] = reserved_texture_result.value();
  }

  // Skip reserved bytes
  for (size_t i = 0; i < 68; ++i) {
    auto reserved_result = reader.read<uint8_t>();
    check_result(reserved_result, "MaterialAssetDesc.reserved");
    desc.reserved[i] = reserved_result.value();
  }
  LOG_F(INFO, "material_domain : {}", desc.material_domain);
  LOG_F(INFO, "shader_stages   : 0x{:08X}", desc.shader_stages);

  // Count set bits in shader_stages to determine number of shader references
  uint32_t shader_stage_bits = desc.shader_stages;
  size_t shader_count = std::popcount(shader_stage_bits);
  std::vector<ShaderReference> shader_refs;
  shader_refs.reserve(shader_count);

  // For each set bit, read a ShaderReferenceDesc and construct a
  // ShaderReference
  for (uint32_t i = 0; i < 32; ++i) {
    if ((shader_stage_bits & (1u << i)) != 0) {
      auto shader_result = reader.read<ShaderReferenceDesc>();
      check_result(shader_result, "ShaderReferenceDesc");
      ShaderType stage = static_cast<ShaderType>(i);
      shader_refs.emplace_back(stage, shader_result.value());
    }
  }

  return std::make_unique<data::MaterialAsset>(desc, std::move(shader_refs));
}

} // namespace oxygen::content::loaders
