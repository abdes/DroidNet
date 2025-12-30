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

  desc.base_color[0] = 0.2F;
  desc.base_color[1] = 0.3F;
  desc.base_color[2] = 0.4F;
  desc.base_color[3] = 1.0F;

  return std::make_shared<oxygen::data::MaterialAsset>(
    oxygen::data::AssetKey {}, desc,
    std::vector<oxygen::data::ShaderReference> {},
    std::vector { base_color_key, normal_key });
}

class MaterialBinderLifecycleTest : public MaterialBinderTest { };

//! Material handles for identical materials remain stable across frames.
NOLINT_TEST_F(MaterialBinderLifecycleTest, HandlesStableAcrossFrames)
{
  const ResourceKey base_color_key { 6001U };
  const ResourceKey normal_key { 6002U };

  // Frame 1
  Uploader().OnFrameStart(oxygen::renderer::internal::RendererTagFactory::Get(),
    oxygen::frame::Slot { 1 });
  MatBinder().OnFrameStart(
    oxygen::renderer::internal::RendererTagFactory::Get(),
    oxygen::frame::Slot { 1 });

  oxygen::engine::sceneprep::MaterialRef ref;
  ref.resolved_asset = MakeMaterial(base_color_key, normal_key, 1U, 2U);
  ref.source_asset_key = ref.resolved_asset->GetAssetKey();
  ref.resolved_asset_key = ref.resolved_asset->GetAssetKey();

  const auto handle0 = MatBinder().GetOrAllocate(ref);
  ASSERT_TRUE(MatBinder().IsHandleValid(handle0));

  // Frame 2 (new slot) - identical material should resolve to same handle
  Uploader().OnFrameStart(oxygen::renderer::internal::RendererTagFactory::Get(),
    oxygen::frame::Slot { 2 });
  MatBinder().OnFrameStart(
    oxygen::renderer::internal::RendererTagFactory::Get(),
    oxygen::frame::Slot { 2 });

  const auto handle1 = MatBinder().GetOrAllocate(ref);
  ASSERT_TRUE(MatBinder().IsHandleValid(handle1));

  EXPECT_EQ(handle0, handle1);
}

//! Material handles remain stable when encounter order changes across frames.
NOLINT_TEST_F(MaterialBinderLifecycleTest, HandlesStableAcrossFramesWithReorder)
{
  const ResourceKey base_color_key_a { 7101U };
  const ResourceKey normal_key_a { 7102U };
  const ResourceKey base_color_key_b { 7201U };
  const ResourceKey normal_key_b { 7202U };

  oxygen::engine::sceneprep::MaterialRef a;
  a.resolved_asset = MakeMaterial(base_color_key_a, normal_key_a, 1U, 2U);
  a.source_asset_key = a.resolved_asset->GetAssetKey();
  a.resolved_asset_key = a.resolved_asset->GetAssetKey();
  oxygen::engine::sceneprep::MaterialRef b;
  b.resolved_asset = MakeMaterial(base_color_key_b, normal_key_b, 3U, 4U);
  b.source_asset_key = b.resolved_asset->GetAssetKey();
  b.resolved_asset_key = b.resolved_asset->GetAssetKey();

  // Frame 1: A then B
  Uploader().OnFrameStart(oxygen::renderer::internal::RendererTagFactory::Get(),
    oxygen::frame::Slot { 1 });
  MatBinder().OnFrameStart(
    oxygen::renderer::internal::RendererTagFactory::Get(),
    oxygen::frame::Slot { 1 });

  const auto ha1 = MatBinder().GetOrAllocate(a);
  const auto hb1 = MatBinder().GetOrAllocate(b);
  ASSERT_TRUE(MatBinder().IsHandleValid(ha1));
  ASSERT_TRUE(MatBinder().IsHandleValid(hb1));

  // Frame 2: B then A (reordered)
  Uploader().OnFrameStart(oxygen::renderer::internal::RendererTagFactory::Get(),
    oxygen::frame::Slot { 2 });
  MatBinder().OnFrameStart(
    oxygen::renderer::internal::RendererTagFactory::Get(),
    oxygen::frame::Slot { 2 });

  const auto hb2 = MatBinder().GetOrAllocate(b);
  const auto ha2 = MatBinder().GetOrAllocate(a);
  ASSERT_TRUE(MatBinder().IsHandleValid(ha2));
  ASSERT_TRUE(MatBinder().IsHandleValid(hb2));

  EXPECT_EQ(ha1, ha2);
  EXPECT_EQ(hb1, hb2);
}

