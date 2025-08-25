//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <chrono>
#include <thread>

#include <Oxygen/Base/Logging.h>

#include "../FrameContext.h"
#include "../IEngineModule.h"
#include "../Renderer/Graph/ExecutionContext.h"
#include "../Renderer/Graph/RenderGraphBuilder.h"
#include "../Renderer/Graph/Types.h"

using namespace std::chrono_literals;

namespace oxygen::examples::asyncsim {

//! Engine debug overlay module for development and profiling
/*!
 This module provides debug visualization and performance monitoring:
 - Frame statistics and performance metrics display
 - Debug geometry rendering (lines, wireframes, collision volumes)
 - Low-priority rendering that doesn't impact main frame performance
 - Background profiling data collection
 - Runtime toggleable debug overlays
 */
class DebugOverlayModule final : public EngineModuleBase {
public:
  DebugOverlayModule();
  ~DebugOverlayModule() override = default;

  OXYGEN_MAKE_NON_COPYABLE(DebugOverlayModule)
  OXYGEN_MAKE_NON_MOVABLE(DebugOverlayModule)

  // === LIFECYCLE MANAGEMENT ===

  auto Initialize(AsyncEngineSimulator& engine) -> co::Co<> override;
  auto Shutdown() -> co::Co<> override;

  // === FRAME PHASE IMPLEMENTATIONS ===

  //! Parallel work phase - build debug visualization data
  auto OnParallelWork(FrameContext& context) -> co::Co<> override;

  //! Frame graph phase - contribute debug overlay passes
  auto OnFrameGraph(FrameContext& context) -> co::Co<> override;

  //! Command recording phase - record debug rendering commands
  auto OnCommandRecord(FrameContext& context) -> co::Co<> override;

  //! Detached work phase - background profiling data collection
  auto OnDetachedWork(FrameContext& context) -> co::Co<> override;

  // === PUBLIC API ===

  //! Enable or disable debug overlay rendering
  void SetEnabled(bool enabled) noexcept { enabled_ = enabled; }

  //! Check if debug overlay is currently enabled
  [[nodiscard]] bool IsEnabled() const noexcept { return enabled_; }

  //! Debug statistics for monitoring
  struct DebugStats {
    uint32_t frames_presented { 0 };
    uint32_t background_updates { 0 };
    uint32_t debug_lines_count { 0 };
    uint32_t debug_text_items { 0 };
  };

  //! Get current debug statistics
  [[nodiscard]] auto GetDebugStats() const -> DebugStats
  {
    return { debug_frames_presented_, background_updates_, debug_lines_count_,
      debug_text_items_ };
  }

private:
  struct DebugFrameStats {
    uint64_t frame_index { 0 };
    std::chrono::microseconds frame_time { 0 };
    float cpu_usage { 0.0f };
    float gpu_usage { 0.0f };
  };

  bool enabled_ { false };
  ResourceHandle debug_font_handle_ { 0 };
  ResourceHandle debug_line_buffer_handle_ { 0 };

  DebugFrameStats frame_stats_ {};
  uint32_t debug_lines_count_ { 0 };
  uint32_t debug_text_items_ { 0 };
  bool debug_commands_recorded_ { false };

  uint32_t debug_frames_presented_ { 0 };
  uint32_t background_updates_ { 0 };
};

} // namespace oxygen::examples::asyncsim
