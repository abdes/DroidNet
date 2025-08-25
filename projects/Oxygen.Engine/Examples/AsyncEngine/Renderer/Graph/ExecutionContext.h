//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <memory>
#include <string>
#include <vector>

#include "../../FrameContext.h"
#include "Resource.h"
#include "Types.h"
#include <Oxygen/Base/Logging.h>

// Forward declarations for AsyncEngine integration
namespace oxygen::examples::asyncsim {
class FrameContext;
class GraphicsLayerIntegration;
}

namespace oxygen::examples::asyncsim {

//! Draw item for Structure-of-Arrays draw data
/*!
 Represents a single draw call with all necessary parameters.
 Used in SOA layout for cache efficiency.
 */
struct DrawItem {
  uint32_t index { 0 }; //!< Draw index for bindless access
  uint32_t index_count { 0 }; //!< Number of indices to draw
  uint32_t instance_count { 1 }; //!< Number of instances to draw
  uint32_t start_index { 0 }; //!< First index location
  int32_t base_vertex { 0 }; //!< Value added to vertex index
  uint32_t start_instance { 0 }; //!< First instance location
};

//! Structure-of-Arrays draw packet collection
/*!
 Provides cache-efficient storage for draw calls with separate arrays
 for each field to improve memory access patterns.
 */
class DrawPackets {
public:
  DrawPackets() = default;

  //! Add a draw item to the packets
  auto AddDraw(const DrawItem& item) -> void
  {
    indices_.push_back(item.index);
    index_counts_.push_back(item.index_count);
    instance_counts_.push_back(item.instance_count);
    start_indices_.push_back(item.start_index);
    base_vertices_.push_back(item.base_vertex);
    start_instances_.push_back(item.start_instance);
  }

  //! Get number of draw items
  [[nodiscard]] auto Size() const -> size_t { return indices_.size(); }

  //! Clear all draw items
  auto Clear() -> void
  {
    indices_.clear();
    index_counts_.clear();
    instance_counts_.clear();
    start_indices_.clear();
    base_vertices_.clear();
    start_instances_.clear();
  }

  //! Get draw item at index (reconstructed from SOA)
  [[nodiscard]] auto GetDrawItem(size_t index) const -> DrawItem
  {
    if (index >= Size()) {
      return {};
    }

    return DrawItem { .index = indices_[index],
      .index_count = index_counts_[index],
      .instance_count = instance_counts_[index],
      .start_index = start_indices_[index],
      .base_vertex = base_vertices_[index],
      .start_instance = start_instances_[index] };
  }

  //! Get all indices array
  [[nodiscard]] auto GetIndices() const -> const std::vector<uint32_t>&
  {
    return indices_;
  }

  //! Get all index counts array
  [[nodiscard]] auto GetIndexCounts() const -> const std::vector<uint32_t>&
  {
    return index_counts_;
  }

private:
  std::vector<uint32_t> indices_;
  std::vector<uint32_t> index_counts_;
  std::vector<uint32_t> instance_counts_;
  std::vector<uint32_t> start_indices_;
  std::vector<int32_t> base_vertices_;
  std::vector<uint32_t> start_instances_;
};

//! Interface for GPU command recording
/*!
 Provides a platform-agnostic interface for recording GPU commands.
 This is a stub implementation for Phase 1.
 */
class CommandRecorder {
public:
  CommandRecorder() = default;
  virtual ~CommandRecorder() = default;

  // Non-copyable, movable
  CommandRecorder(const CommandRecorder&) = delete;
  auto operator=(const CommandRecorder&) -> CommandRecorder& = delete;
  CommandRecorder(CommandRecorder&&) = default;
  auto operator=(CommandRecorder&&) -> CommandRecorder& = default;

  //! Set viewport for rendering
  virtual auto SetViewport(const std::vector<float>& viewport) -> void
  {
    // Stub implementation - Phase 1
    (void)viewport;
  }

  //! Set pipeline state object
  virtual auto SetPipelineState(void* pso) -> void
  {
    // Stub implementation - Phase 1
    (void)pso;
  }

  //! Clear render target
  virtual auto ClearRenderTarget(
    ResourceHandle target, const std::vector<float>& color) -> void
  {
    // Stub implementation - Phase 1
    (void)target;
    (void)color;
  }

  //! Clear depth stencil view
  virtual auto ClearDepthStencilView(
    ResourceHandle target, float depth, uint8_t stencil) -> void
  {
    // Stub implementation - Phase 1
    (void)target;
    (void)depth;
    (void)stencil;
  }

