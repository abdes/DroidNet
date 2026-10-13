//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Commands/Command.h>
#include <Oxygen/Commands/CommandRegistry.h>
#include <Oxygen/Control/api_export.h>
#include <Oxygen/Core/EngineModule.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/OxCo/LiveObject.h>
#include <Oxygen/OxCo/Nursery.h>
#include <Oxygen/OxCo/ParkingLot.h>

namespace oxygen {
class IAsyncEngine;
} // namespace oxygen

namespace oxygen::control {

class EventLog;

//! Runs every command invocation, from any source, as a job.
/*!
 Invocations queue until the next frame start, then start in arrival order on
 the engine thread. Each job runs until it first waits before the next one
 starts, so effects apply in arrival order: `ngin.step 5` followed by
 `ngin.resume` requests the steps, then cancels them. A synchronous command
 completes there; an asynchronous one keeps running in the dispatcher's nursery
 and may await frames.

 The dispatcher is a `co::LiveObject` owned by whoever composes the engine
 (the host), activated in that owner's nursery so its jobs outlive module
 teardown. `CommandDispatchModule` ticks it from the engine's frame start.

 It registers the run-state commands (`ngin.pause`, `ngin.resume`,
 `ngin.step`, `ngin.session.end`, `ngin.quit`) and `cmd.jobs`, `cmd.cancel`:
 these need frames to report their outcome.
*/
class CommandDispatcher final : public co::LiveObject {
public:
  //! `events`, when given, receives a `command` event per finished job.
  OXGN_CTRL_API CommandDispatcher(
    observer_ptr<IAsyncEngine> engine, observer_ptr<EventLog> events);
  OXGN_CTRL_API ~CommandDispatcher() override;

  OXYGEN_MAKE_NON_COPYABLE(CommandDispatcher)
  OXYGEN_MAKE_NON_MOVABLE(CommandDispatcher)

  OXGN_CTRL_NDAPI auto ActivateAsync(co::TaskStarted<> started)
    -> co::Co<> override;
  OXGN_CTRL_API auto Stop() -> void override;
  OXGN_CTRL_NDAPI auto IsRunning() const -> bool override
  {
    return nursery_ != nullptr;
  }

  //! Queue an invocation for the next frame start.
  OXGN_CTRL_API auto Post(commands::CommandInvocation invocation,
    commands::CommandContext context) -> commands::JobId;
  //! Queue a console line (commands separated by `;`, run in order, stopping
  //! at the first failure). The outcome is recorded in the console's output;
  //! a line that does not parse is recorded at once.
  OXGN_CTRL_API auto PostLine(
    std::string line, commands::CommandContext context) -> void;
  //! Queue an invocation and await its result. Cancelling the awaiter
  //! cancels the job.
  OXGN_CTRL_NDAPI auto Run(commands::CommandInvocation invocation,
    commands::CommandContext context) -> co::Co<commands::CommandResult>;
  OXGN_CTRL_API auto Cancel(commands::JobId job) -> bool;

  //! Completes at the next frame start.
  OXGN_CTRL_NDAPI auto NextFrame() -> co::Co<>;

  //! Counts a frame step, wakes frame waiters and starts the queued jobs;
  //! called at frame start. `simulating` is false on a paused frame.
  OXGN_CTRL_API auto OnFrameStart(uint64_t frame, bool simulating) -> void;
  OXGN_CTRL_API auto RegisterCommands(commands::CommandRegistry& registry)
    -> void;

private:
  struct Job;

  auto Enqueue(std::vector<commands::CommandInvocation> invocations,
    commands::CommandContext context, std::string line = {})
    -> std::shared_ptr<Job>;
  auto RunJob(std::shared_ptr<Job> job) -> co::Co<>;

  observer_ptr<IAsyncEngine> engine_;
  observer_ptr<EventLog> events_;
  co::Nursery* nursery_ { nullptr };
  uint64_t frame_ { 0U };
  co::ParkingLot frame_start_;
  //! Frames simulated while paused, that is frame steps.
  uint64_t stepped_frames_ { 0U };
  uint64_t next_job_ { 1U };
  std::vector<std::shared_ptr<Job>> pending_;
  std::unordered_map<commands::JobId, std::shared_ptr<Job>> running_;
};

//! Engine module that ticks a `CommandDispatcher` and registers its commands.
class CommandDispatchModule final : public engine::EngineModule {
  OXYGEN_TYPED(CommandDispatchModule)
public:
  OXGN_CTRL_API explicit CommandDispatchModule(
    observer_ptr<CommandDispatcher> dispatcher);
  ~CommandDispatchModule() override = default;

  OXYGEN_MAKE_NON_COPYABLE(CommandDispatchModule)
  OXYGEN_MAKE_NON_MOVABLE(CommandDispatchModule)

  [[nodiscard]] auto GetName() const noexcept -> std::string_view override
  {
    return "CommandDispatchModule";
  }
  [[nodiscard]] auto GetPriority() const noexcept
    -> engine::ModulePriority override
  {
    return engine::kModulePriorityHighest;
  }
  [[nodiscard]] auto GetSupportedPhases() const noexcept
    -> engine::ModulePhaseMask override
  {
    return engine::MakeModuleMask<core::PhaseId::kFrameStart>();
  }

  OXGN_CTRL_API auto RegisterCommands(
    observer_ptr<commands::CommandRegistry> registry) noexcept -> void override;
  OXGN_CTRL_API auto OnFrameStart(observer_ptr<engine::FrameContext> context)
    -> void override;

private:
  observer_ptr<CommandDispatcher> dispatcher_;
};

} // namespace oxygen::control
