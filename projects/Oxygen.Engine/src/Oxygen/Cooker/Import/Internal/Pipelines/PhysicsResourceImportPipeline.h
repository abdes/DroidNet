//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <optional>
#include <stop_token>
#include <string>
#include <vector>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Composition/TypedObject.h>
#include <Oxygen/Cooker/Import/ImportDiagnostics.h>
#include <Oxygen/Cooker/Import/ImportReport.h>
#include <Oxygen/Cooker/Import/Internal/Emitters/PhysicsResourceEmitter.h>
#include <Oxygen/Cooker/Import/Internal/ImportPipeline.h>
#include <Oxygen/Cooker/api_export.h>
#include <Oxygen/OxCo/Channel.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/OxCo/Nursery.h>
#include <Oxygen/OxCo/ThreadPool.h>

namespace oxygen::content::import {

//! Pipeline for CPU-bound physics resource preparation.
class PhysicsResourceImportPipeline final : public Object {
  OXYGEN_TYPED(PhysicsResourceImportPipeline)
public:
  static constexpr PlanItemKind kItemKind = PlanItemKind::kBufferResource;

  struct Config final {
    size_t queue_capacity = 32;
    uint32_t worker_count = 2;
    bool with_content_hashing = true;
  };

  struct WorkItem final {
    std::string source_id;
    CookedPhysicsResourcePayload cooked;
    std::function<void()> on_started;
    std::function<void()> on_finished;
    std::stop_token stop_token;
  };

  struct WorkResult final {
    std::string source_id;
    CookedPhysicsResourcePayload cooked;
    std::vector<ImportDiagnostic> diagnostics;
    ImportWorkItemTelemetry telemetry {};
    bool success = false;
  };

  OXGN_COOK_API explicit PhysicsResourceImportPipeline(
    co::ThreadPool& thread_pool, Config config = {});
  OXGN_COOK_API ~PhysicsResourceImportPipeline();

  OXYGEN_MAKE_NON_COPYABLE(PhysicsResourceImportPipeline)
  OXYGEN_MAKE_NON_MOVABLE(PhysicsResourceImportPipeline)

  OXGN_COOK_API auto Start(co::Nursery& nursery) -> void;
  OXGN_COOK_NDAPI auto Submit(WorkItem item) -> co::Co<>;
  OXGN_COOK_NDAPI auto TrySubmit(WorkItem item) -> bool;
  OXGN_COOK_NDAPI auto Collect() -> co::Co<WorkResult>;
  OXGN_COOK_API auto Close() -> void;

  OXGN_COOK_NDAPI auto HasPending() const noexcept -> bool;
  OXGN_COOK_NDAPI auto PendingCount() const noexcept -> size_t;
  OXGN_COOK_NDAPI auto GetProgress() const noexcept -> PipelineProgress;

  OXGN_COOK_NDAPI auto OutputQueueSize() const noexcept -> size_t
  {
    return output_channel_.Size();
  }

  OXGN_COOK_NDAPI auto OutputQueueCapacity() const noexcept -> size_t
  {
    return config_.queue_capacity;
  }

private:
  [[nodiscard]] auto Worker() -> co::Co<>;
  [[nodiscard]] auto ComputeContentHash(WorkItem& item)
    -> co::Co<std::optional<ImportDiagnostic>>;
  auto ReportCancelled(WorkItem item) -> co::Co<>;

  co::ThreadPool& thread_pool_;
  Config config_;
  co::Channel<WorkItem> input_channel_;
  co::Channel<WorkResult> output_channel_;
  std::atomic<size_t> pending_ { 0 };
  std::atomic<size_t> submitted_ { 0 };
  std::atomic<size_t> completed_ { 0 };
  std::atomic<size_t> failed_ { 0 };
  bool started_ = false;
};

static_assert(ImportPipeline<PhysicsResourceImportPipeline>);

} // namespace oxygen::content::import
