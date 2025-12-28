//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Testing/GTest.h>

#include <algorithm>
#include <cstddef>

#include <Oxygen/Renderer/Test/Resources/TextureBinderTest.h>
#include <Oxygen/Renderer/Test/Resources/TextureBinderTestPayloads.h>

namespace {

using oxygen::content::ResourceKey;
using oxygen::renderer::testing::FakeGraphics;
using oxygen::renderer::testing::MakeCookedTexture1x1Rgba8Payload;
using oxygen::renderer::testing::MakeCookedTexture4x4Bc1Payload;
using oxygen::renderer::testing::MakeInvalidTightPackedTexture1x1Rgba8Payload;
using oxygen::renderer::testing::TextureBinderTest;

[[nodiscard]] auto CountSrvViewCreationsForIndex(
  const FakeGraphics& gfx, const uint32_t index) -> std::size_t
{
  return static_cast<std::size_t>(std::count_if(
    gfx.srv_view_log_.events.begin(), gfx.srv_view_log_.events.end(),
    [&](const auto& e) { return e.index == index; }));
}

[[nodiscard]] auto LastSrvViewTextureForIndex(const FakeGraphics& gfx,
  const uint32_t index) -> const oxygen::graphics::Texture*
{
  for (auto it = gfx.srv_view_log_.events.rbegin();
    it != gfx.srv_view_log_.events.rend(); ++it) {
    if (it->index == index) {
      return it->texture;
    }
  }
  return nullptr;
}

[[nodiscard]] auto CaptureErrorTexturePtr(const FakeGraphics& gfx,
  const oxygen::bindless::ShaderVisibleIndex error_index)
  -> const oxygen::graphics::Texture*
{
  return LastSrvViewTextureForIndex(gfx, error_index.get());
}

class TextureBinderFailureTest : public TextureBinderTest { };

class TextureBinderUploadFailureTest : public TextureBinderTest {
protected:
  auto ConfigureGraphics(FakeGraphics& gfx) -> void override
  {
    gfx.SetFailMap(true);
  }
};

//! Error texture index must be stable and backed by a real SRV view.
/*! The binder exposes the shared error texture SRV index for debugging and
    for systems that want a deterministic error binding. The getter must be
    stable and it must correspond to a real SRV view created by the backend.
*/
NOLINT_TEST_F(TextureBinderFailureTest, GetErrorTextureIndex_IsStable)
{
  // Arrange
  const auto idx_0 = Binder().GetErrorTextureIndex();
  const auto idx_1 = Binder().GetErrorTextureIndex();

  // Assert
  EXPECT_EQ(idx_0, idx_1);
  EXPECT_NE(idx_0, Binder().GetOrAllocate(ResourceKey::kFallback));
  EXPECT_NE(idx_0, Binder().GetOrAllocate(ResourceKey::kPlaceholder));

  const auto* const error_texture = CaptureErrorTexturePtr(Gfx(), idx_0);
  ASSERT_NE(error_texture, nullptr);
}

//! Load failures repoint the per-entry descriptor to the error texture.
/*! The shader-visible index returned by GetOrAllocate must remain stable, but
    the underlying SRV view should be repointed to the shared error texture.

    This test observes repointing via FakeGraphics SRV view creation telemetry,
    without accessing any TextureBinder internals.
*/
NOLINT_TEST_F(TextureBinderFailureTest, LoadFailure_RepointsToError)
{
  // Arrange
  const auto before = AllocatedSrvCount();
  const ResourceKey key = Loader().MintSyntheticTextureKey();

  const auto error_index = Binder().GetErrorTextureIndex();
  const auto* const error_texture = CaptureErrorTexturePtr(Gfx(), error_index);
  ASSERT_NE(error_texture, nullptr);

  Gfx().srv_view_log_.events.clear();

  // Act
  const auto index_0 = Binder().GetOrAllocate(key);
  const auto index_1 = Binder().GetOrAllocate(key);

  // Assert
  EXPECT_EQ(index_0, index_1);
  EXPECT_NE(index_0, error_index);
  EXPECT_EQ(AllocatedSrvCount(), before + 1U);

  const auto* const bound_texture
    = LastSrvViewTextureForIndex(Gfx(), index_0.get());
  ASSERT_NE(bound_texture, nullptr);
  EXPECT_EQ(bound_texture, error_texture);
}

//! Forced-error mode must be deterministic.
/*! When the loader cannot resolve a valid resource for a key, the binder must
    repoint the descriptor to the shared error texture and keep the SRV index
    stable on subsequent calls.
*/
NOLINT_TEST_F(TextureBinderFailureTest, ForcedError_IsDeterministic)
{
  // Arrange
  const auto before = AllocatedSrvCount();
  const ResourceKey key = Loader().MintSyntheticTextureKey();

  const auto error_index = Binder().GetErrorTextureIndex();
  const auto* const error_texture = CaptureErrorTexturePtr(Gfx(), error_index);
  ASSERT_NE(error_texture, nullptr);

  Gfx().srv_view_log_.events.clear();

  // Act
  const auto index_0 = Binder().GetOrAllocate(key);
  const auto u_index = index_0.get();
  const auto creations_after_first
    = CountSrvViewCreationsForIndex(Gfx(), u_index);

  const auto index_1 = Binder().GetOrAllocate(key);

  // Assert
  EXPECT_EQ(index_0, index_1);
  EXPECT_NE(index_0, error_index);
  EXPECT_EQ(AllocatedSrvCount(), before + 1U);

  const auto* const bound_texture = LastSrvViewTextureForIndex(Gfx(), u_index);
  ASSERT_NE(bound_texture, nullptr);
  EXPECT_EQ(bound_texture, error_texture);

  EXPECT_EQ(
    CountSrvViewCreationsForIndex(Gfx(), u_index), creations_after_first);
}

//! Cooked texture layout violations must be rejected deterministically.
/*! The binder expects cooked mip blobs to use a 256-byte row pitch and a
    512-byte mip placement alignment. If the payload violates these
    assumptions, the binder must repoint to the error texture and must not
    allocate additional descriptors on subsequent calls.
*/
NOLINT_TEST_F(TextureBinderFailureTest, InvalidCookedLayout_Rejected)
{
  // Arrange
  const auto before = AllocatedSrvCount();
  const ResourceKey key = Loader().MintSyntheticTextureKey();
  const auto payload = MakeInvalidTightPackedTexture1x1Rgba8Payload();
  Loader().PreloadCookedTexture(
    key, std::span<const uint8_t>(payload.data(), payload.size()));

  const auto error_index = Binder().GetErrorTextureIndex();
  const auto* const error_texture = CaptureErrorTexturePtr(Gfx(), error_index);
  ASSERT_NE(error_texture, nullptr);

  Gfx().srv_view_log_.events.clear();

  // Act
  const auto index_0 = Binder().GetOrAllocate(key);
  const auto index_1 = Binder().GetOrAllocate(key);

  // Assert
  EXPECT_EQ(index_0, index_1);
  EXPECT_NE(index_0, error_index);
  EXPECT_EQ(AllocatedSrvCount(), before + 1U);

  const auto* const bound_texture
    = LastSrvViewTextureForIndex(Gfx(), index_0.get());
  ASSERT_NE(bound_texture, nullptr);
  EXPECT_EQ(bound_texture, error_texture);
}

//! Unsupported formats must be rejected via the error texture.
/*! This covers the format mismatch behavior: the binder must repoint the
    descriptor to the shared error texture while keeping the SRV index stable.
*/
NOLINT_TEST_F(TextureBinderFailureTest, UnsupportedFormat_Rejected)
{
  // Arrange
  const auto before = AllocatedSrvCount();
  const ResourceKey key = Loader().MintSyntheticTextureKey();
  const auto payload = MakeCookedTexture4x4Bc1Payload();
  Loader().PreloadCookedTexture(
    key, std::span<const uint8_t>(payload.data(), payload.size()));

  const auto error_index = Binder().GetErrorTextureIndex();
  const auto* const error_texture = CaptureErrorTexturePtr(Gfx(), error_index);
  ASSERT_NE(error_texture, nullptr);

  Gfx().srv_view_log_.events.clear();

  // Act
  const auto index_0 = Binder().GetOrAllocate(key);
  const auto index_1 = Binder().GetOrAllocate(key);

  // Assert
  EXPECT_EQ(index_0, index_1);
  EXPECT_NE(index_0, error_index);
  EXPECT_EQ(AllocatedSrvCount(), before + 1U);

  const auto* const bound_texture
    = LastSrvViewTextureForIndex(Gfx(), index_0.get());
  ASSERT_NE(bound_texture, nullptr);
  EXPECT_EQ(bound_texture, error_texture);
}

//! Upload submission failures must keep the placeholder bound.
/*! If the UploadCoordinator cannot submit work (e.g. staging allocation/map
    fails), the binder must keep the placeholder SRV active (no descriptor
    repoint to error) and mark the entry as failed deterministically.
*/
NOLINT_TEST_F(
  TextureBinderUploadFailureTest, UploadSubmissionFailure_KeepsPlaceholder)
{
  // Arrange
  const auto before = AllocatedSrvCount();
  const ResourceKey key = Loader().MintSyntheticTextureKey();
  const auto payload = MakeCookedTexture1x1Rgba8Payload();
  Loader().PreloadCookedTexture(
    key, std::span<const uint8_t>(payload.data(), payload.size()));

  const auto error_index = Binder().GetErrorTextureIndex();
  const auto* const error_texture
    = LastSrvViewTextureForIndex(Gfx(), error_index.get());
  ASSERT_NE(error_texture, nullptr);

  Gfx().srv_view_log_.events.clear();

  // Act
  const auto index_0 = Binder().GetOrAllocate(key);
  const auto index_1 = Binder().GetOrAllocate(key);

  // Assert
  EXPECT_EQ(index_0, index_1);
  EXPECT_NE(index_0, Binder().GetErrorTextureIndex());
  EXPECT_EQ(AllocatedSrvCount(), before + 1U);

  const auto u_index = index_0.get();

  const auto creations_after_allocate
    = CountSrvViewCreationsForIndex(Gfx(), u_index);
  ASSERT_GE(creations_after_allocate, 1U);

  const auto* const bound_texture = LastSrvViewTextureForIndex(Gfx(), u_index);
  ASSERT_NE(bound_texture, nullptr);
  EXPECT_NE(bound_texture, error_texture);

  EXPECT_EQ(
    CountSrvViewCreationsForIndex(Gfx(), u_index), creations_after_allocate);
}

} // namespace
