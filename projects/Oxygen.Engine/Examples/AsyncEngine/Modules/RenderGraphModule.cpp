//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "RenderGraphModule.h"

#include <chrono>

#include <Oxygen/Base/Logging.h>

#include "../Renderer/Graph/Validator.h"

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

  // Initialize render graph builder with graphics layer integration
  // Note: Full integration will be implemented in subsequent tasks
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
  is_initialized_ = false;

  LOG_F(INFO, "[RenderGraph] Render graph module shutdown complete");
  co_return;
}

auto RenderGraphModule::OnFrameGraph(ModuleContext& context) -> co::Co<>
{
  const auto frame_start = std::chrono::high_resolution_clock::now();

  LOG_F(2, "[RenderGraph] Building render graph for frame {}",
    context.GetFrameIndex());

  // Register this module with the context so other modules can access the
  // builder
  context.SetRenderGraphModule(this);

  // Check if this is a new frame
  if (context.GetFrameIndex() != current_frame_index_) {
    current_frame_index_ = context.GetFrameIndex();
    ResetBuilderForNewFrame(context);
  }

  // Set frame context for multi-view rendering
  FrameContext frame_context;
  frame_context.frame_index = context.GetFrameIndex();
  // TODO: Populate frame context with actual view data from ModuleContext
  // This will be implemented when ModuleContext is enhanced

  render_graph_builder_.SetFrameContext(frame_context);

  // Enable multi-view rendering if multiple surfaces are available
  // TODO: Check actual surface count from ModuleContext
  render_graph_builder_.EnableMultiViewRendering(true);

  LOG_F(
    2, "[RenderGraph] Render graph builder prepared for module contributions");

  // Note: Other modules will contribute to the render graph during their
  // OnFrameGraph calls. The ModuleManager will handle the ordering.
  // After all modules have contributed, we'll compile the graph in a
  // post-processing step (to be implemented when ModuleContext is enhanced).

  const auto frame_end = std::chrono::high_resolution_clock::now();
  const auto build_time = std::chrono::duration_cast<std::chrono::microseconds>(
    frame_end - frame_start);

  LOG_F(2, "[RenderGraph] Frame graph preparation complete ({} μs)",
    build_time.count());

  co_return;
}

auto RenderGraphModule::OnResourceTransitions(ModuleContext& context)
  -> co::Co<>
{
  if (!render_graph_) {
    LOG_F(WARNING,
      "[RenderGraph] No render graph available for resource transitions");
    co_return;
  }

  LOG_F(2, "[RenderGraph] Planning resource transitions for frame {}",
    context.GetFrameIndex());

  // TODO: Implement actual resource transition planning
  // This will coordinate with GraphicsLayer systems when integration is
  // complete

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

  // TODO: Implement actual render graph execution
  // This will coordinate with GraphicsLayer command recording when integration
  // is complete

  LOG_F(2, "[RenderGraph] Render graph execution complete");
  co_return;
}

auto RenderGraphModule::ResetBuilderForNewFrame(const ModuleContext& context)
  -> void
{
  LOG_F(
    3, "[RenderGraph] Resetting builder for frame {}", context.GetFrameIndex());

  // Reset the builder for the new frame
  render_graph_builder_ = RenderGraphBuilder {};
  render_graph_.reset();

  // Clear previous frame stats
  last_frame_stats_ = FrameStats {};
}

auto RenderGraphModule::ValidateRenderGraph(const ModuleContext& context)
  -> bool
{
  const auto validation_start = std::chrono::high_resolution_clock::now();

  LOG_F(2, "[RenderGraph] Validating render graph for frame {}",
    context.GetFrameIndex());

  // TODO: Implement actual validation using RenderGraphValidator
  // For now, always return true for stub implementation

  const auto validation_end = std::chrono::high_resolution_clock::now();
  const auto validation_time
    = std::chrono::duration_cast<std::chrono::microseconds>(
      validation_end - validation_start);

  LOG_F(
    2, "[RenderGraph] Validation complete ({} μs)", validation_time.count());

  return true;
}

auto RenderGraphModule::UpdateFrameStats(std::chrono::microseconds build_time,
  std::chrono::microseconds validation_time) -> void
{
  last_frame_stats_.build_time = build_time;
  last_frame_stats_.validation_time = validation_time;
  // TODO: Update pass_count and resource_count when render graph is compiled
}

} // namespace oxygen::examples::asyncsim
