//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <Oxygen/Content/Loaders/MaterialLoader.h>
#include <Oxygen/Cooker/Import/Internal/AssetReferenceBuilder.h>
#include <Oxygen/Cooker/testing/Descriptors.h>
#include <Oxygen/Cooker/testing/MaterialBuilder.h>
#include <Oxygen/Cooker/testing/ProductLoad.h>
#include <Oxygen/Core/Meta/Data/ResourceIndex.h>
#include <Oxygen/Core/Types/AssetKey.h>
#include <Oxygen/Core/Types/ShaderType.h>
#include <Oxygen/Data/AssetReferences.h>
#include <Oxygen/Data/AssetType.h>
#include <Oxygen/Data/DescriptorHash.h>
#include <Oxygen/Data/DescriptorStrings.h>
#include <Oxygen/Data/PakFormat_render.h>

namespace oxygen::cooker::testing {

namespace {

  namespace render = data::pak::render;

} // namespace

MaterialBuilder::MaterialBuilder(
  const std::string_view name, const render::MaterialAssetDesc record)
  : name_(name)
  , record_(record)
{
}

auto MaterialBuilder::Texture(const TextureSlot slot, const ResourceIndexT row)
  -> MaterialBuilder&
{
  textures_.emplace_back(slot, data::TextureTarget { row });
  return *this;
}

auto MaterialBuilder::ExternalTexture(
  const TextureSlot slot, const data::AssetKey& texture) -> MaterialBuilder&
{
  textures_.emplace_back(slot, data::TextureTarget { texture });
  return *this;
}

auto MaterialBuilder::PixelShader(std::string source, std::string entry)
  -> MaterialBuilder&
{
  pixel_shader_.emplace(std::move(source), std::move(entry));
  return *this;
}

auto MaterialBuilder::Bytes() const -> std::vector<std::byte>
{
  auto descriptor = record_;
  descriptor.header.asset_type
    = static_cast<uint8_t>(data::AssetType::kMaterial);
  descriptor.header.version = render::kMaterialAssetVersion;
  auto strings = data::DescriptorStringsBuilder {};
  descriptor.header.name_offset = strings.Add(name_);
  auto references = content::import::AssetReferenceBuilder {};
  for (const auto& [slot, target] : textures_) {
    descriptor.*slot = references.AddTexture(target);
  }
  auto shader = render::ShaderReferenceDesc {};
  if (pixel_shader_.has_value()) {
    descriptor.shader_stages = 1U << static_cast<uint32_t>(ShaderType::kPixel);
    shader.shader_type = static_cast<uint8_t>(ShaderType::kPixel);
    shader.source_path_offset = strings.Add(pixel_shader_->first);
    shader.entry_point_offset = strings.Add(pixel_shader_->second);
  }
  auto bytes = std::vector<std::byte> {};
  AppendRecord(bytes, descriptor);
  if (pixel_shader_.has_value()) {
    AppendRecord(bytes, shader);
  }
  data::AppendDescriptorStrings(bytes, strings);
  data::StampDescriptorHash(bytes);
  const auto encoded_references = std::move(references).Build();
  detail::CheckWithLoader("material '" + name_ + "'", bytes, encoded_references,
    content::loaders::LoadMaterialAsset);
  return bytes;
}

auto MaterialBuilder::References() const -> data::AssetReferences
{
  auto references = content::import::AssetReferenceBuilder {};
  for (const auto& texture : textures_) {
    static_cast<void>(references.AddTexture(texture.second));
  }
  return std::move(references).Build();
}

} // namespace oxygen::cooker::testing
