//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/Types/Geometry.h>
#include <Oxygen/Core/Types/Frame.h>
#include <Oxygen/Graphics/Headless/Surface.h>
#include <Oxygen/Graphics/Headless/Texture.h>

namespace oxygen::graphics::headless {

HeadlessSurface::HeadlessSurface(std::string_view name)
  : Surface(name)
{
  // initially backbuffers are empty; entries will be created when a renderer
  // attaches
  for (auto& b : backbuffers_) {
    b.reset();
  }
}

auto HeadlessSurface::AttachRenderer(
  std::shared_ptr<RenderController> /*renderer*/) -> void
{
  LOG_F(INFO, "HeadlessSurface attached to renderer");
  // Create a set of backbuffers using the engine constant for frames-in-flight
  // (don't query the renderer here; rendering architecture will be overhauled).
  constexpr uint32_t frames_in_flight = frame::kFramesInFlight.get();
  for (uint32_t i = 0; i < frames_in_flight; ++i) {
    TextureDesc desc;
    desc.width = width_;
    desc.height = height_;
    desc.format = Format::kRGBA8UNorm;
    backbuffers_[i] = std::make_shared<Texture>(desc);
  }
}

auto HeadlessSurface::DetachRenderer() -> void
{
  LOG_F(INFO, "HeadlessSurface detached from renderer");
  for (auto& b : backbuffers_) {
    b.reset();
  }
}

auto HeadlessSurface::Resize() -> void
{
  // Recreate backing textures if already allocated.
  constexpr uint32_t frames = frame::kFramesInFlight.get();
  for (uint32_t i = 0; i < frames; ++i) {
    auto& tb = backbuffers_[i];
    if (tb) {
      TextureDesc desc = tb->GetDescriptor();
      desc.width = width_;
      desc.height = height_;
      tb = std::make_shared<Texture>(desc);
    }
  }
  // Clear the resize hint after handling it.
  ShouldResize(false);
}

auto HeadlessSurface::SetSize(PixelExtent size) -> void
{
  width_ = static_cast<uint32_t>(size.width);
  height_ = static_cast<uint32_t>(size.height);
  ShouldResize(true);
}

auto HeadlessSurface::GetCurrentBackBufferIndex() const -> uint32_t
{
  return current_index_ % frame::kFramesInFlight.get();
}

auto HeadlessSurface::GetCurrentBackBuffer() const
  -> std::shared_ptr<graphics::Texture>
{
  const uint32_t idx = GetCurrentBackBufferIndex();
  return backbuffers_[idx];
}

auto HeadlessSurface::GetBackBuffer(uint32_t index) const
  -> std::shared_ptr<graphics::Texture>
{
  // The 'index' parameter is a frames-in-flight slot index. Assert it is
  // within range rather than silently wrapping.
  constexpr uint32_t frames = frame::kFramesInFlight.get();
  CHECK_F(index < frames,
    "HeadlessSurface::GetBackBuffer: invalid slot index {} "
    "(frames_in_flight={})",
    index, frames);
  return backbuffers_[index];
}

auto HeadlessSurface::Present() const -> void
{
  // Advance the current backbuffer index to emulate a swapchain present.
  // Note: Present is logically const for the Surface API; mutate via const_cast
  // to keep the interface but update internal state.
  auto self = const_cast<HeadlessSurface*>(this);
  constexpr uint32_t frames = frame::kFramesInFlight.get();
  self->current_index_ = (self->current_index_ + 1) % frames;
}

[[nodiscard]] auto HeadlessSurface::Width() const -> uint32_t { return width_; }

[[nodiscard]] auto HeadlessSurface::Height() const -> uint32_t
{
  return height_;
}

} // namespace oxygen::graphics::headless
