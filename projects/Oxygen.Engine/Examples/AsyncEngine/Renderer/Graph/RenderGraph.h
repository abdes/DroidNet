//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

// Forward declarations
namespace oxygen::engine::asyncsim {

}

#include <memory>
#include <stdexcept>
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
#include <Oxygen/Core/FrameContext.h>
#include <Oxygen/OxCo/Co.h>

namespace oxygen::engine::asyncsim {

// Forward declarations

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
  friend class AsyncRenderGraph;

public:
  // Strategy interfaces
  struct IExecutor {
    virtual ~IExecutor() = default;
    virtual co::Co<> Execute(RenderGraph& graph, FrameContext& ctx) = 0;
    virtual co::Co<> ExecutePassBatches(RenderGraph& graph, FrameContext& ctx)
      = 0;
    virtual co::Co<> Present(RenderGraph& graph, FrameContext& ctx) = 0;
  };

  struct IScheduler {
    virtual ~IScheduler() = default;
    virtual void Schedule(RenderGraph& graph) = 0;
  };

  struct ITransitionPlanner {
    virtual ~ITransitionPlanner() = default;
    virtual co::Co<> PlanTransitions(RenderGraph& graph, FrameContext& ctx) = 0;
  };

public:
  // Construction requires exclusive ownership of mandatory components.
  RenderGraph(std::unique_ptr<IExecutor> executor,
    std::unique_ptr<IScheduler> scheduler,
    std::unique_ptr<ITransitionPlanner> planner)
    : executor_(std::move(executor))
    , scheduler_(std::move(scheduler))
    , planner_(std::move(planner))
  {
    if (!executor_ || !scheduler_ || !planner_)
      throw std::invalid_argument(
        "RenderGraph requires non-null executor, scheduler and planner");
  }

  virtual ~RenderGraph() = default; // ensure proper polymorphic deletion

  // Non-copyable, movable
  RenderGraph(const RenderGraph&) = delete;
  auto operator=(const RenderGraph&) -> RenderGraph& = delete;
  RenderGraph(RenderGraph&&) = default;
  auto operator=(RenderGraph&&) -> RenderGraph& = default;

  //! Execute the render graph (delegates to executor)
  auto Execute(FrameContext& context) -> co::Co<>
  {
    return executor_->Execute(*this, context);
  }

  //! Plan resource state transitions (delegates to planner)
  auto PlanResourceTransitions(FrameContext& context) -> co::Co<>
  {
    return planner_->PlanTransitions(*this, context);
  }

  //! Execute pass batches in parallel (delegates to executor)
  auto ExecutePassBatches(FrameContext& context) -> co::Co<>
  {
    return executor_->ExecutePassBatches(*this, context);
  }

