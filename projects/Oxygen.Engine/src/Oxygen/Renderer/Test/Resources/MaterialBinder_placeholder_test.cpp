//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cstdint>
#include <memory>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Content/ResourceKey.h>
#include <Oxygen/Data/MaterialAsset.h>
#include <Oxygen/Renderer/RendererTag.h>
#include <Oxygen/Renderer/ScenePrep/MaterialRef.h>
#include <Oxygen/Renderer/Types/MaterialConstants.h>
#include <Oxygen/Renderer/Upload/UploadCoordinator.h>

#include <Oxygen/Renderer/Test/Resources/MaterialBinderTest.h>

namespace {

using oxygen::content::ResourceKey;
using oxygen::renderer::testing::MaterialBinderTest;

[[nodiscard]] auto MakeMaterial(ResourceKey base_color_key,
  ResourceKey normal_key, uint32_t raw_base_color_index,
  uint32_t raw_normal_index)
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

  return std::make_shared<oxygen::data::MaterialAsset>(
    oxygen::data::AssetKey {}, desc,
    std::vector<oxygen::data::ShaderReference> {},
    std::vector { base_color_key, normal_key });
}

class MaterialBinderPlaceholderTest : public MaterialBinderTest { };

//! Material constants must repoint from placeholders to final SRV indices when
//! textures become available.
NOLINT_TEST_F(
  MaterialBinderPlaceholderTest, PlaceholderRepointingUpdatesConstants)
{
  constexpr ResourceKey base_color_key { 5001U };
  constexpr ResourceKey normal_key { 5002U };

  Uploader().OnFrameStart(oxygen::renderer::internal::RendererTagFactory::Get(),
    oxygen::frame::Slot { 1 });
  MatBinder().OnFrameStart(
    oxygen::renderer::internal::RendererTagFactory::Get(),
    oxygen::frame::Slot { 1 });

  constexpr uint32_t kRawBaseColorIndex = 999999U;
  constexpr uint32_t kRawNormalIndex = 888888U;

  oxygen::engine::sceneprep::MaterialRef ref;
  ref.resolved_asset = MakeMaterial(
    base_color_key, normal_key, kRawBaseColorIndex, kRawNormalIndex);
  ref.source_asset_key = ref.resolved_asset->GetAssetKey();
  ref.resolved_asset_key = ref.resolved_asset->GetAssetKey();

  // Allocate material before textures exist — binder may use placeholders.
  const auto material_handle = MatBinder().GetOrAllocate(ref);
  ASSERT_TRUE(MatBinder().IsHandleValid(material_handle));

  // Now create the textures — binder is expected to repoint constants to final
  // SRV indices.
  const auto expected_base_color_srv
    = TexBinder().GetOrAllocate(base_color_key).get();
  const auto expected_normal_srv = TexBinder().GetOrAllocate(normal_key).get();

  const auto all_constants = MatBinder().GetMaterialConstants();
  ASSERT_LT(
    static_cast<std::size_t>(material_handle.get()), all_constants.size());
  const auto& constants
    // NOLINTNEXTLINE(*-pro-bounds-avoid-unchecked-container-access)
    = all_constants[static_cast<std::size_t>(material_handle.get())];

  EXPECT_EQ(constants.base_color_texture_index, expected_base_color_srv);
  EXPECT_EQ(constants.normal_texture_index, expected_normal_srv);

  EXPECT_NE(constants.base_color_texture_index, kRawBaseColorIndex);
  EXPECT_NE(constants.normal_texture_index, kRawNormalIndex);
}

//! Allocate material in one frame and textures in a subsequent frame; constants
//! must repoint.
NOLINT_TEST_F(MaterialBinderPlaceholderTest, RepointingAcrossFrames)
{
  constexpr ResourceKey base_color_key { 51001U };
  constexpr ResourceKey normal_key { 51002U };

  // Frame 1: allocate material only
  Uploader().OnFrameStart(oxygen::renderer::internal::RendererTagFactory::Get(),
    oxygen::frame::Slot { 1 });
  MatBinder().OnFrameStart(
    oxygen::renderer::internal::RendererTagFactory::Get(),
    oxygen::frame::Slot { 1 });

  oxygen::engine::sceneprep::MaterialRef ref;
  ref.resolved_asset = MakeMaterial(base_color_key, normal_key, 9U, 10U);
  ref.source_asset_key = ref.resolved_asset->GetAssetKey();
  ref.resolved_asset_key = ref.resolved_asset->GetAssetKey();
  const auto h = MatBinder().GetOrAllocate(ref);
  ASSERT_TRUE(MatBinder().IsHandleValid(h));

  // Frame 2: allocate textures
  Uploader().OnFrameStart(oxygen::renderer::internal::RendererTagFactory::Get(),
    oxygen::frame::Slot { 2 });
  MatBinder().OnFrameStart(
    oxygen::renderer::internal::RendererTagFactory::Get(),
    oxygen::frame::Slot { 2 });

  const auto expectedBase = TexBinder().GetOrAllocate(base_color_key).get();
  const auto expectedNormal = TexBinder().GetOrAllocate(normal_key).get();

  const auto all_constants = MatBinder().GetMaterialConstants();
  ASSERT_LT(static_cast<std::size_t>(h.get()), all_constants.size());
  // NOLINTNEXTLINE(*-pro-bounds-avoid-unchecked-container-access)
  const auto& constants = all_constants[static_cast<std::size_t>(h.get())];

  EXPECT_EQ(constants.base_color_texture_index, expectedBase);
  EXPECT_EQ(constants.normal_texture_index, expectedNormal);
}

//! If only one resource exists, constants must reflect available SRV and a
//! placeholder for the missing one.
NOLINT_TEST_F(MaterialBinderPlaceholderTest, PartialResourceAvailability)
{
  constexpr ResourceKey base_color_key { 51011U };
  constexpr ResourceKey normal_key { 51012U };

  Uploader().OnFrameStart(oxygen::renderer::internal::RendererTagFactory::Get(),
    oxygen::frame::Slot { 1 });
  MatBinder().OnFrameStart(
    oxygen::renderer::internal::RendererTagFactory::Get(),
    oxygen::frame::Slot { 1 });

  oxygen::engine::sceneprep::MaterialRef ref;
  ref.resolved_asset = MakeMaterial(base_color_key, normal_key, 123U, 456U);
  ref.source_asset_key = ref.resolved_asset->GetAssetKey();
  ref.resolved_asset_key = ref.resolved_asset->GetAssetKey();

  // Allocate only one texture
  const auto baseSrv = TexBinder().GetOrAllocate(base_color_key).get();
  const auto h = MatBinder().GetOrAllocate(ref);
  ASSERT_TRUE(MatBinder().IsHandleValid(h));

  const auto constants
    // NOLINTNEXTLINE(*-pro-bounds-avoid-unchecked-container-access)
    = MatBinder().GetMaterialConstants()[static_cast<std::size_t>(h.get())];
  EXPECT_EQ(constants.base_color_texture_index, baseSrv);
  // Normal texture not allocated yet — expect not equal to baseSrv (placeholder
  // or zero)
  EXPECT_NE(constants.normal_texture_index, baseSrv);
}

} // namespace
