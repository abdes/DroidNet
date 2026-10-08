//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma unmanaged

#include "pch.h"

#include "EditorModule/SurfaceFramebuffers.h"

namespace oxygen::interop::module {

  SurfaceFramebuffers::SurfaceFramebuffers(std::weak_ptr<Graphics> graphics)
    : graphics_(std::move(graphics)) {
  }

  SurfaceFramebuffers::~SurfaceFramebuffers() = default;

  auto SurfaceFramebuffers::GetCurrent(const SurfaceRegistry::GuidKey& key,
    const graphics::Surface& surface) -> std::shared_ptr<graphics::Framebuffer> {
    const auto index = surface.GetCurrentBackBufferIndex();
    auto& cached = framebuffers_[key];
    if (cached.empty()) {
      const auto gfx = graphics_.lock();
      if (!gfx) {
        return {};
      }

      // Composition writes colour only: the framebuffer wraps the backbuffer
      // without a depth attachment.
      const auto count = static_cast<std::size_t>(frame::kFramesInFlight.get());
      cached.resize(count);
      for (std::size_t i = 0; i < count; ++i) {
        if (auto backbuffer = surface.GetBackBuffer(static_cast<uint32_t>(i))) {
          cached[i] = gfx->CreateFramebuffer(
            graphics::FramebufferDesc {}.AddColorAttachment(std::move(backbuffer)));
        }
      }
    }

    if (index >= cached.size()) {
      LOG_F(WARNING,
        "SurfaceFramebuffers: backbuffer index {} is outside the cache of {} "
        "for surface '{}'",
        index, cached.size(), surface.GetName());
      return {};
    }

    return cached[index];
  }

  void SurfaceFramebuffers::ReleaseForResize(
    const SurfaceRegistry::GuidKey& key) {
    framebuffers_.erase(key);
  }

  void SurfaceFramebuffers::ReleaseDeferred(const SurfaceRegistry::GuidKey& key) {
    const auto iter = framebuffers_.find(key);
    if (iter == framebuffers_.end()) {
      return;
    }

    // Frames in flight may still target these backbuffers.
    if (const auto gfx = graphics_.lock()) {
      auto& reclaimer = gfx->GetDeferredReclaimer();
      for (auto& framebuffer : iter->second) {
        if (framebuffer) {
          graphics::DeferredObjectRelease(framebuffer, reclaimer);
        }
      }
    }
    framebuffers_.erase(iter);
  }

} // namespace oxygen::interop::module
