//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <memory>
#include <string>
#include <vector>

#include "../Graph/Resource.h"
#include "../Graph/Types.h"

namespace oxygen::examples::asyncsim {

// Forward declarations
class RenderGraphBuilder;
class PassBuilder;

//! Base class for all render passes
/*!
 Render passes are the fundamental building blocks of the render graph.
 Each pass represents a distinct rendering operation that can read from
 and write to resources.
 */
class RenderPass {
public:
  RenderPass() = default;
  virtual ~RenderPass() = default;

  // Non-copyable, movable
  RenderPass(const RenderPass&) = delete;
  auto operator=(const RenderPass&) -> RenderPass& = delete;
  RenderPass(RenderPass&&) = default;
  auto operator=(RenderPass&&) -> RenderPass& = default;

  //! Get the pass handle (unique identifier)
  [[nodiscard]] auto GetHandle() const -> PassHandle { return handle_; }

  //! Get the debug name for this pass
  [[nodiscard]] auto GetDebugName() const -> const std::string&
  {
    return debug_name_;
  }

  //! Get the pass scope
  [[nodiscard]] auto GetScope() const -> PassScope { return scope_; }

  //! Get the priority level
  [[nodiscard]] auto GetPriority() const -> Priority { return priority_; }

  //! Get the queue type
  [[nodiscard]] auto GetQueueType() const -> QueueType { return queue_type_; }

  //! Get resources read by this pass
  [[nodiscard]] auto GetReadResources() const
    -> const std::vector<ResourceHandle>&
  {
    return read_resources_;
  }

  //! Get resources written by this pass
  [[nodiscard]] auto GetWriteResources() const
    -> const std::vector<ResourceHandle>&
  {
    return write_resources_;
  }

  //! Get pass dependencies
  [[nodiscard]] auto GetDependencies() const -> const std::vector<PassHandle>&
  {
    return dependencies_;
  }

  //! Get the pass executor function
  [[nodiscard]] auto GetExecutor() const -> const PassExecutor&
  {
    return executor_;
  }

  //! Set the pass executor function
  auto SetExecutor(PassExecutor executor) -> void
  {
    executor_ = std::move(executor);
  }

  //! Set whether this pass should iterate over all views
  auto SetIterateAllViews(bool iterate_all) -> void
  {
    iterate_all_views_ = iterate_all;
  }

  //! Check if this pass iterates over all views
  [[nodiscard]] auto ShouldIterateAllViews() const -> bool
  {
    return iterate_all_views_;
  }

  //! Add a resource read dependency
  auto AddReadResource(ResourceHandle resource, ResourceState state) -> void
  {
    read_resources_.push_back(resource);
    read_states_.push_back(state);
  }

  //! Add a resource write dependency
  auto AddWriteResource(ResourceHandle resource, ResourceState state) -> void
  {
    write_resources_.push_back(resource);
    write_states_.push_back(state);
  }

  //! Add a pass dependency
  auto AddDependency(PassHandle dependency) -> void
  {
    dependencies_.push_back(dependency);
  }

  //! Get type information for this pass
  [[nodiscard]] virtual auto GetTypeInfo() const -> std::string = 0;

  //! Execute this pass (calls the executor function)
  virtual auto Execute(TaskExecutionContext& context) -> void
  {
    if (executor_) {
      executor_(context);
    }
  }

protected:
  friend class PassBuilder;
  friend class RenderGraphBuilder;

  PassHandle handle_ { 0 };
  std::string debug_name_;
  PassScope scope_ { PassScope::PerView };
  Priority priority_ { Priority::Normal };
  QueueType queue_type_ { QueueType::Graphics };
  bool iterate_all_views_ { false };

  std::vector<ResourceHandle> read_resources_;
  std::vector<ResourceState> read_states_;
  std::vector<ResourceHandle> write_resources_;
  std::vector<ResourceState> write_states_;
  std::vector<PassHandle> dependencies_;

  PassExecutor executor_;
};

//! Raster pass for traditional graphics pipeline rendering
class RasterPass : public RenderPass {
public:
  RasterPass() = default;
  ~RasterPass() override = default;

  [[nodiscard]] auto GetTypeInfo() const -> std::string override
  {
    return "RasterPass";
  }
};

//! Compute pass for compute shader execution
class ComputePass : public RenderPass {
public:
  ComputePass() = default;
  ~ComputePass() override = default;