  //! Set graphics root constant buffer view
  virtual auto SetGraphicsRootConstantBufferView(
    uint32_t index, uint64_t gpu_address) -> void
  {
    // Stub implementation - Phase 1
    (void)index;
    (void)gpu_address;
  }

  //! Set graphics root 32-bit constant
  virtual auto SetGraphicsRoot32BitConstant(
    uint32_t index, uint32_t value, uint32_t offset) -> void
  {
    // Stub implementation - Phase 1
    (void)index;
    (void)value;
    (void)offset;
  }

  //! Draw indexed instanced
  virtual auto DrawIndexedInstanced(uint32_t index_count,
    uint32_t instance_count, uint32_t start_index = 0, int32_t base_vertex = 0,
    uint32_t start_instance = 0) -> void
  {
    // Stub implementation - Phase 1
    (void)index_count;
    (void)instance_count;
    (void)start_index;
    (void)base_vertex;
    (void)start_instance;
  }

  //! Dispatch compute shader
  virtual auto Dispatch(uint32_t thread_group_count_x,
    uint32_t thread_group_count_y, uint32_t thread_group_count_z) -> void
  {
    // Stub implementation - Phase 1
    (void)thread_group_count_x;
    (void)thread_group_count_y;
    (void)thread_group_count_z;
  }

  //! Copy texture
  virtual auto CopyTexture(ResourceHandle source, ResourceHandle dest) -> void
  {
    // Stub implementation - Phase 1
    (void)source;
    (void)dest;
  }

  //! Get debug info
  [[nodiscard]] virtual auto GetDebugInfo() const -> std::string
  {
    return "CommandRecorder (stub implementation)";
  }
};

//! Task execution context for pass execution
/*!
 Provides access to resources, draw data, and command recording for pass
 executors. Contains all the context needed for a pass to execute its work.

 Enhanced with AsyncEngine integration for cross-module data access,
 graphics layer coordination, and thread-safe operation during command
 recording.
 */
class TaskExecutionContext {
public:
  TaskExecutionContext() = default;
  virtual ~TaskExecutionContext() = default;

  // Non-copyable, movable
  TaskExecutionContext(const TaskExecutionContext&) = delete;
  auto operator=(const TaskExecutionContext&) -> TaskExecutionContext& = delete;
  TaskExecutionContext(TaskExecutionContext&&) = default;
  auto operator=(TaskExecutionContext&&) -> TaskExecutionContext& = default;

  // === ASYNCENGINE INTEGRATION ===

  //! Set module context for cross-module data access
  auto SetModuleContext(FrameContext* module_context) -> void
  {
    module_context_ = module_context;
  }

  //! Get module context for accessing engine systems
  [[nodiscard]] auto GetModuleContext() const -> FrameContext*
  {
    return module_context_;
  }

  //! Set graphics integration for bindless resource access
  auto SetGraphicsIntegration(GraphicsLayerIntegration* integration) -> void
  {
    graphics_integration_ = integration;
  }

  //! Get graphics integration
  [[nodiscard]] auto GetGraphicsIntegration() const -> GraphicsLayerIntegration*
  {
    return graphics_integration_;
  }

  //! Check if AsyncEngine integration is available
  [[nodiscard]] auto HasAsyncEngineIntegration() const -> bool
  {
    return module_context_ != nullptr && graphics_integration_ != nullptr;
  }

  //! Get current frame index (from module context)
  [[nodiscard]] auto GetFrameIndex() const -> uint64_t
  {
    return module_context_ ? module_context_->GetFrameIndex() : 0;
  }

  //! Check if execution is in parallel-safe mode
  [[nodiscard]] auto IsParallelSafe() const -> bool
  {
    // Command recording phase supports parallel execution per surface
    return is_parallel_safe_;
  }

  //! Set parallel safety mode (used by render graph executor)
  auto SetParallelSafe(bool safe) -> void { is_parallel_safe_ = safe; }

  //! Get command recorder for GPU operations
  [[nodiscard]] virtual auto GetCommandRecorder() -> CommandRecorder&
  {
    if (!command_recorder_) {
      command_recorder_ = std::make_unique<CommandRecorder>();
    }
    return *command_recorder_;
  }

  //! Get view context for this execution
  [[nodiscard]] auto GetViewInfo() const -> const ViewInfo&
  {
    return view_context_;
  }

  //! Set view context for this execution
  auto SetViewInfo(const ViewInfo& context) -> void { view_context_ = context; }

  //! Get read resource by index
  [[nodiscard]] auto GetReadResource(size_t index) const -> ResourceHandle
  {
    if (index >= read_resources_.size()) {
      return ResourceHandle { 0 };
    }
    return read_resources_[index];
  }

