//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//
#pragma once

#include <chrono>
#include <deque>
#include <functional>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include <Oxygen/Base/Logging.h>

#include "IEngineModule.h"
#include "ModuleContext.h"

using namespace std::chrono_literals;

namespace oxygen::examples::asyncsim {

//! Example game module demonstrating core gameplay logic
//! Participates in multiple frame phases with different responsibilities
class GameModule final : public EngineModuleBase {
public:
  GameModule()
    : EngineModuleBase("Game",
        ModulePhases::CoreGameplay | ModulePhases::ParallelWork
          | ModulePhases::PostParallel,
        ModulePriority::High)
  {
  }

  auto Initialize(ModuleContext& context) -> co::Co<> override
  {
    LOG_F(INFO, "[Game] Initializing game systems");

    // Initialize game state
    player_health_ = 100.0f;
    game_time_ = 0.0f;

    // Register some game entities with graphics
    auto& registry = context.GetGraphics().GetResourceRegistry();
    player_entity_handle_ = registry.RegisterResource("PlayerEntity");
    world_state_handle_ = registry.RegisterResource("WorldState");

    LOG_F(INFO,
      "[Game] Game systems initialized (player_handle={}, world_handle={})",
      player_entity_handle_, world_state_handle_);
    co_return;
  }

  auto OnInput(ModuleContext& context) -> co::Co<> override
  {
    LOG_F(2, "[Game] Processing input for frame {}", context.GetFrameIndex());

    // In a real game, would process input events and update input state
    // For simulation, just track input processing
    input_events_processed_++;

    co_await context.GetThreadPool().Run([](auto) {
      std::this_thread::sleep_for(100us); // Simulate input processing
    });

    LOG_F(
      2, "[Game] Input processed (total events: {})", input_events_processed_);
    co_return;
  }

