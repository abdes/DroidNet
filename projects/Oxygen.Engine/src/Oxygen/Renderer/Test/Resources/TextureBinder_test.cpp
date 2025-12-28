//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Testing/GTest.h>

#include <algorithm>
#include <cstdint>
#include <limits>
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

[[nodiscard]] auto CountSrvViewCreationsForIndex(
  const oxygen::renderer::testing::FakeGraphics& gfx, const uint32_t index)
  -> std::size_t
{
  return static_cast<std::size_t>(std::count_if(
    gfx.srv_view_log_.events.begin(), gfx.srv_view_log_.events.end(),
    [&](const auto& e) { return e.index == index; }));
}

[[nodiscard]] auto LastSrvViewTextureForIndex(
  const oxygen::renderer::testing::FakeGraphics& gfx, const uint32_t index)
  -> const oxygen::graphics::Texture*
{
  for (auto it = gfx.srv_view_log_.events.rbegin();
    it != gfx.srv_view_log_.events.rend(); ++it) {
    if (it->index == index) {
      return it->texture;
    }
  }
  return nullptr;
}

[[nodiscard]] auto CaptureErrorTexturePtr(
  const oxygen::renderer::testing::FakeGraphics& gfx,
  const oxygen::bindless::ShaderVisibleIndex error_index)
  -> const oxygen::graphics::Texture*
{
  return LastSrvViewTextureForIndex(gfx, error_index.get());
}

//! The same key must always map to the same bindless SRV index.
/*! The TextureBinder must not allocate multiple descriptors for repeated
    requests of the same resource key. */
NOLINT_TEST_F(TextureBinderTest, SameKey_IsStable)
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

//! ResourceKey(0) is a renderer-side fallback sentinel.
/*! The binder must not trigger descriptor allocation for this key and must not
    return the shared error-texture index. */
NOLINT_TEST_F(TextureBinderTest, ZeroKey_ReturnsPlaceholder)
{
  // Arrange
  const auto before = AllocatedSrvCount();

  // Act
  const auto idx = Binder().GetOrAllocate(ResourceKey { 0 });

  // Assert
  EXPECT_NE(idx, Binder().GetErrorTextureIndex());
  EXPECT_EQ(AllocatedSrvCount(), before);
}

//! Load failures repoint the per-entry descriptor to the error texture.
/*! The shader-visible index returned by GetOrAllocate must remain stable, but
    the underlying SRV view should be repointed (via
   ResourceRegistry::UpdateView) to the shared error texture.

    This test observes repointing via FakeGraphics SRV view creation telemetry,
    without accessing any TextureBinder internals.
*/
NOLINT_TEST_F(TextureBinderTest, LoadFailure_RepointsToError)
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

  // Assert: stable SRV index, single allocation.
  EXPECT_EQ(index_0, index_1);
  EXPECT_NE(index_0, error_index);
  EXPECT_EQ(AllocatedSrvCount(), before + 1U);

  // Assert: descriptor for this entry is repointed to the shared error texture.
  const auto* const bound_texture
    = LastSrvViewTextureForIndex(Gfx(), index_0.get());
  ASSERT_NE(bound_texture, nullptr);
  EXPECT_EQ(bound_texture, error_texture);
}

