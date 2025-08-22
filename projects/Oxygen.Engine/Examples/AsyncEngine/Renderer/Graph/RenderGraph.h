//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "../Passes/RenderPass.h"
#include "Cache.h"
#include "ExecutionContext.h"
#include "RenderGraphBuilder.h"
#include "Resource.h"
#include "Scheduler.h"
#include "Types.h"
#include "Validator.h"

namespace oxygen::examples::asyncsim {

//! Execution statistics for performance monitoring
struct ExecutionStats {
  uint32_t passes_executed { 0 };
  uint32_t resources_created { 0 };
  float total_cpu_time_ms { 0.0f };
  float total_gpu_time_ms { 0.0f };
  size_t peak_memory_usage { 0 };

  ExecutionStats() = default;

  //! Reset all statistics
  auto Reset() -> void
  {
    passes_executed = 0;
    resources_created = 0;
    total_cpu_time_ms = 0.0f;
    total_gpu_time_ms = 0.0f;
    peak_memory_usage = 0;
  }
};

//! Main render graph class that orchestrates rendering
/*!
 The RenderGraph is the compiled, optimized representation of a render graph
 built from a RenderGraphBuilder. It contains all passes, resources, and
 execution logic needed to render a frame.
 */
class RenderGraph {
public:
  RenderGraph() = default;
  ~RenderGraph() = default;

  // Non-copyable, movable
  RenderGraph(const RenderGraph&) = delete;
  auto operator=(const RenderGraph&) -> RenderGraph& = delete;
  RenderGraph(RenderGraph&&) = default;
  auto operator=(RenderGraph&&) -> RenderGraph& = default;

  //! Execute the render graph
  /*!
   Executes all passes in the scheduled order, managing resource state
   transitions and multi-view rendering.
   */
  virtual auto Execute() -> bool
  {
    execution_stats_.Reset();

    // Stub implementation - Phase 1
    // In a real implementation, this would:
    // 1. Prepare resources for the frame
    // 2. Execute passes in dependency order
    // 3. Handle multi-view rendering
    // 4. Manage resource state transitions
    // 5. Collect performance metrics

    for (const auto& [handle, pass] : passes_) {
      if (pass) {
        TaskExecutionContext context;
        // Set up context for the pass
        SetupExecutionContext(context, handle);

        // Execute the pass
        pass->Execute(context);
        execution_stats_.passes_executed++;
      }
    }

    return true;
  }

  //! Execute with specific frame context
  virtual auto Execute(const FrameContext& frame_context) -> bool
  {
    frame_context_ = frame_context;
    return Execute();
  }

  //! Get execution statistics
  [[nodiscard]] auto GetExecutionStats() const -> const ExecutionStats&
  {
    return execution_stats_;
  }

  //! Get frame context
  [[nodiscard]] auto GetFrameContext() const -> const FrameContext&
  {
    return frame_context_;
  }

  //! Get all passes in execution order
  [[nodiscard]] auto GetExecutionOrder() const -> const std::vector<PassHandle>&
  {
    return execution_order_;
  }

  //! Get pass by handle
  [[nodiscard]] auto GetPass(PassHandle handle) const -> const RenderPass*
  {
    auto it = passes_.find(handle);
    return (it != passes_.end()) ? it->second.get() : nullptr;
  }

  //! Get resource descriptor by handle
  [[nodiscard]] auto GetResourceDescriptor(ResourceHandle handle) const
    -> const ResourceDesc*
  {
    auto it = resource_descriptors_.find(handle);
    return (it != resource_descriptors_.end()) ? it->second.get() : nullptr;
  }

  //! Get all resource handles
  [[nodiscard]] auto GetResourceHandles() const -> std::vector<ResourceHandle>
  {
    std::vector<ResourceHandle> handles;
    handles.reserve(resource_descriptors_.size());
    for (const auto& [handle, desc] : resource_descriptors_) {
      handles.push_back(handle);
    }
    return handles;
  }

