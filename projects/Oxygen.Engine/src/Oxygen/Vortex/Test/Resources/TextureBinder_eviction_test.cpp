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
#include <Oxygen/Core/Bindless/Types.h>
#include <Oxygen/Core/Types/TextureType.h>
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
using oxygen::vortex::testing::MakeTightPackedTextureCube1x1Rgba8Payload;
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

class TextureBinderEvictionTest : public TextureBinderTest {
protected:
  auto StartUploaderFrame(const std::uint32_t slot) const -> void
  {
    Uploader().OnFrameStart(oxygen::vortex::internal::RendererTagFactory::Get(),
      oxygen::frame::Slot { slot });
  }

  //! Requests `key`, completes its upload and publishes it.
  auto LoadResident(const ResourceKey key) -> oxygen::ShaderVisibleIndex
  {
    StartUploaderFrame(1);
    (void)TexBinder().GetOrAllocate(key);
    BeginVisibleFrame();
    StartUploaderFrame(2);
    BeginVisibleFrame();
    EXPECT_TRUE(TexBinder().IsResourceReady(key));
    return TexBinder().GetOrAllocate(key);
  }

  [[nodiscard]] auto Placeholder() const -> oxygen::ShaderVisibleIndex
  {
    return TexBinder().GetOrAllocate(ResourceKey::kPlaceholder);
  }
};

//! Eviction publishes the shared placeholder; the evicted version keeps its
//! descriptor, unchanged, until the frames that may read it complete.
NOLINT_TEST_F(TextureBinderEvictionTest, EvictionPublishesPlaceholder)
{
  // Arrange
  const auto payload = MakeCookedTexture1x1Rgba8Payload();
  const ResourceKey key
    = Loader().PreloadCookedTexture(std::span(payload.data(), payload.size()));
  const auto loaded = LoadResident(key);
  ASSERT_NE(loaded, Placeholder());
  const auto descriptors = AllocatedSrvCount();
  const auto revision = TexBinder().GetResidentContentRevision();

  // Act
  Loader().EmitTextureEviction(key, EvictionReason::kRefCountZero);
  BeginVisibleFrame();

  // Assert: the placeholder is published; the old descriptor is untouched.
  EXPECT_FALSE(TexBinder().IsResourceReady(key));
  EXPECT_GT(TexBinder().GetResidentContentRevision(), revision);
  EXPECT_EQ(CountSrvViewCreationsForIndex(Gfx(), loaded.get()), 1U);
  EXPECT_EQ(
    GetTextureDebugName(LastSrvViewTextureForIndex(Gfx(), loaded.get())),
    MakeTextureDebugName(key));
  EXPECT_EQ(AllocatedSrvCount(), descriptors);

  // Assert: the descriptor returns to the allocator once frames complete.
  CompleteRetiredFrames();
  EXPECT_EQ(AllocatedSrvCount(), descriptors - 1U);
  EXPECT_EQ(TexBinder().GetOrAllocate(key), Placeholder());
}

//! An evicted cube publishes the shared cube placeholder, so cube readers
//! never sample a 2D view.
NOLINT_TEST_F(TextureBinderEvictionTest, CubeEvictionPublishesCubePlaceholder)
{
  // Arrange
  const auto payload = MakeTightPackedTextureCube1x1Rgba8Payload();
  const ResourceKey key
    = Loader().PreloadCookedTexture(std::span(payload.data(), payload.size()));
  const auto loaded = LoadResident(key);
  ASSERT_NE(loaded, Placeholder());

  // Act
  Loader().EmitTextureEviction(key, EvictionReason::kRefCountZero);
  BeginVisibleFrame();

  // Assert
  const auto evicted = TexBinder().GetOrAllocate(key);
  EXPECT_NE(evicted, loaded);
  EXPECT_NE(evicted, Placeholder());
  const auto* const texture = LastSrvViewTextureForIndex(Gfx(), evicted.get());
  ASSERT_NE(texture, nullptr);
  EXPECT_EQ(
    texture->GetDescriptor().texture_type, oxygen::TextureType::kTextureCube);
  EXPECT_EQ(texture->GetDescriptor().array_size, 6U);
  EXPECT_EQ(GetTextureDebugName(texture), "TextureBinder.PlaceholderCube");
}

