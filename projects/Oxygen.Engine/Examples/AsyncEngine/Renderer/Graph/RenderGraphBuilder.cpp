//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "RenderGraphBuilder.h"
#include "AliasLifetimeAnalysis.h"
#include "BuildPipeline.h"
#include "PerViewExpansionService.h"
#include "RenderGraphStrategies.h"
#include "SharedReadOnlyPromotionStrategy.h"

#include <algorithm>

#include <Oxygen/Base/Logging.h>

#include "../../FrameContext.h"
#include "../Integration/GraphicsLayerIntegration.h"
#include "Cache.h"
#include "RenderGraph.h"
#include "Scheduler.h"
#include "Validator.h"

namespace oxygen::examples::asyncsim {

// DiagnosticsSink implementation that forwards into ValidationResult so
// strategies can report issues in a structured way without pulling in
// logging directly.
struct ValidationDiagnosticsSink : public DiagnosticsSink {
  explicit ValidationDiagnosticsSink(ValidationResult& r)
    : result(r)
  {
  }
  void AddError(const ValidationError& err) override { result.AddError(err); }
  void AddWarning(const ValidationError& w) override { result.AddWarning(w); }
  ValidationResult& result;
};

// Simple phase: view configuration
class ViewConfigPhase final : public IBuildPhase {
public:
  PhaseResult Run(BuildContext& ctx) const override
  {
    if (!ctx.builder || !ctx.render_graph)
      return PhaseResult { std::unexpected(PhaseError { "Invalid context" }) };
    ctx.builder->RunProcessViewConfiguration(ctx.render_graph);
    return PhaseResult { std::expected<void, PhaseError> {} };
  }
};

// Simple phase: transfer passes with view filtering
class PassTransferPhase final : public IBuildPhase {
public:
  PhaseResult Run(BuildContext& ctx) const override
  {
    if (!ctx.builder || !ctx.render_graph)
      return PhaseResult { std::unexpected(PhaseError { "Invalid context" }) };
    ctx.builder->RunProcessPassesWithViewFiltering(ctx.render_graph);
    return PhaseResult { std::expected<void, PhaseError> {} };
  }
};

// Simple phase: optimize duplicated per-view resources
class SharedPromotePhase final : public IBuildPhase {
public:
  PhaseResult Run(BuildContext& ctx) const override
  {
    if (!ctx.builder || !ctx.render_graph)
      return PhaseResult { std::unexpected(PhaseError { "Invalid context" }) };
    ctx.builder->RunOptimizationStrategies(ctx.render_graph);
    return PhaseResult { std::expected<void, PhaseError> {} };
  }
};

auto RenderGraphBuilder::BeginGraph(FrameContext& context) -> void
{
  // Reset builder state for new graph
  frame_context_ = &context;

  // Configure builder based on frame context
  is_thread_safe_
    = false; // Will be set by engine if needed for parallel phases

  // Clear any previous state
  resource_descriptors_.clear();
  passes_.clear();
  surface_mappings_.clear();
  active_view_indices_.clear();
  per_view_resource_mapping_.clear();
  per_view_pass_mapping_.clear();
  expanded_per_view_passes_.clear();

  // Reset ID counters
  next_resource_id_ = 0;
  next_pass_id_ = 0;

  // Reset view configuration
  iterate_all_views_ = false;
  restricted_view_index_.reset();
  view_filter_ = nullptr;

  // Ensure default optimization strategies are present (promotion by default)
  optimization_strategies_.clear();
  optimization_strategies_.push_back(
    std::make_unique<SharedReadOnlyPromotionStrategy>());

  LOG_F(
    2, "[RenderGraphBuilder] BeginGraph: views={}", context.GetViews().size());
}

auto RenderGraphBuilder::Build() -> std::unique_ptr<RenderGraph>
{
  LOG_SCOPE_F(3, "[RenderGraphBuilder] Build");

  // Validate that BeginGraph was called
  if (!frame_context_) {
    LOG_F(ERROR,
      "[RenderGraphBuilder] Build() called without BeginGraph() - invalid "
      "state");
    return nullptr;
  }

  LOG_F(2, "[RenderGraphBuilder] Build start: passes={} resources={} views={}",
    passes_.size(), resource_descriptors_.size(),
    frame_context_->GetViews().size());

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
      "processing views (total resources={})",
      null_descriptor_count, resource_descriptors_.size());
  }

  // Create the render graph
  auto render_graph = CreateAsyncRenderGraph();

  // Attach pass cost profiler if provided
  if (pass_cost_profiler_) {
    render_graph->SetPassCostProfiler(pass_cost_profiler_);
  }

  // Run the initial build pipeline phases. Each phase may use existing
  // RenderGraphBuilder helpers to keep behavior identical while enabling
  // easier unit testing and future extension.
  BuildContext ctx;
  ctx.builder = this;
  ctx.render_graph = render_graph.get();
  ctx.frame_context = frame_context_;

  PhaseList phases;
  phases.emplace_back(std::make_unique<ViewConfigPhase>());
  phases.emplace_back(std::make_unique<PassTransferPhase>());
  phases.emplace_back(std::make_unique<SharedPromotePhase>());

  for (const auto& p : phases) {
    auto res = p->Run(ctx);
    if (!res.status) {
      LOG_F(ERROR, "[RenderGraphBuilder] Build pipeline phase failed: {}",
        res.status.error().message);
      return nullptr;
    }
  }

  LOG_F(3,
    "[RenderGraphBuilder] View configuration & pass transfer complete "
    "(active_views={})",
    active_view_indices_.size());
  // (Deferred) Transfer of resource descriptors now happens AFTER
  // alias/lifetime analysis so that the validator can still observe descriptors
  // locally. This avoids moved-out (nullptr) descriptors causing "unknown
  // resource" warnings.

  // Enhanced validation (Phase 2)
  LOG_F(3, "[RenderGraphBuilder] Validation start");
  auto validator = CreateAsyncRenderGraphValidator();
  auto validation_result = validator->ValidateGraph(*this);
  render_graph->SetValidationResult(validation_result);
  LOG_F(3, "[RenderGraphBuilder] Validation complete (errors={})",
    validation_result.GetErrorCount());

  // Resource lifetime & alias analysis (Phase 2 partial)
  // Use AliasLifetimeAnalysis wrapper to collect resources and usages. This
  // provides a clean seam for testing and future strategy injection.
  AliasLifetimeAnalysis alias_analysis;
  alias_analysis.Initialize(frame_context_->AcquireGraphics().get());
  // Add resources (descriptors still owned by builder at this stage)
  for (const auto& [handle, desc_ptr] : resource_descriptors_) {
    if (desc_ptr) {
      LOG_F(1, "[RenderGraphBuilder] Registering resource handle {} ({})",
        handle.get(), desc_ptr->GetDebugName());
      alias_analysis.AddResource(handle, *desc_ptr);
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
      LOG_F(9, "[RenderGraphBuilder] Pass {} reading resource {}", ph.get(),
        read_resources[i].get());
      alias_analysis.AddUsage(
        read_resources[i], ph, read_states[i], false, view_index);
    }
    const auto& write_resources = pass_ptr->GetWriteResources();
    const auto& write_states = pass_ptr->GetWriteStates();
    for (size_t i = 0; i < write_resources.size(); ++i) {
      LOG_F(9, "[RenderGraphBuilder] Pass {} writing resource {}", ph.get(),
        write_resources[i].get());
      alias_analysis.AddUsage(
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
  // Prefer injected scheduler if present, else create default engine scheduler
  std::unique_ptr<RenderGraphScheduler> local_scheduler;
  RenderGraphScheduler* scheduler_ptr = nullptr;
  if (scheduler_) {
    scheduler_ptr = scheduler_.get();
  } else {
    local_scheduler = CreateAsyncRenderGraphScheduler();
    scheduler_ptr = local_scheduler.get();
  }
  auto scheduling_result = scheduler_ptr->SchedulePasses(*render_graph);
  render_graph->SetSchedulingResult(scheduling_result);
  render_graph->SetExecutionOrder(scheduling_result.execution_order);

  // (Future) If adaptive scheduling enabled, pass profiler updated costs to
  // scheduler
  // TODO(Phase2): Re-schedule using profiler updated dynamic costs when
  // adaptive enabled.

  // Optimize for multi-queue execution
  scheduler_ptr->OptimizeMultiQueue(scheduling_result);
  render_graph->SetSchedulingResult(scheduling_result);
  LOG_F(3, "[RenderGraphBuilder] Scheduling complete (execution_order={})",
    scheduling_result.execution_order.size());

  // Now that we have a definitive execution order, perform lifetime analysis
  // using the topological indices to derive precise begin/end intervals.
  if (!scheduling_result.execution_order.empty()) {
    std::unordered_map<PassHandle, uint32_t> topo_index;
    topo_index.reserve(scheduling_result.execution_order.size());
    for (auto i = 0U; i < scheduling_result.execution_order.size(); ++i) {
      topo_index.emplace(
        scheduling_result.execution_order[i], static_cast<uint32_t>(i));
    }
    alias_analysis.SetTopologicalOrder(topo_index); // new API expected
    alias_analysis.AnalyzeLifetimes();
    LOG_F(3,
      "[RenderGraphBuilder] Lifetime analysis complete (topological order "
      "applied)");
  } else {
    // Fallback if scheduling failed (should already have errors logged)
    alias_analysis.AnalyzeLifetimes();
    LOG_F(3,
      "[RenderGraphBuilder] Lifetime analysis complete (fallback no topo "
      "order)");
  }

  // Perform hazard validation now that lifetimes are analyzed.
  {
    auto analysis_out = alias_analysis.ValidateAndCollect();
    if (!analysis_out.hazards.empty()) {
      for (const auto& err : analysis_out.hazards) {
        validation_result.AddError(err);
      }
      render_graph->SetValidationResult(validation_result);
    }

    // Log safe alias candidates (informational)
    if (!analysis_out.candidates.empty()) {
      LOG_F(3, "[RenderGraphBuilder] {} safe alias candidates detected",
        analysis_out.candidates.size());
      if (loguru::g_stderr_verbosity >= 5) {
        for (auto const& c : analysis_out.candidates) {
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
    if (desc && !desc->HasDescriptor()) {
      if (auto integration = frame_context_->AcquireGraphics()) {
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
  cache_key.view_count
    = static_cast<uint32_t>(frame_context_->GetViews().size());
  cache_key.structure_hash
    = cache_utils::ComputeStructureHash(render_graph->GetPassHandles());
  cache_key.resource_hash
    = cache_utils::ComputeResourceHash(render_graph->GetResourceHandles());
  cache_key.viewport_hash
    = cache_utils::ComputeViewportHash(frame_context_->GetViews());
  render_graph->SetCacheKey(cache_key);

  // Note: Render graph caching and ownership is managed by the engine-level
  // module (`RenderGraphModule`). The builder does not transfer the compiled
  // graph into the cache to avoid ambiguous ownership. If a cache is injected
  // the module is expected to call cache->Set(...) after taking ownership of
  // the compiled graph.

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

  // Reset frame context to prevent accidental reuse
  frame_context_ = nullptr;

  return render_graph;
}

auto RenderGraphBuilder::OptimizeSharedPerViewResources(
  RenderGraph* render_graph) -> void
{
  if (active_view_indices_.size() <= 1) {
    return; // Nothing to optimize (single view)
  }
  // Build reverse map: base_handle -> vector<view_index, variant_handle>
  struct VariantSet {
    std::vector<std::pair<ViewIndex, ResourceHandle>>
      variants; // (view, handle)
    const ResourceDesc* prototype { nullptr };
  };
  std::unordered_map<ResourceHandle, VariantSet> groups;

  for (auto const& [key, variant] : per_view_resource_mapping_) {
    const auto& base = key.first;
    auto view_index = key.second;
    auto* desc = GetResourceDescriptor(variant);
    if (!desc)
      continue;
    auto& set = groups[base];
    set.variants.emplace_back(view_index, variant);
    if (!set.prototype)
      set.prototype = desc;
  }

  size_t promoted_count = 0;
  size_t bytes_saved_estimate = 0;

  // Helper to test if a resource handle is written by any pass
  auto is_written = [render_graph](ResourceHandle h) {
    for (auto const& [ph, pass_ptr] : render_graph->GetPasses()) {
      (void)ph; // unused
      if (!pass_ptr)
        continue;
      for (auto const& w : pass_ptr->GetWriteResources()) {
        if (w == h)
          return true;
      }
    }
    return false;
  };

  for (auto& [base_handle, set] : groups) {
    // Skip if original base descriptor already gone or not PerView
    auto base_it = resource_descriptors_.find(base_handle);
    if (base_it == resource_descriptors_.end() || !base_it->second)
      continue;
    if (base_it->second->GetScope() != ResourceScope::PerView)
      continue;
    // Require full coverage of active views
    if (set.variants.size() != active_view_indices_.size())
      continue;
    // Check compatibility & read-only
    bool can_promote = true;
    const auto* proto = set.prototype;
    if (!proto)
      continue;
    for (auto const& [view_idx, handle] : set.variants) {
      (void)view_idx;
      auto* desc = GetResourceDescriptor(handle);
      if (!desc || !proto->IsFormatCompatibleWith(*desc)) {
        can_promote = false;
        break;
      }
      if (is_written(handle)) {
        can_promote = false;
        break;
      }
    }
    if (!can_promote)
      continue;

    // Choose first variant as the shared resource representative
    auto shared_handle = set.variants.front().second;
    auto* shared_desc = resource_descriptors_[shared_handle].get();
    if (!shared_desc)
      continue;
    shared_desc->SetScope(ResourceScope::Shared);

    // Redirect all pass reads of other variants to shared_handle
    for (auto const& [view_idx, handle] : set.variants) {
      if (handle == shared_handle)
        continue;
      for (auto& [ph, pass_ptr] : const_cast<
             std::unordered_map<PassHandle, std::unique_ptr<RenderPass>>&>(
             render_graph->GetPasses())) {
        if (!pass_ptr)
          continue;
        // Replace in read arrays
        auto& reads = pass_ptr->MutableReadResources();
        for (auto& r : reads)
          if (r == handle)
            r = shared_handle;
        // Writes should not exist (guarded), but keep defensive replacement
        auto& writes = pass_ptr->MutableWriteResources();
        for (auto& w : writes)
          if (w == handle)
            w = shared_handle;
      }
      // Erase descriptor for redundant variant and mapping entries
      resource_descriptors_.erase(handle);
    }
    // Erase base descriptor if it's distinct and unused; keep shared variant
    // only
    if (base_handle != shared_handle) {
      resource_descriptors_.erase(base_handle);
    }

    // Update per_view_resource_mapping_ so subsequent lookups yield shared
    // handle
    for (auto const& [view_idx, _h] : set.variants) {
      per_view_resource_mapping_[std::make_pair(base_handle, view_idx)]
        = shared_handle;
    }

    // Estimate memory saving: (variants-1) * prototype size heuristic
    // We do not have explicit size; approximate using compatibility hash
    // uniqueness. Provide count-based estimate only.
    bytes_saved_estimate += (set.variants.size() - 1) * 0; // unknown size
    ++promoted_count;
  }

  if (promoted_count) {
    LOG_F(3,
      "[RenderGraphBuilder] Shared resource optimization: promoted {} "
      "duplicated per-view read-only resource groups (est_saved={}B, "
      "new_resource_count={})",
      promoted_count, bytes_saved_estimate, resource_descriptors_.size());
  }
}

auto RenderGraphBuilder::ProcessViewConfiguration(RenderGraph* render_graph)
  -> void
{
  LOG_F(3, "[RenderGraphBuilder] Processing view configuration for {} views",
    frame_context_->GetViews().size());

  if (frame_context_->GetViews().empty()) {
    LOG_F(3, "[RenderGraphBuilder] No views available");
    return;
  }

  // Delegate per-view expansion responsibilities to dedicated service to
  // improve testability and separate concerns.
  PerViewExpansionService svc(*this);
  // Use the render_graph pointer if available, otherwise pass nullptr safe
  // handling in the service.
  RenderGraph* rg = render_graph;
  svc.ExpandPerViewResources(rg);

  // Apply view filters to determine active views
  active_view_indices_ = svc.DetermineActiveViews();

  // Expansion of per-view passes is handled separately in the pass-transfer
  // phase so that view configuration remains focused on resource cloning and
  // active view determination.
  LOG_F(3, "[RenderGraphBuilder] View configuration complete (passes={})",
    passes_.size());
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

  for (auto view_index = ViewIndex { 0 };
    view_index < ViewIndex(frame_context_->GetViews().size());
    (void)++view_index) {
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
      const auto& view = frame_context_->GetViews()[view_index.get()];
      const auto view_suffix = view.view_name.empty()
        ? "_view" + nostd::to_string(view_index.get())
        : "_" + view.view_name;
      view_desc->SetDebugName(desc.GetDebugName() + view_suffix);
      view_desc->SetScope(ResourceScope::PerView);
      view_desc->SetLifetime(desc.GetLifetime());

      // Store mapping from base handle to view-specific handle
      per_view_resource_mapping_[std::make_pair(base_handle, view_index)]
        = view_handle;
      resource_descriptors_[view_handle] = std::move(view_desc);

      LOG_F(1,
        "[RenderGraphBuilder] Created view-specific resource '{}' (handle {} "
        "-> {}) for view {}",
        resource_descriptors_[view_handle]->GetDebugName(), base_handle.get(),
        view_handle.get(), view_index);
    }
  }
}

auto RenderGraphBuilder::CreatePerViewPasses(PassHandle base_handle,
  RenderPass* base_pass, RenderGraph* render_graph) -> void
{
  LOG_F(1, "[RenderGraphBuilder] Creating per-view passes for '{}'",
    base_pass->GetDebugName());

  // Developer note: Ownership & ordering rationale
  // ----------------------------------------------
  // Per-view pass cloning is intentionally performed here in the builder and
  // not inside the PerViewExpansionService. Reasons:
  //  - The final RenderGraph owns the runtime containers for passes; cloning
  //    and calling RenderGraph::AddPass must happen while those containers are
  //    being populated so that ownership transfers (std::move) are safe.
  //  - Performing cloning in the service led to double-insert and
  //    use-after-move bugs where the same pass object could be moved or
  //    inserted from two places. Centralizing cloning in the builder avoids
  //    that by making this the single canonical insertion point.
  //  - The builder has immediate access to view filters, active view indices,
  //    and remapping helpers (RemapResourceHandlesForView) which are needed
  //    to produce correct per-view clones before insertion into the graph.
  //  - Keeping cloning here keeps the service focused on resource descriptor
  //    expansion and active-view determination, improving testability and
  //    separation of concerns.

  expanded_per_view_passes_.insert(base_handle);
  // NOTE: Executor propagation for per-view cloning.
  // RenderPass::Clone() intentionally does not copy the executor_ because it
  // is a move-only function. For per-view expansion we still need each
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
    const auto& view = frame_context_->GetViews()[view_index.get()];
    const auto view_suffix = view.view_name.empty()
      ? "_view" + std::to_string(view_index.get())
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
      base_pass->GetDebugName() + view_suffix, view_index.get());
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
    = [&](PassHandle base, ViewIndex view_index) -> std::optional<PassHandle> {
    auto it = per_view_pass_mapping_.find(std::make_pair(base, view_index));
    if (it != per_view_pass_mapping_.end())
      return it->second;
    return std::nullopt;
  };

  // Iterate passes that actually exist in the final graph
  for (const auto& [handle, pass_ptr] : render_graph->GetPasses()) {
    if (!pass_ptr)
      continue;
    const auto view_index = pass_ptr->GetViewIndex();

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

auto RenderGraphBuilder::DetermineActiveViews() -> std::vector<ViewIndex>
{
  std::vector<ViewIndex> active_views;

  if (iterate_all_views_) {
    // Include all views
    for (auto i = 0U; i < frame_context_->GetViews().size(); ++i) {
      active_views.push_back(ViewIndex { i });
    }
  } else if (restricted_view_index_.has_value()) {
    // Include only the restricted view
    if (*restricted_view_index_
      < ViewIndex { frame_context_->GetViews().size() }) {
      active_views.push_back(*restricted_view_index_);
    }
  } else if (view_filter_) {
    // Apply custom filter
    for (ViewIndex index = ViewIndex { 0 };
      index.get() < frame_context_->GetViews().size(); (void)++index) {
      if (view_filter_(frame_context_->GetViews()[index.get()])) {
        active_views.push_back(index);
      }
    }
  } else {
    // Default: include all views
    for (ViewIndex index = ViewIndex { 0 };
      index.get() < frame_context_->GetViews().size(); (void)++index) {
      active_views.push_back(index);
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
  RenderPass* pass, ViewIndex view_index) -> void
{
  LOG_F(1,
    "[RenderGraphBuilder] Remapping resource handles for pass '{}' view {}",
    pass->GetDebugName(), view_index);

  // Replace read handles
  for (auto& r : pass->MutableReadResources()) {
    if (auto mapped = GetViewSpecificResourceHandle(r, view_index)) {
      LOG_F(1,
        "[RenderGraphBuilder] Remapping read handle {} -> {} for view {}",
        r.get(), mapped->get(), view_index);
      r = *mapped;
    } else {
      LOG_F(1,
        "[RenderGraphBuilder] No mapping found for read handle {} view {}",
        r.get(), view_index);
    }
  }
  // Replace write handles
  for (auto& w : pass->MutableWriteResources()) {
    if (auto mapped = GetViewSpecificResourceHandle(w, view_index)) {
      LOG_F(1,
        "[RenderGraphBuilder] Remapping write handle {} -> {} for view {}",
        w.get(), mapped->get(), view_index);
      w = *mapped;
    } else {
      LOG_F(1,
        "[RenderGraphBuilder] No mapping found for write handle {} view {}",
        w.get(), view_index);
    }
  }
}

auto RenderGraphBuilder::GetViewSpecificResourceHandle(
  ResourceHandle base_handle, ViewIndex view_index) const
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

auto RenderGraphBuilder::RunBuildPipeline(BuildContext& ctx)
  -> std::expected<void, PhaseError>
{
  // Default pipeline for now mirrors Build()'s initial phases
  PhaseList phases;
  phases.emplace_back(std::make_unique<ViewConfigPhase>());
  phases.emplace_back(std::make_unique<PassTransferPhase>());
  phases.emplace_back(std::make_unique<SharedPromotePhase>());

  for (const auto& p : phases) {
    auto res = p->Run(ctx);
    if (!res.status)
      return std::unexpected(res.status.error());
  }
  return {};
}

} // namespace oxygen::examples::asyncsim

namespace oxygen::examples::asyncsim {

auto RenderGraphBuilder::RegisterOptimizationStrategy(
  std::unique_ptr<IGraphOptimization> s) -> void
{
  if (s)
    optimization_strategies_.push_back(std::move(s));
}

auto RenderGraphBuilder::ClearOptimizationStrategies() -> void
{
  optimization_strategies_.clear();
}

auto RenderGraphBuilder::RunOptimizationStrategies(RenderGraph* render_graph)
  -> void
{
  if (!render_graph)
    return;
  BuildContext ctx;
  ctx.builder = this;
  ctx.render_graph = render_graph;
  ctx.frame_context = frame_context_;

  ValidationResult tmp_result;
  ValidationDiagnosticsSink sink(tmp_result);

  for (const auto& strat : optimization_strategies_) {
    if (strat)
      strat->apply(ctx, sink);
  }
}

auto RenderGraphBuilder::RegisterScheduler(
  std::unique_ptr<RenderGraphScheduler> s) -> void
{
  scheduler_ = std::move(s);
}

auto RenderGraphBuilder::RegisterRenderGraphCache(
  std::unique_ptr<RenderGraphCache> c) -> void
{
  render_graph_cache_ = std::move(c);
}

} // namespace oxygen::examples::asyncsim
