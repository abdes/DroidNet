//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <memory>
#include <span>
#include <vector>

#include <Oxygen/Core/Types/Frame.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/OxCo/Value.h>
#include <Oxygen/Renderer/Upload/StagingProvider.h>
#include <Oxygen/Renderer/Upload/Types.h>
#include <Oxygen/Renderer/Upload/UploadDiagnostics.h>
#include <Oxygen/Renderer/Upload/UploadPolicy.h>
#include <Oxygen/Renderer/Upload/UploadTracker.h>
#include <Oxygen/Renderer/api_export.h>

namespace oxygen {
class Graphics;
} // namespace oxygen

namespace oxygen::engine::upload {

class UploadCoordinator {
public:
  /*!
   @note UploadCoordinator lifetime is entirely linked to the Renderer. We
         completely rely on the Renderer to handle the lifetime of the Graphics
         backend, and we assume that for as long as we are alive, the Graphics
         backend is stable. When it is no longer stable, the Renderer is
         responsible for destroying and re-creating the UploadCoordinator.
  */
  OXGN_RNDR_API explicit UploadCoordinator(
    observer_ptr<Graphics> gfx, UploadPolicy policy = DefaultUploadPolicy());

  OXYGEN_MAKE_NON_COPYABLE(UploadCoordinator)
  OXYGEN_DEFAULT_MOVABLE(UploadCoordinator)

  ~UploadCoordinator() = default;

  // Factory methods for staging providers
  // Returns a shared_ptr to the created provider and tracks it for lifecycle
  // management
  auto CreateSingleBufferStaging(
    StagingProvider::MapPolicy policy = StagingProvider::MapPolicy::kPinned,
    float slack = 0.5f) -> std::shared_ptr<StagingProvider>;

  auto CreateRingBufferStaging(
    frame::SlotCount partitions, std::uint32_t alignment, float slack = 0.5f)
    -> std::shared_ptr<StagingProvider>;

  // Provider-aware submissions
  OXGN_RNDR_API auto Submit(const UploadRequest& req, StagingProvider& provider)
    -> UploadTicket;
  OXGN_RNDR_API auto SubmitMany(std::span<const UploadRequest> reqs,
    StagingProvider& provider) -> std::vector<UploadTicket>;

  auto IsComplete(UploadTicket t) const -> bool
  {
    return tracker_.IsComplete(t.id);
  }
  auto TryGetResult(UploadTicket t) const -> std::optional<UploadResult>
  {
    return tracker_.TryGetResult(t.id);
  }
  auto Await(UploadTicket t) -> UploadResult { return tracker_.Await(t.id); }
  auto AwaitAll(std::span<const UploadTicket> tickets)
    -> std::vector<UploadResult>
  {
    return tracker_.AwaitAll(tickets);
  }

  OXGN_RNDR_API auto Flush() -> void;
  OXGN_RNDR_API auto RetireCompleted() -> void;

  /*!
   * All staging providers must be created via UploadCoordinator factory
   * methods. This ensures correct lifecycle management, frame notifications,
   * and retirement. Do not construct providers directly; always use
   * CreateSingleBufferStaging or CreateRingBufferStaging.
   */
  // Notify all tracked providers of frame slot change
  auto OnFrameStart(frame::Slot slot) -> void;

  // Diagnostics and control
  OXGN_RNDR_API auto GetStats() const -> UploadStats
  {
    return tracker_.GetStats();
  }
  // Best-effort cancellation; may not prevent GPU copy if already submitted.
  OXGN_RNDR_API auto Cancel(UploadTicket t) -> bool
  {
    return tracker_.Cancel(t.id);
  }

  // OxCo helpers
  auto SubmitAsync(const UploadRequest& req, StagingProvider& provider)
    -> co::Co<UploadResult>
  {
    auto t = Submit(req, provider);
    co_await AwaitAsync(t);
    co_return *TryGetResult(t);
  }

  auto SubmitManyAsync(std::span<const UploadRequest> reqs,
    StagingProvider& provider) -> co::Co<std::vector<UploadResult>>
  {
    auto tickets = SubmitMany(reqs, provider);
    co_await AwaitAllAsync(tickets);
    std::vector<UploadResult> out;
    out.reserve(tickets.size());
    for (auto t : tickets) {
      out.emplace_back(*TryGetResult(t));
    }
    co_return out;
  }

  // Legacy async overloads (default SingleBufferStaging)
  auto SubmitAsync(const UploadRequest& req) -> co::Co<UploadResult>;
  auto SubmitManyAsync(std::span<const UploadRequest> reqs)
    -> co::Co<std::vector<UploadResult>>;

  auto AwaitAsync(UploadTicket t) -> co::Co<void>
  {
    co_await Until(tracker_.CompletedFenceValue() >= t.fence);
    co_return; // result can be queried if needed
  }

  auto AwaitAllAsync(std::span<const UploadTicket> tickets) -> co::Co<void>
  {
    if (tickets.empty()) {
      co_return;
    }
    FenceValue max_fence { 0 };
    for (const auto& t : tickets) {
      if (max_fence < t.fence) {
        max_fence = t.fence;
      }
    }
    co_await Until(tracker_.CompletedFenceValue() >= max_fence);
    co_return;
  }

private:
  observer_ptr<Graphics> gfx_;
  UploadPolicy policy_;
  UploadTracker tracker_;

  std::vector<std::weak_ptr<StagingProvider>> providers_;
};

} // namespace oxygen::engine::upload