  [[nodiscard]] auto GetTypeInfo() const -> std::string override
  {
    return "ComputePass";
  }
};

//! Copy pass for resource transfer operations
class CopyPass : public RenderPass {
public:
  CopyPass() = default;
  ~CopyPass() override = default;

  [[nodiscard]] auto GetTypeInfo() const -> std::string override
  {
    return "CopyPass";
  }
};

//! Cost estimation for pass scheduling
struct PassCost {
  uint32_t cpu_us { 0 }; //!< Estimated CPU time in microseconds
  uint32_t gpu_us { 0 }; //!< Estimated GPU time in microseconds
  uint32_t memory_bytes { 0 }; //!< Estimated memory usage in bytes
};

//! Fluent interface for building render passes
/*!
 Provides a chainable API for configuring render passes before adding them
 to the render graph.
 */
class PassBuilder {
public:
  explicit PassBuilder(std::string name, std::unique_ptr<RenderPass> pass)
    : pass_(std::move(pass))
  {
    pass_->debug_name_ = std::move(name);
  }

  // Non-copyable but movable for fluent interface
  PassBuilder(const PassBuilder&) = delete;
  auto operator=(const PassBuilder&) -> PassBuilder& = delete;
  PassBuilder(PassBuilder&&) = default;
  auto operator=(PassBuilder&&) -> PassBuilder& = default;

  //! Set the pass priority
  auto SetPriority(Priority priority) -> PassBuilder&
  {
    pass_->priority_ = priority;
    return *this;
  }

  //! Set the pass scope
  auto SetScope(PassScope scope) -> PassBuilder&
  {
    pass_->scope_ = scope;
    return *this;
  }

  //! Set the queue type
  auto SetQueue(QueueType queue) -> PassBuilder&
  {
    pass_->queue_type_ = queue;
    return *this;
  }

  //! Set estimated cost for scheduling
  auto SetEstimatedCost(const PassCost& cost) -> PassBuilder&
  {
    estimated_cost_ = cost;
    return *this;
  }

  //! Add a pass dependency
  auto DependsOn(PassHandle dependency) -> PassBuilder&
  {
    pass_->AddDependency(dependency);
    return *this;
  }

  //! Add multiple pass dependencies
  auto DependsOn(const std::vector<PassHandle>& dependencies) -> PassBuilder&
  {
    for (const auto& dep : dependencies) {
      pass_->AddDependency(dep);
    }
    return *this;
  }

  //! Add a resource read
  auto Read(ResourceHandle resource, ResourceState state) -> PassBuilder&
  {
    pass_->AddReadResource(resource, state);
    return *this;
  }

  //! Add a resource read (convenience method)
  auto Reads(ResourceHandle resource) -> PassBuilder&
  {
    pass_->AddReadResource(resource, ResourceState::AllShaderResource);
    return *this;
  }

  //! Add a resource write
  auto Write(ResourceHandle resource, ResourceState state) -> PassBuilder&
  {
    pass_->AddWriteResource(resource, state);
    return *this;
  }

  //! Add a resource write (convenience method)
  auto Outputs(ResourceHandle resource) -> PassBuilder&
  {
    pass_->AddWriteResource(resource, ResourceState::RenderTarget);
    return *this;
  }

  //! Set view context for per-view passes
  auto SetViewContext(const ViewContext& view) -> PassBuilder&
  {
    view_context_ = view;
    return *this;
  }

  //! Configure this pass to iterate over all views
  auto IterateAllViews() -> PassBuilder&
  {
    pass_->SetIterateAllViews(true);
    return *this;
  }

  //! Set the pass executor
  auto SetExecutor(PassExecutor executor) -> PassBuilder&
  {
    pass_->SetExecutor(std::move(executor));
    return *this;
  }

  //! Build and return the configured pass
  auto Build() -> std::unique_ptr<RenderPass> { return std::move(pass_); }

  //! Get the estimated cost
  [[nodiscard]] auto GetEstimatedCost() const -> const PassCost&
  {
    return estimated_cost_;
  }

  //! Get the view context
  [[nodiscard]] auto GetViewContext() const -> const ViewContext&
  {
    return view_context_;
  }

private:
  std::unique_ptr<RenderPass> pass_;
  PassCost estimated_cost_;
  ViewContext view_context_;
};

} // namespace oxygen::examples::asyncsim
