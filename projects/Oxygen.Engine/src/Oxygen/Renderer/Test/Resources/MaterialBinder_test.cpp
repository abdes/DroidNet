//===----------------------------------------------------------------------===//
// Copyright (c) DroidNet
//
// This file is part of Oxygen.Engine.
//
// Oxygen.Engine is free software: you can redistribute it and/or modify it
// under the terms of the GNU General Public License as published by the Free
// Software Foundation, either version 3 of the License, or (at your option)
// any later version.
//
// Oxygen.Engine is distributed in the hope that it will be useful, but WITHOUT
// ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
// FOR A PARTICULAR PURPOSE. See the GNU General Public License for more
// details.
//
// You should have received a copy of the GNU General Public License along with
// Oxygen.Engine. If not, see <https://www.gnu.org/licenses/>.
//
// SPDX-License-Identifier: GPL-3.0-or-later
//===----------------------------------------------------------------------===//

#include <Oxygen/Renderer/Test/Resources/MaterialBinderTest.h>

#include <Oxygen/Data/MaterialAsset.h>
#include <Oxygen/Renderer/RendererTag.h>
#include <Oxygen/Renderer/ScenePrep/MaterialRef.h>
#include <Oxygen/Renderer/Types/MaterialConstants.h>

#include <cstdint>
#include <memory>

using oxygen::content::ResourceKey;
using oxygen::renderer::testing::MaterialBinderTest;

namespace {

auto MakeMaterial(ResourceKey base_color_key, ResourceKey normal_key,
  uint32_t raw_base_color_index, uint32_t raw_normal_index)
  -> std::shared_ptr<const oxygen::data::MaterialAsset>
{
  using oxygen::data::pak::MaterialAssetDesc;

  MaterialAssetDesc desc {};
  desc.base_color_texture = raw_base_color_index;
  desc.normal_texture = raw_normal_index;

  // Non-zero defaults so we can distinguish from memset/zero init.
  desc.base_color[0] = 1.0F;
  desc.base_color[1] = 0.5F;
  desc.base_color[2] = 0.25F;
  desc.base_color[3] = 1.0F;

  return std::make_shared<oxygen::data::MaterialAsset>(desc,
    std::vector<oxygen::data::ShaderReference> {},
    std::vector<oxygen::content::ResourceKey> { base_color_key, normal_key });
}

} // namespace

//! Material binder must return stable handles for identical inputs.
/*! This contract ensures materials can be cached and referenced reliably.
 */
NOLINT_TEST_F(MaterialBinderTest, GetOrAllocate_SameMaterial_ReturnsSameHandle)
{
  // Arrange
  const ResourceKey base_color_key = AssetLoaderRef().MintSyntheticTextureKey();
  const ResourceKey normal_key = AssetLoaderRef().MintSyntheticTextureKey();

  MaterialBinderRef().OnFrameStart(
    oxygen::renderer::internal::RendererTagFactory::Get(),
    oxygen::frame::Slot { 1 });

  oxygen::engine::sceneprep::MaterialRef ref;
  ref.asset = MakeMaterial(base_color_key, normal_key, 100000U, 200000U);

  // Act
  const auto handle_0 = MaterialBinderRef().GetOrAllocate(ref);
  const auto handle_1 = MaterialBinderRef().GetOrAllocate(ref);

  // Assert
  EXPECT_TRUE(MaterialBinderRef().IsValidHandle(handle_0));
  EXPECT_TRUE(MaterialBinderRef().IsValidHandle(handle_1));
  EXPECT_EQ(handle_0, handle_1);
}

//! MaterialConstants must store bindless SRV indices, not raw author indices.
/*! This contract ensures the shader-visible constants refer to the stable SRV
    indices allocated by TextureBinder.
*/
NOLINT_TEST_F(
  MaterialBinderTest, SerializeMaterialConstants_UsesTextureBinderSrvIndices)
{
  // Arrange
  const ResourceKey base_color_key = AssetLoaderRef().MintSyntheticTextureKey();
  const ResourceKey normal_key = AssetLoaderRef().MintSyntheticTextureKey();

  MaterialBinderRef().OnFrameStart(
    oxygen::renderer::internal::RendererTagFactory::Get(),
    oxygen::frame::Slot { 1 });

  constexpr uint32_t kRawBaseColorIndex = 123456U;
  constexpr uint32_t kRawNormalIndex = 654321U;

  oxygen::engine::sceneprep::MaterialRef ref;
  ref.asset = MakeMaterial(
    base_color_key, normal_key, kRawBaseColorIndex, kRawNormalIndex);

  // Act
  const auto material_handle = MaterialBinderRef().GetOrAllocate(ref);
  ASSERT_TRUE(MaterialBinderRef().IsValidHandle(material_handle));

  const auto expected_base_color_srv
    = TextureBinderRef().GetOrAllocate(base_color_key).get();
  const auto expected_normal_srv
    = TextureBinderRef().GetOrAllocate(normal_key).get();

  const auto all_constants = MaterialBinderRef().GetMaterialConstants();
  ASSERT_LT(
    static_cast<std::size_t>(material_handle.get()), all_constants.size());
  const auto& constants
    = all_constants[static_cast<std::size_t>(material_handle.get())];

  // Assert
  EXPECT_EQ(constants.base_color_texture_index, expected_base_color_srv);
  EXPECT_EQ(constants.normal_texture_index, expected_normal_srv);

  EXPECT_NE(constants.base_color_texture_index, kRawBaseColorIndex);
  EXPECT_NE(constants.normal_texture_index, kRawNormalIndex);
}