//! Eviction discards late upload completions for in-flight uploads.
NOLINT_TEST_F(TextureBinderEvictionTest, InFlightCompletionIsDiscarded)
{
  // Arrange
  const auto payload = MakeCookedTexture1x1Rgba8Payload();
  const ResourceKey key
    = Loader().PreloadCookedTexture(std::span(payload.data(), payload.size()));
  StartUploaderFrame(1);
  (void)TexBinder().GetOrAllocate(key);
  auto q = Gfx().GetFakeCommandQueue(oxygen::graphics::QueueRole::kTransfer);
  ASSERT_NE(q, nullptr);
  q->SetAutoComplete(false);
  q->CompleteThrough(0);
  BeginVisibleFrame();
  const auto descriptors = AllocatedSrvCount();

  // Act
  Loader().EmitTextureEviction(key, EvictionReason::kRefCountZero);
  BeginVisibleFrame();
  q->CompleteThrough(q->GetCurrentValue());
  StartUploaderFrame(2);
  BeginVisibleFrame();

  // Assert: nothing published for the discarded upload.
  EXPECT_FALSE(TexBinder().IsResourceReady(key));
  EXPECT_EQ(AllocatedSrvCount(), descriptors);
  EXPECT_TRUE(std::ranges::none_of(
    Gfx().srv_view_log_.events, [&](const auto& event) -> bool {
      return GetTextureDebugName(event.texture) == MakeTextureDebugName(key);
    }));
}

//! An evicted entry reloads into a new version with a new descriptor.
NOLINT_TEST_F(TextureBinderEvictionTest, EvictionThenReloadPublishesNewVersion)
{
  // Arrange
  const auto payload = MakeCookedTexture1x1Rgba8Payload();
  const ResourceKey key
    = Loader().PreloadCookedTexture(std::span(payload.data(), payload.size()));
  const auto first = LoadResident(key);
  const auto* const first_texture
    = LastSrvViewTextureForIndex(Gfx(), first.get());
  Loader().EmitTextureEviction(key, EvictionReason::kRefCountZero);
  BeginVisibleFrame();
  ASSERT_EQ(TexBinder().GetOrAllocate(key), Placeholder());

  // Act: the request above started the reload; complete it.
  BeginVisibleFrame();
  StartUploaderFrame(3);
  BeginVisibleFrame();

  // Assert
  ASSERT_TRUE(TexBinder().IsResourceReady(key));
  const auto second = TexBinder().GetOrAllocate(key);
  EXPECT_NE(second, Placeholder());
  const auto* const second_texture
    = LastSrvViewTextureForIndex(Gfx(), second.get());
  EXPECT_EQ(GetTextureDebugName(second_texture), MakeTextureDebugName(key));
  EXPECT_NE(second_texture, first_texture);
}

//! A repeated eviction of an evicted entry changes nothing.
NOLINT_TEST_F(TextureBinderEvictionTest, EvictionIsIdempotent)
{
  // Arrange
  const auto payload = MakeCookedTexture1x1Rgba8Payload();
  const ResourceKey key
    = Loader().PreloadCookedTexture(std::span(payload.data(), payload.size()));
  (void)LoadResident(key);
  Loader().EmitTextureEviction(key, EvictionReason::kRefCountZero);
  BeginVisibleFrame();
  const auto views = Gfx().srv_view_log_.events.size();
  const auto revision = TexBinder().GetResidentContentRevision();

  // Act
  Loader().EmitTextureEviction(key, EvictionReason::kRefCountZero);
  BeginVisibleFrame();

  // Assert
  EXPECT_EQ(Gfx().srv_view_log_.events.size(), views);
  EXPECT_EQ(TexBinder().GetResidentContentRevision(), revision);
}

