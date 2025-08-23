//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "../Passes/RenderPass.h"
#include "ExecutionContext.h"
#include "Resource.h"
#include "Scheduler.h"
#include "Types.h"

// Hash function for (ResourceHandle, size_t) pairs
namespace std {
template <>
struct hash<std::pair<oxygen::examples::asyncsim::ResourceHandle, size_t>> {
  auto operator()(
    const std::pair<oxygen::examples::asyncsim::ResourceHandle, size_t>& p)
    const -> std::size_t
  {
    return std::hash<uint32_t> {}(p.first.get())
      ^ (std::hash<size_t> {}(p.second) << 1);
  }
};

// Hash function for (PassHandle, size_t) pairs
template <>
struct hash<std::pair<oxygen::examples::asyncsim::PassHandle, size_t>> {
  auto operator()(
    const std::pair<oxygen::examples::asyncsim::PassHandle, size_t>& p) const
    -> std::size_t
  {
    return std::hash<uint32_t> {}(p.first.get())
      ^ (std::hash<size_t> {}(p.second) << 1);
  }
};
}

// Forward declarations for AsyncEngine integration
namespace oxygen::examples::asyncsim {
class ModuleContext;
class GraphicsLayerIntegration;
class RenderGraphValidator; // base interface
class RenderGraphScheduler;
class ResourceAliasValidator;

// Factory for resource alias validator
auto CreateAsyncEngineResourceValidator(GraphicsLayerIntegration* integration)
  -> std::unique_ptr<ResourceAliasValidator>;

// Factory function (defined in Validator.cpp) returns derived async validator
auto CreateAsyncEngineRenderGraphValidator()
  -> std::unique_ptr<RenderGraphValidator>;
auto CreateAsyncEngineRenderGraphScheduler()
  -> std::unique_ptr<RenderGraphScheduler>;
}

namespace oxygen::examples::asyncsim {

// Forward declarations
class RenderGraph;

//! Main builder interface for constructing render graphs
/*!
 Provides a fluent API for creating resources, passes, and configuring
 multi-view rendering. The builder validates and optimizes the graph
 during construction.

 Enhanced with AsyncEngine integration for:
 - ModuleContext integration for cross-module data access
 - Thread-safety for ModulePhases::ParallelWork
 - GraphicsLayer resource lifetime management
 */
class RenderGraphBuilder {
public:
  RenderGraphBuilder() = default;
  ~RenderGraphBuilder() = default;

  // Non-copyable, movable
  RenderGraphBuilder(const RenderGraphBuilder&) = delete;
  auto operator=(const RenderGraphBuilder&) -> RenderGraphBuilder& = delete;
  RenderGraphBuilder(RenderGraphBuilder&&) = default;
  auto operator=(RenderGraphBuilder&&) -> RenderGraphBuilder& = default;

  // === ASYNCENGINE INTEGRATION ===

  //! Set module context for engine integration
  auto SetModuleContext(ModuleContext* module_context) -> void
  {
    module_context_ = module_context;
  }

  //! Set graphics integration for resource management
  auto SetGraphicsIntegration(GraphicsLayerIntegration* integration) -> void
  {
    graphics_integration_ = integration;
  }

  //! Check if AsyncEngine integration is available
  [[nodiscard]] auto HasAsyncEngineIntegration() const -> bool
  {
    return module_context_ != nullptr && graphics_integration_ != nullptr;
  }

  //! Enable thread-safe mode for parallel work phases
  auto SetThreadSafeMode(bool thread_safe) -> void
  {
    is_thread_safe_ = thread_safe;
  }

  //! Check if builder is in thread-safe mode
  [[nodiscard]] auto IsThreadSafe() const -> bool { return is_thread_safe_; }

  //! Set the frame context for this render graph
  auto SetFrameContext(const FrameContext& context) -> void
  {
    frame_context_ = context;
  }

  //! Enable or disable multi-view rendering
  auto EnableMultiViewRendering(bool enabled) -> void
  {
    multi_view_enabled_ = enabled;
  }

  //! Inject pass cost profiler to be attached to final graph
  auto SetPassCostProfiler(std::shared_ptr<PassCostProfiler> profiler) -> void
  {
    pass_cost_profiler_ = std::move(profiler);
  }

  //! Create a texture resource
  auto CreateTexture(const std::string& name, TextureDesc desc,
    ResourceLifetime lifetime = ResourceLifetime::FrameLocal,
    ResourceScope scope = ResourceScope::PerView) -> ResourceHandle
  {
    auto handle = GetNextResourceHandle();

    auto resource_desc = std::make_unique<TextureDesc>(std::move(desc));
    resource_desc->SetDebugName(name);
    resource_desc->SetLifetime(lifetime);
    resource_desc->SetScope(scope);

    // Set graphics integration for AsyncEngine resource management
    if (graphics_integration_) {
      resource_desc->SetGraphicsIntegration(graphics_integration_);
    }

    resource_descriptors_[handle] = std::move(resource_desc);
    return handle;
  }

