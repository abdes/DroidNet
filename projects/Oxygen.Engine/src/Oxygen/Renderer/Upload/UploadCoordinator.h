//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <memory>
#include <span>
#include <vector>

#include <Oxygen/OxCo/Co.h>
#include <Oxygen/OxCo/Value.h>
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
  explicit UploadCoordinator(std::shared_ptr<oxygen::Graphics> graphics,
    UploadPolicy policy = DefaultUploadPolicy())
    : graphics_(std::move(graphics))
    , policy_(policy)
  {
  }

  OXYGEN_MAKE_NON_COPYABLE(UploadCoordinator)
  OXYGEN_DEFAULT_MOVABLE(UploadCoordinator)

  ~UploadCoordinator() = default;

  OXGN_RNDR_API auto Submit(const UploadRequest& req) -> UploadTicket;
  OXGN_RNDR_API auto SubmitMany(std::span<const UploadRequest> reqs)
    -> std::vector<UploadTicket>;

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
  auto SubmitAsync(const UploadRequest& req) -> co::Co<UploadResult>
  {
    auto t = Submit(req);
    co_await AwaitAsync(t);
    co_return *TryGetResult(t);
  }

  auto SubmitManyAsync(std::span<const UploadRequest> reqs)
    -> co::Co<std::vector<UploadResult>>
  {
    auto tickets = SubmitMany(reqs);
    co_await AwaitAllAsync(tickets);
    std::vector<UploadResult> out;
    out.reserve(tickets.size());
    for (auto t : tickets) {
      out.emplace_back(*TryGetResult(t));
    }
    co_return out;
  }

  auto AwaitAsync(UploadTicket t) -> co::Co<void>
  {
    co_await Until(tracker_.CompletedFenceValue() >= t.fence);
    co_return; // result can be queried if needed
  }

  auto AwaitAllAsync(std::span<const UploadTicket> tickets) -> co::Co<void>
  {
    if (tickets.empty())
      co_return;
    FenceValue max_fence { 0 };
    for (const auto& t : tickets) {
      if (max_fence < t.fence)
        max_fence = t.fence;
    }
    co_await Until(tracker_.CompletedFenceValue() >= max_fence);
    co_return;
  }

private:
  std::shared_ptr<oxygen::Graphics> graphics_;
  UploadPolicy policy_;
  UploadTracker tracker_;
};

} // namespace oxygen::engine::upload
