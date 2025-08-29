//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <memory>
#include <vector>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/OxCo/Co.h>

#include "../IEngineModule.h"
#include "../Renderer/Graph/RenderGraphBuilder.h"
#include "../Renderer/Graph/Types.h"
#include <Oxygen/Engine/FrameContext.h>

namespace oxygen::engine::asyncsim {

//! Example geometry rendering module demonstrating render graph API usage
/*!
 This module demonstrates:
 - How to integrate with ModulePhases::FrameGraph
 - Creating resources using the render graph builder
 - Adding passes with dependencies
 - Working with view rendering
 - Resource lifetime management with AsyncEngine integration

 ## ARCHITECTURAL ROLE & POSITIONING

 **GeometryRenderModule** serves as the *Rendering Infrastructure & Technique
 Owner* in the async engine pipeline. It operates in the later pipeline phases
 and is responsible for defining *how* content should be rendered efficiently on
 the GPU.

 **Key Responsibilities:**
 - Graphics technique implementation (depth prepass, deferred rendering,
   transparency)
 - Render graph construction and pass orchestration
 - GPU resource management and lifetime tracking
 - Performance-critical geometry processing (culling, batching, LOD)

 **Pipeline Phases:**
 - `SnapshotBuild`: Convert scene data to renderable form
 - `FrameGraph`: Build render graph infrastructure (depth/opaque/transparency
   passes)
 - `ParallelWork`: Process geometry in parallel (frustum culling, batching)

 ## SYNERGY WITH GAMEMODULE

 **GameModule** (Content Creator) → **GeometryRenderModule** (Rendering
 Infrastructure)

 **Data Flow Contract:**
 1. GameModule creates/updates scene entities in early phases (Input → Gameplay
    → SceneMutation)
 2. **FrameSnapshot** serves as immutable contract: GameModule populates "what
    to render", GeometryRenderModule reads "how to render it"
 3. Both modules collaborate during FrameGraph phase:
    - GameModule adds game-specific passes (UI, HUD, effects)
    - GeometryRenderModule provides core geometry rendering infrastructure

 **Example Collaboration:**
 ```cpp
 // GameModule: Defines scene content
 snapshot->AddRenderableEntity(player_entity, transform);
 snapshot->AddLightSources(lights);

 // GeometryRenderModule: Processes scene data into render commands
 for (auto& entity : snapshot->GetRenderableEntities()) {
   PrepareGeometryData(entity);
   AddToRenderBatch(entity);
 }
 ```

 **Separation of Concerns:**
 - **GameModule**: Scene semantics, entity lifecycle, game logic, "what to
   render"
 - **GeometryRenderModule**: Rendering techniques, GPU optimization, "how to
   render"

 This design enables modularity (game logic decoupled from rendering),
 reusability (same renderer works for different game types), and optimal
 performance through phase-separated parallelization.
 */
class GeometryRenderModule final : public EngineModuleBase {
public:
  GeometryRenderModule();
  ~GeometryRenderModule() override = default;

  OXYGEN_MAKE_NON_COPYABLE(GeometryRenderModule)
  OXYGEN_MAKE_NON_MOVABLE(GeometryRenderModule)

  // === LIFECYCLE MANAGEMENT ===

  auto Initialize(AsyncEngineSimulator& engine) -> co::Co<> override;
  auto Shutdown() -> co::Co<> override;

  // === FRAME PHASE IMPLEMENTATIONS ===

  //! Frame graph phase - contribute geometry passes to render graph
  auto OnFrameGraph(FrameContext& context) -> co::Co<> override;

  //! Parallel work phase - process geometry in parallel (culling, etc.)
  auto OnParallelWork(FrameContext& context) -> co::Co<> override;

  // === PUBLIC API ===

  //! Configuration for geometry rendering
  struct GeometryConfig {
    bool enable_depth_prepass { true };
    bool enable_transparency { true };
    bool enable_instancing { false };
    uint32_t max_instances { 1000 };
  };

  //! Set geometry rendering configuration
  auto SetConfiguration(const GeometryConfig& config) -> void
  {
    config_ = config;
  }

  //! Get current configuration
  [[nodiscard]] auto GetConfiguration() const -> const GeometryConfig&
  {
    return config_;
  }

  //! Get render statistics for debugging
  struct RenderStats {
    uint32_t depth_pass_draws { 0 };
    uint32_t opaque_draws { 0 };
    uint32_t transparent_draws { 0 };
    uint32_t total_vertices { 0 };
    uint32_t total_instances { 0 };
    uint32_t lighting_passes { 0 };
    uint32_t post_process_passes { 0 };
    uint32_t ui_passes { 0 };
  };

  [[nodiscard]] auto GetLastFrameStats() const -> const RenderStats&
  {
    return last_frame_stats_;
  }

private:
  // === INTERNAL STATE ===

  GeometryConfig config_;
  RenderStats last_frame_stats_;
  bool is_initialized_ { false };

  // Example geometry data
  struct GeometryData {
    uint32_t vertex_count { 0 };
    uint32_t index_count { 0 };
    uint32_t instance_count { 1 };
  };

  std::vector<GeometryData> geometry_objects_;

  // Resource handles created during FrameGraph phase
  ResourceHandle depth_buffer_ { 0 };
  ResourceHandle color_buffer_ { 0 };
  ResourceHandle vertex_buffer_ { 0 };
  ResourceHandle index_buffer_ { 0 };
  ResourceHandle lighting_buffer_ { 0 };
  ResourceHandle post_process_buffer_ { 0 };

  // Pass handles for dependencies
  PassHandle depth_prepass_;
  PassHandle opaque_pass_;
  PassHandle transparency_pass_;
  PassHandle lighting_pass_;
  PassHandle post_process_pass_;
  PassHandle ui_pass_;

  // === INTERNAL METHODS ===

  //! Create render graph resources
  auto CreateRenderResources(RenderGraphBuilder& builder) -> void;

  //! Add depth prepass to render graph
  auto AddDepthPrepass(RenderGraphBuilder& builder) -> void;

  //! Add opaque geometry pass to render graph
  auto AddOpaquePass(RenderGraphBuilder& builder) -> void;

  //! Add transparency pass to render graph
  auto AddTransparencyPass(RenderGraphBuilder& builder) -> void;

  //! Add lighting pass
  auto AddLightingPass(RenderGraphBuilder& builder) -> void;

  //! Add post-process pass
  auto AddPostProcessPass(RenderGraphBuilder& builder) -> void;

  //! Add UI overlay pass
  auto AddUIPass(RenderGraphBuilder& builder) -> void;

  //! Execute depth prepass rendering
  auto ExecuteDepthPrepass(TaskExecutionContext& ctx) -> void;

  //! Execute opaque geometry rendering
  auto ExecuteOpaqueGeometry(TaskExecutionContext& ctx) -> void;

  //! Execute transparency rendering
  auto ExecuteTransparency(TaskExecutionContext& ctx) -> void;

  //! Execute lighting
  auto ExecuteLighting(TaskExecutionContext& ctx) -> void;

  //! Execute post process
  auto ExecutePostProcess(TaskExecutionContext& ctx) -> void;

  //! Execute UI overlay
  auto ExecuteUI(TaskExecutionContext& ctx) -> void;

  //! Update render statistics
  auto UpdateRenderStats() -> void;

  //! Initialize example geometry data
  auto InitializeGeometryData() -> void;
};

} // namespace oxygen::engine::asyncsim
