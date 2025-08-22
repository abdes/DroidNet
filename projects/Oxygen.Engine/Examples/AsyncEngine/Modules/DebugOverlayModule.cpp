//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "DebugOverlayModule.h"

namespace oxygen::examples::asyncsim {

DebugOverlayModule::DebugOverlayModule()
  : EngineModuleBase("DebugOverlay",
      ModulePhases::SnapshotBuild | ModulePhases::ParallelWork
        | ModulePhases::CommandRecord | ModulePhases::Present
        | ModulePhases::DetachedWork | ModulePhases::FrameGraph,
      ModulePriorities::Low)
{
}

auto DebugOverlayModule::Initialize(ModuleContext& context) -> co::Co<>
{
  LOG_F(INFO, "[Debug] Initializing debug overlay");

  // Initialize debug rendering resources
  auto& registry = context.GetGraphics().GetResourceRegistry();
  debug_font_handle_ = registry.RegisterResource("DebugFont");
  debug_line_buffer_handle_ = registry.RegisterResource("DebugLineBuffer");

  enabled_ = true; // Enable debug overlay by default

  LOG_F(INFO, "[Debug] Debug overlay initialized (font={}, lines={})",
    debug_font_handle_, debug_line_buffer_handle_);
  co_return;
}

auto DebugOverlayModule::OnSnapshotBuild(ModuleContext& context) -> co::Co<>
{
  if (!enabled_)
    co_return;

  LOG_F(
    3, "[Debug] Building debug snapshot for frame {}", context.GetFrameIndex());

  // Capture frame statistics for debug display
  frame_stats_.frame_index = context.GetFrameIndex();
  frame_stats_.frame_time = context.GetFrameTiming().frame_duration;
  frame_stats_.cpu_usage = context.GetFrameTiming().cpu_usage_percent;
  frame_stats_.gpu_usage = context.GetFrameTiming().gpu_usage_percent;

  co_return;
}

auto DebugOverlayModule::OnParallelWork(ModuleContext& context) -> co::Co<>
{
  if (!enabled_)
    co_return;

  LOG_F(3, "[Debug] Parallel debug work for frame {}", context.GetFrameIndex());

  // Build debug visualization data in parallel
  co_await context.GetThreadPool().Run([this](auto cancel_token) {
    std::this_thread::sleep_for(50us); // Minimal debug processing

    // Update debug statistics
    debug_lines_count_ = 42; // Simulate some debug geometry
    debug_text_items_ = 8; // Simulate debug text elements
  });

  co_return;
}

auto DebugOverlayModule::OnFrameGraph(ModuleContext& context) -> co::Co<>
{
  LOG_F(3, "[Debug] Contributing debug overlay to render graph for frame {}",
    context.GetFrameIndex());

  // Only contribute debug passes if the overlay is enabled
  if (enabled_) {
    if (auto* builder = context.GetRenderGraphBuilder()) {
      LOG_F(3, "[Debug] Adding debug overlay render passes");

      // Add debug overlay pass that renders over all views
      auto debugHandle
        = builder->AddRasterPass("DebugOverlay", [this](PassBuilder& pass) {
            pass.SetScope(PassScope::PerView)
              .SetPriority(
                Priority::Low) // Low priority - render after main content
              .IterateAllViews()
              .SetExecutor([this](TaskExecutionContext& exec) {
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
              .SetExecutor([this](TaskExecutionContext& exec) {
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

auto DebugOverlayModule::OnCommandRecord(ModuleContext& context) -> co::Co<>
{
  if (!enabled_)
    co_return;

  LOG_F(3, "[Debug] Recording debug commands for frame {}",
    context.GetFrameIndex());

  // Record debug rendering commands
  co_await context.GetThreadPool().Run([this](auto cancel_token) {
    std::this_thread::sleep_for(30us); // Minimal command recording

    debug_commands_recorded_ = true;
  });

  co_return;
}

auto DebugOverlayModule::OnPresent(ModuleContext& context) -> co::Co<>
{
  if (!enabled_)
    co_return;

  LOG_F(
    3, "[Debug] Debug overlay present for frame {}", context.GetFrameIndex());

  // Present debug overlay (minimal cost)
  debug_frames_presented_++;

  // Log periodic debug info
  if (context.GetFrameIndex() % 60 == 0) { // Every ~1 second at 60fps
    LOG_F(INFO, "[Debug] Frame {}: {:.1f}ms, CPU {:.1f}%, GPU {:.1f}%",
      frame_stats_.frame_index, frame_stats_.frame_time.count() / 1000.0f,
      frame_stats_.cpu_usage, frame_stats_.gpu_usage);
  }

  co_return;
}

auto DebugOverlayModule::OnDetachedWork(ModuleContext& context) -> co::Co<>
{
  if (!enabled_)
    co_return;

  // Background debug work (profiling data collection, etc.)
  co_await context.GetThreadPool().Run([this](auto cancel_token) {
    std::this_thread::sleep_for(10us); // Minimal background work

    // Collect profiling data, update debug statistics
    background_updates_++;
  });

  co_return;
}

auto DebugOverlayModule::Shutdown(ModuleContext& context) -> co::Co<>
{
  LOG_F(INFO, "[Debug] Shutting down debug overlay");

  // Clean up debug resources
  auto& reclaimer = context.GetGraphics().GetDeferredReclaimer();
  reclaimer.ScheduleReclaim(
    debug_font_handle_, context.GetFrameIndex(), "DebugFont");
  reclaimer.ScheduleReclaim(
    debug_line_buffer_handle_, context.GetFrameIndex(), "DebugLineBuffer");

  LOG_F(INFO,
    "[Debug] Debug overlay shutdown (presented {} frames, {} background "
    "updates)",
    debug_frames_presented_, background_updates_);
  co_return;
}

} // namespace oxygen::examples::asyncsim
