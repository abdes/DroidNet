//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "GameModule.h"

#include "../AsyncEngineSimulator.h"
#include <cmath>

namespace oxygen::examples::asyncsim {

GameModule::GameModule()
  : EngineModuleBase("Game",
      // Enable full gameplay + scene mutation + transforms + rendering phases
      ModulePhases::CoreGameplay | ModulePhases::SceneMutation
        | ModulePhases::TransformPropagation | ModulePhases::ParallelWork
        | ModulePhases::PostParallel | ModulePhases::FrameGraph,
      ModulePriorities::High)
{
}

auto GameModule::Initialize(ModuleContext& context) -> co::Co<>
{
  LOG_F(INFO, "[Game] Initializing game systems");

  // Initialize game state
  player_health_ = 100.0f;
  game_time_ = 0.0f;

  // Register some game entities with graphics
  auto& registry = context.GetGraphics().GetResourceRegistry();
  player_entity_handle_
    = static_cast<uint32_t>(registry.RegisterResource("PlayerEntity"));
  world_state_handle_
    = static_cast<uint32_t>(registry.RegisterResource("WorldState"));

  LOG_F(INFO,
    "[Game] Game systems initialized (player_handle={}, world_handle={})",
    player_entity_handle_, world_state_handle_);
  co_return;
}

auto GameModule::OnInput(ModuleContext& context) -> co::Co<>
{
  LOG_F(2, "[Game] Processing input for frame {}", context.GetFrameIndex());

  // In a real game, would process input events and update input state
  // For simulation, just track input processing
  input_events_processed_++;

  co_await context.GetThreadPool().Run([](auto /*cancel_token*/) {
    std::this_thread::sleep_for(100us); // Simulate input processing
  });

  LOG_F(
    2, "[Game] Input processed (total events: {})", input_events_processed_);
  co_return;
}

auto GameModule::OnFixedSimulation(ModuleContext& context) -> co::Co<>
{
  LOG_F(
    2, "[Game] Fixed simulation step for frame {}", context.GetFrameIndex());

  // Fixed timestep gameplay logic (deterministic)
  const float fixed_dt = 1.0f / 60.0f; // 60 FPS fixed timestep
  game_time_ += fixed_dt;

  // Simulate some gameplay logic
  co_await context.GetThreadPool().Run([this](auto /*cancel_token*/) {
    std::this_thread::sleep_for(200us); // Simulate physics integration

    // Update player health as example of authoritative state mutation
    if (game_time_ > 5.0f) {
      player_health_ = std::max(0.0f, player_health_ - 0.1f);
    }
  });

  LOG_F(2, "[Game] Fixed sim complete (time={:.2f}s, health={:.1f})",
    game_time_, player_health_);
  co_return;
}

auto GameModule::OnGameplay(ModuleContext& context) -> co::Co<>
{
  LOG_F(
    2, "[Game] Variable gameplay logic for frame {}", context.GetFrameIndex());

  // Variable timestep gameplay (AI decisions, high-level game logic)
  co_await context.GetThreadPool().Run([this](auto /*cancel_token*/) {
    std::this_thread::sleep_for(300us); // Simulate AI processing

    // Make some high-level game decisions
    if (player_health_ <= 0.0f && !game_over_) {
      game_over_ = true;
      LOG_F(INFO, "[Game] Game Over! Player health reached zero.");
    }
  });

  LOG_F(2, "[Game] Gameplay logic complete (game_over={})", game_over_);
  co_return;
}

auto GameModule::OnSceneMutation(ModuleContext& context) -> co::Co<>
{
  LOG_F(2, "[Game] Scene mutations for frame {}", context.GetFrameIndex());

  // Spawn/despawn entities, structural scene changes
  if (context.GetFrameIndex() % 10 == 0) {
    // Simulate spawning a new entity every 10 frames
    auto& registry = context.GetGraphics().GetResourceRegistry();
    auto entity_handle = registry.RegisterResource(
      "DynamicEntity_" + std::to_string(context.GetFrameIndex()));
    dynamic_entities_.push_back(entity_handle);

    LOG_F(2, "[Game] Spawned entity {} (total: {})", entity_handle,
      dynamic_entities_.size());
  }

  co_await context.GetThreadPool().Run([](auto /*cancel_token*/) {
    std::this_thread::sleep_for(150us); // Simulate scene mutation work
  });
  co_return;
}

auto GameModule::OnTransformPropagation(ModuleContext& context) -> co::Co<>
{
  LOG_F(
    2, "[Game] Transform propagation for frame {}", context.GetFrameIndex());

  // Update world transforms for game entities
  co_await context.GetThreadPool().Run([this](auto /*cancel_token*/) {
    std::this_thread::sleep_for(200us); // Simulate transform calculations

    // Update player position (example)
    player_position_x_ += 0.1f * std::sin(game_time_);
  });

  LOG_F(2, "[Game] Transforms updated (player_x={:.2f})", player_position_x_);
  co_return;
}

auto GameModule::OnParallelWork(ModuleContext& context) -> co::Co<>
{
  LOG_F(2, "[Game] Parallel work for frame {}", context.GetFrameIndex());

  // Parallel work using read-only snapshot
  const auto* snapshot = context.GetFrameSnapshot();
  if (!snapshot) {
    LOG_F(WARNING, "[Game] No frame snapshot available for parallel work");
    co_return;
  }

  // Simulate parallel game calculations (AI, animation, etc.)
  co_await context.GetThreadPool().Run([snapshot](auto /*cancel_token*/) {
    std::this_thread::sleep_for(400us); // Simulate AI batch processing

    // In real implementation, would process game logic using snapshot data
    LOG_F(3, "[Game] AI processing complete for snapshot frame {}",
      snapshot->frame_index);
  });

  LOG_F(2, "[Game] Parallel work complete");
  co_return;
}

auto GameModule::OnPostParallel(ModuleContext& context) -> co::Co<>
{
  LOG_F(2, "[Game] Post-parallel integration for frame {}",
    context.GetFrameIndex());

  // Integrate results from parallel work phase
  co_await context.GetThreadPool().Run([this](auto /*cancel_token*/) {
    std::this_thread::sleep_for(100us); // Simulate result integration

    // Update game state with parallel work results
    parallel_work_results_integrated_++;
  });

  LOG_F(2, "[Game] Post-parallel complete (integrations: {})",
    parallel_work_results_integrated_);
  co_return;
}

auto GameModule::OnFrameGraph(ModuleContext& context) -> co::Co<>
{
  LOG_F(2, "[Game] Contributing to render graph for frame {}",
    context.GetFrameIndex());

  // Only contribute to render graph if we have active content to render
  if (auto* builder = context.GetRenderGraphBuilder()) {
    // Multi-view example: Always contribute HUD & entity passes when any
    // dynamic content exists
    if (!game_over_ && !dynamic_entities_.empty()) {
      LOG_F(2,
        "[Game] Adding multi-view game passes (views={} dynamic_entities={})",
        builder->GetFrameContext().views.size(), dynamic_entities_.size());

      // Shared (once-per-frame) analytics/update pass prior to per-view drawing
      PassBuilder analyticsBuilder = builder->AddComputePass("GameAnalytics");
      analyticsBuilder.SetScope(PassScope::Shared)
        .SetPriority(Priority::Low)
        .SetExecutor([this](TaskExecutionContext& /*exec*/) {
          LOG_F(3, "[Game] Shared analytics (time={:.2f})", game_time_);
          std::this_thread::sleep_for(30us);
        });
      auto sharedAnalyticsHandle
        = builder->AddPass(std::move(analyticsBuilder));

      // Per-view HUD pass (depends on analytics)
      auto hudHandle = builder->AddRasterPass(
        "GameHUD", [this, sharedAnalyticsHandle](PassBuilder& pass) {
          pass.SetScope(PassScope::PerView)
            .IterateAllViews()
            .DependsOn(sharedAnalyticsHandle)
            .SetPriority(Priority::Normal)
            .SetExecutor([this](TaskExecutionContext& exec) {
              const auto& view_ctx = exec.GetViewContext();
              LOG_F(3,
                "[Game][HUD][View:{}] HUD pass (health={:.1f}, time={:.2f})",
                view_ctx.view_name, player_health_, game_time_);
              std::this_thread::sleep_for(40us);
            });
        });

      // Per-view dynamic entities pass (depends on HUD)
      auto entitiesHandle = builder->AddRasterPass(
        "GameEntities", [this, hudHandle](PassBuilder& pass) {
          pass.SetScope(PassScope::PerView)
            .IterateAllViews()
            .DependsOn(hudHandle)
            .SetPriority(Priority::High)
            .SetExecutor([this](TaskExecutionContext& exec) {
              const auto& view_ctx = exec.GetViewContext();
              LOG_F(3,
                "[Game][ENT][View:{}] Entities pass ({} entities, "
                "player_x={:.2f})",
                view_ctx.view_name, dynamic_entities_.size(),
                player_position_x_);
              for (auto handle : dynamic_entities_) {
                (void)handle; // placeholder for draw submission
              }
              std::this_thread::sleep_for(80us);
            });
        });

      // Optional: Per-view minimal debug overlay (depends on entities)
      builder->AddRasterPass(
        "GameViewDebug", [this, entitiesHandle](PassBuilder& pass) {
          pass.SetScope(PassScope::PerView)
            .IterateAllViews()
            .DependsOn(entitiesHandle)
            .SetPriority(Priority::Low)
            .SetExecutor([this](TaskExecutionContext& exec) {
              const auto& view_ctx = exec.GetViewContext();
              LOG_F(3, "[Game][DBG][View:{}] Debug overlay stub",
                view_ctx.view_name);
              std::this_thread::sleep_for(20us);
            });
        });

      LOG_F(2, "[Game] Multi-view game passes added");
    }
  } else {
    LOG_F(WARNING,
      "[Game] No render graph builder available - using legacy rendering");
  }

  co_return;
}

auto GameModule::Shutdown(ModuleContext& context) -> co::Co<>
{
  LOG_F(INFO, "[Game] Shutting down game systems");

  // Clean up game state
  auto& reclaimer = context.GetGraphics().GetDeferredReclaimer();

  // Schedule cleanup of game resources
  reclaimer.ScheduleReclaim(
    player_entity_handle_, context.GetFrameIndex(), "PlayerEntity");
  reclaimer.ScheduleReclaim(
    world_state_handle_, context.GetFrameIndex(), "WorldState");

  for (auto handle : dynamic_entities_) {
    reclaimer.ScheduleReclaim(handle, context.GetFrameIndex(),
      "DynamicEntity_" + std::to_string(handle));
  }

  LOG_F(INFO,
    "[Game] Game systems shutdown complete (cleaned {} dynamic entities)",
    dynamic_entities_.size());
  co_return;
}

} // namespace oxygen::examples::asyncsim
