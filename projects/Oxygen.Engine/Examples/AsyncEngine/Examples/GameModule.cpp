//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cmath>

#include "GameModule.h"

#include "../AsyncEngineSimulator.h"
#include "../Renderer/Graph/Resource.h" // For TextureDesc, ResourceState
#include "../Renderer/Integration/GraphicsLayerIntegration.h"

namespace oxygen::examples::asyncsim {

GameModule::GameModule()
  : EngineModuleBase("Game",
      ModulePhases::CoreGameplay | ModulePhases::FrameStart
        | ModulePhases::FrameGraph,
      ModulePriorities::High)
{
}

auto GameModule::Initialize(AsyncEngineSimulator& engine) -> co::Co<>
{
  // Store engine reference for later use
  engine_ = observer_ptr { &engine };

  LOG_F(INFO, "[Game] Initializing game systems");

  LOG_F(INFO, "Creating surfaces");

  // Configure multi-surface rendering example

  surfaces_.push_back(RenderSurface {
    .name = "MainWindow",
    .record_cost = 800us,
    .submit_cost = 200us,
    .present_cost = 300us,
  });
  surface_change_set_.push_back({ 0, SurfaceChange::kAdd });
  surfaces_.push_back(RenderSurface {
    .name = "ShadowMap",
    .record_cost = 400us,
    .submit_cost = 100us,
    .present_cost = 50us,
  });
  surface_change_set_.push_back({ 1, SurfaceChange::kAdd });
  surfaces_.push_back(RenderSurface {
    .name = "ReflectionProbe",
    .record_cost = 600us,
    .submit_cost = 150us,
    .present_cost = 100us,
  });
  surface_change_set_.push_back({ 2, SurfaceChange::kAdd });
  surfaces_.push_back(RenderSurface {
    .name = "UI_Overlay",
    .record_cost = 200us,
    .submit_cost = 50us,
    .present_cost = 150us,
  });
  surface_change_set_.push_back({ 3, SurfaceChange::kAdd });

  // Initialize game state
  player_health_ = 100.0f;
  game_time_ = 0.0f;

  // Register some game entities with graphics
  auto gfx = engine.GetGraphics().lock();
  player_entity_handle_ = gfx->RegisterResource("PlayerEntity");
  world_state_handle_ = gfx->RegisterResource("WorldState");

  LOG_F(INFO,
    "[Game] Game systems initialized (player_handle={}, world_handle={})",
    player_entity_handle_.get(), world_state_handle_.get());
  co_return;
}

void GameModule::OnFrameStart(FrameContext& context)
{
  LOG_F(2, "[Game] OnFrameStart: {} surface changes to apply",
    surface_change_set_.size());

  // Apply surface updates
  for (auto& change : surface_change_set_) {
    if (change.type == SurfaceChange::kAdd) {
      context.AddSurface(surfaces_[change.index]);
      LOG_F(2, "[Game] Added surface {} to frame context", change.index);
    } else {
      context.RemoveSurfaceAt(change.index);
      LOG_F(2, "[Game] Removed surface {} from frame context", change.index);
    }
  }

  LOG_F(2, "[Game] Frame context now has {} surfaces",
    context.GetSurfaces().size());
}
void GameModule::OnFrameEnd(FrameContext& context) { }

auto GameModule::OnInput(FrameContext& context) -> co::Co<>
{
  LOG_F(2, "[Game] Processing input for frame {}", context.GetFrameIndex());

  // In a real game, would process input events and update input state
  // For simulation, just track input processing
  input_events_processed_++;

  co_await context.GetThreadPool()->Run([](auto /*cancel_token*/) {
    std::this_thread::sleep_for(100us); // Simulate input processing
  });

  LOG_F(
    2, "[Game] Input processed (total events: {})", input_events_processed_);
  co_return;
}

auto GameModule::OnFixedSimulation(FrameContext& context) -> co::Co<>
{
  LOG_F(
    2, "[Game] Fixed simulation step for frame {}", context.GetFrameIndex());

  // Fixed timestep gameplay logic (deterministic)
  const float fixed_dt = 1.0f / 60.0f; // 60 FPS fixed timestep
  game_time_ += fixed_dt;

  // Simulate some gameplay logic
  co_await context.GetThreadPool()->Run([this](auto /*cancel_token*/) {
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

auto GameModule::OnGameplay(FrameContext& context) -> co::Co<>
{
  LOG_F(
    2, "[Game] Variable gameplay logic for frame {}", context.GetFrameIndex());

  // Variable timestep gameplay (AI decisions, high-level game logic)
  co_await context.GetThreadPool()->Run([this](auto /*cancel_token*/) {
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

auto GameModule::OnSceneMutation(FrameContext& context) -> co::Co<>
{
  LOG_F(2, "[Game] Scene mutations for frame {}", context.GetFrameIndex());

  // Spawn/despawn entities, structural scene changes
  if (context.GetFrameIndex() % 10 == 0) {
    // Simulate spawning a new entity every 10 frames
    auto gfx = context.AcquireGraphics();
    auto entity_handle = gfx->RegisterResource(
      "DynamicEntity_" + std::to_string(context.GetFrameIndex()));
    dynamic_entities_.push_back(entity_handle);

    LOG_F(2, "[Game] Spawned entity {} (total: {})", entity_handle.get(),
      dynamic_entities_.size());
  }

  co_await context.GetThreadPool()->Run([](auto /*cancel_token*/) {
    std::this_thread::sleep_for(150us); // Simulate scene mutation work
  });
  co_return;
}

auto GameModule::OnTransformPropagation(FrameContext& context) -> co::Co<>
{
  LOG_F(
    2, "[Game] Transform propagation for frame {}", context.GetFrameIndex());

  // Update world transforms for game entities
  co_await context.GetThreadPool()->Run([this](auto /*cancel_token*/) {
    std::this_thread::sleep_for(200us); // Simulate transform calculations

    // Update player position (example)
    player_position_x_ += 0.1f * std::sin(game_time_);
  });

  LOG_F(2, "[Game] Transforms updated (player_x={:.2f})", player_position_x_);
  co_return;
}

auto GameModule::OnParallelWork(FrameContext& context) -> co::Co<>
{
  LOG_F(2, "[Game] Parallel work for frame {}", context.GetFrameIndex());

  // Parallel work using read-only snapshot
  const auto* snapshot = context.GetFrameSnapshot();
  if (!snapshot) {
    LOG_F(WARNING, "[Game] No frame snapshot available for parallel work");
    co_return;
  }

  // Simulate parallel game calculations (AI, animation, etc.)
  co_await context.GetThreadPool()->Run([snapshot](auto /*cancel_token*/) {
    std::this_thread::sleep_for(400us); // Simulate AI batch processing

    // In real implementation, would process game logic using snapshot data
    LOG_F(3, "[Game] AI processing complete for snapshot frame {}",
      snapshot->frame_index);
  });

  LOG_F(2, "[Game] Parallel work complete");
  co_return;
}

auto GameModule::OnPostParallel(FrameContext& context) -> co::Co<>
{
  LOG_F(2, "[Game] Post-parallel integration for frame {}",
    context.GetFrameIndex());

  // Integrate results from parallel work phase
  co_await context.GetThreadPool()->Run([this](auto /*cancel_token*/) {
    std::this_thread::sleep_for(100us); // Simulate result integration

    // Update game state with parallel work results
    parallel_work_results_integrated_++;
  });

  LOG_F(2, "[Game] Post-parallel complete (integrations: {})",
    parallel_work_results_integrated_);
  co_return;
}

auto GameModule::OnFrameGraph(FrameContext& context) -> co::Co<>
{
  LOG_F(2, "[Game] Contributing to render graph for frame {}",
    context.GetFrameIndex());

  // Create views from the surfaces we added during OnFrameStart
  auto surfaces = context.GetSurfaces();
  LOG_F(2, "[Game] Creating views from {} surfaces", surfaces.size());

  if (!surfaces.empty()) {
    std::vector<ViewInfo> views;
    views.reserve(surfaces.size());

    for (size_t view_index = 0; view_index < surfaces.size(); ++view_index) {
      const auto& surface = surfaces[view_index];
      ViewInfo view;
      view.view_name = "GameView_" + std::to_string(view_index);
      view.surface
        = ViewInfo::SurfaceHandle { std::make_shared<RenderSurface>(surface) };
      views.push_back(view);
    }

    context.SetViews(std::move(views));
    LOG_F(2, "[Game] Created {} views from surfaces", surfaces.size());
  } else {
    LOG_F(2, "[Game] No surfaces available - no views created");
  }

  // Only contribute to render graph if we have active content to render
  if (auto& builder = context.GetRenderGraphBuilder()) {
    // View example: Always contribute HUD & entity passes when any
    // dynamic content exists
    if (!game_over_ && !dynamic_entities_.empty()) {
      LOG_F(2, "[Game] Adding view game passes (views={} dynamic_entities={})",
        context.GetViews().size(), dynamic_entities_.size());

      // Create a deliberately duplicated per-view read-only resource to
      // exercise the shared resource optimization promotion. This texture is
      // never written (only sampled), so the optimizer should detect the
      // identical per-view variants and promote them to a single shared
      // instance, logging the promotion.
      auto overlayDataHandle = builder->CreateTexture("HUDOverlayData",
        TextureDesc { 64, 64, TextureDesc::Format::RGBA8_UNorm,
          TextureDesc::Usage::ShaderResource },
        ResourceLifetime::FrameLocal, ResourceScope::PerView);

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
      auto hudHandle = builder->AddRasterPass("GameHUD",
        [this, sharedAnalyticsHandle, overlayDataHandle](PassBuilder& pass) {
          pass.SetScope(PassScope::PerView)
            .IterateAllViews()
            .DependsOn(sharedAnalyticsHandle)
            .SetPriority(Priority::Normal)
            .Read(overlayDataHandle, ResourceState::PixelShaderResource)
            .SetExecutor([this](TaskExecutionContext& exec) {
              const auto& view_ctx = exec.GetViewInfo();
              LOG_F(3,
                "[Game][HUD][View:{}] HUD pass (health={:.1f}, time={:.2f})",
                view_ctx.view_name, player_health_, game_time_);
              std::this_thread::sleep_for(40us);
            });
        });

      // Per-view dynamic entities pass (depends on HUD)
      auto entitiesHandle = builder->AddRasterPass("GameEntities",
        [this, hudHandle, overlayDataHandle](PassBuilder& pass) {
          pass.SetScope(PassScope::PerView)
            .IterateAllViews()
            .DependsOn(hudHandle)
            .SetPriority(Priority::High)
            .Read(overlayDataHandle, ResourceState::PixelShaderResource)
            .SetExecutor([this](TaskExecutionContext& exec) {
              const auto& view_ctx = exec.GetViewInfo();
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
      builder->AddRasterPass("GameViewDebug",
        [this, entitiesHandle, overlayDataHandle](PassBuilder& pass) {
          pass.SetScope(PassScope::PerView)
            .IterateAllViews()
            .DependsOn(entitiesHandle)
            .SetPriority(Priority::Low)
            .Read(overlayDataHandle, ResourceState::PixelShaderResource)
            .SetExecutor([this](TaskExecutionContext& exec) {
              const auto& view_ctx = exec.GetViewInfo();
              LOG_F(3, "[Game][DBG][View:{}] Debug overlay stub",
                view_ctx.view_name);
              std::this_thread::sleep_for(20us);
            });
        });

      LOG_F(2, "[Game] View game passes added");
    }
  } else {
    LOG_F(WARNING,
      "[Game] No render graph builder available - using legacy rendering");
  }

  co_return;
}

auto GameModule::Shutdown() -> co::Co<>
{
  LOG_F(INFO, "[Game] Shutting down game systems");

  // Clean up game state - use stored engine reference
  if (engine_) {
    auto gfx = engine_->GetGraphics().lock();

    // Schedule cleanup of game resources
    // Note: We don't have frame index here, so use 0 or a special value
    gfx->ScheduleResourceReclaim(player_entity_handle_, 0, "PlayerEntity");
    gfx->ScheduleResourceReclaim(world_state_handle_, 0, "WorldState");

    for (auto handle : dynamic_entities_) {
      gfx->ScheduleResourceReclaim(
        handle, 0, "DynamicEntity_" + std::to_string(handle.get()));
    }
  }

  LOG_F(INFO,
    "[Game] Game systems shutdown complete (cleaned {} dynamic entities)",
    dynamic_entities_.size());
  co_return;
}

} // namespace oxygen::examples::asyncsim
