//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Testing/GTest.h>

#include <algorithm>
#include <string>

#include <Oxygen/Renderer/RendererTag.h>

#include <Oxygen/Renderer/Test/Resources/TextureBinderTest.h>
#include <Oxygen/Renderer/Test/Resources/TextureBinderTestPayloads.h>

namespace {

using oxygen::content::ResourceKey;
using oxygen::renderer::testing::FakeGraphics;
using oxygen::renderer::testing::MakeCookedTexture1x1Rgba8Payload;
using oxygen::renderer::testing::MakeCookedTexture4x4Bc1Payload;
using oxygen::renderer::testing::MakeInvalidTightPackedTexture1x1Rgba8Payload;
using oxygen::renderer::testing::TextureBinderTest;

[[nodiscard]] auto GetTextureDebugName(const oxygen::graphics::Texture* texture)
  -> std::string_view
{
  if (texture == nullptr) {
    return {};
  }
  return texture->GetDescriptor().debug_name;
}

[[nodiscard]] auto MakePlaceholderDebugName(const ResourceKey key)
  -> std::string
{
  return std::string("Placeholder(") + oxygen::content::to_string(key) + ")";
}

[[nodiscard]] auto CountSrvViewCreationsForIndex(
  const FakeGraphics& gfx, const uint32_t index) -> std::size_t
{
  return static_cast<std::size_t>(
    std::ranges::count_if(gfx.srv_view_log_.events,
      [&](const auto& e) -> auto { return e.index == index; }));
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

class TextureBinderFailureTest : public TextureBinderTest { };

class TextureBinderUploadFailureTest : public TextureBinderTest {
protected:
  auto ConfigureGraphics(FakeGraphics& gfx) -> void override
  {
    gfx.SetFailMap(true);
  }
};

//! Error texture index must be stable and backed by a real SRV view.
/*! The binder must use a single shared error texture for all failures.
    This is verified purely via fake-backend SRV view creation telemetry and
    the publicly observable `TextureDesc::debug_name`.
*/
NOLINT_TEST_F(TextureBinderFailureTest, ErrorTexture_IsSharedAndObservable)
{
  // Arrange
  const auto before = AllocatedSrvCount();
  const ResourceKey key_a = Loader().MintSyntheticTextureKey();
  const ResourceKey key_b = Loader().MintSyntheticTextureKey();

  Gfx().srv_view_log_.events.clear();

  // Act
  const auto idx_a = TexBinder().GetOrAllocate(key_a);
  const auto idx_b = TexBinder().GetOrAllocate(key_b);

  // Drain queued load failures.
  TexBinder().OnFrameStart();

  // Assert
  EXPECT_NE(idx_a, idx_b);
  EXPECT_EQ(AllocatedSrvCount(), before + 2U);

  const auto* const tex_a = LastSrvViewTextureForIndex(Gfx(), idx_a.get());
  const auto* const tex_b = LastSrvViewTextureForIndex(Gfx(), idx_b.get());
  ASSERT_NE(tex_a, nullptr);
  ASSERT_NE(tex_b, nullptr);
  EXPECT_EQ(GetTextureDebugName(tex_a), "ErrorTexture");
  EXPECT_EQ(GetTextureDebugName(tex_b), "ErrorTexture");
  EXPECT_EQ(tex_a, tex_b);
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

  Gfx().srv_view_log_.events.clear();

  // Act
  const auto index_0 = TexBinder().GetOrAllocate(key);
  const auto index_1 = TexBinder().GetOrAllocate(key);

  // Drain queued load failure.
  TexBinder().OnFrameStart();

  // Assert
  EXPECT_EQ(index_0, index_1);
  EXPECT_EQ(AllocatedSrvCount(), before + 1U);

  const auto* const bound_texture
    = LastSrvViewTextureForIndex(Gfx(), index_0.get());
  ASSERT_NE(bound_texture, nullptr);
  EXPECT_EQ(GetTextureDebugName(bound_texture), "ErrorTexture");
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

  Gfx().srv_view_log_.events.clear();

  // Act
  const auto index_0 = TexBinder().GetOrAllocate(key);
  const auto u_index = index_0.get();

  // Drain queued load failure and observe the stable error binding.
  TexBinder().OnFrameStart();

  const auto creations_after_first
    = CountSrvViewCreationsForIndex(Gfx(), u_index);

  const auto index_1 = TexBinder().GetOrAllocate(key);

  // No further updates expected.
  TexBinder().OnFrameStart();

  // Assert
  EXPECT_EQ(index_0, index_1);
  EXPECT_EQ(AllocatedSrvCount(), before + 1U);

  const auto* const bound_texture = LastSrvViewTextureForIndex(Gfx(), u_index);
  ASSERT_NE(bound_texture, nullptr);
  EXPECT_EQ(GetTextureDebugName(bound_texture), "ErrorTexture");

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
  Loader().PreloadCookedTexture(key, std::span(payload.data(), payload.size()));

  Gfx().srv_view_log_.events.clear();

  // Act
  const auto index_0 = TexBinder().GetOrAllocate(key);
  const auto index_1 = TexBinder().GetOrAllocate(key);

  // Process queued upload attempt and observe rejection.
  TexBinder().OnFrameStart();

  // Assert
  EXPECT_EQ(index_0, index_1);
  EXPECT_EQ(AllocatedSrvCount(), before + 1U);

  const auto* const bound_texture
    = LastSrvViewTextureForIndex(Gfx(), index_0.get());
  ASSERT_NE(bound_texture, nullptr);
  EXPECT_EQ(GetTextureDebugName(bound_texture), "ErrorTexture");
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
  Loader().PreloadCookedTexture(key, std::span(payload.data(), payload.size()));

  Gfx().srv_view_log_.events.clear();

  // Act
  const auto index_0 = TexBinder().GetOrAllocate(key);
  const auto index_1 = TexBinder().GetOrAllocate(key);

  // Process queued upload attempt and observe rejection.
  TexBinder().OnFrameStart();

  // Assert
  EXPECT_EQ(index_0, index_1);
  EXPECT_EQ(AllocatedSrvCount(), before + 1U);

  const auto* const bound_texture
    = LastSrvViewTextureForIndex(Gfx(), index_0.get());
  ASSERT_NE(bound_texture, nullptr);
  EXPECT_EQ(GetTextureDebugName(bound_texture), "ErrorTexture");
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
  Loader().PreloadCookedTexture(key, std::span(payload.data(), payload.size()));

  Uploader().OnFrameStart(oxygen::renderer::internal::RendererTagFactory::Get(),
    oxygen::frame::Slot { 1 });

  Gfx().srv_view_log_.events.clear();

  // Act
  const auto index_0 = TexBinder().GetOrAllocate(key);
  const auto index_1 = TexBinder().GetOrAllocate(key);

  // Process queued upload submission; staging map is configured to fail.
  TexBinder().OnFrameStart();

  // Assert
  EXPECT_EQ(index_0, index_1);
  EXPECT_EQ(AllocatedSrvCount(), before + 1U);

  const auto u_index = index_0.get();

  const auto creations_after_allocate
    = CountSrvViewCreationsForIndex(Gfx(), u_index);
  ASSERT_GE(creations_after_allocate, 1U);

  const auto* const bound_texture = LastSrvViewTextureForIndex(Gfx(), u_index);
  ASSERT_NE(bound_texture, nullptr);
  EXPECT_EQ(GetTextureDebugName(bound_texture), MakePlaceholderDebugName(key));
  EXPECT_NE(GetTextureDebugName(bound_texture), "ErrorTexture");

  EXPECT_EQ(
    CountSrvViewCreationsForIndex(Gfx(), u_index), creations_after_allocate);
}

} // namespace
