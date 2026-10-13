//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <iterator>
#include <span>

#include "../Utils/PakUtils.h"
#include "AssetLoaderSources.h"
#include "LooseCookedTestWriter.h"

#include <Oxygen/Base/Uuid.h>
#include <Oxygen/Core/Types/AssetKey.h>
#include <Oxygen/Core/Types/Format.h>
#include <Oxygen/Core/Types/TextureType.h>
#include <Oxygen/Data/AssetReferences.h>
#include <Oxygen/Data/AssetType.h>
#include <Oxygen/Data/LooseCookedIndexFormat.h>
#include <Oxygen/Data/PakFormat_core.h>
#include <Oxygen/Data/PakFormat_render.h>
#include <Oxygen/Data/SourceKey.h>

namespace oxygen::content::testing {
namespace {
  auto MaterialDescriptor() -> data::pak::render::MaterialAssetDesc
  {
    data::pak::render::MaterialAssetDesc descriptor {};
    descriptor.header.asset_type
      = static_cast<uint8_t>(data::AssetType::kMaterial);
    descriptor.header.version = data::pak::render::kMaterialAssetVersion;
    return descriptor;
  }
}

auto WriteMaterialSource(const std::filesystem::path& root,
  const data::AssetKey& key, const std::array<float, 4>& color)
  -> data::SourceKey
{
  auto descriptor = MaterialDescriptor();
  std::ranges::copy(color, std::begin(descriptor.base_color));
  const data::SourceKey source { Uuid::Generate() };
  LooseCookedTestWriter writer(root);
  writer.SetSourceKey(source);
  writer.WriteAssetDescriptor(key, data::AssetType::kMaterial,
    "/test/Surface.omat", "Surface.omat",
    std::as_bytes(std::span(&descriptor, 1U)));
  static_cast<void>(writer.Finish());
  return source;
}

auto WriteTexturedMaterialSource(const std::filesystem::path& root,
  const data::AssetKey& key) -> data::SourceKey
{
  auto material = MaterialDescriptor();
  material.base_color_texture = data::ResourceReferenceIndex { 0U };
  material.normal_texture = data::ResourceReferenceIndex { 0U };
  constexpr uint32_t kTextureAlignment = 256U;
  const auto payload
    = MakeV4TexturePayload(kTextureAlignment, std::byte { 0x55 },
      data::pak::render::TexturePackingPolicyId::kD3D12, kTextureAlignment);
  data::pak::core::TextureResourceDesc texture {};
  texture.size_bytes = static_cast<uint32_t>(payload.size());
  texture.texture_type = static_cast<uint8_t>(TextureType::kTexture2D);
  texture.width = 1U;
  texture.height = 1U;
  texture.depth = 1U;
  texture.array_layers = 1U;
  texture.mip_levels = 1U;
  texture.format = static_cast<uint8_t>(Format::kRGBA8UNorm);
  texture.alignment = kTextureAlignment;
  const auto table
    = std::array { data::pak::core::TextureResourceDesc {}, texture };
  const auto references = data::AssetReferences::Create(
    {
      { .kind = data::ResourceKind::kTexture, .index = ResourceIndexT { 1U } },
    },
    {})
                            .value();
  const data::SourceKey source { Uuid::Generate() };
  LooseCookedTestWriter writer(root);
  writer.SetSourceKey(source);
  writer.WriteFile(data::loose_cooked::FileKind::kTexturesTable,
    "textures.table", std::as_bytes(std::span(table)));
  writer.WriteFile(data::loose_cooked::FileKind::kTexturesData, "textures.data",
    std::as_bytes(std::span(payload)));
  writer.WriteAssetDescriptor(key, data::AssetType::kMaterial,
    "/test/Surface.omat", "Surface.omat",
    std::as_bytes(std::span(&material, 1U)), references);
  static_cast<void>(writer.Finish());
  return source;
}

} // namespace oxygen::content::testing
