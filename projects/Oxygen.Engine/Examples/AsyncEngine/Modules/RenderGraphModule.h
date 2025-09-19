//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <chrono>
#include <memory>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/OxCo/Co.h>

#include "../IEngineModule.h"
#include "../Renderer/Graph/Cache.h"
#include "../Renderer/Graph/RenderGraph.h"
#include "../Renderer/Integration/GraphicsLayerIntegration.h"
#include <Oxygen/Core/FrameContext.h>

namespace oxygen::engine::asyncsim {

class RenderGraph; // use base type
class GraphicsLayerIntegration;

//! Core render graph module that orchestrates graph-based rendering
/*!
 The RenderGraphModule is responsible for:
 - Providing render graph builder access to other modules during FrameGraph
 phase
 - Compiling and validating the render graph after all modules contribute
 - Managing render graph resources with AsyncEngine's GraphicsLayer
 - Coordinating view rendering across different surfaces
 */
class RenderGraphModule final : public EngineModuleBase {
public:
  RenderGraphModule();
  ~RenderGraphModule() override = default;

  OXYGEN_MAKE_NON_COPYABLE(RenderGraphModule)
  OXYGEN_MAKE_NON_MOVABLE(RenderGraphModule)

  // === LIFECYCLE MANAGEMENT ===

  auto Initialize(AsyncEngineSimulator& engine) -> co::Co<> override;
  auto Shutdown() -> co::Co<> override;

  // === FRAME PHASE IMPLEMENTATIONS ===

  //! Frame graph phase - orchestrate render graph construction
  auto OnFrameGraph(FrameContext& context) -> co::Co<> override;

  //! Command recording phase - execute render graph
  auto OnCommandRecord(FrameContext& context) -> co::Co<> override;

  // === PUBLIC API ===

  //! Get the compiled render graph (available after OnFrameGraph)
  [[nodiscard]] auto GetRenderGraph() const noexcept
    -> const std::shared_ptr<RenderGraph>&
  {
    return render_graph_;
  }

  //! Check if render graph is ready for execution
  [[nodiscard]] auto IsRenderGraphReady() const noexcept -> bool
  {
    return render_graph_ != nullptr;
  }

  //! Get frame statistics for debugging and profiling
  struct FrameStatistics {
    std::uint32_t pass_count { 0 };
    std::uint32_t resource_count { 0 };
    std::chrono::microseconds build_time { 0 };
    std::chrono::microseconds validation_time { 0 };
  };

  [[nodiscard]] auto GetLastFrameStats() const -> const FrameStatistics&;

private:
  // === INTERNAL STATE ===

  //! Compiled render graph for current frame (shared for cache reuse)
  std::shared_ptr<RenderGraph> render_graph_;

  //! Cache for compiled render graphs
  std::unique_ptr<RenderGraphCache> render_graph_cache_;

  //! Frame statistics for debugging
  FrameStatistics last_frame_stats_;

  //! Integration state
  bool is_initialized_ { false };
  std::uint64_t current_frame_index_ { 0 };

  // === INTERNAL METHODS ===

  //! Create view contexts from available rendering surfaces
  auto CreateViewInfosFromSurfaces(FrameContext& frame_context) -> void;

  //! Wait for all modules to contribute to the render graph
  auto WaitForModuleContributions(FrameContext& context) -> co::Co<>;

  //! Compile the render graph from builder data
  auto CompileRenderGraph(FrameContext& context) -> co::Co<>;

  //! Plan resource transitions for the compiled render graph
  auto PlanResourceTransitions(FrameContext& context) -> co::Co<>;
};

} // namespace oxygen::engine::asyncsim
