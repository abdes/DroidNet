//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <ranges>
#include <string>
#include <string_view>

#include <Oxygen/Content/ResourceKey.h>
#include <Oxygen/Testing/GTest.h>
#include <Oxygen/Vortex/Resources/TextureBinder.h>
#include <Oxygen/Vortex/Test/Fakes/Graphics.h>
#include <Oxygen/Vortex/Test/Fixtures/TextureBinderTest.h>

namespace {

using oxygen::content::ResourceKey;
using oxygen::vortex::testing::FakeGraphics;
using oxygen::vortex::testing::TextureBinderTest;

[[nodiscard]] auto GetTextureDebugName(const oxygen::graphics::Texture* texture)
  -> std::string_view
{
  if (texture == nullptr) {
    return {};
  }
  return texture->GetDescriptor().debug_name;
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
  for (auto& event : std::views::reverse(gfx.srv_view_log_.events)) {
    if (event.index == index) {
      return event.texture;
    }
  }
  return nullptr;
}

[[nodiscard]] auto FindLastTextureByDebugName(const FakeGraphics& gfx,
  const std::string_view debug_name) -> const oxygen::graphics::Texture*
{
  for (auto& event : std::views::reverse(gfx.srv_view_log_.events)) {
    if (event.texture != nullptr
      && GetTextureDebugName(event.texture) == debug_name) {
      return event.texture;
    }
  }
  return nullptr;
}

class TextureBinderBasicTest : public TextureBinderTest { };

//! Repeated requests for a loading key return the same index and allocate no
//! descriptor: the entry publishes the shared placeholder until it loads.
NOLINT_TEST_F(TextureBinderBasicTest, LoadingKey_PublishesSharedPlaceholder)
{
  // Arrange
  const auto before = AllocatedSrvCount();
  const ResourceKey key = Loader().MintSyntheticTextureKey();
  const auto placeholder = TexBinder().GetOrAllocate(ResourceKey::kPlaceholder);

  // Act
  const auto index_0 = TexBinder().GetOrAllocate(key);
  const auto index_1 = TexBinder().GetOrAllocate(key);

  // Assert
  EXPECT_EQ(index_0, index_1);
  EXPECT_EQ(index_0, placeholder);
  EXPECT_EQ(AllocatedSrvCount(), before);
}

//! Loading keys share the placeholder; they get distinct descriptors only
//! when their own versions load.
NOLINT_TEST_F(TextureBinderBasicTest, LoadingKeys_ShareThePlaceholder)
{
  // Arrange
  const auto before = AllocatedSrvCount();
  const ResourceKey key_a = Loader().MintSyntheticTextureKey();
  const ResourceKey key_b = Loader().MintSyntheticTextureKey();

  // Act
  const auto index_a = TexBinder().GetOrAllocate(key_a);
  const auto index_b = TexBinder().GetOrAllocate(key_b);

  // Assert
  EXPECT_EQ(index_a, index_b);
  EXPECT_EQ(AllocatedSrvCount(), before);
}

//! Reserved placeholder key must not allocate per-entry descriptors.
/*! ResourceKey::kPlaceholder is a fast-path sentinel; it must not allocate
    per-entry descriptors and must not return the shared error-texture index. */
NOLINT_TEST_F(TextureBinderBasicTest, PlaceholderKey_NoAllocation)
{
  // Arrange
  const auto before = AllocatedSrvCount();

  // Act
  const auto idx_0 = TexBinder().GetOrAllocate(ResourceKey::kPlaceholder);
  const auto idx_1 = TexBinder().GetOrAllocate(ResourceKey::kPlaceholder);

  // Assert
  EXPECT_EQ(idx_0, idx_1);
  EXPECT_EQ(AllocatedSrvCount(), before);

  const auto* const texture = LastSrvViewTextureForIndex(Gfx(), idx_0.get());
  ASSERT_NE(texture, nullptr);
  EXPECT_EQ(GetTextureDebugName(texture), "TextureBinder.Placeholder2D");
}

NOLINT_TEST_F(
  TextureBinderBasicTest, ErrorKeyUsesExistingCheckerboardWithoutAllocation)
{
  const auto before = AllocatedSrvCount();
  const auto first = TexBinder().GetOrAllocate(ResourceKey::kError);
  const auto second = TexBinder().GetOrAllocate(ResourceKey::kError);
  EXPECT_EQ(first, second);
  EXPECT_EQ(AllocatedSrvCount(), before);
  EXPECT_TRUE(TexBinder().HasResourceFailed(ResourceKey::kError));
  EXPECT_FALSE(TexBinder().IsResourceReady(ResourceKey::kError));
  const auto* texture = LastSrvViewTextureForIndex(Gfx(), first.get());
  ASSERT_NE(texture, nullptr);
  EXPECT_EQ(GetTextureDebugName(texture), "TextureBinder.Error2D");
}

//! Reserved fallback key must not allocate per-entry descriptors.
/*! ResourceKey::kFallback is a fast-path sentinel; it must not allocate
    per-entry descriptors and must not return the shared error-texture index. */
NOLINT_TEST_F(TextureBinderBasicTest, FallbackKey_NoAllocation)
{
  // Arrange
  const auto before = AllocatedSrvCount();

  // Act
  const auto idx_0 = TexBinder().GetOrAllocate(ResourceKey::kFallback);
  const auto idx_1 = TexBinder().GetOrAllocate(ResourceKey::kFallback);

  // Assert
  EXPECT_EQ(idx_0, idx_1);
  EXPECT_EQ(AllocatedSrvCount(), before);

  const auto* const texture = LastSrvViewTextureForIndex(Gfx(), idx_0.get());
  ASSERT_NE(texture, nullptr);
  EXPECT_EQ(GetTextureDebugName(texture), "TextureBinder.Placeholder2D");
}

//! Reserved keys must never bind the shared error texture.
/*! The fallback and placeholder keys are fast-path sentinels. They must not
    consult the loader and they must never resolve to the shared error texture.

    This test also asserts that the fake backend registers an SRV view for
    these indices.
*/
NOLINT_TEST_F(TextureBinderBasicTest, ReservedKeys_NeverBindErrorTexture)
{
  // Arrange
  const auto before = AllocatedSrvCount();

  const auto* const error_texture
    = FindLastTextureByDebugName(Gfx(), "TextureBinder.Error2D");
  ASSERT_NE(error_texture, nullptr);

  // Act
  const auto fallback_index = TexBinder().GetOrAllocate(ResourceKey::kFallback);
  const auto placeholder_index
    = TexBinder().GetOrAllocate(ResourceKey::kPlaceholder);

  // Assert
  EXPECT_EQ(fallback_index, placeholder_index);
  EXPECT_EQ(AllocatedSrvCount(), before);

  ASSERT_GE(CountSrvViewCreationsForIndex(Gfx(), placeholder_index.get()), 1U);

  const auto* const placeholder_texture
    = LastSrvViewTextureForIndex(Gfx(), placeholder_index.get());
  ASSERT_NE(placeholder_texture, nullptr);
  EXPECT_NE(placeholder_texture, error_texture);
  EXPECT_EQ(
    GetTextureDebugName(placeholder_texture), "TextureBinder.Placeholder2D");
}

//! Cache hits must not recreate SRV views.
/*! Repeated GetOrAllocate calls for the same key must be a cache hit and must
    not recreate or repoint the SRV view unless a completion is drained. */
NOLINT_TEST_F(TextureBinderBasicTest, CacheHit_DoesNotRecreateView)
{
  // Arrange
  const ResourceKey key = Loader().MintSyntheticTextureKey();

  // Act
  const auto index_0 = TexBinder().GetOrAllocate(key);
  const auto u_index = index_0.get();

  const auto creations_after_first
    = CountSrvViewCreationsForIndex(Gfx(), u_index);
  ASSERT_GE(creations_after_first, 1U);

  const auto index_1 = TexBinder().GetOrAllocate(key);

  // Assert
  EXPECT_EQ(index_0, index_1);
  EXPECT_EQ(
    CountSrvViewCreationsForIndex(Gfx(), u_index), creations_after_first);
}

//! Shared fallback textures retire their descriptors on teardown.
/*! TextureBinder owns one placeholder and one error texture per shape (2D,
    cube, 2D array). Teardown retires them; their descriptors return to the
    allocator once the frames that may read them complete. */
NOLINT_TEST_F(TextureBinderBasicTest, Teardown_RetiresSharedFallbackDescriptors)
{
  constexpr uint32_t kFallbackDescriptors = 6U;
  ASSERT_EQ(AllocatedSrvCount(), kFallbackDescriptors);

  DestroyBinder();
  EXPECT_EQ(AllocatedSrvCount(), kFallbackDescriptors);
  CompleteRetiredFrames();

  EXPECT_EQ(AllocatedSrvCount(), 0U);
}

//! Allocating an entry publishes the existing placeholder, which is not a
//! change of any published index.
NOLINT_TEST_F(
  TextureBinderBasicTest, ResidentContentRevision_UnchangedByAllocation)
{
  // Arrange
  const auto before = TexBinder().GetResidentContentRevision();
  const ResourceKey key = Loader().MintSyntheticTextureKey();

  // Act
  const auto first = TexBinder().GetOrAllocate(key);
  const auto second = TexBinder().GetOrAllocate(key);

  // Assert
  EXPECT_EQ(first, second);
  EXPECT_EQ(TexBinder().GetResidentContentRevision(), before);
}

} // namespace
