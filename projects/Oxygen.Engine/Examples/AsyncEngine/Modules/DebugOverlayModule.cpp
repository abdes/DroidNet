//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "DebugOverlayModule.h"
#include "../AsyncEngineSimulator.h"
#include "../Renderer/Integration/GraphicsLayerIntegration.h"

#include <Oxygen/OxCo/ThreadPool.h>

namespace oxygen::engine::asyncsim {

DebugOverlayModule::DebugOverlayModule()
  : EngineModuleBase("DebugOverlay",
      ModulePhases::ParallelWork | ModulePhases::CommandRecord
        | ModulePhases::DetachedWork | ModulePhases::FrameGraph,
      ModulePriorities::Low)
{
}

auto DebugOverlayModule::Initialize(AsyncEngineSimulator& engine) -> co::Co<>
{
  // Store engine reference for later use
  engine_ = observer_ptr { &engine };

  LOG_F(INFO, "[Debug] Initializing debug overlay");

  // Initialize debug rendering resources
  auto gfx = engine.GetGraphics().lock();
  debug_font_handle_ = gfx->RegisterResource("DebugFont");
  debug_line_buffer_handle_ = gfx->RegisterResource("DebugLineBuffer");

  enabled_ = true; // Enable debug overlay by default

  LOG_F(INFO, "[Debug] Debug overlay initialized (font={}, lines={})",
    debug_font_handle_.get(), debug_line_buffer_handle_.get());
  co_return;
}

auto DebugOverlayModule::OnParallelWork(FrameContext& context) -> co::Co<>
{
  if (!enabled_)
    co_return;

  LOG_F(3, "[Debug] Parallel debug work for frame {}", context.GetFrameIndex());

  // Build debug visualization data in parallel
  co_await context.GetThreadPool()->Run([this](auto /*cancel_token*/) {
    std::this_thread::sleep_for(50us); // Minimal debug processing

    // Update debug statistics
    debug_lines_count_ = 42; // Simulate some debug geometry
    debug_text_items_ = 8; // Simulate debug text elements
  });

  co_return;
}

auto DebugOverlayModule::OnFrameGraph(FrameContext& context) -> co::Co<>
{
  LOG_F(3, "[Debug] Contributing debug overlay to render graph for frame {}",
    context.GetFrameIndex());

  // Only contribute debug passes if the overlay is enabled
  if (enabled_) {
    if (auto builder = context.GetRenderGraphBuilder()) {
      LOG_F(3, "[Debug] Adding debug overlay render passes");

      // Add debug overlay pass that renders over all views
      auto debugHandle
        = builder->AddRasterPass("DebugOverlay", [this](PassBuilder& pass) {
            pass.SetScope(PassScope::PerView)
              .SetPriority(
                Priority::Low) // Low priority - render after main content
              .IterateAllViews()
              .SetExecutor([this](TaskExecutionContext& /*exec*/) {
                LOG_F(4, "[Debug] Executing debug overlay render pass");

                // Render debug information (frame stats, performance metrics,
                // etc.) In a real implementation, this would render debug text
                // and visualizations
                std::this_thread::sleep_for(
                  25us); // Simulate lightweight debug rendering
              });
          });

      // Add debug lines/wireframes pass for development visualization
      auto debugLinesHandle
        = builder->AddRasterPass("DebugLines", [this](PassBuilder& pass) {
            pass.SetScope(PassScope::PerView)
              .SetPriority(Priority::Low)
              .IterateAllViews()
              .SetExecutor([this](TaskExecutionContext& /*exec*/) {
                LOG_F(4, "[Debug] Executing debug lines render pass");

                // Render debug lines, wireframes, collision volumes, etc.
                std::this_thread::sleep_for(15us);
              });
          });

      LOG_F(3, "[Debug] Debug overlay render graph contribution complete");
    } else {
      LOG_F(WARNING,
        "[Debug] No render graph builder available - using legacy debug "
        "rendering");
    }
  } else {
    LOG_F(
      4, "[Debug] Debug overlay disabled - skipping render graph contribution");
  }

  co_return;
}

auto DebugOverlayModule::OnCommandRecord(FrameContext& context) -> co::Co<>
{
  if (!enabled_)
    co_return;

  LOG_F(3, "[Debug] Recording debug commands for frame {}",
    context.GetFrameIndex());

  // Record debug rendering commands
  co_await context.GetThreadPool()->Run([this](auto /*cancel_token*/) {
    std::this_thread::sleep_for(30us); // Minimal command recording

    debug_commands_recorded_ = true;
  });

  co_return;
}

auto DebugOverlayModule::OnDetachedWork(FrameContext& context) -> co::Co<>
{
  if (!enabled_)
    co_return;

  // Background debug work (profiling data collection, etc.)
  co_await context.GetThreadPool()->Run([this](auto /*cancel_token*/) {
    std::this_thread::sleep_for(10us); // Minimal background work

    // Collect profiling data, update debug statistics
    background_updates_++;
  });

  co_return;
}

auto DebugOverlayModule::Shutdown() -> co::Co<>
{
  LOG_F(INFO, "Shutting down");
  {
    LOG_SCOPE_F(2, "Resources");
    // Clean up debug resources using stored engine reference
    if (engine_) {
      auto gfz = engine_->GetGraphics().lock();
      gfz->ScheduleResourceReclaim(debug_font_handle_, 0, "DebugFont");
      gfz->ScheduleResourceReclaim(
        debug_line_buffer_handle_, 0, "DebugLineBuffer");
    }
  }
  {
    LOG_SCOPE_F(3, "Statistics");
    LOG_F(3, "frames presented   : {}", debug_frames_presented_);
    LOG_F(3, "background updates : {}", background_updates_);
  }
  co_return;
}

} // namespace oxygen::engine::asyncsim