  //! Create a buffer resource
  auto CreateBuffer(const std::string& name, BufferDesc desc,
    ResourceLifetime lifetime = ResourceLifetime::FrameLocal,
    ResourceScope scope = ResourceScope::PerView) -> ResourceHandle
  {
    auto handle = GetNextResourceHandle();

    auto resource_desc = std::make_unique<BufferDesc>(std::move(desc));
    resource_desc->SetDebugName(name);
    resource_desc->SetLifetime(lifetime);
    resource_desc->SetScope(scope);

    // Set graphics integration for AsyncEngine resource management
    if (graphics_integration_) {
      resource_desc->SetGraphicsIntegration(graphics_integration_);
    }

    resource_descriptors_[handle] = std::move(resource_desc);
    return handle;
  }

  //! Create a surface target resource (for presentation)
  auto CreateSurfaceTarget(
    const std::string& name, std::shared_ptr<void> surface) -> ResourceHandle
  {

    auto handle = GetNextResourceHandle();

    // Create a texture descriptor for the surface
    auto resource_desc = std::make_unique<TextureDesc>();
    resource_desc->SetDebugName(name);
    resource_desc->SetLifetime(ResourceLifetime::FrameLocal);
    resource_desc->SetScope(ResourceScope::PerView);
    resource_desc->format = TextureDesc::Format::RGBA8_UNorm;
    resource_desc->usage = TextureDesc::Usage::RenderTarget;

    resource_descriptors_[handle] = std::move(resource_desc);
    surface_mappings_[handle] = surface;
    return handle;
  }

  //! Add a raster pass to the graph
  template <typename ConfigFunc>
  auto AddRasterPass(const std::string& name, ConfigFunc&& config) -> PassHandle
  {
    auto pass = std::make_unique<RasterPass>();
    auto handle = GetNextPassHandle();
    pass->handle_ = handle;

    auto builder = PassBuilder(name, std::move(pass));
    config(builder);

    auto built_pass = builder.Build();
    passes_[handle] = std::move(built_pass);
    return handle;
  }

  //! Add a compute pass to the graph
  auto AddComputePass(const std::string& name) -> PassBuilder
  {
    auto pass = std::make_unique<ComputePass>();
    pass->handle_ = GetNextPassHandle();

    auto builder = PassBuilder(name, std::move(pass));
    return builder;
  }

  //! Add a copy pass to the graph
  auto AddCopyPass(const std::string& name) -> PassBuilder
  {
    auto pass = std::make_unique<CopyPass>();
    pass->handle_ = GetNextPassHandle();

    auto builder = PassBuilder(name, std::move(pass));
    return builder;
  }

  //! Get a pass by handle for additional configuration
  auto GetPass(PassHandle handle) -> PassBuilder
  {
    auto it = passes_.find(handle);
    if (it != passes_.end()) {
      // Return a builder wrapping the existing pass
      auto pass_copy = std::unique_ptr<RenderPass>(it->second.get());
      return PassBuilder(it->second->GetDebugName(), std::move(pass_copy));
    }

    // Return a dummy builder for invalid handles
    auto dummy_pass = std::make_unique<RasterPass>();
    return PassBuilder("invalid", std::move(dummy_pass));
  }

  //! Add a configured pass to the graph
  auto AddPass(PassBuilder&& builder) -> PassHandle
  {
    auto pass = builder.Build();
    auto handle = pass->GetHandle();

    passes_[handle] = std::move(pass);
    return handle;
  }

  //! Configure iteration over all views
  auto IterateAllViews() -> RenderGraphBuilder&
  {
    iterate_all_views_ = true;
    return *this;
  }

  //! Restrict to a specific view
  auto RestrictToView(size_t view_index) -> RenderGraphBuilder&
  {
    restricted_view_index_ = view_index;
    return *this;
  }

  //! Restrict to views matching a filter
  template <typename FilterFunc>
  auto RestrictToViews(FilterFunc&& filter) -> RenderGraphBuilder&
  {
    view_filter_ = std::forward<FilterFunc>(filter);
    return *this;
  }

  //! Build the final render graph
  auto Build() -> std::unique_ptr<RenderGraph>;

  //! (Phase 2) Optimize per-view duplicated read-only resources into a single
  //! shared resource when safe.
  /*!\n   * Detects groups of per-view resources cloned from the same base
   * handle\n   * (via internal mapping) whose descriptors are compatible and
   * which are\n   * only ever read (never written) by passes. Such resources
   * can be promoted\n   * to a single ResourceScope::Shared instance to reduce
   * memory usage.\n   *\n   * Safety constraints for promotion:\n   *  - All
   * variants have identical descriptor compatibility hash\n   *  - No pass
   * writes to any variant (read-only across frame)\n   *  - Variants span all
   * active views (partial sets skipped)\n   *  - Original scope was PerView (we
   * never downgrade Shared)\n   *\n   * The optimization occurs prior to
   * validation & scheduling so subsequent\n   * lifetime analysis sees the
   * promoted shared resource.\n   */
  auto OptimizeSharedPerViewResources(class RenderGraph* render_graph) -> void;

  //! Get resource descriptor by handle
  [[nodiscard]] auto GetResourceDescriptor(ResourceHandle handle) const
    -> const ResourceDesc*
  {
    auto it = resource_descriptors_.find(handle);
    return (it != resource_descriptors_.end()) ? it->second.get() : nullptr;
  }