  //! Get write resource by index
  [[nodiscard]] auto GetWriteResource(size_t index) const -> ResourceHandle
  {
    if (index >= write_resources_.size()) {
      return ResourceHandle { 0 };
    }
    return write_resources_[index];
  }

  //! Add read resource
  auto AddReadResource(ResourceHandle resource) -> void
  {
    read_resources_.push_back(resource);
  }

  //! Add write resource
  auto AddWriteResource(ResourceHandle resource) -> void
  {
    write_resources_.push_back(resource);
  }

  //! Get opaque draw list
  [[nodiscard]] auto GetOpaqueDrawList() const -> const std::vector<DrawItem>&
  {
    return opaque_draws_;
  }

  //! Get transparent draw list
  [[nodiscard]] auto GetTransparentDrawList() const
    -> const std::vector<DrawItem>&
  {
    return transparent_draws_;
  }

  //! Add opaque draw
  auto AddOpaqueDraw(const DrawItem& item) -> void
  {
    opaque_draws_.push_back(item);
  }

  //! Add transparent draw
  auto AddTransparentDraw(const DrawItem& item) -> void
  {
    transparent_draws_.push_back(item);
  }

  //! Get draw count (total)
  [[nodiscard]] auto GetDrawCount() const -> uint32_t
  {
    return static_cast<uint32_t>(
      opaque_draws_.size() + transparent_draws_.size());
  }

  //! Get instance count (for instanced rendering)
  [[nodiscard]] auto GetInstanceCount() const -> uint32_t
  {
    return instance_count_;
  }

  //! Set instance count
  auto SetInstanceCount(uint32_t count) -> void { instance_count_ = count; }

  //! Clear all draw lists
  auto ClearDrawLists() -> void
  {
    opaque_draws_.clear();
    transparent_draws_.clear();
  }

  // === DEBUGGING AND DIAGNOSTICS ===

  //! Get execution context statistics
  struct ExecutionStats {
    uint32_t total_draws { 0 };
    uint32_t opaque_draws { 0 };
    uint32_t transparent_draws { 0 };
    uint32_t read_resources { 0 };
    uint32_t write_resources { 0 };
    bool has_command_recorder { false };
    bool has_async_integration { false };
  };

  [[nodiscard]] auto GetExecutionStats() const -> ExecutionStats
  {
    ExecutionStats stats;
    stats.opaque_draws = static_cast<uint32_t>(opaque_draws_.size());
    stats.transparent_draws = static_cast<uint32_t>(transparent_draws_.size());
    stats.total_draws = stats.opaque_draws + stats.transparent_draws;
    stats.read_resources = static_cast<uint32_t>(read_resources_.size());
    stats.write_resources = static_cast<uint32_t>(write_resources_.size());
    stats.has_command_recorder = command_recorder_ != nullptr;
    stats.has_async_integration = HasAsyncEngineIntegration();
    return stats;
  }

private:
  std::unique_ptr<CommandRecorder> command_recorder_;
  ViewInfo view_context_;
  std::vector<ResourceHandle> read_resources_;
  std::vector<ResourceHandle> write_resources_;
  std::vector<DrawItem> opaque_draws_;
  std::vector<DrawItem> transparent_draws_;
  uint32_t instance_count_ { 1 };

  // AsyncEngine integration
  FrameContext* module_context_ { nullptr };
  GraphicsLayerIntegration* graphics_integration_ { nullptr };
  bool is_parallel_safe_ { false };
};

} // namespace oxygen::examples::asyncsim

// Forward declarations for factory functions
namespace oxygen::examples::asyncsim {
class TaskExecutionContext;

//! AsyncEngine-specific execution context with enhanced capabilities
class AsyncEngineTaskExecutionContext : public TaskExecutionContext {
public:
  AsyncEngineTaskExecutionContext() = default;
  ~AsyncEngineTaskExecutionContext() override = default;

  OXYGEN_MAKE_NON_COPYABLE(AsyncEngineTaskExecutionContext)
  OXYGEN_MAKE_NON_MOVABLE(AsyncEngineTaskExecutionContext)

  //! Prepare context for pass execution
  auto PrepareForPassExecution(const std::string& pass_name) -> void
  {
    // Prepare execution context for the given pass
    LOG_F(3, "[ExecutionContext] Preparing for pass: {}", pass_name);
  }

  //! Finalize pass execution
  auto FinalizePassExecution() -> void
  {
    // Clean up after pass execution
    LOG_F(3, "[ExecutionContext] Finalizing pass execution");
  }
};

//! Factory function to create enhanced execution context
auto CreateAsyncEngineTaskExecutionContext()
  -> std::unique_ptr<TaskExecutionContext>;
}
