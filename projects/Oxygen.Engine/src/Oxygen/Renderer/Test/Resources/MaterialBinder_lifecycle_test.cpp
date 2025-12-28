//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Testing/GTest.h>

#include <cstdint>
#include <memory>

#include <Oxygen/Content/ResourceKey.h>
#include <Oxygen/Data/MaterialAsset.h>
#include <Oxygen/Renderer/RendererTag.h>
#include <Oxygen/Renderer/ScenePrep/MaterialRef.h>
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

  return std::make_shared<oxygen::data::MaterialAsset>(desc,
    std::vector<oxygen::data::ShaderReference> {},
    std::vector<oxygen::content::ResourceKey> { base_color_key, normal_key });
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
  MaterialBinderRef().OnFrameStart(
    oxygen::renderer::internal::RendererTagFactory::Get(),
    oxygen::frame::Slot { 1 });

  oxygen::engine::sceneprep::MaterialRef ref;
  ref.asset = MakeMaterial(base_color_key, normal_key, 1U, 2U);

  const auto handle0 = MaterialBinderRef().GetOrAllocate(ref);
  ASSERT_TRUE(MaterialBinderRef().IsValidHandle(handle0));

  // Frame 2 (new slot) - identical material should resolve to same handle
  Uploader().OnFrameStart(oxygen::renderer::internal::RendererTagFactory::Get(),
    oxygen::frame::Slot { 2 });
  MaterialBinderRef().OnFrameStart(
    oxygen::renderer::internal::RendererTagFactory::Get(),
    oxygen::frame::Slot { 2 });

  const auto handle1 = MaterialBinderRef().GetOrAllocate(ref);
  ASSERT_TRUE(MaterialBinderRef().IsValidHandle(handle1));

  EXPECT_EQ(handle0, handle1);
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
  MaterialBinderRef().OnFrameStart(
    oxygen::renderer::internal::RendererTagFactory::Get(),
    oxygen::frame::Slot { 1 });

  const auto texBaseA = TextureBinderRef().GetOrAllocate(base_color_key).get();
  const auto texNormalA = TextureBinderRef().GetOrAllocate(normal_key).get();

  oxygen::engine::sceneprep::MaterialRef mA;
  mA.asset = MakeMaterial(base_color_key, normal_key, 111U, 222U);
  const auto handleA = MaterialBinderRef().GetOrAllocate(mA);
  ASSERT_TRUE(MaterialBinderRef().IsValidHandle(handleA));

  const auto constantsA
    = MaterialBinderRef()
        .GetMaterialConstants()[static_cast<std::size_t>(handleA.get())];

  // Case B: new slot, allocate material first, then textures
  Uploader().OnFrameStart(oxygen::renderer::internal::RendererTagFactory::Get(),
    oxygen::frame::Slot { 2 });
  MaterialBinderRef().OnFrameStart(
    oxygen::renderer::internal::RendererTagFactory::Get(),
    oxygen::frame::Slot { 2 });

  oxygen::engine::sceneprep::MaterialRef mB;
  mB.asset = MakeMaterial(base_color_key, normal_key, 111U, 222U);
  const auto handleB = MaterialBinderRef().GetOrAllocate(mB);
  ASSERT_TRUE(MaterialBinderRef().IsValidHandle(handleB));

  const auto texBaseB = TextureBinderRef().GetOrAllocate(base_color_key).get();
  const auto texNormalB = TextureBinderRef().GetOrAllocate(normal_key).get();

  const auto constantsB
    = MaterialBinderRef()
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
  MaterialBinderRef().OnFrameStart(
    oxygen::renderer::internal::RendererTagFactory::Get(),
    oxygen::frame::Slot { 1 });

  oxygen::engine::sceneprep::MaterialRef ref;
  ref.asset = MakeMaterial(base_color_key, normal_key, 5U, 6U);
  const auto h = MaterialBinderRef().GetOrAllocate(ref);
  ASSERT_TRUE(MaterialBinderRef().IsValidHandle(h));

  MaterialBinderRef().EnsureFrameResources();
  const auto constantsA
    = MaterialBinderRef()
        .GetMaterialConstants()[static_cast<std::size_t>(h.get())];
  MaterialBinderRef().EnsureFrameResources();
  const auto constantsB
    = MaterialBinderRef()
        .GetMaterialConstants()[static_cast<std::size_t>(h.get())];

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
  MaterialBinderRef().OnFrameStart(
    oxygen::renderer::internal::RendererTagFactory::Get(),
    oxygen::frame::Slot { 1 });

  auto a = MakeMaterial(base_color_key, normal_key, 1U, 2U);
  oxygen::engine::sceneprep::MaterialRef ra;
  ra.asset = a;

  const auto h = MaterialBinderRef().GetOrAllocate(ra);
  ASSERT_TRUE(MaterialBinderRef().IsValidHandle(h));

  const auto before
    = MaterialBinderRef()
        .GetMaterialConstants()[static_cast<std::size_t>(h.get())];

  // New material uses different texture keys
  const ResourceKey new_base { 63011U };
  const ResourceKey new_normal { 63012U };
  auto b = MakeMaterial(new_base, new_normal, 11U, 12U);

  MaterialBinderRef().Update(h, b);
  const auto after
    = MaterialBinderRef()
        .GetMaterialConstants()[static_cast<std::size_t>(h.get())];

  EXPECT_NE(before.base_color_texture_index, after.base_color_texture_index);
  EXPECT_NE(before.normal_texture_index, after.normal_texture_index);
}

} // namespace
