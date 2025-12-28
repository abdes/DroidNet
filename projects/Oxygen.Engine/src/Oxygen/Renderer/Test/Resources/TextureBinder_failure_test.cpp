//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Testing/GTest.h>

#include <algorithm>
#include <cstdint>
#include <limits>
#include <memory>

#include <Oxygen/Renderer/Test/Resources/TextureBinderTest.h>
#include <Oxygen/Renderer/Test/Resources/TextureBinderTestPayloads.h>

namespace {

using oxygen::content::ResourceKey;
using oxygen::renderer::testing::FakeGraphics;
using oxygen::renderer::testing::MakeCookedTexture1x1Rgba8Payload;
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

class TextureBinderUploadFailureTest : public TextureBinderTest {
protected:
  auto ConfigureGraphics(FakeGraphics& gfx) -> void override
  {
    gfx.SetFailMap(true);
  }
};

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

  // One SRV view creation for the entry (initial placeholder binding).
  const auto creations_after_allocate
    = CountSrvViewCreationsForIndex(Gfx(), u_index);
  ASSERT_GE(creations_after_allocate, 1U);

  // Upload submission failure must not repoint the descriptor to the error
  // texture. The entry remains bound to the per-entry placeholder.
  const auto* const bound_texture = LastSrvViewTextureForIndex(Gfx(), u_index);
  ASSERT_NE(bound_texture, nullptr);
  EXPECT_NE(bound_texture, error_texture);

  // Second GetOrAllocate is a cache hit: it must not create/repoint the view.
  EXPECT_EQ(
    CountSrvViewCreationsForIndex(Gfx(), u_index), creations_after_allocate);
}

} // namespace
