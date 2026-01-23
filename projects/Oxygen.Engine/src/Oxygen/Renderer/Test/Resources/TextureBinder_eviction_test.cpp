//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <cstdint>
#include <limits>
#include <string>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Graphics/Common/Queues.h>
#include <Oxygen/Renderer/RendererTag.h>
#include <Oxygen/Renderer/Test/Resources/TextureBinderTest.h>
#include <Oxygen/Renderer/Test/Resources/TextureBinderTestPayloads.h>

namespace {

using oxygen::content::EvictionReason;
using oxygen::content::ResourceKey;
using oxygen::renderer::testing::FakeGraphics;
using oxygen::renderer::testing::MakeCookedTexture1x1Rgba8Payload;
using oxygen::renderer::testing::TextureBinderTest;

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
  for (auto it = gfx.srv_view_log_.events.rbegin();
    it != gfx.srv_view_log_.events.rend(); ++it) {
    if (it->index == index) {
      return it->texture;
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

  Uploader().OnFrameStart(oxygen::renderer::internal::RendererTagFactory::Get(),
    oxygen::frame::Slot { 1 });

  Gfx().srv_view_log_.events.clear();
  const auto srv_index = TexBinder().GetOrAllocate(key);
  const auto u_srv_index = srv_index.get();

  auto q
    = GfxPtr()->GetCommandQueue(oxygen::graphics::SingleQueueStrategy().KeyFor(
      oxygen::graphics::QueueRole::kTransfer));
  ASSERT_NE(q, nullptr);

  q->QueueSignalCommand((std::numeric_limits<std::uint64_t>::max)());

  TexBinder().OnFrameStart();
  Uploader().OnFrameStart(oxygen::renderer::internal::RendererTagFactory::Get(),
    oxygen::frame::Slot { 2 });
  TexBinder().OnFrameStart();

  const auto* const resident_texture
    = LastSrvViewTextureForIndex(Gfx(), u_srv_index);
  ASSERT_NE(resident_texture, nullptr);
  EXPECT_NE(GetTextureDebugName(resident_texture), "FallbackTexture");

  // Act
  Loader().EmitTextureEviction(key, EvictionReason::kRefCountZero);
  TexBinder().OnFrameStart();

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

  Uploader().OnFrameStart(oxygen::renderer::internal::RendererTagFactory::Get(),
    oxygen::frame::Slot { 1 });

  Gfx().srv_view_log_.events.clear();
  const auto srv_index = TexBinder().GetOrAllocate(key);
  const auto u_srv_index = srv_index.get();

  auto q
    = GfxPtr()->GetCommandQueue(oxygen::graphics::SingleQueueStrategy().KeyFor(
      oxygen::graphics::QueueRole::kTransfer));
  ASSERT_NE(q, nullptr);

  q->QueueSignalCommand(0);
  TexBinder().OnFrameStart();

  const auto creations_after_submit
    = CountSrvViewCreationsForIndex(Gfx(), u_srv_index);

  // Act
  Loader().EmitTextureEviction(key, EvictionReason::kRefCountZero);
  TexBinder().OnFrameStart();

  const auto creations_after_eviction
    = CountSrvViewCreationsForIndex(Gfx(), u_srv_index);
  ASSERT_GT(creations_after_eviction, creations_after_submit);

  q->QueueSignalCommand((std::numeric_limits<std::uint64_t>::max)());
  Uploader().OnFrameStart(oxygen::renderer::internal::RendererTagFactory::Get(),
    oxygen::frame::Slot { 2 });
  TexBinder().OnFrameStart();

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

  Uploader().OnFrameStart(oxygen::renderer::internal::RendererTagFactory::Get(),
    oxygen::frame::Slot { 1 });

  Gfx().srv_view_log_.events.clear();
  const auto srv_index = TexBinder().GetOrAllocate(key);
  const auto u_srv_index = srv_index.get();

  auto q
    = GfxPtr()->GetCommandQueue(oxygen::graphics::SingleQueueStrategy().KeyFor(
      oxygen::graphics::QueueRole::kTransfer));
  ASSERT_NE(q, nullptr);

  q->QueueSignalCommand((std::numeric_limits<std::uint64_t>::max)());
  TexBinder().OnFrameStart();
  Uploader().OnFrameStart(oxygen::renderer::internal::RendererTagFactory::Get(),
    oxygen::frame::Slot { 2 });
  TexBinder().OnFrameStart();

  // Act
  Loader().EmitTextureEviction(key, EvictionReason::kRefCountZero);
  TexBinder().OnFrameStart();

  const auto* const evicted_texture
    = LastSrvViewTextureForIndex(Gfx(), u_srv_index);
  ASSERT_NE(evicted_texture, nullptr);
  EXPECT_EQ(GetTextureDebugName(evicted_texture), "FallbackTexture");

  (void)TexBinder().GetOrAllocate(key);

  q->QueueSignalCommand((std::numeric_limits<std::uint64_t>::max)());
  TexBinder().OnFrameStart();
  Uploader().OnFrameStart(oxygen::renderer::internal::RendererTagFactory::Get(),
    oxygen::frame::Slot { 3 });
  TexBinder().OnFrameStart();

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

  Uploader().OnFrameStart(oxygen::renderer::internal::RendererTagFactory::Get(),
    oxygen::frame::Slot { 1 });

  Gfx().srv_view_log_.events.clear();
  const auto srv_index = TexBinder().GetOrAllocate(key);
  const auto u_srv_index = srv_index.get();

  auto q
    = GfxPtr()->GetCommandQueue(oxygen::graphics::SingleQueueStrategy().KeyFor(
      oxygen::graphics::QueueRole::kTransfer));
  ASSERT_NE(q, nullptr);

  q->QueueSignalCommand((std::numeric_limits<std::uint64_t>::max)());
  TexBinder().OnFrameStart();
  Uploader().OnFrameStart(oxygen::renderer::internal::RendererTagFactory::Get(),
    oxygen::frame::Slot { 2 });
  TexBinder().OnFrameStart();

  // Act
  Loader().EmitTextureEviction(key, EvictionReason::kRefCountZero);
  TexBinder().OnFrameStart();

  const auto creations_after_first
    = CountSrvViewCreationsForIndex(Gfx(), u_srv_index);

  Loader().EmitTextureEviction(key, EvictionReason::kRefCountZero);
  TexBinder().OnFrameStart();

  // Assert
  EXPECT_EQ(
    CountSrvViewCreationsForIndex(Gfx(), u_srv_index), creations_after_first);
}

} // namespace
