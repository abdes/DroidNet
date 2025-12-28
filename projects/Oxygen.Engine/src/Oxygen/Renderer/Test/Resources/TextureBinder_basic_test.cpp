//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Testing/GTest.h>

#include <algorithm>
#include <cstddef>

#include <Oxygen/Content/ResourceKey.h>
#include <Oxygen/Renderer/Resources/TextureBinder.h>
#include <Oxygen/Renderer/Test/Resources/TextureBinderTest.h>

namespace {

using oxygen::content::ResourceKey;
using oxygen::renderer::testing::FakeGraphics;
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

class TextureBinderBasicTest : public TextureBinderTest { };

//! The same key must always map to the same bindless SRV index.
/*! The TextureBinder must not allocate multiple descriptors for repeated
    requests of the same resource key. */
NOLINT_TEST_F(TextureBinderBasicTest, SameKey_IsStable)
{
  // Arrange
  const auto before = AllocatedSrvCount();
  const ResourceKey key = Loader().MintSyntheticTextureKey();

  // Act
  const auto index_0 = Binder().GetOrAllocate(key);
  const auto index_1 = Binder().GetOrAllocate(key);

  // Assert
  EXPECT_EQ(index_0, index_1);
  EXPECT_EQ(AllocatedSrvCount(), before + 1U);
}

//! Different keys must map to different SRV indices.
/*! Allocating distinct resource keys must allocate distinct shader-visible
    descriptors. */
NOLINT_TEST_F(TextureBinderBasicTest, DifferentKeys_AreDistinct)
{
  // Arrange
  const auto before = AllocatedSrvCount();
  const ResourceKey key_a = Loader().MintSyntheticTextureKey();
  const ResourceKey key_b = Loader().MintSyntheticTextureKey();

  // Act
  const auto index_a = Binder().GetOrAllocate(key_a);
  const auto index_b = Binder().GetOrAllocate(key_b);

  // Assert
  EXPECT_NE(index_a, index_b);
  EXPECT_EQ(AllocatedSrvCount(), before + 2U);
}

//! Reserved placeholder key must not allocate per-entry descriptors.
/*! ResourceKey::kPlaceholder is a fast-path sentinel; it must not allocate
    per-entry descriptors and must not return the shared error-texture index. */
NOLINT_TEST_F(TextureBinderBasicTest, PlaceholderKey_NoAllocation)
{
  // Arrange
  const auto before = AllocatedSrvCount();

  // Act
  const auto idx_0 = Binder().GetOrAllocate(ResourceKey::kPlaceholder);
  const auto idx_1 = Binder().GetOrAllocate(ResourceKey::kPlaceholder);

  // Assert
  EXPECT_EQ(idx_0, idx_1);
  EXPECT_NE(idx_0, Binder().GetErrorTextureIndex());
  EXPECT_EQ(AllocatedSrvCount(), before);
}

//! Reserved fallback key must not allocate per-entry descriptors.
/*! ResourceKey::kFallback is a fast-path sentinel; it must not allocate
    per-entry descriptors and must not return the shared error-texture index. */
NOLINT_TEST_F(TextureBinderBasicTest, FallbackKey_NoAllocation)
{
  // Arrange
  const auto before = AllocatedSrvCount();

  // Act
  const auto idx_0 = Binder().GetOrAllocate(ResourceKey::kFallback);
  const auto idx_1 = Binder().GetOrAllocate(ResourceKey::kFallback);

  // Assert
  EXPECT_EQ(idx_0, idx_1);
  EXPECT_NE(idx_0, Binder().GetErrorTextureIndex());
  EXPECT_EQ(AllocatedSrvCount(), before);
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

  const auto error_index = Binder().GetErrorTextureIndex();
  const auto* const error_texture = CaptureErrorTexturePtr(Gfx(), error_index);
  ASSERT_NE(error_texture, nullptr);

  // Act
  const auto fallback_index = Binder().GetOrAllocate(ResourceKey::kFallback);
  const auto placeholder_index
    = Binder().GetOrAllocate(ResourceKey::kPlaceholder);

  // Assert
  EXPECT_NE(fallback_index, error_index);
  EXPECT_NE(placeholder_index, error_index);
  EXPECT_EQ(fallback_index, placeholder_index);
  EXPECT_EQ(AllocatedSrvCount(), before);

  ASSERT_GE(CountSrvViewCreationsForIndex(Gfx(), error_index.get()), 1U);
  ASSERT_GE(CountSrvViewCreationsForIndex(Gfx(), placeholder_index.get()), 1U);

  const auto* const placeholder_texture
    = LastSrvViewTextureForIndex(Gfx(), placeholder_index.get());
  ASSERT_NE(placeholder_texture, nullptr);
  EXPECT_NE(placeholder_texture, error_texture);
}

//! Cache hits must not recreate SRV views.
/*! Repeated GetOrAllocate calls for the same key must be a cache hit and must
    not recreate or repoint the SRV view unless a completion is drained. */
NOLINT_TEST_F(TextureBinderBasicTest, CacheHit_DoesNotRecreateView)
{
  // Arrange
  const ResourceKey key = Loader().MintSyntheticTextureKey();

  // Act
  const auto index_0 = Binder().GetOrAllocate(key);
  const auto u_index = index_0.get();

  const auto creations_after_first
    = CountSrvViewCreationsForIndex(Gfx(), u_index);
  ASSERT_GE(creations_after_first, 1U);

  const auto index_1 = Binder().GetOrAllocate(key);

  // Assert
  EXPECT_EQ(index_0, index_1);
  EXPECT_EQ(
    CountSrvViewCreationsForIndex(Gfx(), u_index), creations_after_first);
}

} // namespace