//! Descriptor repoint must happen only after upload completion.
/*! The binder stores an upload ticket and must not repoint (or reclaim the
    per-entry placeholder) until UploadCoordinator reports completion.

    This test drives completion deterministically by controlling the fake
    transfer queue's completed fence value.
*/
NOLINT_TEST_F(TextureBinderTest, RepointOccursOnlyAfterCompletion)
{
  // Arrange: preload a valid CPU-side texture resource so StartLoadTexture
  // completes immediately.
  const auto payload = MakeCookedTexture1x1Rgba8Payload();
  const ResourceKey key = Loader().PreloadCookedTexture(
    std::span<const uint8_t>(payload.data(), payload.size()));

  // Set a non-invalid frame slot so UploadTracker records a creation slot.
  Uploader().OnFrameStart(oxygen::renderer::internal::RendererTagFactory::Get(),
    oxygen::frame::Slot { 1 });

  const auto error_index = Binder().GetErrorTextureIndex();
  const auto* const error_texture = CaptureErrorTexturePtr(Gfx(), error_index);
  ASSERT_NE(error_texture, nullptr);

  Gfx().srv_view_log_.events.clear();

  const auto srv_index = Binder().GetOrAllocate(key);
  const auto u_srv_index = srv_index.get();

  auto q
    = GfxPtr()->GetCommandQueue(oxygen::graphics::SingleQueueStrategy().KeyFor(
      oxygen::graphics::QueueRole::kTransfer));
  ASSERT_NE(q, nullptr);

  // After allocation and load submission, the entry must have an SRV view
  // registered at its bindless slot.
  const auto creations_after_allocate
    = CountSrvViewCreationsForIndex(Gfx(), u_srv_index);
  ASSERT_GE(creations_after_allocate, 1U);

  // The entry must not have been repointed to the error texture.
  EXPECT_NE(LastSrvViewTextureForIndex(Gfx(), u_srv_index), error_texture);

  // Simulate that the transfer queue has NOT completed yet.
  q->QueueSignalCommand(0);
  Uploader().OnFrameStart(oxygen::renderer::internal::RendererTagFactory::Get(),
    oxygen::frame::Slot { 2 });

  // Act: binder frame start should not observe completion -> no repoint.
  Binder().OnFrameStart();

  // Assert: no repoint while upload is incomplete.
  EXPECT_EQ(CountSrvViewCreationsForIndex(Gfx(), u_srv_index),
    creations_after_allocate);

  // Now simulate completion by advancing the queue's completed fence beyond
  // any possible registered upload fence.
  q->QueueSignalCommand((std::numeric_limits<uint64_t>::max)());
  Uploader().OnFrameStart(oxygen::renderer::internal::RendererTagFactory::Get(),
    oxygen::frame::Slot { 3 });

  // Act: binder should now observe completion and repoint.
  Binder().OnFrameStart();

  // Assert: exactly one additional SRV view creation at the same index,
  // indicating a descriptor repoint via UpdateView.
  EXPECT_EQ(CountSrvViewCreationsForIndex(Gfx(), u_srv_index),
    creations_after_allocate + 1U);
  EXPECT_NE(LastSrvViewTextureForIndex(Gfx(), u_srv_index), error_texture);
}

//! Cooked texture layout violations must be rejected deterministically.
/*! The binder expects cooked mip blobs to use a 256-byte row pitch and a
    512-byte mip placement alignment. If the payload violates these
    assumptions, the binder must repoint to the error texture and must not
    allocate additional descriptors on subsequent calls.
*/
NOLINT_TEST_F(TextureBinderTest, InvalidCookedLayout_IsRejected)
{
  // Arrange: preload a decoded resource that violates the cooked layout
  // assumptions (tight-packed rows).
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
  EXPECT_NE(index_0, Binder().GetErrorTextureIndex());
  EXPECT_EQ(AllocatedSrvCount(), before + 1U);

  const auto* const bound_texture
    = LastSrvViewTextureForIndex(Gfx(), index_0.get());
  ASSERT_NE(bound_texture, nullptr);
  EXPECT_EQ(bound_texture, error_texture);
}

//! Unsupported formats must be rejected via the error texture.
/*! This covers the F3 creation/format mismatch behavior: the binder must
    repoint the per-entry descriptor to the shared error texture while keeping
    the SRV index stable.
*/
NOLINT_TEST_F(TextureBinderTest, UnsupportedFormat_Rejected)
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
  EXPECT_NE(index_0, Binder().GetErrorTextureIndex());
  EXPECT_EQ(AllocatedSrvCount(), before + 1U);

  const auto* const bound_texture
    = LastSrvViewTextureForIndex(Gfx(), index_0.get());
  ASSERT_NE(bound_texture, nullptr);
  EXPECT_EQ(bound_texture, error_texture);
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
NOLINT_TEST_F(TextureBinderTest, ForcedError_IsDeterministic)
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
  EXPECT_NE(index_0, Binder().GetErrorTextureIndex());
  EXPECT_EQ(AllocatedSrvCount(), before + 1U);

  const auto* const bound_texture
    = LastSrvViewTextureForIndex(Gfx(), index_0.get());
  ASSERT_NE(bound_texture, nullptr);
  EXPECT_EQ(bound_texture, error_texture);
}

} // namespace
