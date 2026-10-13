//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <cstdint>
#include <exception>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <fmt/format.h>
#include <fmt/ranges.h>
#include <nlohmann/json.hpp>
#include <nlohmann/json_fwd.hpp>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Base/ScopeGuard.h>
#include <Oxygen/Commands/Command.h>
#include <Oxygen/Commands/CommandRegistry.h>
#include <Oxygen/Commands/Params.h>
#include <Oxygen/Console/Console.h>
#include <Oxygen/Control/CommandDispatcher.h>
#include <Oxygen/Control/EventLog.h>
#include <Oxygen/Core/FrameContext.h>
#include <Oxygen/Engine/IAsyncEngine.h>
#include <Oxygen/OxCo/Algorithms.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/OxCo/Event.h>
#include <Oxygen/OxCo/Executor.h>
#include <Oxygen/OxCo/Nursery.h>

namespace oxygen::control {

using commands::CommandArgs;
using commands::CommandContext;
using commands::CommandDefinition;
using commands::CommandInvocation;
using commands::CommandParam;
using commands::CommandResult;
using commands::ExecutionStatus;
using commands::JobId;
using commands::ParamType;
using commands::ParamValue;

struct CommandDispatcher::Job {
  JobId id;
  std::vector<CommandInvocation> invocations;
  CommandContext context;
  //! The console line the invocations came from; empty for others.
  std::string line;
  uint64_t start_frame { 0U };
  //! Set once the job was reported as long-running.
  bool reported_slow { false };
  co::Event cancel;
  co::Event done;
  std::optional<CommandResult> result;
};

namespace {

  //! A job running this many frames is reported once, so a stuck command is
  //! visible in the log.
  constexpr uint64_t kSlowJobFrames = 600U;

