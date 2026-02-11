//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <bit>
#include <cstdint>
#include <limits>
#include <memory>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Core/Bindless/Types.h>
#include <Oxygen/Data/MaterialAsset.h>
#include <Oxygen/Renderer/RendererTag.h>
#include <Oxygen/Renderer/ScenePrep/MaterialRef.h>
#include <Oxygen/Renderer/Upload/UploadCoordinator.h>

#include <Oxygen/Renderer/Test/Fakes/Graphics.h>
#include <Oxygen/Renderer/Test/Resources/MaterialBinderTest.h>

namespace {

using oxygen::renderer::testing::FakeGraphics;
using oxygen::renderer::testing::MaterialBinderTest;

[[nodiscard]] auto MakeSolidMaterial(const float base_color_r)
  -> std::shared_ptr<const oxygen::data::MaterialAsset>
{
  using oxygen::data::pak::kMaterialFlag_NoTextureSampling;
  using oxygen::data::pak::MaterialAssetDesc;

  oxygen::data::AssetKey asset_key { .guid = {} };
  const auto bits = std::bit_cast<std::uint32_t>(base_color_r);
  asset_key.guid[0] = static_cast<std::uint8_t>((bits >> 0U) & 0xFFU);
  asset_key.guid[1] = static_cast<std::uint8_t>((bits >> 8U) & 0xFFU);
  asset_key.guid[2] = static_cast<std::uint8_t>((bits >> 16U) & 0xFFU);
  asset_key.guid[3] = static_cast<std::uint8_t>((bits >> 24U) & 0xFFU);
  asset_key.guid[15] = 0x4DU;

  MaterialAssetDesc desc {};
  desc.flags |= kMaterialFlag_NoTextureSampling;
  desc.base_color[0] = base_color_r;
  desc.base_color[1] = 0.0F;
  desc.base_color[2] = 0.0F;
  desc.base_color[3] = 1.0F;

  // No runtime texture keys provided -> ResourceKey{0} for all slots.
  return std::make_shared<oxygen::data::MaterialAsset>(asset_key, desc);
}

class MaterialBinderEdgeTest : public MaterialBinderTest { };

//! Atlas resizes must force a full re-upload of existing material constants.
NOLINT_TEST_F(MaterialBinderEdgeTest, ResizingAtlasReuploadsAllMaterials)
{
  auto* fake_gfx = static_cast<FakeGraphics*>(GfxPtr().get());
  ASSERT_NE(fake_gfx, nullptr);

  const auto OnFrameStart = [&](const std::uint32_t slot) {
    Uploader().OnFrameStart(
      oxygen::renderer::internal::RendererTagFactory::Get(),
      oxygen::frame::Slot { slot });
    MatBinder().OnFrameStart(
      oxygen::renderer::internal::RendererTagFactory::Get(),
      oxygen::frame::Slot { slot });
  };

  // Arrange: allocate and upload a baseline set of materials.
  OnFrameStart(0U);

  constexpr std::uint32_t kInitialCount = 8U;
  for (std::uint32_t i = 0U; i < kInitialCount; ++i) {
    oxygen::engine::sceneprep::MaterialRef ref;
    ref.resolved_asset = MakeSolidMaterial(0.1F + static_cast<float>(i) * 0.1F);
    ref.source_asset_key = ref.resolved_asset->GetAssetKey();
    ref.resolved_asset_key = ref.resolved_asset->GetAssetKey();
    const auto h = MatBinder().GetOrAllocate(ref);
    ASSERT_TRUE(MatBinder().IsHandleValid(h));
  }

  fake_gfx->buffer_log_.copies.clear();
  fake_gfx->buffer_log_.copy_called = false;

  // Act: upload for frame 1.
  MatBinder().EnsureFrameResources();

  // Assert: at least one upload occurred.
  ASSERT_TRUE(fake_gfx->buffer_log_.copy_called);
  ASSERT_FALSE(fake_gfx->buffer_log_.copies.empty());

  // Arrange: next frame with no changes should not upload.
  OnFrameStart(1U);

  fake_gfx->buffer_log_.copies.clear();
  fake_gfx->buffer_log_.copy_called = false;

  // Act: ensure resources with no dirty materials.
  MatBinder().EnsureFrameResources();

  // Assert: no uploads.
  EXPECT_FALSE(fake_gfx->buffer_log_.copy_called);
  EXPECT_TRUE(fake_gfx->buffer_log_.copies.empty());

  // Arrange: allocate enough new materials to force at least one atlas resize.
  OnFrameStart(2U);

  constexpr std::uint32_t kAdditionalCount = 200U;
  for (std::uint32_t i = 0U; i < kAdditionalCount; ++i) {
    oxygen::engine::sceneprep::MaterialRef ref;
    ref.resolved_asset
      = MakeSolidMaterial(2.0F + static_cast<float>(i) * (1.0F / 1024.0F));
    ref.source_asset_key = ref.resolved_asset->GetAssetKey();
    ref.resolved_asset_key = ref.resolved_asset->GetAssetKey();
    const auto h = MatBinder().GetOrAllocate(ref);
    ASSERT_TRUE(MatBinder().IsHandleValid(h));
  }

  fake_gfx->buffer_log_.copies.clear();
  fake_gfx->buffer_log_.copy_called = false;

  // Act: ensure resources after growth.
  MatBinder().EnsureFrameResources();

  // Assert: uploads must include existing materials (slot 0 => dst_offset 0).
  ASSERT_TRUE(fake_gfx->buffer_log_.copy_called);
  ASSERT_FALSE(fake_gfx->buffer_log_.copies.empty());

  std::size_t min_dst_offset = (std::numeric_limits<std::size_t>::max)();
  for (const auto& e : fake_gfx->buffer_log_.copies) {
    min_dst_offset = (std::min)(min_dst_offset, e.dst_offset);
  }
  EXPECT_EQ(min_dst_offset, 0U);
}

//! Materials that opt out of texture sampling must not call the texture binder.
NOLINT_TEST_F(MaterialBinderEdgeTest, NoTexturesSkipsTextureBinder)
{
  // Arrange
  Uploader().OnFrameStart(oxygen::renderer::internal::RendererTagFactory::Get(),
    oxygen::frame::Slot { 1 });
  MatBinder().OnFrameStart(
    oxygen::renderer::internal::RendererTagFactory::Get(),
    oxygen::frame::Slot { 1 });

  const auto calls_before = TexBinderGetOrAllocateTotalCalls();

  oxygen::engine::sceneprep::MaterialRef ref;
  ref.resolved_asset = MakeSolidMaterial(0.25F);
  ref.source_asset_key = ref.resolved_asset->GetAssetKey();
  ref.resolved_asset_key = ref.resolved_asset->GetAssetKey();

  // Act
  const auto h = MatBinder().GetOrAllocate(ref);

  // Assert
  ASSERT_TRUE(MatBinder().IsHandleValid(h));
  EXPECT_EQ(TexBinderGetOrAllocateTotalCalls(), calls_before);

  const auto constants
    = MatBinder()
        // NOLINTNEXTLINE(*-pro-bounds-avoid-unchecked-container-access)
        .GetMaterialConstants()[static_cast<std::size_t>(h.get())];

  const auto u_invalid = oxygen::kInvalidShaderVisibleIndex;
  EXPECT_EQ(constants.base_color_texture_index, u_invalid);
  EXPECT_EQ(constants.normal_texture_index, u_invalid);
  EXPECT_EQ(constants.metallic_texture_index, u_invalid);
  EXPECT_EQ(constants.roughness_texture_index, u_invalid);
  EXPECT_EQ(constants.ambient_occlusion_texture_index, u_invalid);
}

} // namespace
