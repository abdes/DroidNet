//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "RenderGraphModule.h"

#include <chrono>

#include <Oxygen/Base/Logging.h>

#include "../Renderer/Graph/RenderGraph.h"
#include "../Renderer/Graph/Validator.h"
#include "../Renderer/Integration/GraphicsLayerIntegration.h"

namespace oxygen::examples::asyncsim {

RenderGraphModule::RenderGraphModule()
  : EngineModuleBase("RenderGraph",
      ModulePhases::FrameGraph | ModulePhases::ResourceTransitions
        | ModulePhases::CommandRecord,
      ModulePriorities::High)
{
}

auto RenderGraphModule::Initialize(ModuleContext& context) -> co::Co<>
{
  LOG_F(INFO, "[RenderGraph] Initializing render graph module");

  // Create graphics layer integration
  graphics_integration_
    = std::make_unique<GraphicsLayerIntegration>(context.GetGraphics());

  // Create render graph cache
  render_graph_cache_ = std::make_unique<RenderGraphCache>();

  // Attach a pass cost profiler to builder (basic implementation)
  render_graph_builder_.SetPassCostProfiler(
    std::make_shared<PassCostProfiler>());

  // Initialize render graph builder with graphics layer integration
  ResetBuilderForNewFrame(context);

  is_initialized_ = true;
  current_frame_index_ = 0;

  LOG_F(INFO, "[RenderGraph] Render graph module initialized successfully");
  co_return;
}

auto RenderGraphModule::Shutdown(ModuleContext& context) -> co::Co<>
{
  LOG_F(INFO, "[RenderGraph] Shutting down render graph module");

  // Clean up render graph resources
  render_graph_.reset();
  graphics_integration_.reset();
  is_initialized_ = false;

  LOG_F(INFO, "[RenderGraph] Render graph module shutdown complete");
  co_return;
}

auto RenderGraphModule::OnFrameGraph(ModuleContext& context) -> co::Co<>
{
  // Preparation only; actual compile deferred to ResourceTransitions phase
  LOG_F(2, "[RenderGraph] Preparing builder for frame {} (deferred compile)",
    context.GetFrameIndex());

  context.SetRenderGraphModule(this);

  if (context.GetFrameIndex() != current_frame_index_) {
    current_frame_index_ = context.GetFrameIndex();
    ResetBuilderForNewFrame(context);
  }

  FrameContext frame_context;
  frame_context.frame_index = context.GetFrameIndex();
  CreateViewContextsFromSurfaces(frame_context, context);
  render_graph_builder_.SetFrameContext(frame_context);
  render_graph_builder_.EnableMultiViewRendering(
    frame_context.views.size() > 1);

  LOG_F(2,
    "[RenderGraph] Builder ready (views={}) waiting for other modules' "
    "contributions",
    frame_context.views.size());
  co_return;
}

auto RenderGraphModule::OnResourceTransitions(ModuleContext& context)
  -> co::Co<>
{
  // If graph not yet compiled this frame, compile now after all FrameGraph
  // contributions
  if (!render_graph_) {
    const auto start = std::chrono::high_resolution_clock::now();
    LOG_F(2,
      "[RenderGraph] Deferred compile at ResourceTransitions for frame {}",
      context.GetFrameIndex());
    co_await CompileRenderGraph(context);
    const auto end = std::chrono::high_resolution_clock::now();
    last_frame_stats_.build_time
      = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    LOG_F(2, "[RenderGraph] Deferred compile complete ({} μs)",
      last_frame_stats_.build_time.count());
  }
  if (!render_graph_) {
    LOG_F(ERROR,
      "[RenderGraph] Cannot plan transitions - graph missing after deferred "
      "compile");
    co_return;
  }
  LOG_F(2, "[RenderGraph] Planning resource transitions for frame {}",
    context.GetFrameIndex());
  co_await render_graph_->PlanResourceTransitions(context);
  LOG_F(2, "[RenderGraph] Resource transitions planned");
  co_return;
}

auto RenderGraphModule::OnCommandRecord(ModuleContext& context) -> co::Co<>
{
  if (!render_graph_) {
    LOG_F(
      WARNING, "[RenderGraph] No render graph available for command recording");
    co_return;
  }

  LOG_F(2, "[RenderGraph] Executing render graph for frame {}",
    context.GetFrameIndex());

  // Execute the render graph with full pipeline
  co_await render_graph_->Execute(context);

  LOG_F(2, "[RenderGraph] Render graph execution complete");
  co_return;
}

auto RenderGraphModule::GetBuilder() -> RenderGraphBuilder&
{
  return render_graph_builder_;
}

auto RenderGraphModule::GetLastFrameStats() const -> const FrameStatistics&
{
  return last_frame_stats_;
}

auto RenderGraphModule::ResetBuilderForNewFrame(ModuleContext& context) -> void
{
  LOG_F(2, "[RenderGraph] Resetting render graph builder for new frame");

  // Create new builder instance for clean state
  render_graph_builder_ = RenderGraphBuilder {};

  // Initialize builder with graphics layer integration
  if (graphics_integration_) {
    render_graph_builder_.SetGraphicsIntegration(graphics_integration_.get());
  }
}

auto RenderGraphModule::CreateViewContextsFromSurfaces(
  FrameContext& frame_context, ModuleContext& module_context) -> void
{
  const auto* surfaces = module_context.GetSurfaces();
  if (!surfaces || surfaces->empty()) {
    // Fallback single view
    ViewContext default_view;
    default_view.view_id = ViewId { 0 };
    default_view.surface_index = 0;
    if (graphics_integration_) {
      const auto& graphics = module_context.GetGraphics();
      const auto vp = graphics.GetDefaultViewport();
      default_view.viewport
        = { vp.x, vp.y, vp.width, vp.height, vp.min_depth, vp.max_depth };
    } else {
      default_view.viewport = { 0.0f, 0.0f, 1920.0f, 1080.0f, 0.0f, 1.0f };
    }
    frame_context.views.push_back(default_view);
  } else {
    frame_context.views.reserve(surfaces->size());
    uint32_t view_index = 0;
    for (const auto& surface : *surfaces) {
      ViewContext view;
      view.view_id = ViewId { view_index };
      view.surface_index = view_index;
      // Attempt to fetch a readable name and dimensions from the surface
      // object. Using generic members (name / width / height) with fallbacks if
      // unavailable.
      if constexpr (requires { surface.name; }) {
        view.view_name = surface.name;
      }
      // RenderSurface currently has no width/height fields; use placeholder
      // defaults (future: query GraphicsLayer / swapchain for actual size).
      float width = 1920.0f;
      float height = 1080.0f;
      view.viewport = { 0.0f, 0.0f, width, height, 0.0f, 1.0f };
      frame_context.views.push_back(view);
      ++view_index;
    }
  }

  LOG_F(2, "[RenderGraph] Created {} view contexts (multi-view={})",
    frame_context.views.size(), frame_context.views.size() > 1);
}

auto RenderGraphModule::WaitForModuleContributions(ModuleContext& context)
  -> co::Co<>
{
  // This is where we would coordinate with other modules
  // For now, we assume all contributions have been made during the FrameGraph
  // phase
  // TODO: Implement proper module coordination when ModuleManager is enhanced

  LOG_F(2, "[RenderGraph] Waiting for module contributions to complete");

  // Simulate module coordination delay
  co_await std::suspend_never {};

  LOG_F(2, "[RenderGraph] All module contributions received");
  co_return;
}

auto RenderGraphModule::CompileRenderGraph(ModuleContext& context) -> co::Co<>
{
  LOG_F(2, "[RenderGraph] Compiling render graph");

  try {
    // Build cache key first (structure/resources/views) using builder data
    RenderGraphCacheKey key;
    key.view_count = static_cast<uint32_t>(
      render_graph_builder_.GetFrameContext().views.size());
    key.structure_hash = cache_utils::ComputeStructureHash(
      render_graph_builder_.GetPassHandles());
    key.resource_hash = cache_utils::ComputeResourceHash(
      render_graph_builder_.GetResourceHandles());
    key.viewport_hash = cache_utils::ComputeViewportHash(
      render_graph_builder_.GetFrameContext().views);

    // Diagnostic: log builder pass/resource counts before cache lookup
    LOG_F(3,
      "[RenderGraph] Builder pre-cache state: passes={} resources={} views={} "
      "(structure_hash={:08x})",
      render_graph_builder_.GetPassHandles().size(),
      render_graph_builder_.GetResourceHandles().size(), key.view_count,
      key.structure_hash);

    // If no passes yet, skip cache lookup and defer (expect later phase to add)
    if (render_graph_builder_.GetPassHandles().empty()) {
      LOG_F(
        2, "[RenderGraph] Compile skipped: zero passes (will attempt later)");
      co_return;
    }
    // Try cache lookup when we have passes
    if (render_graph_cache_) {
      if (auto cached = render_graph_cache_->Get(key)) {
        render_graph_ = cached;
        last_frame_stats_.pass_count = render_graph_->GetPassCount();
        last_frame_stats_.resource_count = render_graph_->GetResourceCount();
        LOG_F(2,
          "[RenderGraph] Reused cached render graph (passes={}, resources={})",
          last_frame_stats_.pass_count, last_frame_stats_.resource_count);
        // Frame budget check (cache hit path)
        const auto& sched = render_graph_->GetSchedulingResult();
        const auto target_fps = context.GetEngineProps().target_fps;
        if (target_fps > 0 && sched.estimated_frame_time_ms > 0.0f) {
          const float budget_ms = 1000.0f / static_cast<float>(target_fps);
          if (sched.estimated_frame_time_ms > budget_ms) {
            LOG_F(WARNING,
              "[RenderGraph] Estimated frame time {:.3f} ms exceeds budget "
              "{:.3f} ms (fps={})",
              sched.estimated_frame_time_ms, budget_ms, target_fps);
          } else {
            LOG_F(3,
              "[RenderGraph] Estimated frame time {:.3f} ms within budget "
              "{:.3f} ms (fps={})",
              sched.estimated_frame_time_ms, budget_ms, target_fps);
          }
        }
        co_return;
      }
    }

    // Build and validate the render graph (validator created internally) when
    // cache miss
    auto built = render_graph_builder_.Build();
    render_graph_ = std::shared_ptr<RenderGraph>(std::move(built));

    if (!render_graph_) {
      LOG_F(ERROR, "[RenderGraph] Failed to build render graph");
      co_return;
    }

    LOG_F(INFO,
      "[RenderGraph] Render graph compiled successfully with {} passes",
      render_graph_->GetPassCount());

    // Update frame statistics
    last_frame_stats_.pass_count = render_graph_->GetPassCount();
    last_frame_stats_.resource_count = render_graph_->GetResourceCount();

    // Store in cache (scheduling result already embedded in graph)
    if (render_graph_cache_) {
      render_graph_cache_->Set(
        key, render_graph_, render_graph_->GetSchedulingResult());
    }

    // Frame budget check (cache miss path)
    const auto& sched = render_graph_->GetSchedulingResult();
    const auto target_fps = context.GetEngineProps().target_fps;
    if (target_fps > 0 && sched.estimated_frame_time_ms > 0.0f) {
      const float budget_ms = 1000.0f / static_cast<float>(target_fps);
      if (sched.estimated_frame_time_ms > budget_ms) {
        LOG_F(WARNING,
          "[RenderGraph] Estimated frame time {:.3f} ms exceeds budget {:.3f} "
          "ms (fps={})",
          sched.estimated_frame_time_ms, budget_ms, target_fps);
      } else {
        LOG_F(3,
          "[RenderGraph] Estimated frame time {:.3f} ms within budget {:.3f} "
          "ms (fps={})",
          sched.estimated_frame_time_ms, budget_ms, target_fps);
      }
    }

  } catch (const std::exception& e) {
    LOG_F(ERROR, "[RenderGraph] Failed to compile render graph: {}", e.what());
    render_graph_.reset();
  }

  co_return;
}

} // namespace oxygen::examples::asyncsim