  // Presentation is not done by the render graph; delegate to executor
  auto PresentResults(FrameContext& context) -> co::Co<>
  {
    return executor_->Present(*this, context);
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
  [[nodiscard]] auto GetPassMutable(PassHandle handle) -> RenderPass*
  {
    auto it = passes_.find(handle);
    return (it != passes_.end()) ? it->second.get() : nullptr;
  }

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

  //! Get number of passes in this graph
  [[nodiscard]] auto GetPassCount() const -> std::uint32_t
  {
    return static_cast<std::uint32_t>(passes_.size());
  }

  //! Get all passes in this graph
  [[nodiscard]] auto GetPasses() const
    -> const std::unordered_map<PassHandle, std::unique_ptr<RenderPass>>&
  {
    return passes_;
  }

  //! Get explicit dependency graph (built by builder) if available
  [[nodiscard]] auto GetExplicitDependencies() const
    -> const std::unordered_map<PassHandle, std::vector<PassHandle>>&
  {
    return explicit_dependencies_;
  }

  //! Get number of resources in this graph
  [[nodiscard]] auto GetResourceCount() const -> std::uint32_t
  {
    return static_cast<std::uint32_t>(resource_descriptors_.size());
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

  //! Access pass cost profiler (may be null in stub builds)
  [[nodiscard]] auto GetPassCostProfiler() const
    -> const std::shared_ptr<PassCostProfiler>&
  {
    return pass_cost_profiler_;
  }

  //! Inject a pass cost profiler (builder/module can provide one)
  auto SetPassCostProfiler(std::shared_ptr<PassCostProfiler> profiler) -> void
  {
    pass_cost_profiler_ = std::move(profiler);
  }

  //! Get cache key for this graph
  [[nodiscard]] auto GetCacheKey() const -> const RenderGraphCacheKey&
  {
    return cache_key_;
  }

  //! Optimize the graph for better performance
  virtual auto Optimize() -> void
  {
    // Stub implementation - Phase 1
    // In real implementation would optimize:
    // - Resource aliasing
    // - Pass ordering
    // - View parallelism
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
    // 2. Configure view context for rendering
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

  //! Set view enabled flag
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

  //! Set explicit dependency graph (builder)
  auto SetExplicitDependencies(
    std::unordered_map<PassHandle, std::vector<PassHandle>> deps) -> void
  {
    explicit_dependencies_ = std::move(deps);
  }

  //! Read-only accessors for injected components
  [[nodiscard]] auto GetExecutor() const noexcept -> IExecutor*
  {
    return executor_.get();
  }
  [[nodiscard]] auto GetScheduler() const noexcept -> IScheduler*
  {
    return scheduler_.get();
  }
  [[nodiscard]] auto GetTransitionPlanner() const noexcept
    -> ITransitionPlanner*
  {
    return planner_.get();
  }

private:
  // Core graph data
  std::unordered_map<PassHandle, std::unique_ptr<RenderPass>> passes_;
  std::unordered_map<ResourceHandle, std::unique_ptr<ResourceDesc>>
    resource_descriptors_;
  std::vector<PassHandle> execution_order_;
  std::unordered_map<PassHandle, std::vector<PassHandle>>
    explicit_dependencies_;

  // Configuration
  FrameContext frame_context_;

  // Compilation results
  ValidationResult validation_result_;
  SchedulingResult scheduling_result_;
  RenderGraphCacheKey cache_key_;

  // Runtime data
  ExecutionStats execution_stats_;

  // Adaptive scheduling support
  std::shared_ptr<PassCostProfiler> pass_cost_profiler_;
  // Injected mandatory strategies (exclusive ownership)
  std::unique_ptr<IExecutor> executor_;
  std::unique_ptr<IScheduler> scheduler_;
  std::unique_ptr<ITransitionPlanner> planner_;
};

//! Enhanced RenderGraph with complete AsyncEngine execution pipeline
/*!
 AsyncRenderGraph provides a fully-featured render graph implementation
 with coroutine-based execution, resource state tracking, and view-agnostic
 rendering support. Supports three execution patterns:
 - PassScope::Shared: Execute once for all views
 - PassScope::PerView: Execute once per view (skipped if no views)
 - PassScope::Viewless: Execute once without view dependency
 */
class AsyncRenderGraph : public RenderGraph {
public:
  AsyncRenderGraph();

private:
  // Implementation entrypoints called by adapters (private)
  auto ExecuteImpl(FrameContext& context) -> co::Co<>;
  auto PlanResourceTransitionsImpl(FrameContext& context) -> co::Co<>;
  auto ExecutePassBatchesImpl(FrameContext& context) -> co::Co<>;
  auto PresentResultsImpl(FrameContext& context) -> co::Co<>;

  // Adapters used to satisfy the base class' strategy interfaces. These are
  // simple thin wrappers that forward into the AsyncRenderGraph impl methods.
  struct ExecAdapter final : IExecutor {
    AsyncRenderGraph* owner;
    explicit ExecAdapter(AsyncRenderGraph* o) noexcept
      : owner(o)
    {
    }
    auto Execute(RenderGraph& g, FrameContext& ctx) -> co::Co<> override
    {
      (void)g;
      return owner->ExecuteImpl(ctx);
    }
    auto ExecutePassBatches(RenderGraph& g, FrameContext& ctx)
      -> co::Co<> override
    {
      (void)g;
      return owner->ExecutePassBatchesImpl(ctx);
    }
    auto Present(RenderGraph& g, FrameContext& ctx) -> co::Co<> override
    {
      (void)g;
      return owner->PresentResultsImpl(ctx);
    }
  };

  struct SchedulerAdapter final : IScheduler {
    AsyncRenderGraph* owner;
    explicit SchedulerAdapter(AsyncRenderGraph* o) noexcept
      : owner(o)
    {
    }
    void Schedule(RenderGraph& g) override { (void)g; /* nop */ }
  };

  struct PlannerAdapter final : ITransitionPlanner {
    AsyncRenderGraph* owner;
    explicit PlannerAdapter(AsyncRenderGraph* o) noexcept
      : owner(o)
    {
    }
    auto PlanTransitions(RenderGraph& g, FrameContext& ctx) -> co::Co<> override
    {
      (void)g;
      return owner->PlanResourceTransitionsImpl(ctx);
    }
  };

private:
  // Helper methods for ExecutePassBatches
  auto BuildDependencyGraph(const std::vector<PassHandle>& order)
    -> std::unordered_map<PassHandle, int>;
  auto LogDependencyDiagnostics(const std::vector<PassHandle>& order,
    const std::unordered_map<PassHandle, int>& remaining_deps) -> void;
  auto BuildExecutionBatches(const std::vector<PassHandle>& order,
    std::unordered_map<PassHandle, int>& remaining_deps)
    -> std::vector<std::vector<PassHandle>>;
  auto ExecuteBatchSerial(const std::vector<PassHandle>& batch,
    const std::span<const ViewInfo>& views, TaskExecutionContext& exec_ctx)
    -> void;
  auto ExecuteBatchParallel(FrameContext& context,
    const std::vector<PassHandle>& batch, size_t bi,
    const std::span<const ViewInfo>& views, TaskExecutionContext& exec_ctx,
    const std::chrono::high_resolution_clock::time_point& batch_start)
    -> co::Co<>;

public:
  //! Enable or disable intra-batch parallel execution (thread pool dispatch)
  auto SetParallelBatchExecution(bool enabled) noexcept -> void
  {
    parallel_batch_execution_enabled_ = enabled;
  }

  [[nodiscard]] auto IsParallelBatchExecutionEnabled() const noexcept -> bool
  {
    return parallel_batch_execution_enabled_;
  }

  // Test access (Google Test)
  friend class RenderGraphSchedulingTests_LinearChain_Test; // gtest naming
                                                            // convention
  friend class RenderGraphSchedulingTests_Independent_Test;
  friend class RenderGraphSchedulingTests_Diamond_Test;
  friend class RenderGraphSchedulingTests_CycleDetection_Test;

  // Tests should use injected strategy objects; keep no test-only mutators.

private:
  // Persistent resource state tracker for transition planning phase
  ResourceStateTracker resource_state_tracker_;
  bool parallel_batch_execution_enabled_ { true }; // default on

  struct BatchMetrics {
    uint32_t batch_count { 0 };
    uint32_t max_width { 0 };
    double avg_width { 0.0 };
    double parallel_pass_fraction {
      0.0
    }; // fraction of passes in multi-pass batches
  } batch_metrics_;

public:
  [[nodiscard]] auto GetBatchMetrics() const -> const BatchMetrics&
  {
    return batch_metrics_;
  }

public:
  //! Access planned transitions (after PlanResourceTransitions)
  [[nodiscard]] auto GetPlannedTransitions() const
    -> const std::vector<ResourceTransition>&
  {
    return resource_state_tracker_.GetPlannedTransitions();
  }
};

// Factory declaration for creating an AsyncEngine-enabled render graph
auto CreateAsyncRenderGraph() -> std::unique_ptr<RenderGraph>;

} // namespace oxygen::engine::asyncsim
