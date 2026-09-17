//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <array>
#include <cstdint>
#include <memory>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Content/ResourceKey.h>
#include <Oxygen/Data/MaterialAsset.h>
#include <Oxygen/Vortex/RendererTag.h>
#include <Oxygen/Vortex/ScenePrep/MaterialRef.h>
#include <Oxygen/Vortex/Types/MaterialShadingConstants.h>
#include <Oxygen/Vortex/Upload/UploadCoordinator.h>

#include <Oxygen/Vortex/Test/Fixtures/MaterialBinderTest.h>

namespace {

using oxygen::content::ResourceKey;
using oxygen::vortex::testing::MaterialBinderTest;

[[nodiscard]] auto MakeMaterial(ResourceKey base_color_key,
  ResourceKey normal_key, uint32_t raw_base_color_index,
  uint32_t raw_normal_index)
  -> std::shared_ptr<const oxygen::data::MaterialAsset>
{
  using oxygen::data::pak::render::MaterialAssetDesc;

  MaterialAssetDesc desc {};
  desc.base_color_texture
    = oxygen::data::pak::core::ResourceIndexT { raw_base_color_index };
  desc.normal_texture
    = oxygen::data::pak::core::ResourceIndexT { raw_normal_index };

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

class MaterialBinderBasicTest : public MaterialBinderTest { };

//! Material binder must return stable handles for identical inputs.
NOLINT_TEST_F(MaterialBinderBasicTest, SameMaterialReturnsSameHandle)
{
  const ResourceKey base_color_key { 1001U };
  const ResourceKey normal_key { 1002U };

  Uploader().OnFrameStart(oxygen::vortex::internal::RendererTagFactory::Get(),
    oxygen::frame::Slot { 1 });
  MatBinder().OnFrameStart(oxygen::vortex::internal::RendererTagFactory::Get(),
    oxygen::frame::Slot { 1 });

  oxygen::vortex::sceneprep::MaterialRef ref;
  ref.resolved_asset
    = MakeMaterial(base_color_key, normal_key, 100000U, 200000U);
  ref.source_asset_key = ref.resolved_asset->GetAssetKey();
  ref.resolved_asset_key = ref.resolved_asset->GetAssetKey();

  const auto handle0 = MatBinder().GetOrAllocate(ref);
  const auto handle1 = MatBinder().GetOrAllocate(ref);

  EXPECT_TRUE(MatBinder().IsHandleValid(handle0));
  EXPECT_TRUE(MatBinder().IsHandleValid(handle1));
  EXPECT_EQ(handle0, handle1);
}

//! Different materials must yield distinct handles.
NOLINT_TEST_F(MaterialBinderBasicTest, DifferentMaterialsReturnDifferentHandle)
{
  const ResourceKey base_color_key0 { 3001U };
  const ResourceKey normal_key0 { 3002U };

  const ResourceKey base_color_key1 { 4001U };
  const ResourceKey normal_key1 { 4002U };

  Uploader().OnFrameStart(oxygen::vortex::internal::RendererTagFactory::Get(),
    oxygen::frame::Slot { 1 });
  MatBinder().OnFrameStart(oxygen::vortex::internal::RendererTagFactory::Get(),
    oxygen::frame::Slot { 1 });

  oxygen::vortex::sceneprep::MaterialRef ref0;
  ref0.resolved_asset = MakeMaterial(base_color_key0, normal_key0, 10U, 20U);
  ref0.source_asset_key = ref0.resolved_asset->GetAssetKey();
  ref0.resolved_asset_key = ref0.resolved_asset->GetAssetKey();

  oxygen::vortex::sceneprep::MaterialRef ref1;
  ref1.resolved_asset = MakeMaterial(base_color_key1, normal_key1, 11U, 21U);
  ref1.source_asset_key = ref1.resolved_asset->GetAssetKey();
  ref1.resolved_asset_key = ref1.resolved_asset->GetAssetKey();

  const auto handle0 = MatBinder().GetOrAllocate(ref0);
  const auto handle1 = MatBinder().GetOrAllocate(ref1);

  EXPECT_TRUE(MatBinder().IsHandleValid(handle0));
  EXPECT_TRUE(MatBinder().IsHandleValid(handle1));
  EXPECT_NE(handle0, handle1);
}

//! Distinct emitted radiance must survive content-based material deduplication.
NOLINT_TEST_F(
  MaterialBinderBasicTest, EmissiveIdentityPreservesAllChannelsAndHalfEndpoints)
{
  using oxygen::data::HalfFloat;
  using oxygen::data::MaterialAsset;
  using oxygen::vortex::sceneprep::MaterialRef;
  Uploader().OnFrameStart(oxygen::vortex::internal::RendererTagFactory::Get(),
    oxygen::frame::Slot { 1 });
  MatBinder().OnFrameStart(oxygen::vortex::internal::RendererTagFactory::Get(),
    oxygen::frame::Slot { 1 });
  const std::array colors { std::array { 0.0F, 0.0F, 0.0F },
    std::array { 0x1p-24F, 0.0F, 0.0F }, std::array { 0.0F, 0x1p-24F, 0.0F },
    std::array { 0.0F, 0.0F, 0x1p-24F }, std::array { 0x1p-23F, 0.0F, 0.0F },
    std::array { 1.0F, 2.0F, 4.0F },
    std::array { 65504.0F, 65504.0F, 65504.0F } };
  std::vector<oxygen::vortex::sceneprep::MaterialHandle> handles;
  const auto make_ref = [&](const auto& rgb) {
    oxygen::data::pak::render::MaterialAssetDesc desc {};
    desc.material_domain
      = static_cast<std::uint8_t>(oxygen::data::MaterialDomain::kOpaque);
    desc.flags = oxygen::data::pak::render::kMaterialFlag_NoTextureSampling;
    for (unsigned c = 0; c < 3; ++c)
      desc.emissive_factor[c] = HalfFloat { rgb[c] };
    return MaterialRef { .resolved_asset
      = std::make_shared<const MaterialAsset>(oxygen::data::AssetKey {}, desc,
        std::vector<oxygen::data::ShaderReference> {}) };
  };
  for (const auto& rgb : colors) {
    const auto handle = MatBinder().GetOrAllocate(make_ref(rgb));
    ASSERT_TRUE(MatBinder().IsHandleValid(handle));
    for (const auto previous : handles)
      EXPECT_NE(handle, previous);
    handles.push_back(handle);
  }
  ASSERT_EQ(MatBinder().GetMaterialShadingConstants().size(), colors.size());
  for (std::size_t i = 0; i < colors.size(); ++i) {
    // Equal emission still deduplicates, including tiny positive values.
    EXPECT_EQ(MatBinder().GetOrAllocate(make_ref(colors[i])), handles[i]);
    const auto constants = MatBinder().GetMaterialShadingConstants();
    EXPECT_TRUE(std::ranges::any_of(constants, [&](const auto& value) {
      return value.emissive_factor
        == glm::vec3 { colors[i][0], colors[i][1], colors[i][2] };
    }));
  }
}

//! Requesting with a null material must return an invalid handle.
NOLINT_TEST_F(MaterialBinderBasicTest, HandleNullAndInvalid)
{
  Uploader().OnFrameStart(oxygen::vortex::internal::RendererTagFactory::Get(),
    oxygen::frame::Slot { 1 });
  MatBinder().OnFrameStart(oxygen::vortex::internal::RendererTagFactory::Get(),
    oxygen::frame::Slot { 1 });

  oxygen::vortex::sceneprep::MaterialRef ref;
  ref.resolved_asset = nullptr;

  const auto handle = MatBinder().GetOrAllocate(ref);
  EXPECT_FALSE(MatBinder().IsHandleValid(handle));
}

//! Identical material content should deduplicate (same handle returned).
NOLINT_TEST_F(MaterialBinderBasicTest, ContentEqualityDedupes)
{
  const ResourceKey base_color_key { 11001U };
  const ResourceKey normal_key { 11002U };

  Uploader().OnFrameStart(oxygen::vortex::internal::RendererTagFactory::Get(),
    oxygen::frame::Slot { 1 });
  MatBinder().OnFrameStart(oxygen::vortex::internal::RendererTagFactory::Get(),
    oxygen::frame::Slot { 1 });

  auto a = MakeMaterial(base_color_key, normal_key, 1U, 2U);
  auto b = MakeMaterial(base_color_key, normal_key, 1U, 2U);

  oxygen::vortex::sceneprep::MaterialRef ra;
  oxygen::vortex::sceneprep::MaterialRef rb;
  ra.resolved_asset = a;
  rb.resolved_asset = b;
  ra.source_asset_key = ra.resolved_asset->GetAssetKey();
  ra.resolved_asset_key = ra.resolved_asset->GetAssetKey();
  rb.source_asset_key = rb.resolved_asset->GetAssetKey();
  rb.resolved_asset_key = rb.resolved_asset->GetAssetKey();

  const auto ha = MatBinder().GetOrAllocate(ra);
  const auto hb = MatBinder().GetOrAllocate(rb);

  EXPECT_TRUE(MatBinder().IsHandleValid(ha));
  EXPECT_TRUE(MatBinder().IsHandleValid(hb));
  EXPECT_EQ(ha, hb);
}

//! Deduplication is based on ResourceKeys, not raw author indices.
NOLINT_TEST_F(MaterialBinderBasicTest, DedupIgnoresRawAuthorIndicesForSameKeys)
{
  const ResourceKey base_color_key { 11101U };
  const ResourceKey normal_key { 11102U };

  Uploader().OnFrameStart(oxygen::vortex::internal::RendererTagFactory::Get(),
    oxygen::frame::Slot { 1 });
  MatBinder().OnFrameStart(oxygen::vortex::internal::RendererTagFactory::Get(),
    oxygen::frame::Slot { 1 });

  auto a = MakeMaterial(base_color_key, normal_key, 1U, 2U);
  auto b = MakeMaterial(base_color_key, normal_key, 999999U, 888888U);

  oxygen::vortex::sceneprep::MaterialRef ra;
  oxygen::vortex::sceneprep::MaterialRef rb;
  ra.resolved_asset = a;
  rb.resolved_asset = b;
  ra.source_asset_key = ra.resolved_asset->GetAssetKey();
  ra.resolved_asset_key = ra.resolved_asset->GetAssetKey();
  rb.source_asset_key = rb.resolved_asset->GetAssetKey();
  rb.resolved_asset_key = rb.resolved_asset->GetAssetKey();

  const auto ha = MatBinder().GetOrAllocate(ra);
  const auto hb = MatBinder().GetOrAllocate(rb);

  EXPECT_TRUE(MatBinder().IsHandleValid(ha));
  EXPECT_TRUE(MatBinder().IsHandleValid(hb));
  EXPECT_EQ(ha, hb);
}
} // namespace
