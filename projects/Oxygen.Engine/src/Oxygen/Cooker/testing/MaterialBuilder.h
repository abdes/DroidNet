//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <Oxygen/Core/Meta/Data/ResourceIndex.h>
#include <Oxygen/Core/Types/AssetKey.h>
#include <Oxygen/Data/AssetReferences.h>
#include <Oxygen/Data/PakFormat_render.h>

namespace oxygen::cooker::testing {

//! A cooked material descriptor, as the material cook writes it.
/*!
 The record a test passes keeps its values; the builder sets the header and
 binds the texture slots through the material's references, as a cook does:
 a slot names a row of the root's texture table, or another source's texture
 by its key. Slots bound to one texture share one binding.

 ```cpp
 auto game = LooseCookedRoot { "Game" };
 const auto wood = game.AddTexture(SeedBytes(1));
 auto material = MaterialBuilder { "Wood" };
 material.Texture(&MaterialAssetDesc::base_color_texture, wood);
 game.Material("Materials/Wood.omat", material);
 ```
*/
class MaterialBuilder final {
public:
  using TextureSlot
    = data::ResourceReferenceIndex data::pak::render::MaterialAssetDesc::*;

  //! An opaque material record with the format's default values, for a test
  //! to change before passing it.
  [[nodiscard]] static auto Record() -> data::pak::render::MaterialAssetDesc;

  explicit MaterialBuilder(std::string_view name = "Material",
    data::pak::render::MaterialAssetDesc record = Record());

  //! Binds `slot` to row `row` of the root's texture table.
  auto Texture(TextureSlot slot, ResourceIndexT row) -> MaterialBuilder&;

  //! Binds `slot` to the texture `texture` another source holds.
  auto ExternalTexture(TextureSlot slot, const data::AssetKey& texture)
    -> MaterialBuilder&;

  //! Adds a pixel shader stage: the entry point `entry` of `source`,
  //! compiled with `defines`.
  auto PixelShader(std::string source = "fixture.hlsl",
    std::string entry = "PS", std::string defines = {}) -> MaterialBuilder&;

  [[nodiscard]] auto Name() const noexcept -> const std::string&
  {
    return name_;
  }

  //! The descriptor.
  /*!
   @throw std::logic_error When the product loader refuses it.
  */
  [[nodiscard]] auto Bytes() const -> std::vector<std::byte>;

  //! The references a cook records of the descriptor: its textures.
  [[nodiscard]] auto References() const -> data::AssetReferences;

private:
  std::string name_;
  data::pak::render::MaterialAssetDesc record_;
  std::vector<std::pair<TextureSlot, data::TextureTarget>> textures_;
  struct Shader {
    std::string source;
    std::string entry;
    std::string defines;
  };
  std::optional<Shader> pixel_shader_;
};

} // namespace oxygen::cooker::testing
