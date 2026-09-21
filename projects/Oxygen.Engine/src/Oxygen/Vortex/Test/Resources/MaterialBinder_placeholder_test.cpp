//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cstddef>
#include <memory>

#include <Oxygen/Content/ResourceKey.h>
#include <Oxygen/Core/Bindless/Types.h>
#include <Oxygen/Data/MaterialAsset.h>
#include <Oxygen/Testing/GTest.h>
#include <Oxygen/Vortex/RendererTag.h>
#include <Oxygen/Vortex/ScenePrep/MaterialRef.h>
#include <Oxygen/Vortex/Test/Fixtures/MaterialBinderTest.h>
#include <Oxygen/Vortex/Types/MaterialShadingConstants.h>
#include <Oxygen/Vortex/Upload/UploadCoordinator.h>

namespace {

using oxygen::content::ResourceKey;
using oxygen::vortex::testing::MaterialBinderTest;

class MaterialBinderPlaceholderTest : public MaterialBinderTest { };

//! Material constants must repoint from placeholders to final SRV indices when
//! textures become available.
NOLINT_TEST_F(
  MaterialBinderPlaceholderTest, PlaceholderRepointingUpdatesConstants)
{
  constexpr ResourceKey base_color_key {
    5001U,
  };
  constexpr ResourceKey normal_key {
    5002U,
  };

  Uploader().OnFrameStart(oxygen::vortex::internal::RendererTagFactory::Get(),
    oxygen::frame::Slot {
      1,
    });
  MatBinder().OnFrameStart(oxygen::vortex::internal::RendererTagFactory::Get(),
    oxygen::frame::Slot {
      1,
    });

  constexpr oxygen::ShaderVisibleIndex kRawBaseColorIndex {
    999999U,
  };
  constexpr oxygen::ShaderVisibleIndex kRawNormalIndex {
    888888U,
  };

  oxygen::vortex::sceneprep::MaterialRef ref;
  ref.resolved_asset = MakeMaterial({
    .base_color_key = base_color_key,
    .normal_key = normal_key,
    .raw_base_color_index = kRawBaseColorIndex.get(),
    .raw_normal_index = kRawNormalIndex.get(),
  });
  ref.source_asset_key = ref.resolved_asset->GetAssetKey();
  ref.resolved_asset_key = ref.resolved_asset->GetAssetKey();

  // Allocate material before textures exist — binder may use placeholders.
  const auto material_handle = MatBinder().GetOrAllocate(ref);
  ASSERT_TRUE(MatBinder().IsHandleValid(material_handle));

  // Now create the textures — binder is expected to repoint constants to final
  // SRV indices.
  const auto expected_base_color_srv
    = TexBinder().GetOrAllocate(base_color_key);
  const auto expected_normal_srv = TexBinder().GetOrAllocate(normal_key);

  const auto all_constants = MatBinder().GetMaterialShadingConstants();
  ASSERT_LT(
    static_cast<std::size_t>(material_handle.get()), all_constants.size());
  const auto& constants = MaterialConstants(material_handle);

  EXPECT_EQ(constants.base_color_texture_index, expected_base_color_srv);
  EXPECT_EQ(constants.normal_texture_index, expected_normal_srv);

  EXPECT_NE(constants.base_color_texture_index, kRawBaseColorIndex);
  EXPECT_NE(constants.normal_texture_index, kRawNormalIndex);
}

//! Allocate material in one frame and textures in a subsequent frame; constants
//! must repoint.
NOLINT_TEST_F(MaterialBinderPlaceholderTest, RepointingAcrossFrames)
{
  constexpr ResourceKey base_color_key {
    51001U,
  };
  constexpr ResourceKey normal_key {
    51002U,
  };

  // Frame 1: allocate material only
  Uploader().OnFrameStart(oxygen::vortex::internal::RendererTagFactory::Get(),
    oxygen::frame::Slot {
      1,
    });
  MatBinder().OnFrameStart(oxygen::vortex::internal::RendererTagFactory::Get(),
    oxygen::frame::Slot {
      1,
    });

  oxygen::vortex::sceneprep::MaterialRef ref;
  ref.resolved_asset = MakeMaterial({
    .base_color_key = base_color_key,
    .normal_key = normal_key,
    .raw_base_color_index = 9U,
    .raw_normal_index = 10U,
  });
  ref.source_asset_key = ref.resolved_asset->GetAssetKey();
  ref.resolved_asset_key = ref.resolved_asset->GetAssetKey();
  const auto h = MatBinder().GetOrAllocate(ref);
  ASSERT_TRUE(MatBinder().IsHandleValid(h));

  // Frame 2: allocate textures
  Uploader().OnFrameStart(oxygen::vortex::internal::RendererTagFactory::Get(),
    oxygen::frame::Slot {
      2,
    });
  MatBinder().OnFrameStart(oxygen::vortex::internal::RendererTagFactory::Get(),
    oxygen::frame::Slot {
      2,
    });

  const auto expectedBase = TexBinder().GetOrAllocate(base_color_key);
  const auto expectedNormal = TexBinder().GetOrAllocate(normal_key);

  const auto all_constants = MatBinder().GetMaterialShadingConstants();
  ASSERT_LT(static_cast<std::size_t>(h.get()), all_constants.size());
  const auto& constants = MaterialConstants(h);

  EXPECT_EQ(constants.base_color_texture_index, expectedBase);
  EXPECT_EQ(constants.normal_texture_index, expectedNormal);
}

//! If only one resource exists, constants must reflect available SRV and a
//! placeholder for the missing one.
NOLINT_TEST_F(MaterialBinderPlaceholderTest, PartialResourceAvailability)
{
  constexpr ResourceKey base_color_key {
    51011U,
  };
  constexpr ResourceKey normal_key {
    51012U,
  };

  Uploader().OnFrameStart(oxygen::vortex::internal::RendererTagFactory::Get(),
    oxygen::frame::Slot {
      1,
    });
  MatBinder().OnFrameStart(oxygen::vortex::internal::RendererTagFactory::Get(),
    oxygen::frame::Slot {
      1,
    });

  oxygen::vortex::sceneprep::MaterialRef ref;
  ref.resolved_asset = MakeMaterial({
    .base_color_key = base_color_key,
    .normal_key = normal_key,
    .raw_base_color_index = 123U,
    .raw_normal_index = 456U,
  });
  ref.source_asset_key = ref.resolved_asset->GetAssetKey();
  ref.resolved_asset_key = ref.resolved_asset->GetAssetKey();

  // Allocate only one texture
  const auto baseSrv = TexBinder().GetOrAllocate(base_color_key);
  const auto h = MatBinder().GetOrAllocate(ref);
  ASSERT_TRUE(MatBinder().IsHandleValid(h));

  const auto constants = MaterialConstants(h);
  EXPECT_EQ(constants.base_color_texture_index, baseSrv);
  // Normal texture not allocated yet — expect not equal to baseSrv (placeholder
  // or zero)
  EXPECT_NE(constants.normal_texture_index, baseSrv);
}

} // namespace
