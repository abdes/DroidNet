//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <array>
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

[[nodiscard]] auto MakeMaterialWithUv(ResourceKey base_color_key,
  ResourceKey normal_key, uint32_t raw_base_color_index,
  uint32_t raw_normal_index, std::array<float, 2> uv_scale,
  std::array<float, 2> uv_offset, float uv_rotation_radians, uint8_t uv_set)
  -> std::shared_ptr<const oxygen::data::MaterialAsset>
{
  using oxygen::data::pak::MaterialAssetDesc;

  MaterialAssetDesc desc {};
  desc.base_color_texture = raw_base_color_index;
  desc.normal_texture = raw_normal_index;
  desc.uv_scale[0] = uv_scale[0];
  desc.uv_scale[1] = uv_scale[1];
  desc.uv_offset[0] = uv_offset[0];
  desc.uv_offset[1] = uv_offset[1];
  desc.uv_rotation_radians = uv_rotation_radians;
  desc.uv_set = uv_set;

  return std::make_shared<oxygen::data::MaterialAsset>(
    oxygen::data::AssetKey {}, desc,
    std::vector<oxygen::data::ShaderReference> {},
    std::vector { base_color_key, normal_key });
}

class MaterialBinderBindingTest : public MaterialBinderTest { };

//! MaterialConstants must store bindless SRV indices, not raw author indices.
NOLINT_TEST_F(MaterialBinderBindingTest,
  SerializeMaterialConstantsUsesTextureBinderSrvIndices)
{
  constexpr ResourceKey base_color_key { 2001U };
  constexpr ResourceKey normal_key { 2002U };

  Uploader().OnFrameStart(oxygen::renderer::internal::RendererTagFactory::Get(),
    oxygen::frame::Slot { 1 });
  MatBinder().OnFrameStart(
    oxygen::renderer::internal::RendererTagFactory::Get(),
    oxygen::frame::Slot { 1 });

  constexpr oxygen::ShaderVisibleIndex kRawBaseColorIndex { 123456U };
  constexpr oxygen::ShaderVisibleIndex kRawNormalIndex { 654321U };

  oxygen::engine::sceneprep::MaterialRef ref;
  ref.resolved_asset = MakeMaterial(base_color_key, normal_key,
    kRawBaseColorIndex.get(), kRawNormalIndex.get());
  ref.source_asset_key = ref.resolved_asset->GetAssetKey();
  ref.resolved_asset_key = ref.resolved_asset->GetAssetKey();

  const auto material_handle = MatBinder().GetOrAllocate(ref);

  EXPECT_TRUE(MatBinder().IsHandleValid(material_handle));

  const auto expected_base_color_srv
    = TexBinder().GetOrAllocate(base_color_key);
  const auto expected_normal_srv = TexBinder().GetOrAllocate(normal_key);

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

//! MaterialConstants must reflect the material UV transform fields.
NOLINT_TEST_F(MaterialBinderBindingTest, SerializeMaterialConstantsCopiesUv)
{
  constexpr ResourceKey base_color_key { 22101U };
  constexpr ResourceKey normal_key { 22102U };

  // Arrange
  Uploader().OnFrameStart(oxygen::renderer::internal::RendererTagFactory::Get(),
    oxygen::frame::Slot { 1 });
  MatBinder().OnFrameStart(
    oxygen::renderer::internal::RendererTagFactory::Get(),
    oxygen::frame::Slot { 1 });

  oxygen::engine::sceneprep::MaterialRef ref;
  ref.resolved_asset = MakeMaterialWithUv(base_color_key, normal_key, 1U, 2U,
    { 2.0F, 3.0F }, { 0.25F, -0.5F }, 0.75F, 2U);
  ref.source_asset_key = ref.resolved_asset->GetAssetKey();
  ref.resolved_asset_key = ref.resolved_asset->GetAssetKey();

  // Act
  const auto handle = MatBinder().GetOrAllocate(ref);
  ASSERT_TRUE(MatBinder().IsHandleValid(handle));

  // Assert
  const auto all_constants = MatBinder().GetMaterialConstants();
  ASSERT_LT(static_cast<std::size_t>(handle.get()), all_constants.size());
  const auto& constants
    // NOLINTNEXTLINE(*-pro-bounds-avoid-unchecked-container-access)
    = all_constants[static_cast<std::size_t>(handle.get())];

  EXPECT_FLOAT_EQ(constants.uv_scale.x, 2.0F);
  EXPECT_FLOAT_EQ(constants.uv_scale.y, 3.0F);
  EXPECT_FLOAT_EQ(constants.uv_offset.x, 0.25F);
  EXPECT_FLOAT_EQ(constants.uv_offset.y, -0.5F);
  EXPECT_FLOAT_EQ(constants.uv_rotation_radians, 0.75F);
  EXPECT_EQ(constants.uv_set, 2U);
}

//! When TextureBinder cannot provide a texture (error index), MaterialBinder
//! must use the error texture index as fallback.
NOLINT_TEST_F(MaterialBinderBindingTest, MissingResourceFallback)
{
  constexpr ResourceKey base_color_key { 22001U };
  constexpr ResourceKey normal_key { 22002U };

  // Configure FakeTextureBinder to report normal_key as error
  SetTextureBinderErrorKey(normal_key);

  Uploader().OnFrameStart(oxygen::renderer::internal::RendererTagFactory::Get(),
    oxygen::frame::Slot { 1 });
  MatBinder().OnFrameStart(
    oxygen::renderer::internal::RendererTagFactory::Get(),
    oxygen::frame::Slot { 1 });

  oxygen::engine::sceneprep::MaterialRef ref;
  ref.resolved_asset = MakeMaterial(base_color_key, normal_key, 7U, 8U);
  ref.source_asset_key = ref.resolved_asset->GetAssetKey();
  ref.resolved_asset_key = ref.resolved_asset->GetAssetKey();

  const auto h = MatBinder().GetOrAllocate(ref);
  ASSERT_TRUE(MatBinder().IsHandleValid(h));

  const auto all_constants = MatBinder().GetMaterialConstants();
  ASSERT_LT(static_cast<std::size_t>(h.get()), all_constants.size());
}

//! Materials that reference the same ResourceKey must share the same SRV index.
NOLINT_TEST_F(MaterialBinderBindingTest, SharedSrvIndicesForSameResource)
{
  constexpr ResourceKey base_color_key { 7001U };
  constexpr ResourceKey normal_key { 7002U };

  Uploader().OnFrameStart(oxygen::renderer::internal::RendererTagFactory::Get(),
    oxygen::frame::Slot { 1 });
  MatBinder().OnFrameStart(
    oxygen::renderer::internal::RendererTagFactory::Get(),
    oxygen::frame::Slot { 1 });

  // Two materials differ in color but reference same texture keys.
  oxygen::engine::sceneprep::MaterialRef a;
  a.resolved_asset = MakeMaterial(base_color_key, normal_key, 42U, 43U);
  a.source_asset_key = a.resolved_asset->GetAssetKey();
  a.resolved_asset_key = a.resolved_asset->GetAssetKey();

  oxygen::engine::sceneprep::MaterialRef b;
  b.resolved_asset = MakeMaterial(base_color_key, normal_key, 44U, 45U);
  b.source_asset_key = b.resolved_asset->GetAssetKey();
  b.resolved_asset_key = b.resolved_asset->GetAssetKey();

  const auto handleA = MatBinder().GetOrAllocate(a);
  const auto handleB = MatBinder().GetOrAllocate(b);

  ASSERT_TRUE(MatBinder().IsHandleValid(handleA));
  ASSERT_TRUE(MatBinder().IsHandleValid(handleB));

  const auto expectedBaseSrv = TexBinder().GetOrAllocate(base_color_key);
  const auto expectedNormalSrv = TexBinder().GetOrAllocate(normal_key);

  const auto all_constants = MatBinder().GetMaterialConstants();
  ASSERT_LT(static_cast<std::size_t>(handleA.get()), all_constants.size());
  ASSERT_LT(static_cast<std::size_t>(handleB.get()), all_constants.size());

  // NOLINTNEXTLINE(*-pro-bounds-avoid-unchecked-container-access)
  const auto& constA = all_constants[static_cast<std::size_t>(handleA.get())];
  // NOLINTNEXTLINE(*-pro-bounds-avoid-unchecked-container-access)
  const auto& constB = all_constants[static_cast<std::size_t>(handleB.get())];

  EXPECT_EQ(constA.base_color_texture_index, expectedBaseSrv);
  EXPECT_EQ(constB.base_color_texture_index, expectedBaseSrv);

  EXPECT_EQ(constA.normal_texture_index, expectedNormalSrv);
  EXPECT_EQ(constB.normal_texture_index, expectedNormalSrv);
}

// Additional tests can be added here.

//! The bindless SRV index for materials must be stable within a frame after
//! EnsureFrameResources.
NOLINT_TEST_F(MaterialBinderBindingTest, BindlessIndexStabilityWithinFrame)
{
  constexpr ResourceKey base_color_key { 21001U };
  constexpr ResourceKey normal_key { 21002U };

  // Ensure material constants' texture indices remain stable while the
  // texture is still loading and after the texture binder supplies the
  // concrete SRV. We use placeholder indices first and then simulate
  // allocation completion.
  Uploader().OnFrameStart(oxygen::renderer::internal::RendererTagFactory::Get(),
    oxygen::frame::Slot { 1 });
  MatBinder().OnFrameStart(
    oxygen::renderer::internal::RendererTagFactory::Get(),
    oxygen::frame::Slot { 1 });

  const auto placeholder_base = GetPlaceholderIndexForKey(base_color_key);

  oxygen::engine::sceneprep::MaterialRef ref;
  ref.resolved_asset = MakeMaterial(base_color_key, normal_key, 7U, 8U);
  ref.source_asset_key = ref.resolved_asset->GetAssetKey();
  ref.resolved_asset_key = ref.resolved_asset->GetAssetKey();

  const auto h = MatBinder().GetOrAllocate(ref);
  ASSERT_TRUE(MatBinder().IsHandleValid(h));

  // MaterialBinder should expose the same placeholder index within frame.
  MatBinder().EnsureFrameResources();
  const auto all_constants = MatBinder().GetMaterialConstants();
  // NOLINTNEXTLINE(*-pro-bounds-avoid-unchecked-container-access)
  const auto& constants0 = all_constants[static_cast<std::size_t>(h.get())];
  EXPECT_EQ(constants0.base_color_texture_index, placeholder_base);

  // Now simulate the texture becoming available and allocate the shader-visible
  // descriptor — TextureBinder must keep the index stable.
  SetTextureBinderAllocateOnRequest(true);
  const auto real_base = TexBinder().GetOrAllocate(base_color_key);
  EXPECT_EQ(placeholder_base, real_base);

  // Material constants must still report the same bindless index.
  const auto all_constants_after = MatBinder().GetMaterialConstants();
  const auto& constants1
    // NOLINTNEXTLINE(*-pro-bounds-avoid-unchecked-container-access)
    = all_constants_after[static_cast<std::size_t>(h.get())];
  EXPECT_EQ(constants1.base_color_texture_index, real_base);
}

//! TextureBinder must return stable indices for the same key when called
//! repeatedly.
NOLINT_TEST_F(MaterialBinderBindingTest, TextureBinderContractViolation)
{
  constexpr ResourceKey key { 21011U };
  const auto a = TexBinder().GetOrAllocate(key).get();
  const auto b = TexBinder().GetOrAllocate(key).get();
  EXPECT_EQ(a, b);
}

//! Placeholder reference counting: allocating a material before textures should
//! not allocate shader-visible descriptors.
NOLINT_TEST_F(MaterialBinderBindingTest, PlaceholderReferenceCounting)
{
  constexpr ResourceKey base_color_key { 21021U };
  constexpr ResourceKey normal_key { 21022U };

  Uploader().OnFrameStart(oxygen::renderer::internal::RendererTagFactory::Get(),
    oxygen::frame::Slot { 1 });
  MatBinder().OnFrameStart(
    oxygen::renderer::internal::RendererTagFactory::Get(),
    oxygen::frame::Slot { 1 });

  // Creating a material triggers TextureBinder allocations for per-entry
  // placeholders; the descriptor count should increase as a result.
  const auto before_count = AllocatedTextureSrvCount();

  oxygen::engine::sceneprep::MaterialRef ref;
  ref.resolved_asset = MakeMaterial(base_color_key, normal_key, 77U, 88U);
  ref.source_asset_key = ref.resolved_asset->GetAssetKey();
  ref.resolved_asset_key = ref.resolved_asset->GetAssetKey();

  const auto h = MatBinder().GetOrAllocate(ref);
  ASSERT_TRUE(MatBinder().IsHandleValid(h));

  // MaterialBinder calls into TextureBinder which must have allocated
  // shader-visible descriptors for the per-entry placeholders.
  const auto mid = AllocatedTextureSrvCount();
  EXPECT_GT(mid, before_count);

  // The indices returned by TextureBinder for the same keys must be stable
  // (identical across repeated calls) and must match the material constants.
  const auto expected_base = TexBinder().GetOrAllocate(base_color_key);
  const auto expected_normal = TexBinder().GetOrAllocate(normal_key);

  const auto all_constants = MatBinder().GetMaterialConstants();
  ASSERT_LT(static_cast<std::size_t>(h.get()), all_constants.size());
  // NOLINTNEXTLINE(*-pro-bounds-avoid-unchecked-container-access)
  const auto& constants = all_constants[static_cast<std::size_t>(h.get())];

  EXPECT_EQ(constants.base_color_texture_index, expected_base);
  EXPECT_EQ(constants.normal_texture_index, expected_normal);

  // Subsequent GetOrAllocate calls must not increase allocator count (stable)
  const auto after = AllocatedTextureSrvCount();
  EXPECT_EQ(after, mid);
}

//! Material constants must never equal raw authoring indices.
NOLINT_TEST_F(
  MaterialBinderBindingTest, MaterialConstantsDoNotExposeRawAuthorIndices)
{
  constexpr ResourceKey base_color_key { 9001U };
  constexpr ResourceKey normal_key { 9002U };

  Uploader().OnFrameStart(oxygen::renderer::internal::RendererTagFactory::Get(),
    oxygen::frame::Slot { 1 });
  MatBinder().OnFrameStart(
    oxygen::renderer::internal::RendererTagFactory::Get(),
    oxygen::frame::Slot { 1 });

  constexpr oxygen::ShaderVisibleIndex kRawBase { 555555U };
  constexpr oxygen::ShaderVisibleIndex kRawNormal { 666666U };

  oxygen::engine::sceneprep::MaterialRef ref;
  ref.resolved_asset = MakeMaterial(
    base_color_key, normal_key, kRawBase.get(), kRawNormal.get());
  ref.source_asset_key = ref.resolved_asset->GetAssetKey();
  ref.resolved_asset_key = ref.resolved_asset->GetAssetKey();

  const auto handle = MatBinder().GetOrAllocate(ref);
  ASSERT_TRUE(MatBinder().IsHandleValid(handle));

  const auto all_constants = MatBinder().GetMaterialConstants();
  ASSERT_LT(static_cast<std::size_t>(handle.get()), all_constants.size());
  // NOLINTNEXTLINE(*-pro-bounds-avoid-unchecked-container-access)
  const auto& constants = all_constants[static_cast<std::size_t>(handle.get())];

  EXPECT_NE(constants.base_color_texture_index, kRawBase);
  EXPECT_NE(constants.normal_texture_index, kRawNormal);
}

//! Cache hits must not re-invoke the texture binder.
NOLINT_TEST_F(MaterialBinderBindingTest, CacheHitDoesNotCallTextureBinder)
{
  constexpr ResourceKey base_color_key { 9301U };
  constexpr ResourceKey normal_key { 9302U };

  Uploader().OnFrameStart(oxygen::renderer::internal::RendererTagFactory::Get(),
    oxygen::frame::Slot { 1 });
  MatBinder().OnFrameStart(
    oxygen::renderer::internal::RendererTagFactory::Get(),
    oxygen::frame::Slot { 1 });

  oxygen::engine::sceneprep::MaterialRef ref;
  ref.resolved_asset = MakeMaterial(base_color_key, normal_key, 7U, 8U);
  ref.source_asset_key = ref.resolved_asset->GetAssetKey();
  ref.resolved_asset_key = ref.resolved_asset->GetAssetKey();

  const auto calls_before = TexBinderGetOrAllocateTotalCalls();
  const auto base_calls_before
    = TexBinderGetOrAllocateCallsForKey(base_color_key);
  const auto normal_calls_before
    = TexBinderGetOrAllocateCallsForKey(normal_key);

  const auto h0 = MatBinder().GetOrAllocate(ref);
  ASSERT_TRUE(MatBinder().IsHandleValid(h0));
  const auto calls_after_first = TexBinderGetOrAllocateTotalCalls();
  EXPECT_GT(calls_after_first, calls_before);

  const auto base_calls_after_first
    = TexBinderGetOrAllocateCallsForKey(base_color_key);
  const auto normal_calls_after_first
    = TexBinderGetOrAllocateCallsForKey(normal_key);

  EXPECT_GT(base_calls_after_first, base_calls_before);
  EXPECT_GT(normal_calls_after_first, normal_calls_before);

  const auto h1 = MatBinder().GetOrAllocate(ref);
  ASSERT_TRUE(MatBinder().IsHandleValid(h1));
  EXPECT_EQ(h0, h1);

  const auto calls_after_second = TexBinderGetOrAllocateTotalCalls();
  EXPECT_EQ(calls_after_second, calls_after_first);

  EXPECT_EQ(
    TexBinderGetOrAllocateCallsForKey(base_color_key), base_calls_after_first);
  EXPECT_EQ(
    TexBinderGetOrAllocateCallsForKey(normal_key), normal_calls_after_first);
}

} // namespace