  auto Cancelled() -> CommandResult
  {
    return CommandResult::Failure(ExecutionStatus::kCancelled, "cancelled");
  }

} // namespace

CommandDispatcher::CommandDispatcher(
  const observer_ptr<IAsyncEngine> engine, const observer_ptr<EventLog> events)
  : engine_(engine)
  , events_(events)
{
  CHECK_NOTNULL_F(engine_);
}

CommandDispatcher::~CommandDispatcher() = default;

auto CommandDispatcher::ActivateAsync(co::TaskStarted<> started) -> co::Co<>
{
  return OpenNursery(nursery_, std::move(started));
}

auto CommandDispatcher::Stop() -> void
{
  if (nursery_ != nullptr) {
    nursery_->Cancel();
  }
}

auto CommandDispatcher::Enqueue(std::vector<CommandInvocation> invocations,
  CommandContext context, std::string line) -> std::shared_ptr<Job>
{
  auto job = std::make_shared<Job>();
  job->id = JobId { next_job_++ };
  context.job = job->id;
  job->invocations = std::move(invocations);
  job->context = std::move(context);
  job->line = std::move(line);
  pending_.push_back(job);
  return job;
}

auto CommandDispatcher::Post(
  CommandInvocation invocation, CommandContext context) -> JobId
{
  auto invocations = std::vector<CommandInvocation> {};
  invocations.push_back(std::move(invocation));
  return Enqueue(std::move(invocations), std::move(context))->id;
}

auto CommandDispatcher::PostLine(std::string line, CommandContext context)
  -> void
{
  auto& console = engine_->GetConsole();
  if (context.source == commands::CommandSource::kLocalConsole) {
    console.RecordHistory(line);
  }
  auto invocations = console.Parse(line);
  if (!invocations) {
    console.RecordExecution(std::move(line), std::move(invocations).error());
    return;
  }
  static_cast<void>(Enqueue(
    std::move(invocations).value(), std::move(context), std::move(line)));
}

auto CommandDispatcher::Run(
  CommandInvocation invocation, CommandContext context) -> co::Co<CommandResult>
{
  auto invocations = std::vector<CommandInvocation> {};
  invocations.push_back(std::move(invocation));
  const auto job = Enqueue(std::move(invocations), std::move(context));
  // An awaiter that goes away cancels the job it was waiting for.
  const auto abandon = ScopeGuard([this, job] noexcept -> void {
    if (!job->result) {
      static_cast<void>(Cancel(job->id));
    }
  });
  co_await job->done;
  co_return *job->result;
}

auto CommandDispatcher::Cancel(const JobId job) -> bool
{
  if (const auto it = running_.find(job); it != running_.end()) {
    it->second->cancel.Trigger();
    return true;
  }
  for (auto it = pending_.begin(); it != pending_.end(); ++it) {
    if ((*it)->id == job) {
      (*it)->result = Cancelled();
      (*it)->done.Trigger();
      pending_.erase(it);
      return true;
    }
  }
  return false;
}

auto CommandDispatcher::NextFrame() -> co::Co<>
{
  co_await frame_start_.Park();
}

auto CommandDispatcher::OnFrameStart(
  const uint64_t frame, const bool simulating) -> void
{
  frame_ = frame;
  if (events_ != nullptr) {
    events_->SetFrame(frame);
  }
  if (simulating && engine_->IsPaused()) {
    ++stepped_frames_;
  }
  frame_start_.UnParkAll();
  for (const auto& [id, job] : running_) {
    if (!job->reported_slow && frame - job->start_frame >= kSlowJobFrames) {
      job->reported_slow = true;
      LOG_F(WARNING,
        "command job {} ({}) from {} still running after {} frames", id.get(),
        job->invocations.front().name, commands::to_string(job->context.source),
        frame - job->start_frame);
    }
  }
  if (nursery_ == nullptr) {
    return;
  }
  auto* executor = nursery_->GetExecutor();
  for (auto& job : std::exchange(pending_, {})) {
    job->start_frame = frame_;
    DLOG_F(1, "command job {} ({}) from {} starts at frame {}", job->id.get(),
      job->invocations.front().name, commands::to_string(job->context.source),
      frame_);
    running_.emplace(job->id, job);
    // Every co_await is an executor hop, so jobs started together would
    // interleave by await depth: a later synchronous command could act before
    // an earlier asynchronous one reached its handler. Draining the captured
    // queue runs this job until it first waits before the next one starts.
    executor->Capture([&]() -> void {
      nursery_->Start(&CommandDispatcher::RunJob, this, std::move(job));
      executor->Drain();
    });
  }
}

auto CommandDispatcher::RunJob(std::shared_ptr<Job> job) -> co::Co<>
{
  auto result = Cancelled();
  auto& registry = engine_->GetCommands();
  co_await co::AnyOf(
    [&]() -> co::Co<> {
      for (const auto& invocation : job->invocations) {
        result = co_await registry.Run(invocation, job->context);
        if (result.status != ExecutionStatus::kOk) {
          break;
        }
        if (!job->line.empty()) {
          engine_->GetConsole().RecordUsage(invocation);
        }
      }
    },
    job->cancel);
  running_.erase(job->id);
  DLOG_F(1, "command job {} ({}) {} after {} frame(s){}", job->id.get(),
    job->invocations.front().name, commands::to_string(result.status),
    frame_ - job->start_frame, result.error.empty() ? "" : ": " + result.error);
  if (!job->line.empty()) {
    engine_->GetConsole().RecordExecution(job->line, result);
  }
  if (events_ != nullptr) {
    auto invoked = nlohmann::json::array();
    for (const auto& invocation : job->invocations) {
      invoked.push_back({
        { "command", invocation.name },
        { "args", invocation.args.ToJson() },
      });
    }
    auto event = nlohmann::json {
      { "job", job->id.get() },
      { "commands", std::move(invoked) },
      { "source", commands::to_string(job->context.source) },
      { "status", commands::to_string(result.status) },
      { "start_frame", job->start_frame },
    };
    if (!job->context.client.empty()) {
      event["client"] = job->context.client;
    }
    if (!result.error.empty()) {
      event["error"] = result.error;
    }
    if (!job->line.empty()) {
      event["line"] = job->line;
    }
    events_->Publish("command", std::move(event));
  }
  job->result = std::move(result);
  job->done.Trigger();
}

auto CommandDispatcher::RegisterCommands(commands::CommandRegistry& registry)
  -> void
{
  const auto add = [&registry](CommandDefinition definition) -> void {
    static_cast<void>(registry.Register(std::move(definition)));
  };

  add(CommandDefinition {
    .name = "ngin.pause",
    .label = "Pause",
    .help = "Pause simulation from the next frame; rendering continues",
    .handler
    = [this](const CommandArgs&, const CommandContext&) -> CommandResult {
      engine_->Pause();
      return CommandResult::Success();
    },
  });

  add(CommandDefinition {
    .name = "ngin.resume",
    .label = "Resume",
    .help = "Resume simulation and cancel pending frame steps",
    .handler
    = [this](const CommandArgs&, const CommandContext&) -> CommandResult {
      engine_->Resume();
      return CommandResult::Success();
    },
  });

  add(CommandDefinition {
    .name = "ngin.step",
    .label = "Step",
    .help = "Pause, simulate the given number of frames, and reply once they "
            "have run",
    .params = {
      CommandParam {
        .name = "frames",
        .type = ParamType::kInt,
        .help = "Frames to simulate",
        .default_value = ParamValue { int64_t { 1 } },
        .min_value = 1.0,
        .max_value = 100000.0,
      },
    },
    .async_handler = [this](
                       CommandArgs args, CommandContext) -> co::Co<CommandResult> {
      const auto frames = args.GetInt("frames");
      const auto first = stepped_frames_;
      engine_->Step(static_cast<std::uint32_t>(frames));
      // The engine owns the step count: it reaches zero once every step has
      // begun, or when Resume or a session change cancels them, so the wait
      // always ends.
      do {
        co_await NextFrame();
      } while (engine_->GetPendingSteps() > 0U);
      // The last step frame has begun; it has run at the next frame start.
      co_await NextFrame();
      const auto ran = (std::min)(
        stepped_frames_ - first, static_cast<uint64_t>(frames));
      auto data = nlohmann::json { { "frames", frames }, { "ran", ran } };
      if (ran < static_cast<uint64_t>(frames)) {
        auto result = CommandResult::Failure(ExecutionStatus::kCancelled,
          fmt::format("{} of {} step frame(s) ran before the steps were "
                      "cancelled",
            ran, frames));
        result.data = std::move(data);
        co_return result;
      }
      co_return CommandResult::Success(
        fmt::format("stepped {} frame(s)", frames), std::move(data));
    },
  });

  add(CommandDefinition {
    .name = "ngin.session.end",
    .label = "Stop",
    .help = "End the play session and reply once it has ended",
    .async_handler
    = [this](CommandArgs, CommandContext) -> co::Co<CommandResult> {
      engine_->EndSession();
      while (engine_->GetSessionState() != IAsyncEngine::SessionState::kNone) {
        co_await NextFrame();
      }
      co_return CommandResult::Success("session ended");
    },
  });

  add(CommandDefinition {
    .name = "ngin.quit",
    .label = "Quit",
    .help = "Stop the engine; the application exits",
    .handler
    = [this](const CommandArgs&, const CommandContext&) -> CommandResult {
      engine_->Stop();
      return CommandResult::Success("stopping");
    },
  });

  add(CommandDefinition {
    .name = "cmd.jobs",
    .label = "Running Commands",
    .help = "List the commands that are still running",
    .handler = [this](const CommandArgs&,
                 const CommandContext& context) -> CommandResult {
      auto jobs = nlohmann::json::array();
      auto lines = std::vector<std::string> {};
      for (const auto& [id, job] : running_) {
        if (id == context.job) {
          continue;
        }
        auto entry = nlohmann::json {
          { "job", id.get() },
          { "command", job->invocations.front().name },
          { "source", commands::to_string(job->context.source) },
          { "start_frame", job->start_frame },
        };
        if (!job->context.client.empty()) {
          entry["client"] = job->context.client;
        }
        jobs.push_back(std::move(entry));
        lines.push_back(
          fmt::format("{} {}", id.get(), job->invocations.front().name));
      }
      return CommandResult::Success(
        fmt::format("{}", fmt::join(lines, "\n")), std::move(jobs));
    },
  });

  add(CommandDefinition {
    .name = "cmd.cancel",
    .label = "Cancel Command",
    .help = "Cancel a running or queued command",
    .params = {
      CommandParam {
        .name = "job",
        .type = ParamType::kInt,
        .required = true,
        .min_value = 1.0,
      },
    },
    .handler = [this](const CommandArgs& args,
                 const CommandContext&) -> CommandResult {
      const auto job = JobId { static_cast<uint64_t>(args.GetInt("job")) };
      if (!Cancel(job)) {
        return CommandResult::Failure(ExecutionStatus::kNotFound,
          fmt::format("no running command {}", job.get()));
      }
      return CommandResult::Success();
    },
  });
}

CommandDispatchModule::CommandDispatchModule(
  const observer_ptr<CommandDispatcher> dispatcher)
  : dispatcher_(dispatcher)
{
  CHECK_NOTNULL_F(dispatcher_);
}

auto CommandDispatchModule::RegisterCommands(
  const observer_ptr<commands::CommandRegistry> registry) noexcept -> void
{
  try {
    dispatcher_->RegisterCommands(*registry);
  } catch (const std::exception& ex) {
    LOG_F(ERROR, "{}: command registration failed: {}", GetName(), ex.what());
  }
}

auto CommandDispatchModule::OnFrameStart(
  const observer_ptr<engine::FrameContext> context) -> void
{
  dispatcher_->OnFrameStart(context->GetFrameSequenceNumber().get(),
    !context->GetModuleTimingData().is_paused);
}

} // namespace oxygen::control
