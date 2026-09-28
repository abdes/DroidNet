//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <atomic>
#include <chrono>
#include <expected>
#include <memory>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Core/Types/Frame.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/Vortex/RendererTag.h>
#include <Oxygen/Vortex/Upload/ImmutableTextureUpload.h>
#include <Oxygen/Vortex/Upload/StagingProvider.h>
#include <Oxygen/Vortex/Upload/Types.h>
#include <Oxygen/Vortex/Upload/UploadPolicy.h>
#include <Oxygen/Vortex/Upload/UploadTicket.h>
#include <Oxygen/Vortex/Upload/UploadTracker.h>
#include <Oxygen/Vortex/api_export.h>

namespace oxygen {
using std::atomic_bool;
class Graphics;
} // namespace oxygen

namespace oxygen::vortex::upload {

// Default growth factor for ring staging buffers. This is the single source of
// truth for RingBufferStaging slack unless a call site explicitly overrides it.
inline constexpr float kDefaultRingBufferStagingSlack = 0.25F;

// Forward declaration to avoid including UploadPlanner.h in the header
struct BufferUploadPlan;

//! Submission, frame progress and shutdown run on the upload-owner thread.
class UploadCoordinator {
public:
  static constexpr std::chrono::milliseconds kDefaultShutdownTimeout { 3000 };
  /*!
   @note UploadCoordinator lifetime is entirely linked to the Renderer. We
         completely rely on the Renderer to handle the lifetime of the Graphics
         backend, and we assume that for as long as we are alive, the Graphics
         backend is stable. When it is no longer stable, the Renderer is
         responsible for destroying and re-creating the UploadCoordinator.
  */
  OXGN_VRTX_API explicit UploadCoordinator(
    observer_ptr<Graphics> gfx, UploadPolicy policy = DefaultUploadPolicy());

  OXYGEN_MAKE_NON_COPYABLE(UploadCoordinator)
  OXYGEN_MAKE_NON_MOVABLE(UploadCoordinator)

  ~UploadCoordinator() = default;

  //! Pack a newly created managed 2D texture without recording or submitting.
  //! Staging uses the destination's allocation budget and debug label.
  //! Select full subresources; boxed updates are not supported here.
  OXGN_VRTX_NDAPI auto PrepareImmutableTexture2D(
    graphics::ManagedTexture destination, const UploadTextureSourceView& source,
    std::span<const UploadSubresource> subresources = {})
    -> Result<ImmutableTextureUpload, UploadError>;

  OXGN_VRTX_API auto CreateRingBufferStaging(frame::SlotCount partitions,
    std::uint32_t alignment, float slack = kDefaultRingBufferStagingSlack,
    std::string_view debug_name = "UploadCoordinator.RingBufferStaging")
    -> std::shared_ptr<StagingProvider>;

  // Provider-aware submissions
  //! Submits a single upload request. No cross-request coalescing.
  OXGN_VRTX_API auto Submit(const UploadRequest& req, StagingProvider& provider)
    -> std::expected<UploadTicket, UploadError>;

  //! Submits multiple requests. Consecutive buffer requests are coalesced
  //! and optimized by UploadPlanner before recording.
  OXGN_VRTX_API auto SubmitMany(
    std::span<const UploadRequest> reqs, StagingProvider& provider)
    -> std::expected<std::vector<UploadTicket>, UploadError>;

  //! Request that a staging provider release excess capacity immediately.
  OXGN_VRTX_API auto TrimStagingProvider(StagingProvider& provider,
    std::string_view reason = "UploadCoordinator.Trim") -> bool;

  // Shutdown helpers -----------------------------------------------------//
  // Prevents new preparation/submission and waits for ticketed upload work to
  // complete. Call during Renderer/Engine shutdown to ensure the transfer
  // queue has finished referencing upload resources before they are
  // destroyed. Caller-recorded immutable uploads use Graphics completion;
  // their submissions are not tracked by this coordinator.
  OXGN_VRTX_API auto Shutdown(std::chrono::milliseconds timeout
    = kDefaultShutdownTimeout) -> std::expected<void, UploadError>;

  /*!
   * All staging providers must be created via UploadCoordinator factory
   * methods. This ensures correct lifecycle management, frame notifications,
   * and retirement. Do not construct providers directly; always use
   * CreateSingleBufferStaging or CreateRingBufferStaging.
   */
  OXGN_VRTX_API auto OnFrameStart(vortex::RendererTag tag, frame::Slot slot)
    -> void;

  // OxCo helpers
  //! Lazy submission owns its request/provider. Source byte views remain
  //! borrowed until packing; producer captures may own their source storage.
  OXGN_VRTX_NDAPI auto SubmitAsync(UploadRequest req,
    std::shared_ptr<StagingProvider> provider) -> co::Co<UploadResult>;

  OXGN_VRTX_NDAPI auto SubmitManyAsync(
    std::vector<UploadRequest> reqs, std::shared_ptr<StagingProvider> provider)
    -> co::Co<std::vector<UploadResult>>;

private:
  OXGN_VRTX_API auto RetireCompleted(
    std::optional<frame::Slot> slot = std::nullopt) -> void;

  observer_ptr<Graphics> gfx_;
  UploadPolicy policy_;
  UploadTracker tracker_;

  std::vector<std::weak_ptr<StagingProvider>> providers_;

  //! Stage 1: Plan a coalescible run of buffer requests.
  auto PlanBufferRun(std::span<const UploadRequest> run)
    -> std::expected<BufferUploadPlan, UploadError>;

  //! Stage 2: Allocate and fill staging according to the plan and policy.
  auto FillStagingForPlan(const BufferUploadPlan& plan,
    std::span<const UploadRequest> run,
    StagingProvider::Allocation& allocation) const -> void;

  //! Stage 3: Optimize the buffer plan by coalescing contiguous regions.
  auto OptimizeBufferRun(
    std::span<const UploadRequest> run, const BufferUploadPlan& plan)
    -> std::expected<BufferUploadPlan, UploadError>;

  //! Stage 4: Record copies and transitions; returns signaled fence value.
  auto RecordBufferRun(const BufferUploadPlan& optimized,
    std::span<const UploadRequest> run, StagingProvider::Allocation& staging)
    -> std::expected<graphics::FenceValue, UploadError>;

  //! Issue per-request tickets based on the original (pre-optimized) plan.
  auto MakeTicketsForPlan(
    const BufferUploadPlan& original_plan, graphics::FenceValue fence)
    -> std::expected<std::vector<UploadTicket>, UploadError>;

  //! Helper: Execute the buffer-run pipeline end-to-end.
  //! Plan → FillStaging → Optimize → Record → Tickets.
  auto SubmitRun(std::span<const UploadRequest> run, StagingProvider& provider)
    -> std::expected<std::vector<UploadTicket>, UploadError>;

  std::atomic_bool shutting_down_ { false };
};

} // namespace oxygen::vortex::upload
