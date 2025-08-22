//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <chrono>
#include <thread>
#include <vector>

#include <Oxygen/Base/Logging.h>

#include "../IEngineModule.h"
#include "../ModuleContext.h"
#include "../Renderer/Graph/ExecutionContext.h"
#include "../Renderer/Graph/RenderGraphBuilder.h"

using namespace std::chrono_literals;

namespace oxygen::examples::asyncsim {

//! Example game module demonstrating core gameplay logic
/*!
 This module demonstrates game-specific logic integration with AsyncEngine:
 - Complete game loop integration (input → simulation → gameplay → rendering)
 - Deterministic fixed timestep physics simulation
 - Variable timestep AI and high-level game logic
 - Scene entity management with spawning/despawning
 - Transform hierarchy updates
 - Parallel work using frame snapshots
 - Render graph contribution for game-specific content

 ## ARCHITECTURAL ROLE & POSITIONING

 **GameModule** serves as the *Content Creator & Logic Owner* in the async
 engine pipeline. It operates in the early pipeline phases and is responsible
 for defining *what* should be rendered and *where* it should be positioned.

 **Key Responsibilities:**
 - Game-specific logic, rules, and state management
 - Scene entity lifecycle (creation, updates, destruction)
 - Input processing and player interaction
 - Physics simulation and gameplay mechanics
 - Transform hierarchy management and spatial positioning

 **Pipeline Phases:**
 - `Input`: Process player input and external events
 - `FixedSimulation`: Deterministic physics and core gameplay
 - `Gameplay`: Variable timestep AI and high-level game logic
 - `SceneMutation`: Entity spawning/despawning and structural changes
 - `TransformPropagation`: Update world transforms and spatial relationships
 - `SnapshotBuild`: Contribute scene data to immutable frame snapshot
 - `FrameGraph`: Add game-specific render passes (UI, HUD, effects)
 - `ParallelWork`: AI processing and game logic that can run in parallel

 ## SYNERGY WITH GEOMETRYRENDERMODULE

 **GameModule** (Content Creator) → **GeometryRenderModule** (Rendering
 Infrastructure)

 **Collaboration Contract:**
 1. **Early Phases**: GameModule creates and updates scene content
    ```cpp
    auto OnSceneMutation() {
      SpawnPlayer(position, rotation);
      SpawnEnemies(enemy_list);
      UpdateWorldState();
    }
    ```

 2. **FrameSnapshot Contract**: GameModule populates snapshot with renderable
    content
    ```cpp
    auto OnSnapshotBuild() {
      snapshot->AddRenderableEntity(player_entity, transform);
      snapshot->AddRenderableEntity(enemies, transforms);
      snapshot->AddLightSources(lights);
      snapshot->AddCameraMatrix(camera);
    }
    ```

 3. **Render Graph Collaboration**: Both modules contribute different aspects
    ```cpp
    // GameModule: Game-specific rendering (UI, HUD, effects)
    auto OnFrameGraph(RenderGraphBuilder& builder) {
      builder.AddPass("GameUI", ui_render_pass);
      builder.AddPass("Minimap", minimap_pass);
    }

    // GeometryRenderModule: Core geometry infrastructure (depth, opaque,
    transparency)
    ```

 **Separation of Concerns:**
 - **GameModule**: Scene semantics, gameplay rules, entity behavior, "what to
   render"
 - **GeometryRenderModule**: Graphics techniques, GPU optimization, "how to
   render"

 This separation enables modular development where gameplay programmers can
 focus on game logic without needing graphics expertise, while rendering
 engineers can optimize GPU performance without understanding game rules.

 Note: This is a simplified example for demonstration purposes. Real games would
 use ECS systems, proper scene graphs, asset management, etc.
 */
class GameModule final : public EngineModuleBase {
public:
  GameModule();
  ~GameModule() override = default;

  OXYGEN_MAKE_NON_COPYABLE(GameModule)
  OXYGEN_MAKE_NON_MOVABLE(GameModule)

  // === LIFECYCLE MANAGEMENT ===

  auto Initialize(ModuleContext& context) -> co::Co<> override;
  auto Shutdown(ModuleContext& context) -> co::Co<> override;

  // === FRAME PHASE IMPLEMENTATIONS ===

  //! Input phase - process player input
  auto OnInput(ModuleContext& context) -> co::Co<> override;

  //! Fixed simulation phase - deterministic physics/gameplay
  auto OnFixedSimulation(ModuleContext& context) -> co::Co<> override;

  //! Gameplay phase - variable timestep game logic
  auto OnGameplay(ModuleContext& context) -> co::Co<> override;

  //! Scene mutation phase - spawn/despawn entities
  auto OnSceneMutation(ModuleContext& context) -> co::Co<> override;

  //! Transform propagation phase - update world transforms
  auto OnTransformPropagation(ModuleContext& context) -> co::Co<> override;

  //! Parallel work phase - AI and batch processing
  auto OnParallelWork(ModuleContext& context) -> co::Co<> override;

  //! Post-parallel phase - integrate parallel work results
  auto OnPostParallel(ModuleContext& context) -> co::Co<> override;

  //! Frame graph phase - contribute game-specific render passes
  auto OnFrameGraph(ModuleContext& context) -> co::Co<> override;

  // === PUBLIC API ===

  //! Game state for monitoring and debugging
  struct GameState {
    float player_health { 100.0f };
    float game_time { 0.0f };
    float player_position_x { 0.0f };
    bool game_over { false };
    uint32_t dynamic_entities_count { 0 };
    uint32_t input_events_processed { 0 };
  };

  //! Get current game state
  [[nodiscard]] auto GetGameState() const -> GameState
  {
    return { player_health_, game_time_, player_position_x_, game_over_,
      static_cast<uint32_t>(dynamic_entities_.size()),
      input_events_processed_ };
  }

  //! Check if game over condition is met
  [[nodiscard]] bool IsGameOver() const noexcept { return game_over_; }

private:
  // Game state
  float player_health_ { 100.0f };
  float game_time_ { 0.0f };
  float player_position_x_ { 0.0f };
  bool game_over_ { false };

  // Resource handles
  uint32_t player_entity_handle_ { 0 };
  uint32_t world_state_handle_ { 0 };
  std::vector<uint32_t> dynamic_entities_;

  // Statistics
  uint32_t input_events_processed_ { 0 };
  uint32_t parallel_work_results_integrated_ { 0 };
};

} // namespace oxygen::examples::asyncsim
