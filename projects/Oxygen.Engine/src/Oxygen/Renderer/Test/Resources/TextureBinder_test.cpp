//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Testing/GTest.h>

#include <array>
#include <cstdint>
#include <cstring>
#include <vector>

#include <Oxygen/Core/Types/Frame.h>
#include <Oxygen/Core/Types/TextureType.h>
#include <Oxygen/Data/PakFormat.h>
#include <Oxygen/Graphics/Common/Queues.h>
#include <Oxygen/Renderer/RendererTag.h>
#include <Oxygen/Renderer/Resources/TextureBinder.h>

#include <Oxygen/Renderer/Test/Resources/TextureBinderTest.h>
#include <Oxygen/Renderer/Test/Resources/TextureBinderTestPayloads.h>

namespace {

using oxygen::content::ResourceKey;
using oxygen::renderer::resources::TextureBinder;
using oxygen::renderer::testing::MakeCookedTexture1x1Rgba8Payload;
using oxygen::renderer::testing::MakeCookedTexture4x4Bc1Payload;
using oxygen::renderer::testing::MakeInvalidTightPackedTexture1x1Rgba8Payload;
using oxygen::renderer::testing::TextureBinderTest;

//! The same key must always map to the same bindless SRV index.
/*! The TextureBinder must not allocate multiple descriptors for repeated
    requests of the same resource key. */
NOLINT_TEST_F(TextureBinderTest, GetOrAllocate_SameKey_IsStable)
{
  // Arrange
  const auto before = AllocatedSrvCount();
  const ResourceKey key = AssetLoaderRef().MintSyntheticTextureKey();

  // Act
  const auto index_0 = Binder().GetOrAllocate(key);
  const auto index_1 = Binder().GetOrAllocate(key);

  // Assert
  EXPECT_EQ(index_0, index_1);
  EXPECT_EQ(AllocatedSrvCount(), before + 1U);
}

//! ResourceKey(0) is a renderer-side fallback sentinel.
/*! The binder must not trigger descriptor allocation for this key and must not
    return the shared error-texture index. */
NOLINT_TEST_F(TextureBinderTest, GetOrAllocate_ZeroKey_ReturnsPlaceholder)
{
  // Arrange
  const auto before = AllocatedSrvCount();

  // Act
  const auto idx = Binder().GetOrAllocate(static_cast<ResourceKey>(0));

  // Assert
  EXPECT_NE(idx, Binder().GetErrorTextureIndex());
  EXPECT_EQ(AllocatedSrvCount(), before);
}

//! A load failure must not change the per-resource SRV index.
/*! When a load fails, the descriptor should be repointed to the error texture,
    but the shader-visible handle returned by GetOrAllocate must remain stable.
 */
NOLINT_TEST_F(TextureBinderTest, GetOrAllocate_LoadFailure_PreservesIndex)
{
  // Arrange
  const ResourceKey key = AssetLoaderRef().MintSyntheticTextureKey();

  // Act
  const auto index_0 = Binder().GetOrAllocate(key);
  const auto index_1 = Binder().GetOrAllocate(key);

  // Assert
  EXPECT_EQ(index_0, index_1);
  EXPECT_NE(index_0, Binder().GetErrorTextureIndex());
}

//! Descriptor repoint must happen only after upload completion.
/*! The binder stores an upload ticket and must not repoint (or reclaim the
    per-entry placeholder) until UploadCoordinator reports completion.

    This test drives completion deterministically by controlling the fake
    transfer queue's completed fence value.
*/
NOLINT_TEST_F(TextureBinderTest, GetOrAllocate_RepointOccursOnlyAfterCompletion)
{
  // Arrange: preload a valid CPU-side texture resource into the AssetLoader
  // cache using the buffer path, so StartLoadTexture returns immediately.
  const ResourceKey key = AssetLoaderRef().MintSyntheticTextureKey();
  const auto payload = MakeCookedTexture1x1Rgba8Payload();

  std::shared_ptr<oxygen::data::TextureResource> decoded;
  AssetLoaderRef().StartLoadTextureFromBuffer(key,
    std::span<const uint8_t>(payload.data(), payload.size()),
    [&](std::shared_ptr<oxygen::data::TextureResource> tex) {
      decoded = std::move(tex);
    });
  ASSERT_NE(decoded, nullptr);

  // Set a non-invalid frame slot so UploadTracker records a creation slot.
  Uploader().OnFrameStart(oxygen::renderer::internal::RendererTagFactory::Get(),
    oxygen::frame::Slot { 1 });

  const auto srv_index = Binder().GetOrAllocate(key);
  (void)srv_index;

  const auto before = Binder().DebugGetEntry(key);
  ASSERT_TRUE(before.has_value());
  ASSERT_TRUE(before->pending_fence.has_value());
  ASSERT_TRUE(before->placeholder_texture);
  ASSERT_TRUE(before->is_placeholder);
  ASSERT_FALSE(before->load_failed);

  auto q
    = GfxPtr()->GetCommandQueue(oxygen::graphics::SingleQueueStrategy().KeyFor(
      oxygen::graphics::QueueRole::kTransfer));
  ASSERT_NE(q, nullptr);

  // Simulate that the transfer queue has NOT completed yet.
  q->QueueSignalCommand(0);
  Uploader().OnFrameStart(oxygen::renderer::internal::RendererTagFactory::Get(),
    oxygen::frame::Slot { 2 });

  // Act: binder frame start should not observe completion -> no repoint.
  Binder().OnFrameStart();

  // Assert: still pending, placeholder not reclaimed.
  const auto mid = Binder().DebugGetEntry(key);
  ASSERT_TRUE(mid.has_value());
  EXPECT_TRUE(mid->pending_fence.has_value());
  EXPECT_TRUE(mid->placeholder_texture);
  EXPECT_TRUE(mid->is_placeholder);

  // Now simulate completion by advancing the queue's completed fence.
  q->QueueSignalCommand(*before->pending_fence);
  Uploader().OnFrameStart(oxygen::renderer::internal::RendererTagFactory::Get(),
    oxygen::frame::Slot { 3 });

  // Act: binder should now observe completion and repoint.
  Binder().OnFrameStart();

  // Assert: ticket drained and placeholder reclaimed after repoint.
  const auto after = Binder().DebugGetEntry(key);
  ASSERT_TRUE(after.has_value());
  EXPECT_FALSE(after->pending_fence.has_value());
  EXPECT_FALSE(after->placeholder_texture);
  EXPECT_FALSE(after->is_placeholder);
  EXPECT_FALSE(after->load_failed);
}

//! Cooked texture layout violations must be rejected deterministically.
/*! The binder expects cooked mip blobs to use a 256-byte row pitch and a
    512-byte mip placement alignment. If the payload violates these
    assumptions, the binder must repoint to the error texture and must not
    allocate additional descriptors on subsequent calls.
*/
NOLINT_TEST_F(TextureBinderTest, GetOrAllocate_InvalidCookedLayout_IsRejected)
{
  // Arrange: preload a decoded resource that violates the cooked layout
  // assumptions (tight-packed rows).
  const auto before = AllocatedSrvCount();
  const ResourceKey key = AssetLoaderRef().MintSyntheticTextureKey();
  const auto payload = MakeInvalidTightPackedTexture1x1Rgba8Payload();

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
  EXPECT_FALSE(snapshot_0->is_placeholder);
  EXPECT_FALSE(snapshot_0->pending_fence.has_value());
  EXPECT_FALSE(snapshot_0->placeholder_texture);

  ASSERT_TRUE(snapshot_1.has_value());
  EXPECT_TRUE(snapshot_1->load_failed);
  EXPECT_FALSE(snapshot_1->is_placeholder);
  EXPECT_FALSE(snapshot_1->placeholder_texture);
}

//! Upload submission failures must keep the placeholder bound.
/*! If the UploadCoordinator cannot submit work (e.g. staging allocation/map
    fails), the binder must keep the placeholder SRV active (no descriptor
    repoint to error) and mark the entry as failed deterministically.
*/
//! Unsupported formats must be rejected via the error texture.
/*! This covers the F3 creation/format mismatch behavior: the binder must
    repoint the per-entry descriptor to the shared error texture while keeping
    the SRV index stable.
*/
NOLINT_TEST_F(TextureBinderTest, GetOrAllocate_UnsupportedFormat_Rejected)
{
  // Arrange
  const auto before = AllocatedSrvCount();
  const ResourceKey key = AssetLoaderRef().MintSyntheticTextureKey();
  const auto payload = MakeCookedTexture4x4Bc1Payload();

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
  EXPECT_FALSE(snapshot_0->is_placeholder);
  EXPECT_FALSE(snapshot_0->pending_fence.has_value());
  EXPECT_FALSE(snapshot_0->placeholder_texture);

  ASSERT_TRUE(snapshot_1.has_value());
  EXPECT_TRUE(snapshot_1->load_failed);
  EXPECT_FALSE(snapshot_1->is_placeholder);
  EXPECT_FALSE(snapshot_1->placeholder_texture);
}

//! Forced-error mode must be deterministic.
/*! When AssetLoader cannot resolve a valid source for a ResourceKey, the binder
    must repoint the existing descriptor to the error texture while preserving
    the per-resource SRV index, and it must not allocate additional descriptors
    on subsequent calls.

    Additionally, once the descriptor has been repointed, the per-entry
    placeholder texture should be released (deferred) to avoid leaking GPU
    resources.
*/
NOLINT_TEST_F(TextureBinderTest, GetOrAllocate_ForcedError_IsDeterministic)
{
  // Arrange
  const auto before = AllocatedSrvCount();
  const ResourceKey key = AssetLoaderRef().MintSyntheticTextureKey();

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
  EXPECT_FALSE(snapshot_0->pending_fence.has_value());
  EXPECT_TRUE(snapshot_0->load_failed);
  EXPECT_FALSE(snapshot_0->is_placeholder);
  EXPECT_FALSE(snapshot_0->placeholder_texture);

  ASSERT_TRUE(snapshot_1.has_value());
  EXPECT_TRUE(snapshot_1->load_failed);
  EXPECT_FALSE(snapshot_1->is_placeholder);
  EXPECT_FALSE(snapshot_1->placeholder_texture);
}

} // namespace
