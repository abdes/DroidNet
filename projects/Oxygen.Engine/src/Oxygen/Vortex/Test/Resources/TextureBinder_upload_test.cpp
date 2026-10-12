//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <ranges>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#if defined(_MSC_VER) && defined(_DEBUG)
#  include <new>

#  include <Oxygen/Graphics/Common/ResourceRegistry.h>
#  include <Oxygen/Graphics/Common/Test/HeapAllocationFailure.h>
#endif
#include <Oxygen/Content/EvictionEvents.h>
#include <Oxygen/Content/ResourceKey.h>
#include <Oxygen/Graphics/Common/Texture.h>
#include <Oxygen/Graphics/Common/Types/QueueRole.h>
#include <Oxygen/Testing/GTest.h>
#include <Oxygen/Vortex/RendererTag.h>
#include <Oxygen/Vortex/Resources/TextureBinder.h>
#include <Oxygen/Vortex/Test/Fakes/AssetLoader.h>
#include <Oxygen/Vortex/Test/Fakes/Graphics.h>
#include <Oxygen/Vortex/Test/Fixtures/TextureBinderPayloads.h>
#include <Oxygen/Vortex/Test/Fixtures/TextureBinderTest.h>

namespace {

using oxygen::content::ResourceKey;
using oxygen::vortex::testing::DecodeCookedTexturePayload;
using oxygen::vortex::testing::FakeGraphics;
using oxygen::vortex::testing::MakeCookedTexture1x1Rgba8Payload;
using oxygen::vortex::testing::MakeCookedTexture8x8Bc7MipChainPayload;
using oxygen::vortex::testing::MakeTightPackedTexture1x1Rgba8Payload;
using oxygen::vortex::testing::TextureBinderTest;

[[nodiscard]] auto GetTextureDebugName(const oxygen::graphics::Texture* texture)
  -> std::string_view
{
  if (texture == nullptr) {
    return {};
  }
  return texture->GetDescriptor().debug_name;
}

[[nodiscard]] auto MakeTextureDebugName(const ResourceKey key) -> std::string
{
  return std::string("Texture(") + oxygen::content::to_string(key) + ")";
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

[[nodiscard]] auto MakeSyntheticTextureKeys(
  oxygen::vortex::testing::FakeAssetLoader& loader,
  const std::span<const std::uint8_t> payload, const std::size_t count)
  -> std::vector<ResourceKey>
{
  auto keys = std::vector<ResourceKey> {};
  keys.reserve(count);
  for (std::size_t i = 0; i < count; ++i) {
    const auto key = loader.MintSyntheticTextureKey();
    loader.SetCookedTexturePayload(key, payload);
    keys.push_back(key);
  }
  return keys;
}

class TextureBinderUploadTest : public TextureBinderTest {
protected:
  auto StartUploaderFrame(const std::uint32_t slot) const -> void
  {
    Uploader().OnFrameStart(oxygen::vortex::internal::RendererTagFactory::Get(),
      oxygen::frame::Slot { slot });
  }

  //! Completes the submitted uploads and publishes them.
  auto CompleteUploads() -> void
  {
    auto queue
      = Gfx().GetFakeCommandQueue(oxygen::graphics::QueueRole::kTransfer);
    ASSERT_NE(queue, nullptr);
    queue->CompleteThrough(queue->GetCurrentValue());
    StartUploaderFrame(2);
    BeginVisibleFrame();
    StartUploaderFrame(3);
    BeginVisibleFrame();
  }
};

class TextureBinderBacklogTest : public TextureBinderTest {
protected:
  [[nodiscard]] auto BinderLimits() const
    -> oxygen::vortex::resources::TextureBinder::UploadLimits override
  {
    return oxygen::vortex::resources::TextureBinder::UploadLimits {
      .max_upload_bytes_per_frame = 512U,
      .max_pending_upload_bytes = 1024U,
      .deferred_retry_low_watermark_bytes = 256U,
      .max_deferred_retries_per_frame = 2U,
    };
  }
};

//! The loaded version is published only after upload completion, through a
//! new descriptor; the shared placeholder descriptor is never rewritten.
/*! Completion is driven deterministically by controlling the fake transfer
    queue's completed fence value. */
NOLINT_TEST_F(
  TextureBinderUploadTest, NewDescriptorPublishedOnlyAfterCompletion)
{
  // Arrange: preload a valid CPU-side texture resource so StartLoadTexture
  // completes immediately.
  const auto payload = MakeCookedTexture1x1Rgba8Payload();
  const ResourceKey key
    = Loader().PreloadCookedTexture(std::span(payload.data(), payload.size()));
  StartUploaderFrame(1);
  const auto placeholder = TexBinder().GetOrAllocate(ResourceKey::kPlaceholder);
  const auto placeholder_views
    = CountSrvViewCreationsForIndex(Gfx(), placeholder.get());
  const auto revision_before = TexBinder().GetResidentContentRevision();
  ASSERT_EQ(TexBinder().GetOrAllocate(key), placeholder);
  auto q = Gfx().GetFakeCommandQueue(oxygen::graphics::QueueRole::kTransfer);
  ASSERT_NE(q, nullptr);

  // Act: the transfer queue has NOT completed yet.
  q->SetAutoComplete(false);
  q->CompleteThrough(0);
  StartUploaderFrame(2);
  BeginVisibleFrame();

  // Assert: still the placeholder.
  EXPECT_EQ(TexBinder().GetOrAllocate(key), placeholder);
  EXPECT_FALSE(TexBinder().IsResourceReady(key));

  // Act: complete exactly the submitted uploads; the maximum fence means
  // device loss.
  q->CompleteThrough(q->GetCurrentValue());
  StartUploaderFrame(3);
  BeginVisibleFrame();

  // Assert: a new descriptor for the loaded texture.
  const auto loaded = TexBinder().GetOrAllocate(key);
  EXPECT_NE(loaded, placeholder);
  EXPECT_TRUE(TexBinder().IsResourceReady(key));
  EXPECT_EQ(CountSrvViewCreationsForIndex(Gfx(), loaded.get()), 1U);
  EXPECT_EQ(
    GetTextureDebugName(LastSrvViewTextureForIndex(Gfx(), loaded.get())),
    MakeTextureDebugName(key));
  EXPECT_EQ(
    CountSrvViewCreationsForIndex(Gfx(), placeholder.get()), placeholder_views);
  EXPECT_GT(TexBinder().GetResidentContentRevision(), revision_before);
  EXPECT_GT(TexBinder().GetContentRevision(loaded),
    TexBinder().GetContentRevision(placeholder));
}

//! Upload completion must not be observed without OnFrameStart().
/*! OnFrameStart() is the mechanism that drains upload completions and
    publishes the loaded version. */
NOLINT_TEST_F(TextureBinderUploadTest, CompletionNotObservedWithoutOnFrameStart)
{
  // Arrange
  const auto payload = MakeCookedTexture1x1Rgba8Payload();
  const ResourceKey key
    = Loader().PreloadCookedTexture(std::span(payload.data(), payload.size()));
  StartUploaderFrame(1);
  const auto placeholder = TexBinder().GetOrAllocate(key);
  auto q = Gfx().GetFakeCommandQueue(oxygen::graphics::QueueRole::kTransfer);
  ASSERT_NE(q, nullptr);

  // Act: complete, but do NOT call BeginVisibleFrame().
  q->CompleteThrough(q->GetCurrentValue());
  StartUploaderFrame(2);

  // Assert
  EXPECT_EQ(TexBinder().GetOrAllocate(key), placeholder);

  // Act: drain. OnFrameStart() only submits and records an upload ticket;
  // completion becomes observable after UploadCoordinator advances one frame
  // with the ticket present.
  BeginVisibleFrame();
  StartUploaderFrame(3);
  BeginVisibleFrame();

  // Assert
  const auto loaded = TexBinder().GetOrAllocate(key);
  EXPECT_NE(loaded, placeholder);
  EXPECT_EQ(
    GetTextureDebugName(LastSrvViewTextureForIndex(Gfx(), loaded.get())),
    MakeTextureDebugName(key));
}

//! A normal key allocates a descriptor only for its loaded version.
NOLINT_TEST_F(TextureBinderUploadTest, NormalKeyAllocatesOnCompletionOnly)
{
  // Arrange
  const auto before = AllocatedSrvCount();
  const auto payload = MakeCookedTexture1x1Rgba8Payload();
  const ResourceKey key
    = Loader().PreloadCookedTexture(std::span(payload.data(), payload.size()));
  StartUploaderFrame(1);

  // Act
  (void)TexBinder().GetOrAllocate(key);

  // Assert: loading allocates nothing.
  EXPECT_EQ(AllocatedSrvCount(), before);

  // Act
  CompleteUploads();

  // Assert: one descriptor for the loaded version.
  EXPECT_EQ(AllocatedSrvCount(), before + 1U);
  EXPECT_TRUE(TexBinder().IsResourceReady(key));
}

//! Tight-packed cooked layouts must upload and publish correctly.
/*! Verifies that TextureBinder consumes the payload layout table rather than
    enforcing D3D12-only alignment assumptions (row pitch 256, placement 512).
*/
NOLINT_TEST_F(TextureBinderUploadTest, TightPackedPayload_UploadsAndPublishes)
{
  // Arrange
  const auto payload = MakeTightPackedTexture1x1Rgba8Payload();
  const ResourceKey key
    = Loader().PreloadCookedTexture(std::span(payload.data(), payload.size()));
  StartUploaderFrame(1);
  const auto placeholder = TexBinder().GetOrAllocate(key);

  // Act
  CompleteUploads();

  // Assert: published, not as the error texture.
  const auto loaded = TexBinder().GetOrAllocate(key);
  EXPECT_NE(loaded, placeholder);
  EXPECT_FALSE(TexBinder().HasResourceFailed(key));
  EXPECT_EQ(
    GetTextureDebugName(LastSrvViewTextureForIndex(Gfx(), loaded.get())),
    MakeTextureDebugName(key));
}

//! BC7 mip chains must upload and publish correctly.
/*! Ensures the binder supports block-compressed textures by consuming cooked
    layout tables and using full-subresource upload descriptors for all mips.
*/
NOLINT_TEST_F(TextureBinderUploadTest, Bc7MipChain_UploadsAndPublishes)
{
  // Arrange
  const auto payload = MakeCookedTexture8x8Bc7MipChainPayload();
  const ResourceKey key
    = Loader().PreloadCookedTexture(std::span(payload.data(), payload.size()));
  StartUploaderFrame(1);
  const auto placeholder = TexBinder().GetOrAllocate(key);

  // Act
  CompleteUploads();

  // Assert: published with the full mip chain, not as the error texture.
  const auto loaded = TexBinder().GetOrAllocate(key);
  EXPECT_NE(loaded, placeholder);
  EXPECT_FALSE(TexBinder().HasResourceFailed(key));
  EXPECT_EQ(
    GetTextureDebugName(LastSrvViewTextureForIndex(Gfx(), loaded.get())),
    MakeTextureDebugName(key));
  EXPECT_GT(TexBinder().TryGetMipLevels(key).value_or(0U), 1U);
}

//! Reserved keys do not allocate descriptors and are never rewritten.
/*! Reserved fast-path keys (ResourceKey::kFallback and
    ResourceKey::kPlaceholder) resolve to the shared placeholder, whatever
    uploads complete. */
NOLINT_TEST_F(TextureBinderUploadTest, ReservedKeysDoNotAllocateAndDoNotRepoint)
{
  // Arrange
  const auto before = AllocatedSrvCount();
  StartUploaderFrame(1);

  // Act
  const auto fallback_index = TexBinder().GetOrAllocate(ResourceKey::kFallback);
  const auto placeholder_index
    = TexBinder().GetOrAllocate(ResourceKey::kPlaceholder);

  // Assert: reserved keys do not allocate descriptors.
  EXPECT_EQ(AllocatedSrvCount(), before);
  EXPECT_EQ(fallback_index, placeholder_index);
  const auto views_before
    = CountSrvViewCreationsForIndex(Gfx(), placeholder_index.get());
  ASSERT_EQ(views_before, 1U);
  const auto* const texture_before
    = LastSrvViewTextureForIndex(Gfx(), placeholder_index.get());
  ASSERT_NE(texture_before, nullptr);
  EXPECT_EQ(GetTextureDebugName(texture_before), "TextureBinder.Placeholder2D");

  // Act
  CompleteUploads();

  // Assert: never rewritten.
  EXPECT_EQ(CountSrvViewCreationsForIndex(Gfx(), placeholder_index.get()),
    views_before);
  EXPECT_EQ(
    LastSrvViewTextureForIndex(Gfx(), placeholder_index.get()), texture_before);
}

//! Pending decoded uploads must stay within a fixed byte budget; excess loads
//! are deferred for later retry instead of accumulating unbounded CPU memory.
NOLINT_TEST_F(
  TextureBinderBacklogTest, PendingUploadBacklogIsBoundedAndDefersExcessLoads)
{
  const auto payload = MakeCookedTexture1x1Rgba8Payload();
  const auto decoded
    = DecodeCookedTexturePayload(std::span(payload.data(), payload.size()));
  ASSERT_NE(decoded, nullptr);

  const auto pending_budget = TexBinder().GetPendingUploadByteBudget();
  const auto bytes_per_texture = decoded->GetDataSize();
  ASSERT_GT(bytes_per_texture, 0U);

  const auto key_count = (pending_budget / bytes_per_texture) + 2U;
  const auto keys = MakeSyntheticTextureKeys(
    Loader(), std::span(payload.data(), payload.size()), key_count);

  Uploader().OnFrameStart(oxygen::vortex::internal::RendererTagFactory::Get(),
    oxygen::frame::Slot {
      1,
    });

  for (const auto key : keys) {
    (void)TexBinder().GetOrAllocate(key);
  }

  EXPECT_LE(TexBinder().GetPendingUploadBytes(), pending_budget);
  EXPECT_LT(TexBinder().GetPendingUploadCount(), keys.size());
  EXPECT_GT(TexBinder().GetDeferredRetryCount(), 0U);
}

//! Deferred overflow retries must eventually drain and leave all requested
//! textures resident once enough frame starts advance upload completion.
NOLINT_TEST_F(TextureBinderBacklogTest,
  DeferredOverflowRetriesEventuallyCompleteQueuedTextures)
{
  const auto payload = MakeCookedTexture1x1Rgba8Payload();
  const auto decoded
    = DecodeCookedTexturePayload(std::span(payload.data(), payload.size()));
  ASSERT_NE(decoded, nullptr);

  const auto pending_budget = TexBinder().GetPendingUploadByteBudget();
  const auto bytes_per_texture = decoded->GetDataSize();
  ASSERT_GT(bytes_per_texture, 0U);

  const auto key_count = (pending_budget / bytes_per_texture) + 2U;
  const auto keys = MakeSyntheticTextureKeys(
    Loader(), std::span(payload.data(), payload.size()), key_count);

  auto q = Gfx().GetFakeCommandQueue(oxygen::graphics::QueueRole::kTransfer);
  ASSERT_NE(q, nullptr);

  Uploader().OnFrameStart(oxygen::vortex::internal::RendererTagFactory::Get(),
    oxygen::frame::Slot {
      1,
    });

  for (const auto key : keys) {
    (void)TexBinder().GetOrAllocate(key);
  }

  for (std::uint32_t slot = 2U; slot < 6U; ++slot) {
    Uploader().OnFrameStart(oxygen::vortex::internal::RendererTagFactory::Get(),
      oxygen::frame::Slot {
        slot,
      });
    BeginVisibleFrame();
    q->CompleteThrough(q->GetCurrentValue());
  }

  Uploader().OnFrameStart(oxygen::vortex::internal::RendererTagFactory::Get(),
    oxygen::frame::Slot {
      6U,
    });
  BeginVisibleFrame();

  for (const auto key : keys) {
    EXPECT_TRUE(TexBinder().IsResourceReady(key));
  }
  EXPECT_EQ(TexBinder().GetPendingUploadCount(), 0U);
  EXPECT_EQ(TexBinder().GetPendingUploadBytes(), 0U);
  EXPECT_EQ(TexBinder().GetDeferredRetryCount(), 0U);
}

class TextureBinderCompletionBudgetTest : public TextureBinderTest {
protected:
  [[nodiscard]] auto BinderLimits() const
    -> oxygen::vortex::resources::TextureBinder::UploadLimits override
  {
    auto limits = oxygen::vortex::resources::TextureBinder::UploadLimits {};
    limits.max_completion_visits_per_frame = 1U;
    return limits;
  }
};

NOLINT_TEST_F(
  TextureBinderCompletionBudgetTest, PublishesOnlyTheBudgetedPendingEntries)
{
  const auto payload = MakeCookedTexture1x1Rgba8Payload();
  const auto keys = MakeSyntheticTextureKeys(Loader(), payload, 3U);
  for (const auto key : keys) {
    (void)TexBinder().GetOrAllocate(key);
  }
  BeginVisibleFrame();
  Uploader().OnFrameStart(oxygen::vortex::internal::RendererTagFactory::Get(),
    oxygen::frame::Slot { 1 });
  for (std::size_t completed = 1U; completed <= keys.size(); ++completed) {
    BeginVisibleFrame();
    EXPECT_EQ(std::ranges::count_if(keys,
                [&](const auto key) -> bool {
                  return TexBinder().IsResourceReady(key);
                }),
      completed);
  }
}

//! A failed view registration keeps the completion queued; the next frame
//! publishes the loaded version.
NOLINT_TEST_F(TextureBinderUploadTest, FailedPublicationKeepsItsWorkForRetry)
{
  const auto payload = MakeCookedTexture1x1Rgba8Payload();
  const auto key = Loader().PreloadCookedTexture(payload);
  const auto placeholder = TexBinder().GetOrAllocate(key);
  BeginVisibleFrame();
  StartUploaderFrame(1);
  const auto descriptors = AllocatedSrvCount();
  Gfx().SetThrowOnTextureViewCreation(true);
  EXPECT_THROW(BeginVisibleFrame(), std::runtime_error);
  EXPECT_FALSE(TexBinder().IsResourceReady(key));
  EXPECT_EQ(TexBinder().GetOrAllocate(key), placeholder);
  EXPECT_EQ(AllocatedSrvCount(), descriptors);
  Gfx().SetThrowOnTextureViewCreation(false);
  BeginVisibleFrame();
  EXPECT_TRUE(TexBinder().IsResourceReady(key));
  EXPECT_NE(TexBinder().GetOrAllocate(key), placeholder);
}

#if defined(_MSC_VER) && defined(_DEBUG)
//! An allocation failure while publishing registers nothing; the next frame
//! publishes the loaded version.
NOLINT_TEST_F(
  TextureBinderUploadTest, AllocationFailurePreservesPendingPublication)
{
  const auto payload = MakeCookedTexture1x1Rgba8Payload();
  const auto key = Loader().PreloadCookedTexture(payload);
  const auto placeholder = TexBinder().GetOrAllocate(key);
  BeginVisibleFrame();
  StartUploaderFrame(1);
  const auto before = Gfx().GetResourceRegistry().GetRegisteredResourceCount();
  const auto descriptors = AllocatedSrvCount();
  bool failed = false;
  {
    const oxygen::graphics::testing::HeapAllocationFailure deny_allocations;
    try {
      BeginVisibleFrame();
    } catch (const std::bad_alloc&) {
      failed = true;
    }
  }
  EXPECT_TRUE(failed);
  EXPECT_EQ(Gfx().GetResourceRegistry().GetRegisteredResourceCount(), before);
  EXPECT_EQ(AllocatedSrvCount(), descriptors);
  EXPECT_EQ(TexBinder().GetOrAllocate(key), placeholder);
  BeginVisibleFrame();
  EXPECT_TRUE(TexBinder().IsResourceReady(key));
  EXPECT_NE(TexBinder().GetOrAllocate(key), placeholder);
}
#endif

class TextureBinderDecodedBudgetTest : public TextureBinderTest {
protected:
  [[nodiscard]] auto BinderLimits() const
    -> oxygen::vortex::resources::TextureBinder::UploadLimits override
  {
    auto limits = oxygen::vortex::resources::TextureBinder::UploadLimits {};
    limits.max_upload_visits_per_frame = 2U;
    return limits;
  }
};

NOLINT_TEST_F(
  TextureBinderDecodedBudgetTest, StaleDecodedCleanupConsumesTheVisitBudget)
{
  const auto payload = MakeCookedTexture1x1Rgba8Payload();
  const auto keys = MakeSyntheticTextureKeys(Loader(), payload, 5U);
  for (const auto key : keys) {
    (void)TexBinder().GetOrAllocate(key);
    Loader().EmitTextureEviction(
      key, oxygen::content::EvictionReason::kRefCountZero);
  }
  ASSERT_EQ(TexBinder().GetPendingUploadCount(), 5U);
  TexBinder().OnFrameStart();
  EXPECT_EQ(TexBinder().GetPendingUploadCount(), 3U);
  TexBinder().OnFrameStart();
  EXPECT_EQ(TexBinder().GetPendingUploadCount(), 1U);
  TexBinder().OnFrameStart();
  EXPECT_EQ(TexBinder().GetPendingUploadCount(), 0U);
  for (const auto key : keys) {
    EXPECT_FALSE(TexBinder().IsResourceReady(key));
  }
}

NOLINT_TEST_F(TextureBinderUploadTest,
  FramesWithoutDemandDiscardEvictedWorkWithoutSubmitting)
{
  const auto payload = MakeCookedTexture1x1Rgba8Payload();
  const auto key = Loader().PreloadCookedTexture(payload);
  (void)TexBinder().GetOrAllocate(key);
  const auto queue
    = Gfx().GetFakeCommandQueue(oxygen::graphics::QueueRole::kTransfer);
  ASSERT_NE(queue, nullptr);
  const auto submissions = queue->submitted_batches;
  TexBinder().OnFrameStart();
  EXPECT_EQ(TexBinder().GetPendingUploadCount(), 1U);
  EXPECT_EQ(queue->submitted_batches, submissions);
  Loader().EmitTextureEviction(key, oxygen::content::EvictionReason::kClear);
  TexBinder().OnFrameStart();
  EXPECT_EQ(TexBinder().GetPendingUploadCount(), 0U);
  EXPECT_EQ(queue->submitted_batches, submissions);
}

NOLINT_TEST_F(
  TextureBinderDecodedBudgetTest, CleanupPreservesUploadAdmissionOrder)
{
  const auto payload = MakeCookedTexture1x1Rgba8Payload();
  const auto keys = MakeSyntheticTextureKeys(Loader(), payload, 3U);
  for (const auto key : keys) {
    (void)TexBinder().GetOrAllocate(key);
  }
  BeginVisibleFrame();
  Uploader().OnFrameStart(oxygen::vortex::internal::RendererTagFactory::Get(),
    oxygen::frame::Slot { 1 });
  BeginVisibleFrame();
  EXPECT_TRUE(TexBinder().IsResourceReady(keys.at(0)));
  EXPECT_TRUE(TexBinder().IsResourceReady(keys.at(1)));
  EXPECT_FALSE(TexBinder().IsResourceReady(keys.at(2)));
}

NOLINT_TEST_F(
  TextureBinderDecodedBudgetTest, NewArrivalsCannotStarveCleanupOfEarlierItems)
{
  const auto payload = MakeCookedTexture1x1Rgba8Payload();
  const auto keys = MakeSyntheticTextureKeys(Loader(), payload, 8U);
  for (std::size_t index = 0U; index < 4U; ++index) {
    (void)TexBinder().GetOrAllocate(keys.at(index));
  }
  TexBinder().OnFrameStart();
  Loader().EmitTextureEviction(
    keys.at(0), oxygen::content::EvictionReason::kClear);
  for (std::size_t index = 4U; index < keys.size(); index += 2U) {
    (void)TexBinder().GetOrAllocate(keys.at(index));
    (void)TexBinder().GetOrAllocate(keys.at(index + 1U));
    TexBinder().OnFrameStart();
  }
  EXPECT_EQ(TexBinder().GetPendingUploadCount(), keys.size() - 1U);
}

} // namespace