  //! Get all pass handles
  [[nodiscard]] auto GetPassHandles() const -> std::vector<PassHandle>
  {
    std::vector<PassHandle> handles;
    handles.reserve(passes_.size());
    for (const auto& [handle, pass] : passes_) {
      handles.push_back(handle);
    }
    return handles;
  }

  //! Check if multi-view rendering is enabled
  [[nodiscard]] auto IsMultiViewEnabled() const -> bool
  {
    return multi_view_enabled_;
  }

  //! Get validation result from compilation
  [[nodiscard]] auto GetValidationResult() const -> const ValidationResult&
  {
    return validation_result_;
  }

  //! Get scheduling result from compilation
  [[nodiscard]] auto GetSchedulingResult() const -> const SchedulingResult&
  {
    return scheduling_result_;
  }

  //! Get cache key for this graph
  [[nodiscard]] auto GetCacheKey() const -> const RenderGraphCacheKey&
  {
    return cache_key_;
  }

  //! Get debug information
  [[nodiscard]] virtual auto GetDebugInfo() const -> std::string
  {
    return "RenderGraph: " + std::to_string(passes_.size()) + " passes, "
      + std::to_string(resource_descriptors_.size()) + " resources";
  }

  //! Optimize the graph for better performance
  virtual auto Optimize() -> void
  {
    // Stub implementation - Phase 1
    // In real implementation would optimize:
    // - Resource aliasing
    // - Pass ordering
    // - Multi-view parallelism
    // - Memory usage
  }

protected:
  friend class RenderGraphBuilder;

  //! Set up execution context for a pass
  virtual auto SetupExecutionContext(
    TaskExecutionContext& context, PassHandle pass_handle) -> void
  {
    // Stub implementation - Phase 1
    (void)context;
    (void)pass_handle;

    // In real implementation would:
    // 1. Set up resource bindings
    // 2. Configure view context for multi-view
    // 3. Prepare draw lists
    // 4. Set up command recorder
  }

  //! Add a pass to the graph (used by builder)
  auto AddPass(PassHandle handle, std::unique_ptr<RenderPass> pass) -> void
  {
    passes_[handle] = std::move(pass);
  }

  //! Add a resource descriptor (used by builder)
  auto AddResourceDescriptor(
    ResourceHandle handle, std::unique_ptr<ResourceDesc> desc) -> void
  {
    resource_descriptors_[handle] = std::move(desc);
  }

  //! Set execution order (used by scheduler)
  auto SetExecutionOrder(std::vector<PassHandle> order) -> void
  {
    execution_order_ = std::move(order);
  }

  //! Set frame context
  auto SetFrameContext(const FrameContext& context) -> void
  {
    frame_context_ = context;
  }

  //! Set multi-view enabled flag
  auto SetMultiViewEnabled(bool enabled) -> void
  {
    multi_view_enabled_ = enabled;
  }

  //! Set validation result
  auto SetValidationResult(const ValidationResult& result) -> void
  {
    validation_result_ = result;
  }

  //! Set scheduling result
  auto SetSchedulingResult(const SchedulingResult& result) -> void
  {
    scheduling_result_ = result;
  }

  //! Set cache key
  auto SetCacheKey(const RenderGraphCacheKey& key) -> void { cache_key_ = key; }

private:
  // Core graph data
  std::unordered_map<PassHandle, std::unique_ptr<RenderPass>> passes_;
  std::unordered_map<ResourceHandle, std::unique_ptr<ResourceDesc>>
    resource_descriptors_;
  std::vector<PassHandle> execution_order_;

  // Configuration
  FrameContext frame_context_;
  bool multi_view_enabled_ { false };

  // Compilation results
  ValidationResult validation_result_;
  SchedulingResult scheduling_result_;
  RenderGraphCacheKey cache_key_;

  // Runtime data
  ExecutionStats execution_stats_;
};

} // namespace oxygen::examples::asyncsim
