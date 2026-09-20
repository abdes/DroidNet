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
#include <Oxygen/Vortex/RendererTag.h>
#include <Oxygen/Vortex/ScenePrep/MaterialRef.h>
#include <Oxygen/Vortex/Upload/UploadCoordinator.h>

#include <Oxygen/Vortex/Test/Fixtures/MaterialBinderTest.h>

namespace {

using oxygen::content::ResourceKey;
using oxygen::vortex::testing::MaterialBinderTest;

class MaterialBinderLifecycleTest : public MaterialBinderTest { };

//! Material handles for identical materials remain stable across frames.
NOLINT_TEST_F(MaterialBinderLifecycleTest, HandlesStableAcrossFrames)
{
  const ResourceKey base_color_key {
    6001U,
  };
  const ResourceKey normal_key {
    6002U,
  };

  // Frame 1
  Uploader().OnFrameStart(oxygen::vortex::internal::RendererTagFactory::Get(),
    oxygen::frame::Slot {
      1,
    });
  MatBinder().OnFrameStart(oxygen::vortex::internal::RendererTagFactory::Get(),
    oxygen::frame::Slot {
      1,
    });

  oxygen::vortex::sceneprep::MaterialRef ref;
  ref.resolved_asset = MakeMaterial({ .base_color_key = base_color_key,
    .normal_key = normal_key,
    .raw_base_color_index = 1U,
    .raw_normal_index = 2U,
    .base_color = { 0.2F, 0.3F, 0.4F, 1.0F, }, });
  ref.source_asset_key = ref.resolved_asset->GetAssetKey();
  ref.resolved_asset_key = ref.resolved_asset->GetAssetKey();

  const auto handle0 = MatBinder().GetOrAllocate(ref);
  ASSERT_TRUE(MatBinder().IsHandleValid(handle0));

  // Frame 2 (new slot) - identical material should resolve to same handle
  Uploader().OnFrameStart(oxygen::vortex::internal::RendererTagFactory::Get(),
    oxygen::frame::Slot {
      2,
    });
  MatBinder().OnFrameStart(oxygen::vortex::internal::RendererTagFactory::Get(),
    oxygen::frame::Slot {
      2,
    });

  const auto handle1 = MatBinder().GetOrAllocate(ref);
  ASSERT_TRUE(MatBinder().IsHandleValid(handle1));

  EXPECT_EQ(handle0, handle1);
}

//! Material handles remain stable when encounter order changes across frames.
NOLINT_TEST_F(MaterialBinderLifecycleTest, HandlesStableAcrossFramesWithReorder)
{
  const ResourceKey base_color_key_a {
    7101U,
  };
  const ResourceKey normal_key_a {
    7102U,
  };
  const ResourceKey base_color_key_b {
    7201U,
  };
  const ResourceKey normal_key_b {
    7202U,
  };

  oxygen::vortex::sceneprep::MaterialRef a;
  a.resolved_asset = MakeMaterial({ .base_color_key = base_color_key_a,
    .normal_key = normal_key_a,
    .raw_base_color_index = 1U,
    .raw_normal_index = 2U,
    .base_color = { 0.2F, 0.3F, 0.4F, 1.0F, }, });
  a.source_asset_key = a.resolved_asset->GetAssetKey();
  a.resolved_asset_key = a.resolved_asset->GetAssetKey();
  oxygen::vortex::sceneprep::MaterialRef b;
  b.resolved_asset = MakeMaterial({ .base_color_key = base_color_key_b,
    .normal_key = normal_key_b,
    .raw_base_color_index = 3U,
    .raw_normal_index = 4U,
    .base_color = { 0.2F, 0.3F, 0.4F, 1.0F, }, });
  b.source_asset_key = b.resolved_asset->GetAssetKey();
  b.resolved_asset_key = b.resolved_asset->GetAssetKey();

  // Frame 1: A then B
  Uploader().OnFrameStart(oxygen::vortex::internal::RendererTagFactory::Get(),
    oxygen::frame::Slot {
      1,
    });
  MatBinder().OnFrameStart(oxygen::vortex::internal::RendererTagFactory::Get(),
    oxygen::frame::Slot {
      1,
    });

  const auto ha1 = MatBinder().GetOrAllocate(a);
  const auto hb1 = MatBinder().GetOrAllocate(b);
  ASSERT_TRUE(MatBinder().IsHandleValid(ha1));
  ASSERT_TRUE(MatBinder().IsHandleValid(hb1));

  // Frame 2: B then A (reordered)
  Uploader().OnFrameStart(oxygen::vortex::internal::RendererTagFactory::Get(),
    oxygen::frame::Slot {
      2,
    });
  MatBinder().OnFrameStart(oxygen::vortex::internal::RendererTagFactory::Get(),
    oxygen::frame::Slot {
      2,
    });

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
  const ResourceKey base_color_key {
    8001U,
  };
  const ResourceKey normal_key {
    8002U,
  };

  // Case A: allocate textures first, then material
  Uploader().OnFrameStart(oxygen::vortex::internal::RendererTagFactory::Get(),
    oxygen::frame::Slot {
      1,
    });
  MatBinder().OnFrameStart(oxygen::vortex::internal::RendererTagFactory::Get(),
    oxygen::frame::Slot {
      1,
    });

  const auto texBaseA = TexBinder().GetOrAllocate(base_color_key).get();
  const auto texNormalA = TexBinder().GetOrAllocate(normal_key).get();

  oxygen::vortex::sceneprep::MaterialRef mA;
  mA.resolved_asset = MakeMaterial({ .base_color_key = base_color_key,
    .normal_key = normal_key,
    .raw_base_color_index = 111U,
    .raw_normal_index = 222U,
    .base_color = { 0.2F, 0.3F, 0.4F, 1.0F, }, });
  mA.source_asset_key = mA.resolved_asset->GetAssetKey();
  mA.resolved_asset_key = mA.resolved_asset->GetAssetKey();
  const auto handleA = MatBinder().GetOrAllocate(mA);
  ASSERT_TRUE(MatBinder().IsHandleValid(handleA));

  const auto constantsA = MaterialConstants(handleA);

  // Case B: new slot, allocate material first, then textures
  Uploader().OnFrameStart(oxygen::vortex::internal::RendererTagFactory::Get(),
    oxygen::frame::Slot {
      2,
    });
  MatBinder().OnFrameStart(oxygen::vortex::internal::RendererTagFactory::Get(),
    oxygen::frame::Slot {
      2,
    });

  oxygen::vortex::sceneprep::MaterialRef mB;
  mB.resolved_asset = MakeMaterial({ .base_color_key = base_color_key,
    .normal_key = normal_key,
    .raw_base_color_index = 111U,
    .raw_normal_index = 222U,
    .base_color = { 0.2F, 0.3F, 0.4F, 1.0F, }, });
  mB.source_asset_key = mB.resolved_asset->GetAssetKey();
  mB.resolved_asset_key = mB.resolved_asset->GetAssetKey();
  const auto handleB = MatBinder().GetOrAllocate(mB);
  ASSERT_TRUE(MatBinder().IsHandleValid(handleB));

  const auto texBaseB = TexBinder().GetOrAllocate(base_color_key).get();
  const auto texNormalB = TexBinder().GetOrAllocate(normal_key).get();

  const auto constantsB = MaterialConstants(handleB);

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
  const ResourceKey base_color_key {
    62001U,
  };
  const ResourceKey normal_key {
    62002U,
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
  ref.resolved_asset = MakeMaterial({ .base_color_key = base_color_key,
    .normal_key = normal_key,
    .raw_base_color_index = 5U,
    .raw_normal_index = 6U,
    .base_color = { 0.2F, 0.3F, 0.4F, 1.0F, }, });
  ref.source_asset_key = ref.resolved_asset->GetAssetKey();
  ref.resolved_asset_key = ref.resolved_asset->GetAssetKey();
  const auto h = MatBinder().GetOrAllocate(ref);
  ASSERT_TRUE(MatBinder().IsHandleValid(h));

  MatBinder().EnsureFrameResources();
  const auto constantsA = MaterialConstants(h);
  MatBinder().EnsureFrameResources();
  const auto constantsB = MaterialConstants(h);

  EXPECT_EQ(
    constantsA.base_color_texture_index, constantsB.base_color_texture_index);
  EXPECT_EQ(constantsA.normal_texture_index, constantsB.normal_texture_index);
}

//! Update an existing handle with new material data and ensure constants
//! change.
NOLINT_TEST_F(MaterialBinderLifecycleTest, UpdateMaterialInPlace)
{
  const ResourceKey base_color_key {
    63001U,
  };
  const ResourceKey normal_key {
    63002U,
  };

  Uploader().OnFrameStart(oxygen::vortex::internal::RendererTagFactory::Get(),
    oxygen::frame::Slot {
      1,
    });
  MatBinder().OnFrameStart(oxygen::vortex::internal::RendererTagFactory::Get(),
    oxygen::frame::Slot {
      1,
    });

  auto a = MakeMaterial({ .base_color_key = base_color_key,
    .normal_key = normal_key,
    .raw_base_color_index = 1U,
    .raw_normal_index = 2U,
    .base_color = { 0.2F, 0.3F, 0.4F, 1.0F, }, });
  oxygen::vortex::sceneprep::MaterialRef ra;
  ra.resolved_asset = a;
  ra.source_asset_key = ra.resolved_asset->GetAssetKey();
  ra.resolved_asset_key = ra.resolved_asset->GetAssetKey();

  const auto h = MatBinder().GetOrAllocate(ra);
  ASSERT_TRUE(MatBinder().IsHandleValid(h));

  const auto before = MaterialConstants(h);

  // New material uses different texture keys
  const ResourceKey new_base {
    63011U,
  };
  const ResourceKey new_normal {
    63012U,
  };
  auto b = MakeMaterial({ .base_color_key = new_base,
    .normal_key = new_normal,
    .raw_base_color_index = 11U,
    .raw_normal_index = 12U,
    .base_color = { 0.2F, 0.3F, 0.4F, 1.0F, }, });

  MatBinder().Update(h, b);
  const auto after = MaterialConstants(h);

  EXPECT_NE(before.base_color_texture_index, after.base_color_texture_index);
  EXPECT_NE(before.normal_texture_index, after.normal_texture_index);
}

//! Evicting a material invalidates its existing handle on the next frame.
NOLINT_TEST_F(MaterialBinderLifecycleTest, EvictionInvalidatesHandle)
{
  const ResourceKey base_color_key {
    70001U,
  };
  const ResourceKey normal_key {
    70002U,
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
  ref.resolved_asset = MakeMaterial({ .base_color_key = base_color_key,
    .normal_key = normal_key,
    .raw_base_color_index = 31U,
    .raw_normal_index = 32U,
    .base_color = { 0.2F, 0.3F, 0.4F, 1.0F, }, });
  ref.source_asset_key = ref.resolved_asset->GetAssetKey();
  ref.resolved_asset_key = ref.resolved_asset->GetAssetKey();

  const auto old_handle = MatBinder().GetOrAllocate(ref);
  ASSERT_TRUE(MatBinder().IsHandleValid(old_handle));

  EmitMaterialAssetEviction(
    ref.resolved_asset->GetAssetKey(), oxygen::content::EvictionReason::kClear);

  Uploader().OnFrameStart(oxygen::vortex::internal::RendererTagFactory::Get(),
    oxygen::frame::Slot {
      2,
    });
  MatBinder().OnFrameStart(oxygen::vortex::internal::RendererTagFactory::Get(),
    oxygen::frame::Slot {
      2,
    });

  EXPECT_FALSE(MatBinder().IsHandleValid(old_handle));
}

//! Evicted material slots are reclaimed and reused with bumped generation.
NOLINT_TEST_F(MaterialBinderLifecycleTest, EvictedSlotReuseBumpsGeneration)
{
  const ResourceKey base_color_key {
    71001U,
  };
  const ResourceKey normal_key {
    71002U,
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
  ref.resolved_asset = MakeMaterial({ .base_color_key = base_color_key,
    .normal_key = normal_key,
    .raw_base_color_index = 41U,
    .raw_normal_index = 42U,
    .base_color = { 0.2F, 0.3F, 0.4F, 1.0F, }, });
  ref.source_asset_key = ref.resolved_asset->GetAssetKey();
  ref.resolved_asset_key = ref.resolved_asset->GetAssetKey();

  const auto old_handle = MatBinder().GetOrAllocate(ref);
  ASSERT_TRUE(MatBinder().IsHandleValid(old_handle));

  EmitMaterialAssetEviction(
    ref.resolved_asset->GetAssetKey(), oxygen::content::EvictionReason::kClear);

  Uploader().OnFrameStart(oxygen::vortex::internal::RendererTagFactory::Get(),
    oxygen::frame::Slot {
      2,
    });
  MatBinder().OnFrameStart(oxygen::vortex::internal::RendererTagFactory::Get(),
    oxygen::frame::Slot {
      2,
    });
  EXPECT_FALSE(MatBinder().IsHandleValid(old_handle));

  // Reclaim deferred releases (same slot cycle) before allocating again.
  Uploader().OnFrameStart(oxygen::vortex::internal::RendererTagFactory::Get(),
    oxygen::frame::Slot {
      2,
    });
  MatBinder().OnFrameStart(oxygen::vortex::internal::RendererTagFactory::Get(),
    oxygen::frame::Slot {
      2,
    });

  const auto new_handle = MatBinder().GetOrAllocate(ref);
  EXPECT_TRUE(MatBinder().IsHandleValid(new_handle));
  EXPECT_EQ(new_handle.get(), old_handle.get());
  EXPECT_NE(new_handle.GenerationValue(), old_handle.GenerationValue());
}

//! Updating a handle to an existing key does not steal canonical mapping.
NOLINT_TEST_F(MaterialBinderLifecycleTest, UpdateDoesNotChangeCanonicalHandle)
{
  const ResourceKey base_color_key_a {
    7401U,
  };
  const ResourceKey normal_key_a {
    7402U,
  };
  const ResourceKey base_color_key_b {
    7501U,
  };
  const ResourceKey normal_key_b {
    7502U,
  };

  Uploader().OnFrameStart(oxygen::vortex::internal::RendererTagFactory::Get(),
    oxygen::frame::Slot {
      1,
    });
  MatBinder().OnFrameStart(oxygen::vortex::internal::RendererTagFactory::Get(),
    oxygen::frame::Slot {
      1,
    });

  oxygen::vortex::sceneprep::MaterialRef a;
  a.resolved_asset = MakeMaterial({ .base_color_key = base_color_key_a,
    .normal_key = normal_key_a,
    .raw_base_color_index = 1U,
    .raw_normal_index = 2U,
    .base_color = { 0.2F, 0.3F, 0.4F, 1.0F, }, });
  a.source_asset_key = a.resolved_asset->GetAssetKey();
  a.resolved_asset_key = a.resolved_asset->GetAssetKey();
  oxygen::vortex::sceneprep::MaterialRef b;
  b.resolved_asset = MakeMaterial({ .base_color_key = base_color_key_b,
    .normal_key = normal_key_b,
    .raw_base_color_index = 3U,
    .raw_normal_index = 4U,
    .base_color = { 0.2F, 0.3F, 0.4F, 1.0F, }, });
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
  const auto ca = MaterialConstants(ha);
  const auto cb = MaterialConstants(hb);
  EXPECT_EQ(ca.base_color_texture_index, cb.base_color_texture_index);
  EXPECT_EQ(ca.normal_texture_index, cb.normal_texture_index);
}

//! MaterialBinder teardown must remain well-defined after atlas uploads have
//! been prepared for the current frame.
NOLINT_TEST_F(
  MaterialBinderLifecycleTest, TeardownAfterEnsureFrameResourcesDoesNotHang)
{
  const ResourceKey base_color_key {
    7601U,
  };
  const ResourceKey normal_key {
    7602U,
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
  ref.resolved_asset = MakeMaterial({ .base_color_key = base_color_key,
    .normal_key = normal_key,
    .raw_base_color_index = 7U,
    .raw_normal_index = 8U,
    .base_color = { 0.2F, 0.3F, 0.4F, 1.0F, }, });
  ref.source_asset_key = ref.resolved_asset->GetAssetKey();
  ref.resolved_asset_key = ref.resolved_asset->GetAssetKey();

  const auto handle = MatBinder().GetOrAllocate(ref);
  ASSERT_TRUE(MatBinder().IsHandleValid(handle));

  MatBinder().EnsureFrameResources();
  EXPECT_NE(MatBinder().GetMaterialShadingSrvIndex(),
    oxygen::kInvalidShaderVisibleIndex);
}

} // namespace
