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
#include <string>
#include <string_view>

#if defined(_MSC_VER) && defined(_DEBUG)
#  include <new>

#  include <Oxygen/Graphics/Common/ResourceRegistry.h>
#  include <Oxygen/Graphics/Common/Test/HeapAllocationFailure.h>
#endif
#include <Oxygen/Content/EvictionEvents.h>
#include <Oxygen/Content/ResourceKey.h>
#include <Oxygen/Graphics/Common/Texture.h>
#include <Oxygen/Graphics/Common/Types/QueueRole.h>
#include <Oxygen/Graphics/Common/Types/ResourceStates.h>
#include <Oxygen/Testing/GTest.h>
#include <Oxygen/Vortex/RendererTag.h>
#include <Oxygen/Vortex/Resources/TextureBinder.h>
#include <Oxygen/Vortex/Test/Fakes/Graphics.h>
#include <Oxygen/Vortex/Test/Fixtures/TextureBinderPayloads.h>
#include <Oxygen/Vortex/Test/Fixtures/TextureBinderTest.h>

namespace {

using oxygen::content::EvictionReason;
using oxygen::content::ResourceKey;
using oxygen::vortex::testing::FakeGraphics;
using oxygen::vortex::testing::MakeCookedTexture1x1Rgba8Payload;
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

class TextureBinderEvictionTest : public TextureBinderTest { };

//! Eviction repoints the descriptor to the global placeholder texture.
NOLINT_TEST_F(TextureBinderEvictionTest, EvictionRepointsToFallback)
{
  // Arrange
  const auto payload = MakeCookedTexture1x1Rgba8Payload();
  const ResourceKey key
    = Loader().PreloadCookedTexture(std::span(payload.data(), payload.size()));

  Uploader().OnFrameStart(oxygen::vortex::internal::RendererTagFactory::Get(),
    oxygen::frame::Slot {
      1,
    });

  Gfx().srv_view_log_.events.clear();
  const auto srv_index = TexBinder().GetOrAllocate(key);
  const auto u_srv_index = srv_index.get();

  auto q = Gfx().GetFakeCommandQueue(oxygen::graphics::QueueRole::kTransfer);
  ASSERT_NE(q, nullptr);

  q->CompleteThrough(q->GetCurrentValue());

  BeginVisibleFrame();
  Uploader().OnFrameStart(oxygen::vortex::internal::RendererTagFactory::Get(),
    oxygen::frame::Slot {
      2,
    });
  BeginVisibleFrame();

  const auto* const resident_texture
    = LastSrvViewTextureForIndex(Gfx(), u_srv_index);
  ASSERT_NE(resident_texture, nullptr);
  EXPECT_NE(GetTextureDebugName(resident_texture), "FallbackTexture");

  // Act
  Loader().EmitTextureEviction(key, EvictionReason::kRefCountZero);
  BeginVisibleFrame();

  // Assert
  const auto* const evicted_texture
    = LastSrvViewTextureForIndex(Gfx(), u_srv_index);
  ASSERT_NE(evicted_texture, nullptr);
  EXPECT_EQ(GetTextureDebugName(evicted_texture), "FallbackTexture");
  EXPECT_FALSE(TexBinder().IsResourceReady(key));
}

//! Eviction suppresses late upload completions for in-flight uploads.
NOLINT_TEST_F(TextureBinderEvictionTest, InFlightCompletionIsDiscarded)
{
  // Arrange
  const auto payload = MakeCookedTexture1x1Rgba8Payload();
  const ResourceKey key
    = Loader().PreloadCookedTexture(std::span(payload.data(), payload.size()));

  Uploader().OnFrameStart(oxygen::vortex::internal::RendererTagFactory::Get(),
    oxygen::frame::Slot {
      1,
    });

  Gfx().srv_view_log_.events.clear();
  const auto srv_index = TexBinder().GetOrAllocate(key);
  const auto u_srv_index = srv_index.get();

  auto q = Gfx().GetFakeCommandQueue(oxygen::graphics::QueueRole::kTransfer);
  ASSERT_NE(q, nullptr);

  q->SetAutoComplete(false);
  q->CompleteThrough(0);
  BeginVisibleFrame();

  const auto creations_after_submit
    = CountSrvViewCreationsForIndex(Gfx(), u_srv_index);

  // Act
  Loader().EmitTextureEviction(key, EvictionReason::kRefCountZero);
  BeginVisibleFrame();

  const auto creations_after_eviction
    = CountSrvViewCreationsForIndex(Gfx(), u_srv_index);
  ASSERT_GT(creations_after_eviction, creations_after_submit);

  q->CompleteThrough(q->GetCurrentValue());
  Uploader().OnFrameStart(oxygen::vortex::internal::RendererTagFactory::Get(),
    oxygen::frame::Slot {
      2,
    });
  BeginVisibleFrame();

  // Assert
  EXPECT_EQ(CountSrvViewCreationsForIndex(Gfx(), u_srv_index),
    creations_after_eviction);

  const auto* const final_texture
    = LastSrvViewTextureForIndex(Gfx(), u_srv_index);
  ASSERT_NE(final_texture, nullptr);
  EXPECT_EQ(GetTextureDebugName(final_texture), "FallbackTexture");
}

//! Evicted entries can be reloaded and repointed to fresh textures.
NOLINT_TEST_F(TextureBinderEvictionTest, EvictionThenReloadRepoints)
{
  // Arrange
  const auto payload = MakeCookedTexture1x1Rgba8Payload();
  const ResourceKey key
    = Loader().PreloadCookedTexture(std::span(payload.data(), payload.size()));

  Uploader().OnFrameStart(oxygen::vortex::internal::RendererTagFactory::Get(),
    oxygen::frame::Slot {
      1,
    });

  Gfx().srv_view_log_.events.clear();
  const auto srv_index = TexBinder().GetOrAllocate(key);
  const auto u_srv_index = srv_index.get();

  auto q = Gfx().GetFakeCommandQueue(oxygen::graphics::QueueRole::kTransfer);
  ASSERT_NE(q, nullptr);

  q->CompleteThrough(q->GetCurrentValue());
  BeginVisibleFrame();
  Uploader().OnFrameStart(oxygen::vortex::internal::RendererTagFactory::Get(),
    oxygen::frame::Slot {
      2,
    });
  BeginVisibleFrame();

  // Act
  Loader().EmitTextureEviction(key, EvictionReason::kRefCountZero);
  BeginVisibleFrame();

  const auto* const evicted_texture
    = LastSrvViewTextureForIndex(Gfx(), u_srv_index);
  ASSERT_NE(evicted_texture, nullptr);
  EXPECT_EQ(GetTextureDebugName(evicted_texture), "FallbackTexture");

  (void)TexBinder().GetOrAllocate(key);

  q->CompleteThrough(q->GetCurrentValue());
  BeginVisibleFrame();
  Uploader().OnFrameStart(oxygen::vortex::internal::RendererTagFactory::Get(),
    oxygen::frame::Slot {
      3,
    });
  BeginVisibleFrame();

  // Assert
  const auto* const final_texture
    = LastSrvViewTextureForIndex(Gfx(), u_srv_index);
  ASSERT_NE(final_texture, nullptr);
  EXPECT_NE(GetTextureDebugName(final_texture), "FallbackTexture");
}

//! Eviction is idempotent and does not repoint repeatedly.
NOLINT_TEST_F(TextureBinderEvictionTest, EvictionIsIdempotent)
{
  // Arrange
  const auto payload = MakeCookedTexture1x1Rgba8Payload();
  const ResourceKey key
    = Loader().PreloadCookedTexture(std::span(payload.data(), payload.size()));

  Uploader().OnFrameStart(oxygen::vortex::internal::RendererTagFactory::Get(),
    oxygen::frame::Slot {
      1,
    });

  Gfx().srv_view_log_.events.clear();
  const auto srv_index = TexBinder().GetOrAllocate(key);
  const auto u_srv_index = srv_index.get();

  auto q = Gfx().GetFakeCommandQueue(oxygen::graphics::QueueRole::kTransfer);
  ASSERT_NE(q, nullptr);

  q->CompleteThrough(q->GetCurrentValue());
  BeginVisibleFrame();
  Uploader().OnFrameStart(oxygen::vortex::internal::RendererTagFactory::Get(),
    oxygen::frame::Slot {
      2,
    });
  BeginVisibleFrame();

  // Act
  Loader().EmitTextureEviction(key, EvictionReason::kRefCountZero);
  BeginVisibleFrame();

  const auto creations_after_first
    = CountSrvViewCreationsForIndex(Gfx(), u_srv_index);

  Loader().EmitTextureEviction(key, EvictionReason::kRefCountZero);
  BeginVisibleFrame();

  // Assert
  EXPECT_EQ(
    CountSrvViewCreationsForIndex(Gfx(), u_srv_index), creations_after_first);
}

NOLINT_TEST_F(
  TextureBinderEvictionTest, ResidentLeasePinsDescriptorUntilAllReadersRetire)
{
  const auto payload = MakeCookedTexture1x1Rgba8Payload();
  const auto key = Loader().PreloadCookedTexture(std::span(payload));
  Uploader().OnFrameStart(oxygen::vortex::internal::RendererTagFactory::Get(),
    oxygen::frame::Slot {
      1,
    });
  const auto srv = TexBinder().GetOrAllocate(key);
  EXPECT_EQ(TexBinder().AcquireReadyTexture(key), nullptr);
  auto queue
    = Gfx().GetFakeCommandQueue(oxygen::graphics::QueueRole::kTransfer);
  ASSERT_NE(queue, nullptr);
  queue->CompleteThrough(queue->GetCurrentValue());
  BeginVisibleFrame();
  Uploader().OnFrameStart(oxygen::vortex::internal::RendererTagFactory::Get(),
    oxygen::frame::Slot {
      2,
    });
  BeginVisibleFrame();
  auto accepted = TexBinder().AcquireReadyTexture(key);
  ASSERT_NE(accepted, nullptr);
  EXPECT_EQ(accepted->srv, srv);
  EXPECT_EQ(accepted->texture->GetDescriptor().initial_state,
    oxygen::graphics::ResourceStates::kCommon);
  EXPECT_FALSE(TexBinder().HasResourceFailed(key));
  auto in_flight = accepted;
  const auto* texture = accepted->texture.get();
  Loader().EmitTextureEviction(key, EvictionReason::kRefCountZero);
  BeginVisibleFrame();
  EXPECT_EQ(LastSrvViewTextureForIndex(Gfx(), srv.get()), texture);
  accepted.reset();
  BeginVisibleFrame();
  EXPECT_TRUE(TexBinder().IsResourceReady(key));
  EXPECT_EQ(LastSrvViewTextureForIndex(Gfx(), srv.get()), texture);
  in_flight.reset();
  BeginVisibleFrame();
  EXPECT_FALSE(TexBinder().IsResourceReady(key));
  EXPECT_EQ(TexBinder().AcquireReadyTexture(key), nullptr);
  EXPECT_EQ(GetTextureDebugName(LastSrvViewTextureForIndex(Gfx(), srv.get())),
    "FallbackTexture");
}

class TextureBinderEvictionBudgetTest : public TextureBinderTest {
protected:
  auto BinderLimits() const
    -> oxygen::vortex::resources::TextureBinder::UploadLimits override
  {
    auto limits = oxygen::vortex::resources::TextureBinder::UploadLimits {};
    limits.max_eviction_visits_per_frame = 1U;
    return limits;
  }
};

NOLINT_TEST_F(TextureBinderEvictionBudgetTest,
  DelayedDuplicateEvictionCannotInvalidateReload)
{
  const auto payload = MakeCookedTexture1x1Rgba8Payload();
  const auto key = Loader().PreloadCookedTexture(payload);
  const auto index = TexBinder().GetOrAllocate(key);
  BeginVisibleFrame();
  Uploader().OnFrameStart(oxygen::vortex::internal::RendererTagFactory::Get(),
    oxygen::frame::Slot { 1 });
  BeginVisibleFrame();
  ASSERT_TRUE(TexBinder().IsResourceReady(key));
  Loader().EmitTextureEviction(key, EvictionReason::kRefCountZero);
  Loader().EmitTextureEviction(key, EvictionReason::kRefCountZero);
  BeginVisibleFrame();
  EXPECT_FALSE(TexBinder().IsResourceReady(key));
  EXPECT_EQ(TexBinder().GetOrAllocate(key), index);
  BeginVisibleFrame();
  Uploader().OnFrameStart(oxygen::vortex::internal::RendererTagFactory::Get(),
    oxygen::frame::Slot { 0 });
  BeginVisibleFrame();
  EXPECT_TRUE(TexBinder().IsResourceReady(key));
  EXPECT_EQ(TexBinder().GetOrAllocate(key), index);
}

NOLINT_TEST_F(
  TextureBinderEvictionBudgetTest, PinnedVisitsYieldToOtherEvictions)
{
  const auto payload = MakeCookedTexture1x1Rgba8Payload();
  const auto pinned = Loader().MintSyntheticTextureKey();
  const auto other = Loader().MintSyntheticTextureKey();
  Loader().SetCookedTexturePayload(pinned, payload);
  Loader().SetCookedTexturePayload(other, payload);
  (void)TexBinder().GetOrAllocate(pinned);
  (void)TexBinder().GetOrAllocate(other);
  BeginVisibleFrame();
  Uploader().OnFrameStart(oxygen::vortex::internal::RendererTagFactory::Get(),
    oxygen::frame::Slot { 1 });
  BeginVisibleFrame();
  auto lease = TexBinder().AcquireReadyTexture(pinned);
  ASSERT_NE(lease, nullptr);
  Loader().EmitTextureEviction(pinned, EvictionReason::kRefCountZero);
  Loader().EmitTextureEviction(other, EvictionReason::kRefCountZero);
  const auto events = Gfx().srv_view_log_.events.size();
  BeginVisibleFrame();
  EXPECT_EQ(Gfx().srv_view_log_.events.size(), events);
  EXPECT_TRUE(TexBinder().IsResourceReady(pinned));
  BeginVisibleFrame();
  EXPECT_EQ(Gfx().srv_view_log_.events.size(), events + 1U);
  EXPECT_TRUE(TexBinder().IsResourceReady(pinned));
  lease.reset();
  BeginVisibleFrame();
  EXPECT_EQ(Gfx().srv_view_log_.events.size(), events + 2U);
  EXPECT_FALSE(TexBinder().IsResourceReady(pinned));
}

#if defined(_MSC_VER) && defined(_DEBUG)
NOLINT_TEST_F(
  TextureBinderEvictionBudgetTest, AllocationFailurePreservesEvictionForRetry)
{
  const auto payload = MakeCookedTexture1x1Rgba8Payload();
  const auto key = Loader().PreloadCookedTexture(payload);
  const auto index = TexBinder().GetOrAllocate(key);
  BeginVisibleFrame();
  Uploader().OnFrameStart(oxygen::vortex::internal::RendererTagFactory::Get(),
    oxygen::frame::Slot { 1 });
  BeginVisibleFrame();
  ASSERT_TRUE(TexBinder().IsResourceReady(key));
  const auto before = Gfx().GetResourceRegistry().GetRegisteredResourceCount();
  Loader().EmitTextureEviction(key, EvictionReason::kRefCountZero);
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
  EXPECT_FALSE(TexBinder().IsResourceReady(key));
  BeginVisibleFrame();
  EXPECT_EQ(
    Gfx().GetResourceRegistry().GetRegisteredResourceCount(), before - 1U);
  EXPECT_EQ(TexBinder().GetOrAllocate(key), index);
}
#endif

} // namespace