  auto OnFixedSimulation(ModuleContext& context) -> co::Co<> override
  {
    LOG_F(
      2, "[Game] Fixed simulation step for frame {}", context.GetFrameIndex());

    // Fixed timestep gameplay logic (deterministic)
    const float fixed_dt = 1.0f / 60.0f; // 60 FPS fixed timestep
    game_time_ += fixed_dt;

    // Simulate some gameplay logic
    co_await context.GetThreadPool().Run([this](auto) {
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

  auto OnGameplay(ModuleContext& context) -> co::Co<> override
  {
    LOG_F(2, "[Game] Variable gameplay logic for frame {}",
      context.GetFrameIndex());

    // Variable timestep gameplay (AI decisions, high-level game logic)
    co_await context.GetThreadPool().Run([this](auto) {
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

  auto OnSceneMutation(ModuleContext& context) -> co::Co<> override
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

    co_await context.GetThreadPool().Run([](auto) {
      std::this_thread::sleep_for(150us); // Simulate scene mutation work
    });
    co_return;
  }

  auto OnTransformPropagation(ModuleContext& context) -> co::Co<> override
  {
    LOG_F(
      2, "[Game] Transform propagation for frame {}", context.GetFrameIndex());

    // Update world transforms for game entities
    co_await context.GetThreadPool().Run([this](auto) {
      std::this_thread::sleep_for(200us); // Simulate transform calculations

      // Update player position (example)
      player_position_x_ += 0.1f * std::sin(game_time_);
    });

    LOG_F(2, "[Game] Transforms updated (player_x={:.2f})", player_position_x_);
    co_return;
  }

  auto OnParallelWork(ModuleContext& context) -> co::Co<> override
  {
    LOG_F(2, "[Game] Parallel work for frame {}", context.GetFrameIndex());

    // Parallel work using read-only snapshot
    const auto* snapshot = context.GetFrameSnapshot();
    if (!snapshot) {
      LOG_F(WARNING, "[Game] No frame snapshot available for parallel work");
      co_return;
    }

    // Simulate parallel game calculations (AI, animation, etc.)
    co_await context.GetThreadPool().Run([snapshot](auto) {
      std::this_thread::sleep_for(400us); // Simulate AI batch processing

      // In real implementation, would process game logic using snapshot data
      LOG_F(3, "[Game] AI processing complete for snapshot frame {}",
        snapshot->frame_index);
    });

    LOG_F(2, "[Game] Parallel work complete");
    co_return;
  }

  auto OnPostParallel(ModuleContext& context) -> co::Co<> override
  {
    LOG_F(2, "[Game] Post-parallel integration for frame {}",
      context.GetFrameIndex());

    // Integrate results from parallel work phase
    co_await context.GetThreadPool().Run([this](auto) {
      std::this_thread::sleep_for(100us); // Simulate result integration

      // Update game state with parallel work results
      parallel_work_results_integrated_++;
    });

    LOG_F(2, "[Game] Post-parallel complete (integrations: {})",
      parallel_work_results_integrated_);
    co_return;
  }

  auto Shutdown(ModuleContext& context) -> co::Co<> override
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

//! Example debug overlay module for development tools
//! Demonstrates low-priority background module with minimal frame impact
class DebugOverlayModule final : public EngineModuleBase {
public:
  DebugOverlayModule()
    : EngineModuleBase("DebugOverlay",
        ModulePhases::SnapshotBuild | ModulePhases::ParallelWork
          | ModulePhases::CommandRecord | ModulePhases::Present
          | ModulePhases::DetachedWork,
        ModulePriority::Low)
  {
  }

  auto Initialize(ModuleContext& context) -> co::Co<> override
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

  auto OnSnapshotBuild(ModuleContext& context) -> co::Co<> override
  {
    if (!enabled_)
      co_return;

    LOG_F(3, "[Debug] Building debug snapshot for frame {}",
      context.GetFrameIndex());

    // Capture frame statistics for debug display
    frame_stats_.frame_index = context.GetFrameIndex();
    frame_stats_.frame_time = context.GetFrameTiming().frame_duration;
    frame_stats_.cpu_usage = context.GetFrameTiming().cpu_usage_percent;
    frame_stats_.gpu_usage = context.GetFrameTiming().gpu_usage_percent;

    co_return;
  }

  auto OnParallelWork(ModuleContext& context) -> co::Co<> override
  {
    if (!enabled_)
      co_return;

    LOG_F(
      3, "[Debug] Parallel debug work for frame {}", context.GetFrameIndex());

    // Build debug visualization data in parallel
    co_await context.GetThreadPool().Run([this](auto) {
      std::this_thread::sleep_for(50us); // Minimal debug processing

      // Update debug statistics
      debug_lines_count_ = 42; // Simulate some debug geometry
      debug_text_items_ = 8; // Simulate debug text elements
    });

    co_return;
  }

  auto OnCommandRecord(ModuleContext& context) -> co::Co<> override
  {
    if (!enabled_)
      co_return;

    LOG_F(3, "[Debug] Recording debug commands for frame {}",
      context.GetFrameIndex());

    // Record debug rendering commands
    co_await context.GetThreadPool().Run([this](auto) {
      std::this_thread::sleep_for(30us); // Minimal command recording

      debug_commands_recorded_ = true;
    });

    co_return;
  }

  auto OnPresent(ModuleContext& context) -> co::Co<> override
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

  auto OnDetachedWork(ModuleContext& context) -> co::Co<> override
  {
    if (!enabled_)
      co_return;

    // Background debug work (profiling data collection, etc.)
    co_await context.GetThreadPool().Run([this](auto) {
      std::this_thread::sleep_for(10us); // Minimal background work

      // Collect profiling data, update debug statistics
      background_updates_++;
    });

    co_return;
  }

  auto Shutdown(ModuleContext& context) -> co::Co<> override
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

  // Public interface for controlling debug overlay
  void SetEnabled(bool enabled) noexcept { enabled_ = enabled; }
  [[nodiscard]] bool IsEnabled() const noexcept { return enabled_; }

private:
  struct DebugFrameStats {
    uint64_t frame_index { 0 };
    std::chrono::microseconds frame_time { 0 };
    float cpu_usage { 0.0f };
    float gpu_usage { 0.0f };
  };

  bool enabled_ { false };
  uint32_t debug_font_handle_ { 0 };
  uint32_t debug_line_buffer_handle_ { 0 };

  DebugFrameStats frame_stats_ {};
  uint32_t debug_lines_count_ { 0 };
  uint32_t debug_text_items_ { 0 };
  bool debug_commands_recorded_ { false };

  uint32_t debug_frames_presented_ { 0 };
  uint32_t background_updates_ { 0 };
};

//! Example console module for command processing
//! Demonstrates async work and detached services for command execution
class ConsoleModule final : public EngineModuleBase {
public:
  ConsoleModule()
    : EngineModuleBase("Console",
        ModulePhases::Input | ModulePhases::AsyncWork
          | ModulePhases::DetachedWork,
        ModulePriority::Normal)
  {
  }

  auto Initialize(ModuleContext& context) -> co::Co<> override
  {
    LOG_F(INFO, "[Console] Initializing console system");

    // Register built-in commands
    RegisterCommand("help", [this](const std::vector<std::string>&) {
      LOG_F(INFO, "[Console] Available commands: help, quit, debug, stats");
    });

    RegisterCommand("quit", [this](const std::vector<std::string>&) {
      LOG_F(INFO, "[Console] Quit command executed");
      quit_requested_ = true;
    });

    RegisterCommand("debug", [this](const std::vector<std::string>& args) {
      bool enable = args.empty() || (args[0] == "on");
      LOG_F(
        INFO, "[Console] Debug overlay {}", enable ? "enabled" : "disabled");
    });

    RegisterCommand("stats", [this](const std::vector<std::string>&) {
      LOG_F(INFO, "[Console] Commands executed: {}, Pending: {}",
        commands_executed_, pending_commands_.size());
    });

    LOG_F(
      INFO, "[Console] Console initialized with {} commands", commands_.size());
    co_return;
  }

  auto OnInput(ModuleContext& context) -> co::Co<> override
  {
    LOG_F(3, "[Console] Processing console input for frame {}",
      context.GetFrameIndex());

    // Simulate checking for console input
    if (context.GetFrameIndex() % 120
      == 0) { // Simulate command every 2 seconds
      std::string simulated_command = "stats";
      QueueCommand(simulated_command);
      LOG_F(2, "[Console] Queued simulated command: {}", simulated_command);
    }

    co_return;
  }

  auto OnAsyncWork(ModuleContext& context) -> co::Co<> override
  {
    LOG_F(3, "[Console] Processing async console work for frame {}",
      context.GetFrameIndex());

    // Process pending commands asynchronously
    if (!pending_commands_.empty()) {
      auto command = pending_commands_.front();
      pending_commands_.pop_front();

      co_await context.GetThreadPool().Run([this, command](auto) {
        std::this_thread::sleep_for(100us); // Simulate command processing
        ExecuteCommand(command);
      });
    }

    co_return;
  }

  auto OnDetachedWork(ModuleContext& context) -> co::Co<> override
  {
    // Background console services (log file management, command history, etc.)
    co_await context.GetThreadPool().Run([this](auto) {
      std::this_thread::sleep_for(20us); // Minimal background work

      // Simulate background console maintenance
      background_maintenance_cycles_++;

      // Cleanup old command history entries
      if (command_history_.size() > 100) {
        command_history_.erase(
          command_history_.begin(), command_history_.begin() + 50);
      }
    });

    co_return;
  }

  auto Shutdown(ModuleContext& context) -> co::Co<> override
  {
    LOG_F(INFO, "[Console] Shutting down console system");

    // Process any remaining commands before shutdown
    while (!pending_commands_.empty()) {
      auto command = pending_commands_.front();
      pending_commands_.pop_front();
      ExecuteCommand(command);
    }

    LOG_F(INFO,
      "[Console] Console shutdown (executed {} commands, {} history entries)",
      commands_executed_, command_history_.size());
    co_return;
  }

  // Public interface for command management
  void QueueCommand(const std::string& command)
  {
    pending_commands_.push_back(command);
  }

  bool IsQuitRequested() const noexcept { return quit_requested_; }

private:
  using CommandHandler = std::function<void(const std::vector<std::string>&)>;

  void RegisterCommand(const std::string& name, CommandHandler handler)
  {
    commands_[name] = std::move(handler);
  }

  void ExecuteCommand(const std::string& command_line)
  {
    command_history_.push_back(command_line);

    // Parse command (simple space separation)
    std::vector<std::string> tokens;
    std::istringstream iss(command_line);
    std::string token;
    while (iss >> token) {
      tokens.push_back(token);
    }

    if (tokens.empty())
      return;

    const auto& command_name = tokens[0];
    std::vector<std::string> args(tokens.begin() + 1, tokens.end());

    auto it = commands_.find(command_name);
    if (it != commands_.end()) {
      it->second(args);
      commands_executed_++;
      LOG_F(2, "[Console] Executed command: {}", command_line);
    } else {
      LOG_F(WARNING, "[Console] Unknown command: {}", command_name);
    }
  }

  std::unordered_map<std::string, CommandHandler> commands_;
  std::deque<std::string> pending_commands_;
  std::vector<std::string> command_history_;

  uint32_t commands_executed_ { 0 };
  uint32_t background_maintenance_cycles_ { 0 };
  bool quit_requested_ { false };
};

} // namespace oxygen::examples::asyncsim
