//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <algorithm>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "../Passes/RenderPass.h"
#include "Types.h"

namespace oxygen::engine::asyncsim {

// Forward declarations
class RenderGraph;

//! Scheduling result information
struct SchedulingResult {
  std::vector<PassHandle>
    execution_order; //!< Topologically sorted pass execution order
  std::vector<QueueType> queue_assignments; //!< Queue assignment for each pass
  float estimated_frame_time_ms { 0.0f }; //!< Estimated total frame time

  SchedulingResult() = default;
};

//! Performance metrics for pass cost profiling
struct PassMetrics {
  float avg_cpu_time_us { 0.0f }; //!< Moving average CPU time
  float avg_gpu_time_us { 0.0f }; //!< Moving average GPU time
  uint32_t memory_peak_bytes { 0 }; //!< Peak memory usage
  uint32_t execution_count { 0 }; //!< Number of times executed

  PassMetrics() = default;
};

//! Interface for render graph scheduling
/*!
 Provides scheduling algorithms to optimize pass execution order,
 queue assignments, and resource allocation for maximum performance.
 */
class RenderGraphScheduler {
public:
  RenderGraphScheduler() = default;
  virtual ~RenderGraphScheduler() = default;

  // Non-copyable, movable
  RenderGraphScheduler(const RenderGraphScheduler&) = delete;
  auto operator=(const RenderGraphScheduler&) -> RenderGraphScheduler& = delete;
  RenderGraphScheduler(RenderGraphScheduler&&) = default;
  auto operator=(RenderGraphScheduler&&) -> RenderGraphScheduler& = default;

  //! Schedule passes for optimal execution
  /*!
   Analyzes pass dependencies, costs, and resource usage to determine
   optimal execution order and queue assignments.
   */
  [[nodiscard]] virtual auto SchedulePasses(const RenderGraph& graph)
    -> SchedulingResult
  {
    // Stub implementation - Phase 1
    (void)graph;
    // TODO(Phase2): Incorporate PassCostProfiler dynamic costs for ordering
    // (minimize critical path)
    return SchedulingResult {};
  }

  //! Perform critical path analysis
  [[nodiscard]] virtual auto AnalyzeCriticalPath(const RenderGraph& graph)
    -> std::vector<PassHandle>
  {
    // Stub implementation - Phase 1
    (void)graph;
    return {};
  }

  //! Optimize for multi-queue execution
  virtual auto OptimizeMultiQueue(SchedulingResult& result) -> void
  {
    // Stub implementation - Phase 1
    (void)result;
    // TODO(Phase2): Implement queue assignment heuristic using pass type +
    // dependencies
  }

  //! Set scheduling priority for a pass type
  virtual auto SetPassTypePriority(
    const std::string& pass_type, Priority priority) -> void
  {
    pass_type_priorities_[pass_type] = priority;
  }

  //! Get estimated frame time
  [[nodiscard]] virtual auto GetEstimatedFrameTime(
    const SchedulingResult& result) const -> float
  {
    return result.estimated_frame_time_ms;
  }

  //! Enable or disable adaptive scheduling
  virtual auto SetAdaptiveScheduling(bool enabled) -> void
  {
    adaptive_scheduling_enabled_ = enabled;
  }

  //! Get debug information
  [[nodiscard]] virtual auto GetDebugInfo() const -> std::string
  {
    return "RenderGraphScheduler (stub implementation)";
  }

protected:
  std::unordered_map<std::string, Priority> pass_type_priorities_;
  bool adaptive_scheduling_enabled_ { false };
};

//! Interface for profiling pass execution costs
/*!
 Collects runtime performance data and provides feedback for adaptive
 scheduling decisions. Uses exponential moving averages for stable metrics.
 */
class PassCostProfiler {
public:
  PassCostProfiler() = default;
  virtual ~PassCostProfiler() = default;

  // Non-copyable, movable
  PassCostProfiler(const PassCostProfiler&) = delete;
  auto operator=(const PassCostProfiler&) -> PassCostProfiler& = delete;
  PassCostProfiler(PassCostProfiler&&) = default;
  auto operator=(PassCostProfiler&&) -> PassCostProfiler& = default;

  //! Begin profiling a pass execution
  virtual auto BeginPass(PassHandle handle) -> void
  {
    // Stub implementation - Phase 1
    (void)handle;
  }

  //! End profiling a pass execution
  virtual auto EndPass(PassHandle handle) -> void
  {
    // Stub implementation - Phase 1
    (void)handle;
  }

  //! Record CPU timing for a pass
  virtual auto RecordCpuTime(PassHandle handle, float time_us) -> void
  {
    auto& metrics = pass_metrics_[handle];

    // Exponential moving average
    const float alpha = 0.1f; // Smoothing factor
    if (metrics.execution_count == 0) {
      metrics.avg_cpu_time_us = time_us;
    } else {
      metrics.avg_cpu_time_us
        = alpha * time_us + (1.0f - alpha) * metrics.avg_cpu_time_us;
    }
    metrics.execution_count++;
  }

  //! Record GPU timing for a pass
  virtual auto RecordGpuTime(PassHandle handle, float time_us) -> void
  {
    auto& metrics = pass_metrics_[handle];

    // Exponential moving average
    const float alpha = 0.1f; // Smoothing factor
    if (metrics.execution_count == 0) {
      metrics.avg_gpu_time_us = time_us;
    } else {
      metrics.avg_gpu_time_us
        = alpha * time_us + (1.0f - alpha) * metrics.avg_gpu_time_us;
    }
  }

  //! Record memory usage for a pass
  virtual auto RecordMemoryUsage(PassHandle handle, uint32_t bytes) -> void
  {
    auto& metrics = pass_metrics_[handle];
    metrics.memory_peak_bytes = std::max(metrics.memory_peak_bytes, bytes);
  }

  //! Get metrics for a specific pass
  [[nodiscard]] virtual auto GetPassMetrics(PassHandle handle) const
    -> PassMetrics
  {
    auto it = pass_metrics_.find(handle);
    return (it != pass_metrics_.end()) ? it->second : PassMetrics {};
  }

  //! Get updated cost estimate for a pass
  [[nodiscard]] virtual auto GetUpdatedCost(PassHandle handle) const -> PassCost
  {
    auto metrics = GetPassMetrics(handle);
    return PassCost { .cpu_us = static_cast<uint32_t>(metrics.avg_cpu_time_us),
      .gpu_us = static_cast<uint32_t>(metrics.avg_gpu_time_us),
      .memory_bytes = metrics.memory_peak_bytes };
  }

  //! Clear all recorded metrics
  virtual auto ClearMetrics() -> void { pass_metrics_.clear(); }

  //! Set the smoothing factor for exponential moving average
  virtual auto SetSmoothingFactor(float alpha) -> void
  {
    smoothing_factor_ = std::clamp(alpha, 0.0f, 1.0f);
  }

  //! Get debug information
  [[nodiscard]] virtual auto GetDebugInfo() const -> std::string
  {
    return "PassCostProfiler with " + std::to_string(pass_metrics_.size())
      + " tracked passes";
  }

protected:
  std::unordered_map<PassHandle, PassMetrics> pass_metrics_;
  float smoothing_factor_ { 0.1f };
};

} // namespace oxygen::engine::asyncsim