  //! Get access to resource descriptors for validation
  [[nodiscard]] auto GetResourceDescriptors() const
    -> const std::unordered_map<ResourceHandle, std::unique_ptr<ResourceDesc>>&
  {
    return resource_descriptors_;
  }

  //! Get pass by handle
  [[nodiscard]] auto GetPassPtr(PassHandle handle) const -> const RenderPass*
  {
    auto it = passes_.find(handle);
    return (it != passes_.end()) ? it->second.get() : nullptr;
  }

  //! Get frame context
  [[nodiscard]] auto GetFrameContext() const -> const FrameContext&
  {
    return frame_context_;
  }

  //! Check if multi-view rendering is enabled
  [[nodiscard]] auto IsMultiViewEnabled() const -> bool
  {
    return multi_view_enabled_;
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

  //! Get all passes (for scheduler/validator)
  [[nodiscard]] auto GetPasses() const -> std::vector<PassHandle>;

  //! Build a dependency adjacency list (Pass -> deps) using explicit pass
  //! dependencies only. Resource hazard based edges are added later in
  //! scheduling phase once lifetimes are known.
  [[nodiscard]] auto GetExplicitDependencyGraph() const
    -> std::unordered_map<PassHandle, std::vector<PassHandle>>
  {
    std::unordered_map<PassHandle, std::vector<PassHandle>> graph;
    for (const auto& [handle, pass] : passes_) {
      auto& list = graph[handle];
      list.insert(list.end(), pass->GetDependencies().begin(),
        pass->GetDependencies().end());
    }
    return graph;
  }

private:
  //! Process multi-view configuration and create per-view resources
  auto ProcessMultiViewConfiguration(RenderGraph* render_graph) -> void;

  //! Process passes with view filtering applied
  auto ProcessPassesWithViewFiltering(RenderGraph* render_graph) -> void;

  //! Create per-view variants of resources
  auto CreatePerViewResources(
    ResourceHandle base_handle, const ResourceDesc& desc) -> void;

  //! Create per-view variants of passes
  auto CreatePerViewPasses(PassHandle base_handle, RenderPass* base_pass,
    RenderGraph* render_graph) -> void;

  //! Rebuild explicit dependency graph after per-view expansion so that
  //! dependencies reference the actual cloned pass handles instead of
  //! template base handles that never execute.
  auto RebuildExplicitDependencies(const RenderGraph* render_graph)
    -> std::unordered_map<PassHandle, std::vector<PassHandle>>;

  //! Determine which views are active based on filters
  [[nodiscard]] auto DetermineActiveViews() -> std::vector<size_t>;

  //! Check if a pass should be executed for current view configuration
  [[nodiscard]] auto ShouldExecutePassForViews(const RenderPass& pass) const
    -> bool;

  //! Remap resource handles in a pass to view-specific variants
  auto RemapResourceHandlesForView(RenderPass* pass, size_t view_index) -> void;

  //! Get view-specific resource handle
  [[nodiscard]] auto GetViewSpecificResourceHandle(ResourceHandle base_handle,
    size_t view_index) const -> std::optional<ResourceHandle>;

  //! Get next unique resource handle
  auto GetNextResourceHandle() -> ResourceHandle
  {
    return ResourceHandle { ++next_resource_id_ };
  }

  //! Get next unique pass handle
  auto GetNextPassHandle() -> PassHandle
  {
    return PassHandle { ++next_pass_id_ };
  }

private:
  // Core state
  FrameContext frame_context_;
  bool multi_view_enabled_ { false };

  // Resource management
  std::unordered_map<ResourceHandle, std::unique_ptr<ResourceDesc>>
    resource_descriptors_;
  std::unordered_map<ResourceHandle, std::shared_ptr<void>> surface_mappings_;
  uint32_t next_resource_id_ { 0 };

  // Pass management
  std::unordered_map<PassHandle, std::unique_ptr<RenderPass>> passes_;
  uint32_t next_pass_id_ { 0 };

  // View configuration
  bool iterate_all_views_ { false };
  std::optional<size_t> restricted_view_index_;
  std::function<bool(const ViewContext&)> view_filter_;

  // Multi-view state tracking
  std::vector<size_t> active_view_indices_;
  std::unordered_map<std::pair<ResourceHandle, size_t>, ResourceHandle>
    per_view_resource_mapping_;
  // Map (base pass handle, view_index) -> cloned per-view pass handle
  std::unordered_map<std::pair<PassHandle, size_t>, PassHandle>
    per_view_pass_mapping_;
  // Track which base pass handles were expanded into per-view clones
  std::unordered_set<PassHandle> expanded_per_view_passes_;

  // AsyncEngine integration
  ModuleContext* module_context_ { nullptr };
  GraphicsLayerIntegration* graphics_integration_ { nullptr };
  bool is_thread_safe_ { false };

  // Adaptive scheduling instrumentation
  std::shared_ptr<PassCostProfiler> pass_cost_profiler_;
};

} // namespace oxygen::examples::asyncsim
