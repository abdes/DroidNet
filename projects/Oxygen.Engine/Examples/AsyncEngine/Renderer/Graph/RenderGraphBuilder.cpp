//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "RenderGraphBuilder.h"

#include <algorithm>

#include <Oxygen/Base/Logging.h>

#include "../Integration/GraphicsLayerIntegration.h"
#include "Cache.h"
#include "RenderGraph.h"
#include "Scheduler.h"
#include "Validator.h"

namespace oxygen::examples::asyncsim {

auto RenderGraphBuilder::Build() -> std::unique_ptr<RenderGraph>
{
  LOG_F(2,
    "[RenderGraphBuilder] Build start: passes={} resources={} views={} "
    "multiview={}",
    passes_.size(), resource_descriptors_.size(), frame_context_.views.size(),
    multi_view_enabled_);

  // Stage 0: Basic invariants
  if (passes_.empty()) {
    LOG_F(2,
      "[RenderGraphBuilder] WARNING: Builder has zero passes at build start");
  }
  // Detect conflicting view filtering configuration (mutually exclusive)
  if (iterate_all_views_
    && (restricted_view_index_.has_value() || view_filter_)) {
    LOG_F(WARNING,
      "[RenderGraphBuilder] Conflicting view configuration: IterateAllViews() "
      "combined with RestrictToView/RestrictToViews. IterateAllViews() wins; "
      "restrictions ignored.");
  }
  if (restricted_view_index_.has_value() && view_filter_) {
    LOG_F(WARNING,
      "[RenderGraphBuilder] Conflicting view configuration: both single view "
      "restriction and custom filter provided. Single view restriction takes "
      "precedence.");
  }
  size_t null_descriptor_count = 0;
  for (const auto& kv : resource_descriptors_) {
    if (!kv.second)
      ++null_descriptor_count;
  }
  if (null_descriptor_count) {
    LOG_F(WARNING,
      "[RenderGraphBuilder] Detected {} null resource descriptors before "
      "processing multi-view (total resources={})",
      null_descriptor_count, resource_descriptors_.size());
  }

  // Create the render graph
  auto render_graph = CreateAsyncEngineRenderGraph();

  // Set frame context and configuration
  render_graph->SetFrameContext(frame_context_);
  render_graph->SetMultiViewEnabled(multi_view_enabled_);

  // Attach pass cost profiler if provided
  if (pass_cost_profiler_) {
    render_graph->SetPassCostProfiler(pass_cost_profiler_);
  }

  // Process multi-view configuration and resource creation
  LOG_F(3, "[RenderGraphBuilder] Multi-view configuration phase (resources={})",
    resource_descriptors_.size());
  ProcessMultiViewConfiguration(render_graph.get());
  LOG_F(3,
    "[RenderGraphBuilder] Multi-view configuration complete (active_views={})",
    active_view_indices_.size());

  // Transfer passes to the graph with view filtering applied
  // (Phase 1) Transfer passes with view filtering before validation &
  // scheduling
  ProcessPassesWithViewFiltering(render_graph.get());
  LOG_F(3,
    "[RenderGraphBuilder] Transferred passes (graph_passes={}) prior to "
    "validation",
    render_graph->GetPassCount());
  // (Deferred) Transfer of resource descriptors now happens AFTER
  // alias/lifetime analysis so that the validator can still observe descriptors
  // locally. This avoids moved-out (nullptr) descriptors causing "unknown
  // resource" warnings.

  // Enhanced validation (Phase 2)
  LOG_F(3, "[RenderGraphBuilder] Validation start");
  auto validator = CreateAsyncEngineRenderGraphValidator();
  auto validation_result = validator->ValidateGraph(*this);
  render_graph->SetValidationResult(validation_result);
  LOG_F(3, "[RenderGraphBuilder] Validation complete (errors={})",
    validation_result.GetErrorCount());

  // Resource lifetime & alias analysis (Phase 2 partial)
  // Build alias validator data from pass resource usages.
  // NOTE: For now we only analyze explicit pass resource arrays; view-specific
  // duplication happens earlier so we use final pass set transferred to graph.
  auto alias_validator = CreateAsyncEngineResourceValidator(nullptr);
  // Add resources (descriptors still owned by builder at this stage)
  for (const auto& [handle, desc_ptr] : resource_descriptors_) {
    if (desc_ptr) {
      alias_validator->AddResource(handle, *desc_ptr);
    }
  }
  // Add usages: iterate the passes actually transferred to the graph so we
  // capture per-view cloned passes (original template per-view passes remain
  // in builder but are never executed).
  for (const auto& [ph, pass_ptr] : render_graph->GetPasses()) {
    if (!pass_ptr)
      continue;
    const auto view_index = pass_ptr->GetViewIndex();
    if (pass_ptr->GetReadResources().size()
      != pass_ptr->GetReadStates().size()) {
      ValidationError err { ValidationErrorType::InvalidConfiguration,
        "Mismatch between read resources and states for pass: "
          + pass_ptr->GetDebugName() };
      validation_result.AddError(err);
    }
    if (pass_ptr->GetWriteResources().size()
      != pass_ptr->GetWriteStates().size()) {
      ValidationError err { ValidationErrorType::InvalidConfiguration,
        "Mismatch between write resources and states for pass: "
          + pass_ptr->GetDebugName() };
      validation_result.AddError(err);
    }
    const auto& read_resources = pass_ptr->GetReadResources();
    const auto& read_states = pass_ptr->GetReadStates();
    for (size_t i = 0; i < read_resources.size(); ++i) {
      alias_validator->AddResourceUsage(
        read_resources[i], ph, read_states[i], false, view_index);
    }
    const auto& write_resources = pass_ptr->GetWriteResources();
    const auto& write_states = pass_ptr->GetWriteStates();
    for (size_t i = 0; i < write_resources.size(); ++i) {
      alias_validator->AddResourceUsage(
        write_resources[i], ph, write_states[i], true, view_index);
    }
  }
  // Defer lifetime analysis until after scheduling so we can use the
  // topological execution order instead of raw handle IDs. We first collect
  // usages (above) then, after scheduling, we provide an order mapping.
  // NOTE: We can't analyze yet; will finalize after scheduling_result is
  // known.
  // alias_validator->AnalyzeLifetimes(); (moved)
  LOG_F(4,
    "[RenderGraphBuilder] Collected resource usages for lifetime analysis "
    "(deferred)");
  // NOTE: Hazard emission deferred until after lifetime analysis
  // (post-scheduling).

  if (!validation_result.IsValid()) {
    LOG_F(ERROR, "[RenderGraphBuilder] Graph validation failed with {} errors",
      validation_result.GetErrorCount());
    for (const auto& error : validation_result.errors) {
      LOG_F(ERROR, "[RenderGraphBuilder] Validation error: {}", error.message);
    }
  }

  // Enhanced scheduling (Phase 2)
  LOG_F(3, "[RenderGraphBuilder] Scheduling start (graph_passes={})",
    render_graph->GetPassCount());
  auto scheduler = CreateAsyncEngineRenderGraphScheduler();
  auto scheduling_result = scheduler->SchedulePasses(*render_graph);
  render_graph->SetSchedulingResult(scheduling_result);
  render_graph->SetExecutionOrder(scheduling_result.execution_order);

  // (Future) If adaptive scheduling enabled, pass profiler updated costs to
  // scheduler
  // TODO(Phase2): Re-schedule using profiler updated dynamic costs when
  // adaptive enabled.

  // Optimize for multi-queue execution
  scheduler->OptimizeMultiQueue(scheduling_result);
  render_graph->SetSchedulingResult(scheduling_result);
  LOG_F(3, "[RenderGraphBuilder] Scheduling complete (execution_order={})",
    scheduling_result.execution_order.size());

  // Now that we have a definitive execution order, perform lifetime analysis
  // using the topological indices to derive precise begin/end intervals.
  if (!scheduling_result.execution_order.empty()) {
    std::unordered_map<PassHandle, uint32_t> topo_index;
    topo_index.reserve(scheduling_result.execution_order.size());
    for (uint32_t i = 0; i < scheduling_result.execution_order.size(); ++i) {
      topo_index.emplace(scheduling_result.execution_order[i], i);
    }
    alias_validator->SetTopologicalOrder(topo_index); // new API expected
    alias_validator->AnalyzeLifetimes();
    LOG_F(3,
      "[RenderGraphBuilder] Lifetime analysis complete (topological order "
      "applied)");
  } else {
    // Fallback if scheduling failed (should already have errors logged)
    alias_validator->AnalyzeLifetimes();
    LOG_F(3,
      "[RenderGraphBuilder] Lifetime analysis complete (fallback no topo "
      "order)");
  }

  // Perform hazard validation now that lifetimes are analyzed.
  {
    auto hazards = alias_validator->ValidateAliasing();
    if (!hazards.empty()) {
      for (const auto& h : hazards) {
        ValidationError err { ValidationErrorType::ResourceAliasHazard,
          std::string("Aliasing ")
            + (h.severity == AliasHazard::Severity::Error ? "error: "
                                                          : "warning: ")
            + h.description };
        err.affected_passes.insert(err.affected_passes.end(),
          h.conflicting_passes.begin(), h.conflicting_passes.end());
        if (h.severity == AliasHazard::Severity::Error) {
          validation_result.AddError(err);
        } else {
          validation_result.AddWarning(err);
        }
      }
      render_graph->SetValidationResult(validation_result);
    }

    // Log safe alias candidates (informational)
    auto candidates = alias_validator->GetAliasCandidates();
    if (!candidates.empty()) {
      LOG_F(3, "[RenderGraphBuilder] {} safe alias candidates detected",
        candidates.size());
      if (loguru::g_stderr_verbosity >= 5) {
        for (auto const& c : candidates) {
          LOG_F(5, "  Candidate: {} <-> {} (mem={} bytes) : {}",
            c.resource_a.get(), c.resource_b.get(), c.combined_memory,
            c.description.c_str());
        }
      }
    }
  }

  // Transfer resource descriptors now that analysis is complete
  LOG_F(4, "[RenderGraphBuilder] Transferring resource descriptors (count={})",
    resource_descriptors_.size());
  for (auto& [handle, desc] : resource_descriptors_) {
    if (desc && desc->HasGraphicsIntegration() && !desc->HasDescriptor()) {
      if (auto* integration = desc->GetGraphicsIntegration()) {
        auto descriptor = integration->AllocateDescriptor();
        desc->SetDescriptorIndex(descriptor.get());
        LOG_F(4,
          "[RenderGraphBuilder] Allocated descriptor {} for resource '{}'",
          descriptor.get(), desc->GetDebugName());
      }
    }
    render_graph->AddResourceDescriptor(handle, std::move(desc));
  }
  LOG_F(4,
    "[RenderGraphBuilder] Resource descriptor transfer complete "
    "(graph_resources={})",
    render_graph->GetResourceCount());

  // Generate cache key
  RenderGraphCacheKey cache_key;
  cache_key.view_count = static_cast<uint32_t>(frame_context_.views.size());
  cache_key.structure_hash
    = cache_utils::ComputeStructureHash(render_graph->GetPassHandles());
  cache_key.resource_hash
    = cache_utils::ComputeResourceHash(render_graph->GetResourceHandles());
  cache_key.viewport_hash
    = cache_utils::ComputeViewportHash(frame_context_.views);
  render_graph->SetCacheKey(cache_key);

  // Store explicit dependency graph for scheduler/hazard analysis
  // IMPORTANT: The original explicit dependency graph built from builder
  // passes contains template per-view pass handles for passes that were
  // expanded into multiple cloned passes. Those template handles are never
  // transferred to the render_graph (only the clones are), so any dependency
  // edge that still references them becomes invalid and is later dropped.
  // We rebuild the dependency graph here so that:
  //  * Each cloned per-view pass depends on the appropriate cloned variant
  //    of its original dependencies (matching by view index).
  //  * Dependencies on shared (non-expanded) passes point to the single
  //    shared pass handle actually present in the graph.
  //  * Template base per-view passes are excluded entirely.
  auto rebuilt_explicit = RebuildExplicitDependencies(render_graph.get());
  render_graph->SetExplicitDependencies(std::move(rebuilt_explicit));

  LOG_F(2,
    "[RenderGraphBuilder] Build success: execution_order={} resources={} "
    "passes={} errors={}",
    scheduling_result.execution_order.size(), render_graph->GetResourceCount(),
    render_graph->GetPassCount(),
    render_graph->GetValidationResult().GetErrorCount());

  return render_graph;
}

auto RenderGraphBuilder::ProcessMultiViewConfiguration(
  RenderGraph* /*render_graph*/) -> void
{
  LOG_F(3,
    "[RenderGraphBuilder] Processing multi-view configuration for {} views",
    frame_context_.views.size());

  if (!multi_view_enabled_ || frame_context_.views.empty()) {
    LOG_F(3, "[RenderGraphBuilder] Multi-view disabled or no views available");
    return;
  }

  // IMPORTANT: We must not mutate resource_descriptors_ while iterating it.
  // The previous implementation called CreatePerViewResources() directly
  // inside the for-range loop, which inserts new entries into the
  // unordered_map. This can trigger a rehash and invalidate the structured
  // binding references (UB / crash). To fix this we:
  //  1. Collect the original handles requiring per-view expansion first.
  //  2. Perform expansion in a second phase.
  std::vector<ResourceHandle> per_view_originals;
  per_view_originals.reserve(resource_descriptors_.size());

  for (auto const& [handle, desc] : resource_descriptors_) {
    if (!desc)
      continue;
    if (desc->GetScope() == ResourceScope::PerView) {
      per_view_originals.push_back(handle);
    }
  }

  // Second phase – safe to mutate container
  for (auto const handle : per_view_originals) {
    auto* desc = GetResourceDescriptor(handle);
    if (!desc)
      continue; // descriptor removed meanwhile (defensive)
    CreatePerViewResources(handle, *desc);
  }

  // Apply view filters to determine active views
  active_view_indices_ = DetermineActiveViews();

  LOG_F(3, "[RenderGraphBuilder] Active views: {}/{}",
    active_view_indices_.size(), frame_context_.views.size());
}

auto RenderGraphBuilder::ProcessPassesWithViewFiltering(
  RenderGraph* render_graph) -> void
{
  LOG_F(3, "[RenderGraphBuilder] Processing {} passes with view filtering",
    passes_.size());

  for (auto& [handle, pass] : passes_) {
    // Check if pass should be executed for current view configuration
    if (ShouldExecutePassForViews(*pass)) {
      // Clone pass for each active view if needed
      if (pass->GetScope() == PassScope::PerView
        && active_view_indices_.size() > 1) {
        CreatePerViewPasses(handle, pass.get(), render_graph);
      } else {
        render_graph->AddPass(handle, std::move(pass));
      }
    } else {
      LOG_F(9, "[RenderGraphBuilder] Skipping pass '{}' due to view filtering",
        pass->GetDebugName());
    }
  }
}

auto RenderGraphBuilder::CreatePerViewResources(
  ResourceHandle base_handle, const ResourceDesc& desc) -> void
{
  LOG_F(9, "[RenderGraphBuilder] Creating per-view resources for '{}'",
    desc.GetDebugName());

  for (size_t view_index = 0; view_index < frame_context_.views.size();
    ++view_index) {
    // Skip views that don't match our filters
    if (!active_view_indices_.empty()
      && std::find(
           active_view_indices_.begin(), active_view_indices_.end(), view_index)
        == active_view_indices_.end()) {
      continue;
    }

    // Create view-specific resource handle
    auto view_handle = GetNextResourceHandle();

    // Clone the resource descriptor
    std::unique_ptr<ResourceDesc> view_desc;
    if (desc.GetTypeInfo() == "TextureDesc") {
      const auto& tex_desc = static_cast<const TextureDesc&>(desc);
      view_desc = std::make_unique<TextureDesc>(tex_desc);
    } else if (desc.GetTypeInfo() == "BufferDesc") {
      const auto& buf_desc = static_cast<const BufferDesc&>(desc);
      view_desc = std::make_unique<BufferDesc>(buf_desc);
    }

    if (view_desc) {
      // Update debug name to include view index
      const auto& view = frame_context_.views[view_index];
      const auto view_suffix = view.view_name.empty()
        ? "_view" + std::to_string(view_index)
        : "_" + view.view_name;
      view_desc->SetDebugName(desc.GetDebugName() + view_suffix);
      view_desc->SetScope(ResourceScope::PerView);
      view_desc->SetLifetime(desc.GetLifetime());

      // Set graphics integration
      if (graphics_integration_) {
        view_desc->SetGraphicsIntegration(graphics_integration_);
      }

      // Store mapping from base handle to view-specific handle
      per_view_resource_mapping_[std::make_pair(base_handle, view_index)]
        = view_handle;
      resource_descriptors_[view_handle] = std::move(view_desc);

      LOG_F(9,
        "[RenderGraphBuilder] Created view-specific resource '{}' for view {}",
        resource_descriptors_[view_handle]->GetDebugName(), view_index);
    }
  }
}

auto RenderGraphBuilder::CreatePerViewPasses(PassHandle base_handle,
  RenderPass* base_pass, RenderGraph* render_graph) -> void
{
  LOG_F(9, "[RenderGraphBuilder] Creating per-view passes for '{}'",
    base_pass->GetDebugName());

  expanded_per_view_passes_.insert(base_handle);
  // NOTE: Executor propagation for per-view cloning.
  // RenderPass::Clone() intentionally does not copy the executor_ because it
  // is a move-only function. For multi-view expansion we still need each
  // cloned pass to invoke the original executor. We solve this by moving the
  // base pass executor into a shared wrapper that each clone calls. The base
  // (template) pass itself is never executed, so transferring ownership is
  // safe. All per-view clones now share the same underlying callable.
  std::shared_ptr<PassExecutor> shared_exec;
  if (base_pass->executor_) {
    shared_exec
      = std::make_shared<PassExecutor>(std::move(base_pass->executor_));
    LOG_F(4,
      "[RenderGraphBuilder] Captured base executor for '{}' into shared "
      "wrapper",
      base_pass->GetDebugName());
  } else {
    // If there was no executor on the template pass, cloned passes will also
    // have none.
    LOG_F(5,
      "[RenderGraphBuilder] No executor present on base pass '{}' (clones will "
      "be inert)",
      base_pass->GetDebugName());
  }
  for (const auto view_index : active_view_indices_) {
    // Clone the pass for this view
    auto view_pass = base_pass->Clone();
    auto view_handle = GetNextPassHandle();

    // Update debug name and view context
    const auto& view = frame_context_.views[view_index];
    const auto view_suffix = view.view_name.empty()
      ? "_view" + std::to_string(view_index)
      : "_" + view.view_name;
    view_pass->SetDebugName(base_pass->GetDebugName() + view_suffix);
    view_pass->SetViewIndex(view_index);
    view_pass->handle_ = view_handle;

    // Update resource handles to point to view-specific resources
    RemapResourceHandlesForView(view_pass.get(), view_index);

    // Assign executor wrapper to cloned pass (if original had one)
    if (shared_exec) {
      view_pass->executor_ = [shared_exec](TaskExecutionContext& ctx) {
        // Defensive: ensure callable still valid
        if (*shared_exec) {
          (*shared_exec)(ctx);
        }
      };
      LOG_F(5,
        "[RenderGraphBuilder] Assigned shared executor to clone '{}' (view={})",
        view_pass->GetDebugName(), view_index);
    } else {
      // Safety: If template had executor originally we should have captured it.
      // (This situation only occurs if executor was empty.)
    }

    render_graph->AddPass(view_handle, std::move(view_pass));
    // Record mapping from (template pass, view) to clone handle
    per_view_pass_mapping_[std::make_pair(base_handle, view_index)]
      = view_handle;

    LOG_F(9, "[RenderGraphBuilder] Created view-specific pass '{}' for view {}",
      base_pass->GetDebugName() + view_suffix, view_index);
  }
}

auto RenderGraphBuilder::RebuildExplicitDependencies(
  const RenderGraph* render_graph)
  -> std::unordered_map<PassHandle, std::vector<PassHandle>>
{
  std::unordered_map<PassHandle, std::vector<PassHandle>> remapped;
  if (!render_graph)
    return remapped;

  // Helper to look up cloned pass for (base, view)
  auto map_clone
    = [&](PassHandle base, uint32_t view_index) -> std::optional<PassHandle> {
    auto it = per_view_pass_mapping_.find(
      std::make_pair(base, static_cast<size_t>(view_index)));
    if (it != per_view_pass_mapping_.end())
      return it->second;
    return std::nullopt;
  };

  // Iterate passes that actually exist in the final graph
  for (const auto& [handle, pass_ptr] : render_graph->GetPasses()) {
    if (!pass_ptr)
      continue;
    const uint32_t view_index = static_cast<uint32_t>(pass_ptr->GetViewIndex());

    // Rebuild dependencies for this pass
    std::vector<PassHandle> deps_out;
    deps_out.reserve(pass_ptr->GetDependencies().size());
    for (auto base_dep : pass_ptr->GetDependencies()) {
      // If dependency was expanded per-view, map to matching view clone
      if (expanded_per_view_passes_.count(base_dep)) {
        if (auto mapped = map_clone(base_dep, view_index)) {
          deps_out.push_back(*mapped);
        } else {
          // Fallback: if no matching view clone (filtering), skip
          LOG_F(6,
            "[RenderGraphBuilder] Skipping dep base={} for pass={} view={} (no "
            "clone)",
            base_dep.get(), handle.get(), view_index);
        }
      } else {
        // Shared/non-expanded pass: only include if it exists in graph
        if (render_graph->GetPass(base_dep)) {
          deps_out.push_back(base_dep);
        } else {
          LOG_F(6,
            "[RenderGraphBuilder] Dropping dep base={} for pass={} (not in "
            "final graph)",
            base_dep.get(), handle.get());
        }
      }
    }
    // Deduplicate while preserving order
    std::vector<PassHandle> dedup;
    dedup.reserve(deps_out.size());
    for (auto d : deps_out) {
      if (std::find(dedup.begin(), dedup.end(), d) == dedup.end())
        dedup.push_back(d);
    }
    remapped[handle] = std::move(dedup);
  }

  if (loguru::g_stderr_verbosity >= 5) {
    size_t edge_count = 0;
    size_t expanded_count = expanded_per_view_passes_.size();
    for (auto& [p, ds] : remapped)
      edge_count += ds.size();
    LOG_F(5,
      "[RenderGraphBuilder] Rebuilt explicit dependency graph: passes={} "
      "edges={} expanded_templates={}",
      static_cast<uint32_t>(remapped.size()), static_cast<uint32_t>(edge_count),
      static_cast<uint32_t>(expanded_count));
  }
  return remapped;
}

auto RenderGraphBuilder::DetermineActiveViews() -> std::vector<size_t>
{
  std::vector<size_t> active_views;

  if (iterate_all_views_) {
    // Include all views
    for (size_t i = 0; i < frame_context_.views.size(); ++i) {
      active_views.push_back(i);
    }
  } else if (restricted_view_index_.has_value()) {
    // Include only the restricted view
    if (*restricted_view_index_ < frame_context_.views.size()) {
      active_views.push_back(*restricted_view_index_);
    }
  } else if (view_filter_) {
    // Apply custom filter
    for (size_t i = 0; i < frame_context_.views.size(); ++i) {
      if (view_filter_(frame_context_.views[i])) {
        active_views.push_back(i);
      }
    }
  } else {
    // Default: include all views
    for (size_t i = 0; i < frame_context_.views.size(); ++i) {
      active_views.push_back(i);
    }
  }

  return active_views;
}

auto RenderGraphBuilder::ShouldExecutePassForViews(const RenderPass& pass) const
  -> bool
{
  // Always execute Shared scope passes
  if (pass.GetScope() == PassScope::Shared) {
    return true;
  }

  // PerView passes are executed based on active views
  if (pass.GetScope() == PassScope::PerView) {
    return !active_view_indices_.empty();
  }

  return true;
}

auto RenderGraphBuilder::RemapResourceHandlesForView(
  RenderPass* pass, size_t view_index) -> void
{
  LOG_F(9,
    "[RenderGraphBuilder] Remapping resource handles for pass '{}' view {}",
    pass->GetDebugName(), view_index);

  // Replace read handles
  for (auto& r : pass->MutableReadResources()) {
    if (auto mapped = GetViewSpecificResourceHandle(r, view_index)) {
      r = *mapped;
    }
  }
  // Replace write handles
  for (auto& w : pass->MutableWriteResources()) {
    if (auto mapped = GetViewSpecificResourceHandle(w, view_index)) {
      w = *mapped;
    }
  }
}

auto RenderGraphBuilder::GetViewSpecificResourceHandle(
  ResourceHandle base_handle, size_t view_index) const
  -> std::optional<ResourceHandle>
{
  const auto key = std::make_pair(base_handle, view_index);
  const auto it = per_view_resource_mapping_.find(key);

  if (it != per_view_resource_mapping_.end()) {
    return it->second;
  }

  return std::nullopt;
}

auto RenderGraphBuilder::GetPasses() const -> std::vector<PassHandle>
{
  return GetPassHandles();
}

} // namespace oxygen::examples::asyncsim
