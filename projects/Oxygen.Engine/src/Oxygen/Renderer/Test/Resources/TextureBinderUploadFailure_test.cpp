//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Testing/GTest.h>

#include <cstdint>
#include <memory>

#include <Oxygen/Renderer/Test/Resources/TextureBinderTest.h>
#include <Oxygen/Renderer/Test/Resources/TextureBinderTestPayloads.h>

namespace {

using oxygen::content::ResourceKey;
using oxygen::renderer::testing::FakeGraphics;
using oxygen::renderer::testing::MakeCookedTexture1x1Rgba8Payload;
using oxygen::renderer::testing::TextureBinderTest;

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
NOLINT_TEST_F(TextureBinderUploadFailureTest,
  GetOrAllocate_UploadSubmissionFailure_KeepsPlaceholder)
{
  // Arrange
  const auto before = AllocatedSrvCount();
  const ResourceKey key = AssetLoaderRef().MintSyntheticTextureKey();
  const auto payload = MakeCookedTexture1x1Rgba8Payload();

  std::shared_ptr<oxygen::data::TextureResource> decoded;
  AssetLoaderRef().StartLoadTextureFromBuffer(key,
    std::span<const uint8_t>(payload.data(), payload.size()),
    [&](std::shared_ptr<oxygen::data::TextureResource> tex) {
      decoded = std::move(tex);
    });
  ASSERT_NE(decoded, nullptr);

  // Act
  const auto index_0 = Binder().GetOrAllocate(key);
  const auto snapshot_0 = Binder().DebugGetEntry(key);
  const auto index_1 = Binder().GetOrAllocate(key);
  const auto snapshot_1 = Binder().DebugGetEntry(key);

  // Assert
  EXPECT_EQ(index_0, index_1);
  EXPECT_NE(index_0, Binder().GetErrorTextureIndex());
  EXPECT_EQ(AllocatedSrvCount(), before + 1U);

  ASSERT_TRUE(snapshot_0.has_value());
  EXPECT_TRUE(snapshot_0->load_failed);
  EXPECT_TRUE(snapshot_0->is_placeholder);
  EXPECT_FALSE(snapshot_0->pending_fence.has_value());
  EXPECT_TRUE(snapshot_0->placeholder_texture);
  ASSERT_TRUE(snapshot_0->texture);
  EXPECT_EQ(snapshot_0->texture, snapshot_0->placeholder_texture);

  ASSERT_TRUE(snapshot_1.has_value());
  EXPECT_TRUE(snapshot_1->load_failed);
  EXPECT_TRUE(snapshot_1->is_placeholder);
  EXPECT_FALSE(snapshot_1->pending_fence.has_value());
  EXPECT_TRUE(snapshot_1->placeholder_texture);
}

} // namespace
