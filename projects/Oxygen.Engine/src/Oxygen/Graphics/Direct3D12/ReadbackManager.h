//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <chrono>
#include <expected>
#include <functional>
#include <memory>
#include <mutex>
#include <unordered_map>

#include <Oxygen/Graphics/Common/ReadbackManager.h>
#include <Oxygen/Graphics/Common/ReadbackTracker.h>
#include <Oxygen/Graphics/Direct3D12/api_export.h>

namespace oxygen::graphics::d3d12 {

class Graphics;
class D3D12BufferReadback;
class D3D12TextureReadback;

class D3D12ReadbackManager final : public graphics::ReadbackManager {
public:
  OXGN_D3D12_API explicit D3D12ReadbackManager(Graphics& graphics);
  OXGN_D3D12_API ~D3D12ReadbackManager() override;

  OXGN_D3D12_NDAPI auto CreateBufferReadback(std::string_view debug_name)
    -> std::shared_ptr<graphics::GpuBufferReadback> override;
  OXGN_D3D12_NDAPI auto CreateTextureReadback(std::string_view debug_name)
    -> std::shared_ptr<graphics::GpuTextureReadback> override;

  OXGN_D3D12_API auto Await(graphics::ReadbackTicket ticket)
    -> std::expected<graphics::ReadbackResult,
      graphics::ReadbackError> override;
  OXGN_D3D12_API auto AwaitAsync(graphics::ReadbackTicket ticket)
    -> co::Co<void> override;
  OXGN_D3D12_API auto Cancel(graphics::ReadbackTicket ticket)
    -> std::expected<bool, graphics::ReadbackError> override;

  OXGN_D3D12_API auto ReadBufferNow(
    const graphics::Buffer& source, graphics::BufferRange range = {})
    -> std::expected<std::vector<std::byte>, graphics::ReadbackError> override;
  OXGN_D3D12_API auto ReadTextureNow(const graphics::Texture& source,
    graphics::TextureReadbackRequest request = {}, bool tightly_pack = true)
    -> std::expected<graphics::OwnedTextureReadbackData,
      graphics::ReadbackError> override;

  OXGN_D3D12_API auto CreateReadbackTextureSurface(
    const graphics::TextureDesc& desc)
    -> std::expected<std::shared_ptr<graphics::Texture>,
      graphics::ReadbackError> override;
  OXGN_D3D12_API auto MapReadbackTextureSurface(
    graphics::Texture& surface, graphics::TextureSlice slice)
    -> std::expected<graphics::ReadbackSurfaceMapping,
      graphics::ReadbackError> override;
  OXGN_D3D12_API auto UnmapReadbackTextureSurface(graphics::Texture& surface)
    -> void override;

  OXGN_D3D12_API auto OnFrameStart(frame::Slot slot) -> void override;
  OXGN_D3D12_API auto Shutdown(std::chrono::milliseconds timeout)
    -> std::expected<void, graphics::ReadbackError> override;

private:
  friend class D3D12BufferReadback;
  friend class D3D12TextureReadback;

  auto EnsureTrackedQueue(observer_ptr<graphics::CommandQueue> queue)
    -> std::expected<void, graphics::ReadbackError>;
  auto AllocateFence(observer_ptr<graphics::CommandQueue> queue)
    -> std::expected<graphics::FenceValue, graphics::ReadbackError>;
  auto PumpCompletions() -> void;
  [[nodiscard]] auto TryGetResult(graphics::ReadbackTicketId id) const
    -> std::optional<graphics::ReadbackResult>;
  auto TrackCancellationHandler(
    graphics::ReadbackTicketId id, std::function<void()> handler) -> void;
  auto UntrackCancellationHandler(graphics::ReadbackTicketId id) -> void;
  auto ForgetTicket(graphics::ReadbackTicketId id) -> void;

  Graphics& graphics_;
  mutable std::mutex mutex_;
  observer_ptr<graphics::CommandQueue> tracked_queue_ {};
  graphics::FenceValue next_fence_ { 0 };
  graphics::ReadbackTracker tracker_ {};
  bool shutdown_ { false };
  std::unordered_map<graphics::ReadbackTicketId, std::function<void()>>
    cancellation_handlers_ {};
};

} // namespace oxygen::graphics::d3d12
