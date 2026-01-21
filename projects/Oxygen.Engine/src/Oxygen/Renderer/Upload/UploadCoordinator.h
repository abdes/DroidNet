//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <expected>
#include <memory>
#include <span>
#include <string_view>
#include <vector>

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Core/Types/Frame.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/OxCo/Value.h>
#include <Oxygen/Renderer/RendererTag.h>
#include <Oxygen/Renderer/Upload/StagingProvider.h>
#include <Oxygen/Renderer/Upload/Types.h>
#include <Oxygen/Renderer/Upload/UploadPolicy.h>
#include <Oxygen/Renderer/Upload/UploadTracker.h>
#include <Oxygen/Renderer/api_export.h>

namespace oxygen {
using std::atomic_bool;
class Graphics;
} // namespace oxygen

namespace oxygen::engine::upload {

// Default growth factor for ring staging buffers. This is the single source of
// truth for RingBufferStaging slack unless a call site explicitly overrides it.
inline constexpr float kDefaultRingBufferStagingSlack = 0.25f;

// Forward declaration to avoid including UploadPlanner.h in the header
struct BufferUploadPlan;

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

  OXGN_RNDR_API auto CreateRingBufferStaging(frame::SlotCount partitions,
    std::uint32_t alignment, float slack = kDefaultRingBufferStagingSlack,
    std::string_view debug_name = "UploadCoordinator.RingBufferStaging")
    -> std::shared_ptr<StagingProvider>;

  // Provider-aware submissions
  //! Submits a single upload request. No cross-request coalescing.
  OXGN_RNDR_API auto Submit(const UploadRequest& req, StagingProvider& provider)
    -> std::expected<UploadTicket, UploadError>;

  //! Submits multiple requests. Consecutive buffer requests are coalesced
  //! and optimized by UploadPlanner before recording.
  OXGN_RNDR_API auto SubmitMany(
    std::span<const UploadRequest> reqs, StagingProvider& provider)
    -> std::expected<std::vector<UploadTicket>, UploadError>;

  // Shutdown helpers -----------------------------------------------------//
  // Prevents any new submissions and waits for outstanding upload work to
  // complete. Call during Renderer/Engine shutdown to ensure the transfer
  // queue has finished referencing upload resources before they are
  // destroyed.
  OXGN_RNDR_API auto Shutdown(std::chrono::milliseconds timeout
    = std::chrono::milliseconds { 3000 }) -> std::expected<void, UploadError>;

  auto IsComplete(UploadTicket t) const -> std::expected<bool, UploadError>
  {
    return tracker_.IsComplete(t.id);
  }

  auto TryGetResult(UploadTicket t) const -> std::optional<UploadResult>
  {
    return tracker_.TryGetResult(t.id);
  }

  auto Await(UploadTicket t) -> std::expected<UploadResult, UploadError>
  {
    return tracker_.Await(t.id);
  }

  auto AwaitAll(std::span<const UploadTicket> tickets)
    -> std::expected<std::vector<UploadResult>, UploadError>
  {
    return tracker_.AwaitAll(tickets);
  }

  /*!
   * All staging providers must be created via UploadCoordinator factory
   * methods. This ensures correct lifecycle management, frame notifications,
   * and retirement. Do not construct providers directly; always use
   * CreateSingleBufferStaging or CreateRingBufferStaging.
   */
  OXGN_RNDR_API auto OnFrameStart(renderer::RendererTag, frame::Slot slot)
    -> void;

  // Best-effort cancellation; may not prevent GPU copy if already submitted.
  OXGN_RNDR_API auto Cancel(UploadTicket t) -> std::expected<bool, UploadError>
  {
    return tracker_.Cancel(t.id);
  }

  // OxCo helpers
  OXGN_RNDR_NDAPI auto SubmitAsync(const UploadRequest& req,
    StagingProvider& provider) -> co::Co<UploadResult>;

  OXGN_RNDR_NDAPI auto SubmitManyAsync(std::span<const UploadRequest> reqs,
    StagingProvider& provider) -> co::Co<std::vector<UploadResult>>;

  OXGN_RNDR_NDAPI auto AwaitAsync(UploadTicket t) -> co::Co<void>;

  OXGN_RNDR_NDAPI auto AwaitAllAsync(std::span<const UploadTicket> tickets)
    -> co::Co<void>;

private:
  OXGN_RNDR_API auto RetireCompleted() -> void;

  observer_ptr<Graphics> gfx_;
  UploadPolicy policy_;
  UploadTracker tracker_;

  std::vector<std::weak_ptr<StagingProvider>> providers_;

  //! Stage 1: Plan a coalescible run of buffer requests.
  auto PlanBufferRun(std::span<const UploadRequest> run)
    -> std::expected<BufferUploadPlan, UploadError>;

  //! Stage 2: Allocate and fill staging according to the plan and policy.
  auto FillStagingForPlan(const BufferUploadPlan& plan,
    std::span<const UploadRequest> run, StagingProvider::Allocation& allocation)
    -> void;

  //! Stage 3: Optimize the buffer plan by coalescing contiguous regions.
  auto OptimizeBufferRun(
    std::span<const UploadRequest> run, const BufferUploadPlan& plan)
    -> std::expected<BufferUploadPlan, UploadError>;

  //! Stage 4: Record copies and transitions; returns signaled fence value.
  auto RecordBufferRun(const BufferUploadPlan& optimized,
    std::span<const UploadRequest> run, StagingProvider::Allocation& staging)
    -> std::expected<graphics::FenceValue, UploadError>;

  //! Issue per-request tickets based on the original (pre-optimized) plan.
  auto MakeTicketsForPlan(const BufferUploadPlan& original_plan,
    std::span<const UploadRequest> run, graphics::FenceValue fence)
    -> std::expected<std::vector<UploadTicket>, UploadError>;

  //! Helper: Execute the buffer-run pipeline end-to-end.
  //! Plan → FillStaging → Optimize → Record → Tickets.
  auto SubmitRun(std::span<const UploadRequest> run, StagingProvider& provider)
    -> std::expected<std::vector<UploadTicket>, UploadError>;

  std::atomic_bool shutting_down_ { false };
};

} // namespace oxygen::engine::upload
