//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "RenderGraphModule.h"

#include <chrono>

#include <Oxygen/Base/Logging.h>

#include "../Renderer/Graph/RenderGraph.h"
#include "../Renderer/Graph/RenderGraphBuilder.h"
#include "../Renderer/Graph/Validator.h"
#include "../Renderer/Integration/GraphicsLayerIntegration.h"
#include <Oxygen/Engine/FrameContext.h>

namespace oxygen::engine::asyncsim {

RenderGraphModule::RenderGraphModule()
  : EngineModuleBase("RenderGraph",
      ModulePhases::FrameGraph | ModulePhases::CommandRecord,
      ModulePriority { ModulePriorities::Low.get()
        - 1 }) // Runs after all content modules have contributed
{
}

auto RenderGraphModule::Initialize(AsyncEngineSimulator& engine) -> co::Co<>
{
  // Store engine reference for later use
  engine_ = observer_ptr { &engine };

  LOG_F(INFO, "[RenderGraph] Initializing render graph module");

  // Create render graph cache using engine factory
  render_graph_cache_ = CreateAsyncRenderGraphCache();

  is_initialized_ = true;
  current_frame_index_ = 0;

  LOG_F(INFO, "[RenderGraph] Render graph module initialized successfully");
  co_return;
}

auto RenderGraphModule::Shutdown() -> co::Co<>
{
  LOG_F(INFO, "Shutting down...");

  // Periodically log cache stats from the module (every 5 frames).
  // Use the frame index from FrameContext so logging aligns with engine
  // frame numbering.
  if (render_graph_cache_) {
    render_graph_cache_->LogStats();
  }

  // Clean up render graph resources
  render_graph_.reset();
  is_initialized_ = false;

  co_return;
}

auto RenderGraphModule::OnFrameGraph(FrameContext& context) -> co::Co<>
{
  LOG_F(2, "[RenderGraph] OnFrameGraph for frame {}", context.GetFrameIndex());

  // Update frame tracking
  current_frame_index_ = context.GetFrameIndex();

  // Views should already be created by GameModule
  LOG_F(
    2, "[RenderGraph] Frame context has {} views", context.GetViews().size());

  // Get the render graph builder from the frame context (set by
  // AsyncEngineSimulator)
  auto builder = context.GetRenderGraphBuilder();
  if (!builder) {
    LOG_F(WARNING,
      "[RenderGraph] No render graph builder available in frame context");
    co_return;
  }

  // Configure the builder with graphics integration
  // Note: The graphics integration should be set by the engine, but the module
  // can provide additional configuration here if needed

  LOG_F(2, "[RenderGraph] Using render graph builder from frame context");

  // Other modules will also access the builder via
  // context.GetRenderGraphBuilder() and add their passes and resources. This
  // module's primary role is to compile the final graph after all contributions
  // are made.

  // Wait for other modules to contribute (this is where the deferred compile
  // pattern helps)
  co_await WaitForModuleContributions(context);

  // Now compile the render graph with all contributions
  co_await CompileRenderGraph(context);

  // Plan resource transitions for the compiled render graph
  if (render_graph_) {
    co_await PlanResourceTransitions(context);
  }

  co_return;
}

auto RenderGraphModule::PlanResourceTransitions(FrameContext& context)
  -> co::Co<>
{
  LOG_F(2, "[RenderGraph] Planning resource transitions for frame {}",
    context.GetFrameIndex());

  if (!render_graph_) {
    LOG_F(WARNING,
      "[RenderGraph] No render graph available for resource transition "
      "planning");
    co_return;
  }

  co_await render_graph_->PlanResourceTransitions(context);
  LOG_F(2, "[RenderGraph] Resource transitions planned");
  co_return;
}

auto RenderGraphModule::OnCommandRecord(FrameContext& context) -> co::Co<>
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

auto RenderGraphModule::GetLastFrameStats() const -> const FrameStatistics&
{
  return last_frame_stats_;
}

auto RenderGraphModule::CreateViewInfosFromSurfaces(FrameContext& frame_context)
  -> void
{
  auto surfaces = frame_context.GetSurfaces();
  LOG_F(2, "[RenderGraph] Found {} surfaces", surfaces.size());

  if (surfaces.empty()) {
    // Fallback single view
    ViewInfo default_view;
    default_view.view_name = "DefaultView";
    // Create a default surface handle
    default_view.surface = ViewInfo::SurfaceHandle { std::make_shared<int>(0) };

    // Add the default view
    frame_context.AddView(default_view);
    LOG_F(2, "[RenderGraph] Created default view (no surfaces available)");
  } else {
    std::vector<ViewInfo> views;
    views.reserve(surfaces.size());

    for (size_t view_index = 0; view_index < surfaces.size(); ++view_index) {
      const auto& surface = surfaces[view_index];
      ViewInfo view;
      view.view_name = "View_" + std::to_string(view_index);

      // Create surface handle from the render surface
      view.surface
        = ViewInfo::SurfaceHandle { std::make_shared<RenderSurface>(surface) };

      views.push_back(view);
    }

    // Set all views at once
    frame_context.SetViews(std::move(views));
    LOG_F(2, "[RenderGraph] Created {} views from surfaces", surfaces.size());

    // Debug: Verify views were set correctly
    LOG_F(2, "[RenderGraph] Verification: frame context now has {} views",
      frame_context.GetViews().size());
  }

  LOG_F(2, "[RenderGraph] Created {} view contexts (views={})",
    frame_context.GetViews().size(), frame_context.GetViews().size() > 1);
}

auto RenderGraphModule::WaitForModuleContributions(FrameContext& context)
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

auto RenderGraphModule::CompileRenderGraph(FrameContext& context) -> co::Co<>
{
  LOG_F(2, "[RenderGraph] Compiling render graph");

  // Get the render graph builder from the frame context
  auto builder = context.GetRenderGraphBuilder();
  if (!builder) {
    LOG_F(ERROR,
      "[RenderGraph] No render graph builder available in frame context");
    co_return;
  }

  try {
    // Build cache key first (structure/resources/views) using builder data
    RenderGraphCacheKey key;
    key.view_count = static_cast<uint32_t>(context.GetViews().size());
    key.structure_hash
      = cache_utils::ComputeStructureHash(builder->GetPassHandles());
    key.resource_hash
      = cache_utils::ComputeResourceHash(builder->GetResourceHandles());
    key.viewport_hash = cache_utils::ComputeViewportHash(context.GetViews());

    // Diagnostic: log builder pass/resource counts before cache lookup
    LOG_F(3,
      "[RenderGraph] Builder pre-cache state: passes={} resources={} views={} "
      "(structure_hash={:08x})",
      builder->GetPassHandles().size(), builder->GetResourceHandles().size(),
      key.view_count, key.structure_hash);

    // If no passes yet, skip cache lookup and defer (expect later phase to add)
    if (builder->GetPassHandles().empty()) {
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
        // TODO: Get target FPS from engine configuration when available
        const uint32_t target_fps = 60; // Default target FPS
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
    auto built = builder->Build();
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
    // TODO: Get target FPS from engine configuration when available
    const uint32_t target_fps = 60; // Default target FPS
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

} // namespace oxygen::engine::asyncsim
