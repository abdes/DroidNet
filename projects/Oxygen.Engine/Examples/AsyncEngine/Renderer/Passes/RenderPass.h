//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <Oxygen/Base/Logging.h>
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

  //! Get read states (1:1 with read resources)
  [[nodiscard]] auto GetReadStates() const -> const std::vector<ResourceState>&
  {
    return read_states_;
  }

  //! Get resources written by this pass
  [[nodiscard]] auto GetWriteResources() const
    -> const std::vector<ResourceHandle>&
  {
    return write_resources_;
  }

  //! Get write states (1:1 with write resources)
  [[nodiscard]] auto GetWriteStates() const -> const std::vector<ResourceState>&
  {
    return write_states_;
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

  //! Set the debug name
  auto SetDebugName(const std::string& name) -> void { debug_name_ = name; }

  //! Set view index (for view-specific passes)
  auto SetViewIndex(uint32_t view_index) -> void { view_index_ = view_index; }

  //! Get type information for this pass
  [[nodiscard]] virtual auto GetTypeInfo() const -> std::string = 0;

  //! Get the type name for debugging and scheduling
  [[nodiscard]] virtual auto GetTypeName() const -> const char* = 0;

  //! Get view index for view-specific passes (returns 0 for shared passes)
  [[nodiscard]] virtual auto GetViewIndex() const -> uint32_t
  {
    return view_index_;
  }

  //! Clone this pass for multi-view rendering
  [[nodiscard]] virtual auto Clone() const -> std::unique_ptr<RenderPass> = 0;

  //! View filtering flags
  [[nodiscard]] auto IsFiltered() const -> bool { return has_view_filter_; }
  [[nodiscard]] auto MatchesView(uint32_t view_index) const -> bool
  {
    if (!has_view_filter_)
      return true;
    if (single_view_only_)
      return view_index == view_index_;
    if (!allowed_views_.empty()) {
      return std::find(allowed_views_.begin(), allowed_views_.end(), view_index)
        != allowed_views_.end();
    }
    return true;
  }
  auto SetSingleView(uint32_t view_index) -> void
  {
    has_view_filter_ = true;
    single_view_only_ = true;
    view_index_ = view_index;
  }
  auto SetAllowedViews(std::vector<uint32_t> views) -> void
  {
    has_view_filter_ = true;
    single_view_only_ = false;
    allowed_views_ = std::move(views);
  }

  //! Execute this pass (calls the executor function)
  virtual auto Execute(TaskExecutionContext& context) -> void
  {
    // Pass executors are required to be set by graph construction. If this
    // triggers it indicates a cloning or builder regression where the
    // move-only executor was not propagated to a per-view instance.
    DCHECK_F(static_cast<bool>(executor_),
      "RenderPass '{}' executed without executor (scope={})", debug_name_,
      static_cast<unsigned>(scope_));
    if (!executor_) [[unlikely]] {
      return; // Safety in non-debug builds
    }
    executor_(context);
  }

protected:
  friend class PassBuilder;
  friend class RenderGraphBuilder;

  //! Copy base class data to another pass (for cloning)
  auto CopyTo(RenderPass& other) const -> void
  {
    other.handle_ = handle_;
    other.debug_name_ = debug_name_;
    other.scope_ = scope_;
    other.priority_ = priority_;
    other.queue_type_ = queue_type_;
    other.iterate_all_views_ = iterate_all_views_;
    other.view_index_ = view_index_;
    other.read_resources_ = read_resources_;
    other.read_states_ = read_states_;
    other.write_resources_ = write_resources_;
    other.write_states_ = write_states_;
    other.dependencies_ = dependencies_;
    // Note: executor_ is not copied as it's move-only
  }

  PassHandle handle_ { 0 };
  std::string debug_name_;
  PassScope scope_ { PassScope::PerView };
  Priority priority_ { Priority::Normal };
  QueueType queue_type_ { QueueType::Graphics };
  bool iterate_all_views_ { false };
  uint32_t view_index_ { 0 };

  std::vector<ResourceHandle> read_resources_;
  std::vector<ResourceState> read_states_;
  std::vector<ResourceHandle> write_resources_;
  std::vector<ResourceState> write_states_;
  std::vector<PassHandle> dependencies_;

  // View filtering meta
  bool has_view_filter_ { false };
  bool single_view_only_ { false };
  std::vector<uint32_t> allowed_views_;

  PassExecutor executor_;

  // Mutable accessors for internal graph construction (builder & friends)
  auto MutableReadResources() -> std::vector<ResourceHandle>&
  {
    return read_resources_;
  }
  auto MutableWriteResources() -> std::vector<ResourceHandle>&
  {
    return write_resources_;
  }
  auto MutableReadStates() -> std::vector<ResourceState>&
  {
    return read_states_;
  }
  auto MutableWriteStates() -> std::vector<ResourceState>&
  {
    return write_states_;
  }
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

  [[nodiscard]] auto GetTypeName() const -> const char* override
  {
    return "RasterPass";
  }

  [[nodiscard]] auto Clone() const -> std::unique_ptr<RenderPass> override
  {
    auto clone = std::make_unique<RasterPass>();
    CopyTo(*clone);
    return clone;
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

  [[nodiscard]] auto GetTypeName() const -> const char* override
  {
    return "ComputePass";
  }

  [[nodiscard]] auto Clone() const -> std::unique_ptr<RenderPass> override
  {
    auto clone = std::make_unique<ComputePass>();
    CopyTo(*clone);
    return clone;
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

  [[nodiscard]] auto GetTypeName() const -> const char* override
  {
    return "CopyPass";
  }

  [[nodiscard]] auto Clone() const -> std::unique_ptr<RenderPass> override
  {
    auto clone = std::make_unique<CopyPass>();
    CopyTo(*clone);
    return clone;
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

  //! Restrict pass to a single specific view index
  auto RestrictToView(uint32_t view_index) -> PassBuilder&
  {
    pass_->SetSingleView(view_index);
    return *this;
  }

  //! Restrict pass to a set of allowed view indices
  auto RestrictToViews(const std::vector<uint32_t>& views) -> PassBuilder&
  {
    pass_->SetAllowedViews(views);
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