//! Material constants must be identical regardless of allocation order.
NOLINT_TEST_F(
  MaterialBinderLifecycleTest, AllocationOrderDoesNotChangeConstants)
{
  const ResourceKey base_color_key { 8001U };
  const ResourceKey normal_key { 8002U };

  // Case A: allocate textures first, then material
  Uploader().OnFrameStart(oxygen::renderer::internal::RendererTagFactory::Get(),
    oxygen::frame::Slot { 1 });
  MatBinder().OnFrameStart(
    oxygen::renderer::internal::RendererTagFactory::Get(),
    oxygen::frame::Slot { 1 });

  const auto texBaseA = TexBinder().GetOrAllocate(base_color_key).get();
  const auto texNormalA = TexBinder().GetOrAllocate(normal_key).get();

  oxygen::engine::sceneprep::MaterialRef mA;
  mA.resolved_asset = MakeMaterial(base_color_key, normal_key, 111U, 222U);
  mA.source_asset_key = mA.resolved_asset->GetAssetKey();
  mA.resolved_asset_key = mA.resolved_asset->GetAssetKey();
  const auto handleA = MatBinder().GetOrAllocate(mA);
  ASSERT_TRUE(MatBinder().IsHandleValid(handleA));

  const auto constantsA
    = MatBinder()
        // NOLINTNEXTLINE(*-pro-bounds-avoid-unchecked-container-access)
        .GetMaterialConstants()[static_cast<std::size_t>(handleA.get())];

  // Case B: new slot, allocate material first, then textures
  Uploader().OnFrameStart(oxygen::renderer::internal::RendererTagFactory::Get(),
    oxygen::frame::Slot { 2 });
  MatBinder().OnFrameStart(
    oxygen::renderer::internal::RendererTagFactory::Get(),
    oxygen::frame::Slot { 2 });

  oxygen::engine::sceneprep::MaterialRef mB;
  mB.resolved_asset = MakeMaterial(base_color_key, normal_key, 111U, 222U);
  mB.source_asset_key = mB.resolved_asset->GetAssetKey();
  mB.resolved_asset_key = mB.resolved_asset->GetAssetKey();
  const auto handleB = MatBinder().GetOrAllocate(mB);
  ASSERT_TRUE(MatBinder().IsHandleValid(handleB));

  const auto texBaseB = TexBinder().GetOrAllocate(base_color_key).get();
  const auto texNormalB = TexBinder().GetOrAllocate(normal_key).get();

  const auto constantsB
    = MatBinder()
        // NOLINTNEXTLINE(*-pro-bounds-avoid-unchecked-container-access)
        .GetMaterialConstants()[static_cast<std::size_t>(handleB.get())];

  EXPECT_EQ(
    constantsA.base_color_texture_index, constantsB.base_color_texture_index);
  EXPECT_EQ(constantsA.normal_texture_index, constantsB.normal_texture_index);
  EXPECT_EQ(texBaseA, texBaseB);
  EXPECT_EQ(texNormalA, texNormalB);
}

//! EnsureFrameResources can be called multiple times safely with no side
//! effects.
NOLINT_TEST_F(MaterialBinderLifecycleTest, EnsureFrameResourcesIdempotent)
{
  const ResourceKey base_color_key { 62001U };
  const ResourceKey normal_key { 62002U };

  Uploader().OnFrameStart(oxygen::renderer::internal::RendererTagFactory::Get(),
    oxygen::frame::Slot { 1 });
  MatBinder().OnFrameStart(
    oxygen::renderer::internal::RendererTagFactory::Get(),
    oxygen::frame::Slot { 1 });

  oxygen::engine::sceneprep::MaterialRef ref;
  ref.resolved_asset = MakeMaterial(base_color_key, normal_key, 5U, 6U);
  ref.source_asset_key = ref.resolved_asset->GetAssetKey();
  ref.resolved_asset_key = ref.resolved_asset->GetAssetKey();
  const auto h = MatBinder().GetOrAllocate(ref);
  ASSERT_TRUE(MatBinder().IsHandleValid(h));

  MatBinder().EnsureFrameResources();
  const auto constantsA
    // NOLINTNEXTLINE(*-pro-bounds-avoid-unchecked-container-access)
    = MatBinder().GetMaterialConstants()[static_cast<std::size_t>(h.get())];
  MatBinder().EnsureFrameResources();
  const auto constantsB
    // NOLINTNEXTLINE(*-pro-bounds-avoid-unchecked-container-access)
    = MatBinder().GetMaterialConstants()[static_cast<std::size_t>(h.get())];

  EXPECT_EQ(
    constantsA.base_color_texture_index, constantsB.base_color_texture_index);
  EXPECT_EQ(constantsA.normal_texture_index, constantsB.normal_texture_index);
}