//! A resident lease keeps the version published until every reader releases
//! it.
NOLINT_TEST_F(
  TextureBinderEvictionTest, ResidentLeasePinsVersionUntilAllReadersRetire)
{
  const auto payload = MakeCookedTexture1x1Rgba8Payload();
  const auto key = Loader().PreloadCookedTexture(std::span(payload));
  StartUploaderFrame(1);
  (void)TexBinder().GetOrAllocate(key);
  EXPECT_EQ(TexBinder().AcquireReadyTexture(key), nullptr);
  BeginVisibleFrame();
  StartUploaderFrame(2);
  BeginVisibleFrame();
  const auto srv = TexBinder().GetOrAllocate(key);
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
  EXPECT_EQ(TexBinder().GetOrAllocate(key), srv);
  accepted.reset();
  BeginVisibleFrame();
  EXPECT_TRUE(TexBinder().IsResourceReady(key));
  EXPECT_EQ(TexBinder().GetOrAllocate(key), srv);
  in_flight.reset();
  BeginVisibleFrame();
  EXPECT_FALSE(TexBinder().IsResourceReady(key));
  EXPECT_EQ(TexBinder().AcquireReadyTexture(key), nullptr);
  EXPECT_EQ(LastSrvViewTextureForIndex(Gfx(), srv.get()), texture);
}

class TextureBinderEvictionBudgetTest : public TextureBinderEvictionTest {
protected:
  [[nodiscard]] auto BinderLimits() const
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
  (void)LoadResident(key);
  Loader().EmitTextureEviction(key, EvictionReason::kRefCountZero);
  Loader().EmitTextureEviction(key, EvictionReason::kRefCountZero);
  BeginVisibleFrame();
  EXPECT_FALSE(TexBinder().IsResourceReady(key));
  EXPECT_EQ(TexBinder().GetOrAllocate(key), Placeholder());
  BeginVisibleFrame();
  StartUploaderFrame(0);
  BeginVisibleFrame();
  EXPECT_TRUE(TexBinder().IsResourceReady(key));
  EXPECT_NE(TexBinder().GetOrAllocate(key), Placeholder());
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
  (void)LoadResident(other);
  const auto other_loaded = TexBinder().GetOrAllocate(other);
  auto lease = TexBinder().AcquireReadyTexture(pinned);
  ASSERT_NE(lease, nullptr);
  Loader().EmitTextureEviction(pinned, EvictionReason::kRefCountZero);
  Loader().EmitTextureEviction(other, EvictionReason::kRefCountZero);
  BeginVisibleFrame();
  EXPECT_TRUE(TexBinder().IsResourceReady(pinned));
  EXPECT_EQ(TexBinder().GetOrAllocate(other), other_loaded);
  BeginVisibleFrame();
  EXPECT_TRUE(TexBinder().IsResourceReady(pinned));
  EXPECT_EQ(TexBinder().GetOrAllocate(other), Placeholder());
  lease.reset();
  BeginVisibleFrame();
  EXPECT_FALSE(TexBinder().IsResourceReady(pinned));
}

#if defined(_MSC_VER) && defined(_DEBUG)
NOLINT_TEST_F(
  TextureBinderEvictionBudgetTest, AllocationFailurePreservesEvictionForRetry)
{
  const auto payload = MakeCookedTexture1x1Rgba8Payload();
  const auto key = Loader().PreloadCookedTexture(payload);
  (void)LoadResident(key);
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
  CompleteRetiredFrames();
  EXPECT_EQ(
    Gfx().GetResourceRegistry().GetRegisteredResourceCount(), before - 1U);
  EXPECT_EQ(TexBinder().GetOrAllocate(key), Placeholder());
}
#endif

} // namespace
