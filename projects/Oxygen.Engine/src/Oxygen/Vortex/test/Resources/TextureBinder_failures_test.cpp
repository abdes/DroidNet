//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cstdint>
#include <ranges>
#include <span>
#include <string_view>

#include <Oxygen/Content/ResourceKey.h>
#include <Oxygen/Core/Bindless/Types.h>
#include <Oxygen/Core/Types/Format.h>
#include <Oxygen/Core/Types/TextureType.h>
#include <Oxygen/Graphics/Common/Texture.h>
#include <Oxygen/Testing/GTest.h>
#include <Oxygen/Vortex/RendererTag.h>
#include <Oxygen/Vortex/test/Fakes/Graphics.h>
#include <Oxygen/Vortex/test/Fixtures/TextureBinderPayloads.h>
#include <Oxygen/Vortex/test/Fixtures/TextureBinderTest.h>

namespace {

using oxygen::content::ResourceKey;
using oxygen::vortex::testing::FakeGraphics;
using oxygen::vortex::testing::MakeCookedTexture1x1Rgba8Payload;
using oxygen::vortex::testing::MakeCookedTexture4x4Bc1Payload;
using oxygen::vortex::testing::
  MakeInvalidTexture1x1Rgba8Payload_RowPitchTooSmall;
using oxygen::vortex::testing::MakeTightPackedTextureCube1x1Rgba8Payload;
using oxygen::vortex::testing::TextureBinderTest;

[[nodiscard]] auto GetTextureDebugName(const oxygen::graphics::Texture* texture)
  -> std::string_view
{
  if (texture == nullptr) {
    return {};
  }
  return texture->GetDescriptor().debug_name;
}

[[nodiscard]] auto LastSrvViewTextureForIndex(const FakeGraphics& gfx,
  const uint32_t index) -> const oxygen::graphics::Texture*
{
  for (auto& event : std::views::reverse(gfx.srv_view_log_.events)) {
    if (event.index == index) {
      return event.texture;
    }
  }
  return nullptr;
}

class TextureBinderFailureTest : public TextureBinderTest {
protected:
  [[nodiscard]] auto ErrorIndex() const -> oxygen::ShaderVisibleIndex
  {
    return TexBinder().GetOrAllocate(ResourceKey::kError);
  }
};

class TextureBinderUploadFailureTest : public TextureBinderTest { };

//! Every failed entry publishes the one shared error binding.
/*! Verified through fake-backend SRV view telemetry and the publicly
    observable `TextureDesc::debug_name`. */
NOLINT_TEST_F(TextureBinderFailureTest, ErrorTexture_IsSharedAndObservable)
{
  // Arrange
  const auto before = AllocatedSrvCount();
  const ResourceKey key_a = Loader().MintSyntheticTextureKey();
  const ResourceKey key_b = Loader().MintSyntheticTextureKey();
  (void)TexBinder().GetOrAllocate(key_a);
  (void)TexBinder().GetOrAllocate(key_b);

  // Act: drain queued load failures.
  BeginVisibleFrame();

  // Assert
  const auto idx_a = TexBinder().GetOrAllocate(key_a);
  EXPECT_EQ(idx_a, TexBinder().GetOrAllocate(key_b));
  EXPECT_EQ(idx_a, ErrorIndex());
  EXPECT_EQ(AllocatedSrvCount(), before);
  EXPECT_EQ(GetTextureDebugName(LastSrvViewTextureForIndex(Gfx(), idx_a.get())),
    "TextureBinder.Error2D");
}

//! A load failure publishes the error binding instead of the placeholder,
//! without rewriting either descriptor.
NOLINT_TEST_F(TextureBinderFailureTest, LoadFailure_PublishesError)
{
  // Arrange
  const ResourceKey key = Loader().MintSyntheticTextureKey();
  const auto loading = TexBinder().GetOrAllocate(key);
  const auto views = Gfx().srv_view_log_.events.size();
  const auto revision = TexBinder().GetResidentContentRevision();

  // Act: drain queued load failure.
  BeginVisibleFrame();

  // Assert
  EXPECT_EQ(loading, TexBinder().GetOrAllocate(ResourceKey::kPlaceholder));
  EXPECT_EQ(TexBinder().GetOrAllocate(key), ErrorIndex());
  EXPECT_TRUE(TexBinder().HasResourceFailed(key));
  EXPECT_GT(TexBinder().GetResidentContentRevision(), revision);
  EXPECT_EQ(Gfx().srv_view_log_.events.size(), views);
}

//! A failed entry keeps publishing the error binding; later frames change
//! nothing.
NOLINT_TEST_F(TextureBinderFailureTest, ForcedError_IsDeterministic)
{
  // Arrange
  const ResourceKey key = Loader().MintSyntheticTextureKey();
  (void)TexBinder().GetOrAllocate(key);
  BeginVisibleFrame();
  const auto revision = TexBinder().GetResidentContentRevision();
  const auto views = Gfx().srv_view_log_.events.size();

  // Act
  const auto index = TexBinder().GetOrAllocate(key);
  BeginVisibleFrame();

  // Assert
  EXPECT_EQ(index, ErrorIndex());
  EXPECT_EQ(TexBinder().GetOrAllocate(key), index);
  EXPECT_EQ(TexBinder().GetResidentContentRevision(), revision);
  EXPECT_EQ(Gfx().srv_view_log_.events.size(), views);
}

//! Cooked texture layout violations must be rejected deterministically.
/*! The binder validates cooked payload layouts against the texture descriptor
    and format metadata (block size and bytes per block). An inconsistent
    layout table (e.g., row pitch too small) publishes the error binding. */
NOLINT_TEST_F(TextureBinderFailureTest, InvalidCookedLayout_Rejected)
{
  // Arrange
  const auto before = AllocatedSrvCount();
  const ResourceKey key = Loader().MintSyntheticTextureKey();
  const auto payload = MakeInvalidTexture1x1Rgba8Payload_RowPitchTooSmall();
  Loader().PreloadCookedTexture(key, std::span(payload.data(), payload.size()));
  (void)TexBinder().GetOrAllocate(key);

  // Act: process the queued upload attempt.
  BeginVisibleFrame();

  // Assert
  EXPECT_EQ(TexBinder().GetOrAllocate(key), ErrorIndex());
  EXPECT_TRUE(TexBinder().HasResourceFailed(key));
  EXPECT_EQ(AllocatedSrvCount(), before);
}

//! Unsupported formats publish the error binding.
NOLINT_TEST_F(TextureBinderFailureTest, UnsupportedFormat_Rejected)
{
  // Arrange
  const auto before = AllocatedSrvCount();
  const ResourceKey key = Loader().MintSyntheticTextureKey();
  const auto payload = MakeCookedTexture4x4Bc1Payload();
  Loader().PreloadCookedTexture(key, std::span(payload.data(), payload.size()));
  (void)TexBinder().GetOrAllocate(key);

  // Act: process the queued upload attempt.
  BeginVisibleFrame();

  // Assert
  EXPECT_EQ(TexBinder().GetOrAllocate(key), ErrorIndex());
  EXPECT_EQ(AllocatedSrvCount(), before);
}

//! A rejected cube publishes the cube error binding, so cube readers never
//! sample a 2D view.
NOLINT_TEST_F(TextureBinderFailureTest, RejectedCube_PublishesCubeError)
{
  // Arrange
  const ResourceKey key = Loader().MintSyntheticTextureKey();
  const auto payload
    = MakeTightPackedTextureCube1x1Rgba8Payload(oxygen::Format::kBC1UNorm);
  Loader().PreloadCookedTexture(key, std::span(payload.data(), payload.size()));
  (void)TexBinder().GetOrAllocate(key);

  // Act: process the queued upload attempt.
  BeginVisibleFrame();

  // Assert
  ASSERT_TRUE(TexBinder().HasResourceFailed(key));
  const auto index = TexBinder().GetOrAllocate(key);
  EXPECT_NE(index, ErrorIndex());
  const auto* const texture = LastSrvViewTextureForIndex(Gfx(), index.get());
  ASSERT_NE(texture, nullptr);
  EXPECT_EQ(
    texture->GetDescriptor().texture_type, oxygen::TextureType::kTextureCube);
  EXPECT_EQ(GetTextureDebugName(texture), "TextureBinder.ErrorCube");
}

//! Upload submission failures keep the placeholder published.
/*! If the UploadCoordinator cannot submit work (e.g. staging allocation/map
    fails), the binder keeps the placeholder (not the error binding) and marks
    the entry as failed deterministically. */
NOLINT_TEST_F(
  TextureBinderUploadFailureTest, UploadSubmissionFailure_KeepsPlaceholder)
{
  // Arrange
  const auto before = AllocatedSrvCount();
  const ResourceKey key = Loader().MintSyntheticTextureKey();
  const auto payload = MakeCookedTexture1x1Rgba8Payload();
  Loader().PreloadCookedTexture(key, std::span(payload.data(), payload.size()));
  Uploader().OnFrameStart(oxygen::vortex::internal::RendererTagFactory::Get(),
    oxygen::frame::Slot { 1 });
  const auto loading = TexBinder().GetOrAllocate(key);
  Gfx().SetFailMap(true);

  // Act: the staging map is configured to fail.
  BeginVisibleFrame();

  // Assert
  EXPECT_EQ(TexBinder().GetOrAllocate(key), loading);
  EXPECT_EQ(loading, TexBinder().GetOrAllocate(ResourceKey::kPlaceholder));
  EXPECT_TRUE(TexBinder().HasResourceFailed(key));
  EXPECT_EQ(AllocatedSrvCount(), before);
}

NOLINT_TEST_F(
  TextureBinderFailureTest, MissingTextureNeverProducesResidentLease)
{
  const auto key = Loader().MintSyntheticTextureKey();
  EXPECT_FALSE(TexBinder().HasResourceFailed(key));
  EXPECT_EQ(TexBinder().AcquireReadyTexture(key), nullptr);
  const auto slot = TexBinder().GetOrAllocate(key);
  EXPECT_TRUE(slot.IsValid());
  BeginVisibleFrame();
  EXPECT_TRUE(TexBinder().HasResourceFailed(key));
  EXPECT_EQ(TexBinder().AcquireReadyTexture(key), nullptr);
}

} // namespace