//! Update an existing handle with new material data and ensure constants
//! change.
NOLINT_TEST_F(MaterialBinderLifecycleTest, UpdateMaterialInPlace)
{
  const ResourceKey base_color_key { 63001U };
  const ResourceKey normal_key { 63002U };

  Uploader().OnFrameStart(oxygen::renderer::internal::RendererTagFactory::Get(),
    oxygen::frame::Slot { 1 });
  MatBinder().OnFrameStart(
    oxygen::renderer::internal::RendererTagFactory::Get(),
    oxygen::frame::Slot { 1 });

  auto a = MakeMaterial(base_color_key, normal_key, 1U, 2U);
  oxygen::engine::sceneprep::MaterialRef ra;
  ra.resolved_asset = a;
  ra.source_asset_key = ra.resolved_asset->GetAssetKey();
  ra.resolved_asset_key = ra.resolved_asset->GetAssetKey();

  const auto h = MatBinder().GetOrAllocate(ra);
  ASSERT_TRUE(MatBinder().IsHandleValid(h));

  const auto before
    // NOLINTNEXTLINE(*-pro-bounds-avoid-unchecked-container-access)
    = MatBinder().GetMaterialConstants()[static_cast<std::size_t>(h.get())];

  // New material uses different texture keys
  const ResourceKey new_base { 63011U };
  const ResourceKey new_normal { 63012U };
  auto b = MakeMaterial(new_base, new_normal, 11U, 12U);

  MatBinder().Update(h, b);
  const auto after
    // NOLINTNEXTLINE(*-pro-bounds-avoid-unchecked-container-access)
    = MatBinder().GetMaterialConstants()[static_cast<std::size_t>(h.get())];

  EXPECT_NE(before.base_color_texture_index, after.base_color_texture_index);
  EXPECT_NE(before.normal_texture_index, after.normal_texture_index);
}

//! Updating a handle to an existing key does not steal canonical mapping.
NOLINT_TEST_F(MaterialBinderLifecycleTest, UpdateDoesNotChangeCanonicalHandle)
{
  const ResourceKey base_color_key_a { 7401U };
  const ResourceKey normal_key_a { 7402U };
  const ResourceKey base_color_key_b { 7501U };
  const ResourceKey normal_key_b { 7502U };

  Uploader().OnFrameStart(oxygen::renderer::internal::RendererTagFactory::Get(),
    oxygen::frame::Slot { 1 });
  MatBinder().OnFrameStart(
    oxygen::renderer::internal::RendererTagFactory::Get(),
    oxygen::frame::Slot { 1 });

  oxygen::engine::sceneprep::MaterialRef a;
  a.resolved_asset = MakeMaterial(base_color_key_a, normal_key_a, 1U, 2U);
  a.source_asset_key = a.resolved_asset->GetAssetKey();
  a.resolved_asset_key = a.resolved_asset->GetAssetKey();
  oxygen::engine::sceneprep::MaterialRef b;
  b.resolved_asset = MakeMaterial(base_color_key_b, normal_key_b, 3U, 4U);
  b.source_asset_key = b.resolved_asset->GetAssetKey();
  b.resolved_asset_key = b.resolved_asset->GetAssetKey();

  const auto ha = MatBinder().GetOrAllocate(a);
  const auto hb = MatBinder().GetOrAllocate(b);
  ASSERT_TRUE(MatBinder().IsHandleValid(ha));
  ASSERT_TRUE(MatBinder().IsHandleValid(hb));

  // Update hb to match the content of A.
  MatBinder().Update(hb, a.resolved_asset);

  // Canonical mapping for A must remain ha.
  const auto h_after = MatBinder().GetOrAllocate(a);
  EXPECT_EQ(h_after, ha);

  // hb remains valid and now points at A constants (duplicate content handle).
  ASSERT_TRUE(MatBinder().IsHandleValid(hb));
  const auto all = MatBinder().GetMaterialConstants();
  // NOLINTNEXTLINE(*-pro-bounds-avoid-unchecked-container-access)
  const auto ca = all[static_cast<std::size_t>(ha.get())];
  // NOLINTNEXTLINE(*-pro-bounds-avoid-unchecked-container-access)
  const auto cb = all[static_cast<std::size_t>(hb.get())];
  EXPECT_EQ(ca.base_color_texture_index, cb.base_color_texture_index);
  EXPECT_EQ(ca.normal_texture_index, cb.normal_texture_index);
}

} // namespace
