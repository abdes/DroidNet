//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Testing/GTest.h>

#include <algorithm>
#include <cstdint>
#include <limits>

#include <Oxygen/Graphics/Common/Queues.h>
#include <Oxygen/Renderer/RendererTag.h>
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

[[nodiscard]] auto CaptureErrorTexturePtr(const FakeGraphics& gfx,
  const oxygen::bindless::ShaderVisibleIndex error_index)
  -> const oxygen::graphics::Texture*
{
  return LastSrvViewTextureForIndex(gfx, error_index.get());
}

class TextureBinderUploadTest : public TextureBinderTest { };

//! Descriptor repoint must happen only after upload completion.
/*! The binder stores an upload ticket and must not repoint the per-entry SRV
    view until UploadCoordinator reports completion.

    This test drives completion deterministically by controlling the fake
    transfer queue's completed fence value.
*/
NOLINT_TEST_F(TextureBinderUploadTest, RepointOccursOnlyAfterCompletion)
{
  // Arrange: preload a valid CPU-side texture resource so StartLoadTexture
  // completes immediately.
  const auto payload = MakeCookedTexture1x1Rgba8Payload();
  const ResourceKey key = Loader().PreloadCookedTexture(
    std::span<const uint8_t>(payload.data(), payload.size()));

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

  const auto creations_after_allocate
    = CountSrvViewCreationsForIndex(Gfx(), u_srv_index);
  ASSERT_GE(creations_after_allocate, 1U);

  const auto* const texture_before_completion
    = LastSrvViewTextureForIndex(Gfx(), u_srv_index);
  ASSERT_NE(texture_before_completion, nullptr);
  EXPECT_NE(texture_before_completion, error_texture);

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
  q->QueueSignalCommand((std::numeric_limits<std::uint64_t>::max)());
  Uploader().OnFrameStart(oxygen::renderer::internal::RendererTagFactory::Get(),
    oxygen::frame::Slot { 3 });

  // Act: binder should now observe completion and repoint.
  Binder().OnFrameStart();

  // Assert: exactly one additional SRV view creation at the same index.
  EXPECT_EQ(CountSrvViewCreationsForIndex(Gfx(), u_srv_index),
    creations_after_allocate + 1U);

  const auto* const texture_after_completion
    = LastSrvViewTextureForIndex(Gfx(), u_srv_index);
  ASSERT_NE(texture_after_completion, nullptr);
  EXPECT_NE(texture_after_completion, error_texture);
  EXPECT_NE(texture_after_completion, texture_before_completion);
}

//! Upload completion must not be observed without OnFrameStart().
/*! This verifies the contract that OnFrameStart() is the mechanism that drains
    upload completions and triggers descriptor repointing.
*/
NOLINT_TEST_F(TextureBinderUploadTest, CompletionNotObservedWithoutOnFrameStart)
{
  // Arrange
  const auto payload = MakeCookedTexture1x1Rgba8Payload();
  const ResourceKey key = Loader().PreloadCookedTexture(
    std::span<const uint8_t>(payload.data(), payload.size()));

  Uploader().OnFrameStart(oxygen::renderer::internal::RendererTagFactory::Get(),
    oxygen::frame::Slot { 1 });

  Gfx().srv_view_log_.events.clear();
  const auto index = Binder().GetOrAllocate(key);
  const auto u_index = index.get();

  auto q
    = GfxPtr()->GetCommandQueue(oxygen::graphics::SingleQueueStrategy().KeyFor(
      oxygen::graphics::QueueRole::kTransfer));
  ASSERT_NE(q, nullptr);

  const auto creations_after_allocate
    = CountSrvViewCreationsForIndex(Gfx(), u_index);
  ASSERT_GE(creations_after_allocate, 1U);

  // Simulate completion but do NOT call Binder().OnFrameStart().
  q->QueueSignalCommand((std::numeric_limits<std::uint64_t>::max)());
  Uploader().OnFrameStart(oxygen::renderer::internal::RendererTagFactory::Get(),
    oxygen::frame::Slot { 2 });

  // Act
  (void)Binder().GetOrAllocate(key);

  // Assert: still no repoint without Binder().OnFrameStart().
  EXPECT_EQ(
    CountSrvViewCreationsForIndex(Gfx(), u_index), creations_after_allocate);

  // Now drain completions.
  Binder().OnFrameStart();

  // Assert: repoint occurs once draining happens.
  EXPECT_EQ(CountSrvViewCreationsForIndex(Gfx(), u_index),
    creations_after_allocate + 1U);
}

