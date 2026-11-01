//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <iterator>
#include <span>

#include "../Utils/PakUtils.h"
#include "AssetLoaderSources.h"

#include <Oxygen/Cooker/testing/LooseCookedRoot.h>
#include <Oxygen/Cooker/testing/MaterialBuilder.h>
#include <Oxygen/Cooker/testing/Materialize.h>
#include <Oxygen/Core/Types/AssetKey.h>
#include <Oxygen/Data/AssetType.h>
#include <Oxygen/Data/PakFormat_render.h>
#include <Oxygen/Data/SourceKey.h>

namespace oxygen::content::testing {
namespace {
  using cooker::testing::LooseAsset;
  using cooker::testing::LooseCookedRoot;
  using cooker::testing::MaterialBuilder;
  using data::pak::render::MaterialAssetDesc;

  constexpr auto kMount = "test";
  constexpr auto kLeaf = "Surface.omat";

  //! A root of its own identity, as every source written to disk has one.
  auto NewSourceRoot() -> LooseCookedRoot
  {
    constexpr auto kFirstSeed = uint8_t { 0x40U };
    static std::atomic<uint8_t> next_seed { kFirstSeed };
    return LooseCookedRoot { kMount, next_seed.fetch_add(1U) };
  }
}

auto WriteMaterialSource(const std::filesystem::path& root,
  const data::AssetKey& key, const std::array<float, 4>& color)
  -> data::SourceKey
{
  auto record = data::pak::render::MaterialAssetDesc {};
  std::ranges::copy(color, std::begin(record.base_color));
  const auto material = MaterialBuilder { "Surface", record };
  auto source = NewSourceRoot();
  source.Add(LooseAsset {
    .type = data::AssetType::kMaterial,
    .leaf = kLeaf,
    .descriptor = material.Bytes(),
    .references = material.References(),
    .key = key,
  });
  cooker::testing::Materialize(source.Files(), root);
  return source.SourceKey();
}

auto WriteTexturedMaterialSource(const std::filesystem::path& root,
  const data::AssetKey& key) -> data::SourceKey
{
  constexpr uint32_t kTextureAlignment = 256U;
  const auto payload
    = MakeV4TexturePayload(kTextureAlignment, std::byte { 0x55 },
      data::pak::render::TexturePackingPolicyId::kD3D12, kTextureAlignment);
  auto source = NewSourceRoot();
  const auto texture = source.AddTexture(
    std::as_bytes(std::span(payload)), LooseCookedRoot::OnePixelTexture());
  auto material = MaterialBuilder { "Surface" };
  material.Texture(&MaterialAssetDesc::base_color_texture, texture)
    .Texture(&MaterialAssetDesc::normal_texture, texture);
  source.Add(LooseAsset {
    .type = data::AssetType::kMaterial,
    .leaf = kLeaf,
    .descriptor = material.Bytes(),
    .references = material.References(),
    .key = key,
  });
  cooker::testing::Materialize(source.Files(), root);
  return source.SourceKey();
}

} // namespace oxygen::content::testing
