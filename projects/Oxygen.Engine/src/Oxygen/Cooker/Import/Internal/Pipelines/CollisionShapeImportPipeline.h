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
#include <Oxygen/Cooker/Import/Internal/ImportPipeline.h>
#include <Oxygen/Cooker/api_export.h>
#include <Oxygen/Data/PakFormat.h>
#include <Oxygen/OxCo/Channel.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/OxCo/Nursery.h>
#include <Oxygen/OxCo/ThreadPool.h>

namespace oxygen::content::import {

//! Pipeline for collision shape descriptor payload construction.
class CollisionShapeImportPipeline final : public Object {
  OXYGEN_TYPED(CollisionShapeImportPipeline)
public:
  static constexpr PlanItemKind kItemKind = PlanItemKind::kMaterialAsset;

  struct Config final {
    size_t queue_capacity = 16;
    uint32_t worker_count = 1;
    bool with_content_hashing = true;
  };

  struct WorkItem final {
    std::string source_id;
    data::pak::physics::CollisionShapeAssetDesc descriptor {};
    std::function<void()> on_started;
    std::function<void()> on_finished;
    std::stop_token stop_token;
  };

  struct WorkResult final {
    std::string source_id;
    std::vector<std::byte> descriptor_bytes;
    std::vector<ImportDiagnostic> diagnostics;
    ImportWorkItemTelemetry telemetry {};
    bool success = false;
  };

  OXGN_COOK_API explicit CollisionShapeImportPipeline(
    co::ThreadPool& thread_pool, Config config = {});
  OXGN_COOK_API ~CollisionShapeImportPipeline();

  OXYGEN_MAKE_NON_COPYABLE(CollisionShapeImportPipeline)
  OXYGEN_MAKE_NON_MOVABLE(CollisionShapeImportPipeline)

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
  [[nodiscard]] auto ComputeContentHash(
    std::vector<std::byte>& descriptor_bytes, std::stop_token stop_token)
    -> co::Co<std::optional<uint64_t>>;
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

static_assert(ImportPipeline<CollisionShapeImportPipeline>);

} // namespace oxygen::content::import