//! Reserved keys never use per-entry placeholders and never repoint.
/*! Normal resource keys allocate a per-entry placeholder and later repoint the
    descriptor once the upload completes. Reserved fast-path keys
    (ResourceKey::kFallback and ResourceKey::kPlaceholder) must not allocate
    per-entry descriptors and must not repoint, even as uploads complete.
*/
NOLINT_TEST_F(
  TextureBinderUploadTest, NormalKeyAllocatesAndRepoinsAfterCompletion)
{
  // Arrange
  const auto before = AllocatedSrvCount();

  const auto payload = MakeCookedTexture1x1Rgba8Payload();
  const ResourceKey normal_key = Loader().PreloadCookedTexture(
    std::span<const uint8_t>(payload.data(), payload.size()));

  Uploader().OnFrameStart(oxygen::renderer::internal::RendererTagFactory::Get(),
    oxygen::frame::Slot { 1 });

  auto q
    = GfxPtr()->GetCommandQueue(oxygen::graphics::SingleQueueStrategy().KeyFor(
      oxygen::graphics::QueueRole::kTransfer));
  ASSERT_NE(q, nullptr);

  // Act
  const auto normal_index = Binder().GetOrAllocate(normal_key);

  // Assert: normal key allocates a per-entry descriptor.
  EXPECT_EQ(AllocatedSrvCount(), before + 1U);

  const auto u_normal = normal_index.get();
  const auto normal_creations_before
    = CountSrvViewCreationsForIndex(Gfx(), u_normal);

  ASSERT_GE(normal_creations_before, 1U);

  const auto* const normal_texture_before
    = LastSrvViewTextureForIndex(Gfx(), u_normal);

  ASSERT_NE(normal_texture_before, nullptr);

  // Drive completion and drain.
  q->QueueSignalCommand((std::numeric_limits<std::uint64_t>::max)());
  Uploader().OnFrameStart(oxygen::renderer::internal::RendererTagFactory::Get(),
    oxygen::frame::Slot { 2 });
  Binder().OnFrameStart();

  // Assert: normal key repoints once.
  EXPECT_EQ(CountSrvViewCreationsForIndex(Gfx(), u_normal),
    normal_creations_before + 1U);

  const auto* const normal_texture_after
    = LastSrvViewTextureForIndex(Gfx(), u_normal);

  ASSERT_NE(normal_texture_after, nullptr);

  EXPECT_NE(normal_texture_after, normal_texture_before);
}

//! Reserved keys do not allocate per-entry descriptors and never repoint.
/*! Reserved fast-path keys (ResourceKey::kFallback and
   ResourceKey::kPlaceholder) must not allocate per-entry descriptors and must
   never repoint, even as uploads complete.
*/
NOLINT_TEST_F(TextureBinderUploadTest, ReservedKeysDoNotAllocateAndDoNotRepoint)
{
  // Arrange
  const auto before = AllocatedSrvCount();

  Uploader().OnFrameStart(oxygen::renderer::internal::RendererTagFactory::Get(),
    oxygen::frame::Slot { 1 });

  auto q
    = GfxPtr()->GetCommandQueue(oxygen::graphics::SingleQueueStrategy().KeyFor(
      oxygen::graphics::QueueRole::kTransfer));
  ASSERT_NE(q, nullptr);

  // Act
  const auto fallback_index = Binder().GetOrAllocate(ResourceKey::kFallback);
  const auto placeholder_index
    = Binder().GetOrAllocate(ResourceKey::kPlaceholder);

  // Assert: reserved keys do not allocate per-entry descriptors.
  EXPECT_EQ(AllocatedSrvCount(), before);

  const auto u_fallback = fallback_index.get();
  const auto u_placeholder = placeholder_index.get();

  const auto fallback_creations_before
    = CountSrvViewCreationsForIndex(Gfx(), u_fallback);
  const auto placeholder_creations_before
    = CountSrvViewCreationsForIndex(Gfx(), u_placeholder);

  ASSERT_GE(fallback_creations_before, 1U);
  ASSERT_GE(placeholder_creations_before, 1U);

  const auto* const fallback_texture_before
    = LastSrvViewTextureForIndex(Gfx(), u_fallback);
  const auto* const placeholder_texture_before
    = LastSrvViewTextureForIndex(Gfx(), u_placeholder);

  ASSERT_NE(fallback_texture_before, nullptr);
  ASSERT_NE(placeholder_texture_before, nullptr);

  // Drive completion and drain.
  q->QueueSignalCommand((std::numeric_limits<std::uint64_t>::max)());
  Uploader().OnFrameStart(oxygen::renderer::internal::RendererTagFactory::Get(),
    oxygen::frame::Slot { 2 });
  Binder().OnFrameStart();

  // Assert: no repoint for reserved keys.
  EXPECT_EQ(CountSrvViewCreationsForIndex(Gfx(), u_fallback),
    fallback_creations_before);
  EXPECT_EQ(CountSrvViewCreationsForIndex(Gfx(), u_placeholder),
    placeholder_creations_before);

  const auto* const fallback_texture_after
    = LastSrvViewTextureForIndex(Gfx(), u_fallback);
  const auto* const placeholder_texture_after
    = LastSrvViewTextureForIndex(Gfx(), u_placeholder);

  ASSERT_NE(fallback_texture_after, nullptr);
  ASSERT_NE(placeholder_texture_after, nullptr);

  EXPECT_EQ(fallback_texture_after, fallback_texture_before);
  EXPECT_EQ(placeholder_texture_after, placeholder_texture_before);
}

} // namespace
