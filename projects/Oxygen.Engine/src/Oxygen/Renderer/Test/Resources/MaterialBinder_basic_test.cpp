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

  // Non-zero defaults so we can distinguish from memset/zero init.
  desc.base_color[0] = 1.0F;
  desc.base_color[1] = 0.5F;
  desc.base_color[2] = 0.25F;
  desc.base_color[3] = 1.0F;

  return std::make_shared<oxygen::data::MaterialAsset>(desc,
    std::vector<oxygen::data::ShaderReference> {},
    std::vector<oxygen::content::ResourceKey> { base_color_key, normal_key });
}

class MaterialBinderBasicTest : public MaterialBinderTest { };

//! Material binder must return stable handles for identical inputs.
NOLINT_TEST_F(MaterialBinderBasicTest, SameMaterialReturnsSameHandle)
{
  const ResourceKey base_color_key { 1001U };
  const ResourceKey normal_key { 1002U };

  Uploader().OnFrameStart(oxygen::renderer::internal::RendererTagFactory::Get(),
    oxygen::frame::Slot { 1 });
  MaterialBinderRef().OnFrameStart(
    oxygen::renderer::internal::RendererTagFactory::Get(),
    oxygen::frame::Slot { 1 });

  oxygen::engine::sceneprep::MaterialRef ref;
  ref.asset = MakeMaterial(base_color_key, normal_key, 100000U, 200000U);

  const auto handle0 = MaterialBinderRef().GetOrAllocate(ref);
  const auto handle1 = MaterialBinderRef().GetOrAllocate(ref);

  EXPECT_TRUE(MaterialBinderRef().IsValidHandle(handle0));
  EXPECT_TRUE(MaterialBinderRef().IsValidHandle(handle1));
  EXPECT_EQ(handle0, handle1);
}

//! Different materials must yield distinct handles.
NOLINT_TEST_F(MaterialBinderBasicTest, DifferentMaterialsReturnDifferentHandle)
{
  const ResourceKey base_color_key0 { 3001U };
  const ResourceKey normal_key0 { 3002U };

  const ResourceKey base_color_key1 { 4001U };
  const ResourceKey normal_key1 { 4002U };

  Uploader().OnFrameStart(oxygen::renderer::internal::RendererTagFactory::Get(),
    oxygen::frame::Slot { 1 });
  MaterialBinderRef().OnFrameStart(
    oxygen::renderer::internal::RendererTagFactory::Get(),
    oxygen::frame::Slot { 1 });

  oxygen::engine::sceneprep::MaterialRef ref0;
  ref0.asset = MakeMaterial(base_color_key0, normal_key0, 10U, 20U);

  oxygen::engine::sceneprep::MaterialRef ref1;
  ref1.asset = MakeMaterial(base_color_key1, normal_key1, 11U, 21U);

  const auto handle0 = MaterialBinderRef().GetOrAllocate(ref0);
  const auto handle1 = MaterialBinderRef().GetOrAllocate(ref1);

  EXPECT_TRUE(MaterialBinderRef().IsValidHandle(handle0));
  EXPECT_TRUE(MaterialBinderRef().IsValidHandle(handle1));
  EXPECT_NE(handle0, handle1);
}

//! Requesting with a null material must return an invalid handle.
NOLINT_TEST_F(MaterialBinderBasicTest, HandleNullAndInvalid)
{
  Uploader().OnFrameStart(oxygen::renderer::internal::RendererTagFactory::Get(),
    oxygen::frame::Slot { 1 });
  MaterialBinderRef().OnFrameStart(
    oxygen::renderer::internal::RendererTagFactory::Get(),
    oxygen::frame::Slot { 1 });

  oxygen::engine::sceneprep::MaterialRef ref;
  ref.asset = nullptr;

  const auto handle = MaterialBinderRef().GetOrAllocate(ref);
  EXPECT_FALSE(MaterialBinderRef().IsValidHandle(handle));
}

//! Identical material content should deduplicate (same handle returned).
NOLINT_TEST_F(MaterialBinderBasicTest, ContentEqualityDedupes)
{
  const ResourceKey base_color_key { 11001U };
  const ResourceKey normal_key { 11002U };

  Uploader().OnFrameStart(oxygen::renderer::internal::RendererTagFactory::Get(),
    oxygen::frame::Slot { 1 });
  MaterialBinderRef().OnFrameStart(
    oxygen::renderer::internal::RendererTagFactory::Get(),
    oxygen::frame::Slot { 1 });

  auto a = MakeMaterial(base_color_key, normal_key, 1U, 2U);
  auto b = MakeMaterial(base_color_key, normal_key, 1U, 2U);

  oxygen::engine::sceneprep::MaterialRef ra, rb;
  ra.asset = a;
  rb.asset = b;

  const auto ha = MaterialBinderRef().GetOrAllocate(ra);
  const auto hb = MaterialBinderRef().GetOrAllocate(rb);

  EXPECT_TRUE(MaterialBinderRef().IsValidHandle(ha));
  EXPECT_TRUE(MaterialBinderRef().IsValidHandle(hb));
  EXPECT_EQ(ha, hb);
}
} // namespace
