//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "TestEngine.h"
#include <nlohmann/json.hpp>

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Commands/Command.h>
#include <Oxygen/Commands/CommandRegistry.h>
#include <Oxygen/Commands/Params.h>
#include <Oxygen/Control/CommandDispatcher.h>
#include <Oxygen/Control/EventLog.h>
#include <Oxygen/OxCo/Algorithms.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/OxCo/Event.h>
#include <Oxygen/OxCo/Nursery.h>
#include <Oxygen/OxCo/Run.h>
#include <Oxygen/OxCo/test/Utils/TestEventLoop.h>
#include <Oxygen/Testing/GTest.h>

namespace {

using namespace std::chrono_literals;
using nlohmann::json;
using oxygen::observer_ptr;
using oxygen::co::Co;
using oxygen::co::Nursery;
using oxygen::co::testing::TestEventLoop;
using oxygen::commands::CommandArgs;
using oxygen::commands::CommandContext;
using oxygen::commands::CommandDefinition;
using oxygen::commands::CommandResult;
using oxygen::commands::CommandSource;
using oxygen::commands::ExecutionStatus;
using oxygen::control::CommandDispatcher;
using oxygen::control::EventLog;
using oxygen::control::testing::TestEngine;

//! A dispatcher over a test engine, activated in a test loop's nursery.
class CommandDispatcherTest : public testing::Test {
protected:
  CommandDispatcherTest()
  {
    dispatcher_.RegisterCommands(engine_.GetCommands());
  }

  //! Runs `body` with the dispatcher active; frames are driven by `Frame`.
  template <typename Body> auto RunActive(Body body) -> void
  {
    oxygen::co::Run(loop_, [&]() -> Co<> {
      OXCO_WITH_NURSERY(n)
      {
        co_await n.Start(&CommandDispatcher::ActivateAsync, &dispatcher_);
        // A body that never finishes fails the test instead of hanging it.
        auto [done, timed_out]
          = co_await oxygen::co::AnyOf(body(), loop_.Sleep(5s));
        if (timed_out) {
          ADD_FAILURE() << "the test body did not finish";
        }
        co_return oxygen::co::kCancel;
      };
    });
  }

  //! One frame start, then a loop turn so started jobs advance.
  auto Frame(const bool simulating = true) -> Co<>
  {
    const bool step_frame = engine_.BeginFrame();
    dispatcher_.OnFrameStart(frame_++, simulating && step_frame);
    co_await loop_.Sleep(1ms);
  }

  auto Bind(const std::string& name, const json& args = json::object())
    -> oxygen::commands::CommandInvocation
  {
    return engine_.GetCommands().BindJson(name, args).value();
  }

  TestEventLoop loop_;
  TestEngine engine_;
  EventLog events_;
  CommandDispatcher dispatcher_ {
    observer_ptr<oxygen::IAsyncEngine> { &engine_ }, observer_ptr { &events_ }
  };
  uint64_t frame_ { 1U };
};

//! Posted commands wait for the next frame start, then run in order; each
//! finished job is a `command` event.
NOLINT_TEST_F(CommandDispatcherTest, PostedCommandsRunAtFrameStart)
{
  RunActive([&]() -> Co<> {
    dispatcher_.Post(Bind("ngin.pause"), CommandContext {});
    EXPECT_FALSE(engine_.paused);

    co_await Frame();

    EXPECT_TRUE(engine_.paused);
    const auto batch
      = events_.Since(0U, std::vector<std::string> { "command" });
    EXPECT_EQ(batch.events.size(), 1U);
  });
}

//! A console line runs through the dispatcher and its outcome is recorded
//! in the console's output; a line that does not parse is recorded at once.
NOLINT_TEST_F(CommandDispatcherTest, PostedLinesAreRecordedInTheConsole)
{
  RunActive([&]() -> Co<> {
    dispatcher_.PostLine(
      "no.such.command", CommandContext { .source = CommandSource::kUi });
    dispatcher_.PostLine(
      "ngin.pause", CommandContext { .source = CommandSource::kLocalConsole });
    const auto& records = engine_.GetConsole().GetExecutionRecords();
    EXPECT_EQ(records.size(), 1U);

    co_await Frame();

    // ASSERT_* cannot return from a coroutine.
    EXPECT_EQ(records.size(), 2U);
    if (records.size() == 2U) {
      EXPECT_EQ(records[1].line, "ngin.pause");
      EXPECT_EQ(records[1].result.status, ExecutionStatus::kOk);
    }
    EXPECT_EQ(engine_.GetConsole().GetHistory().Entries().size(), 1U);
  });
}

//! A command run from a console line ranks first in completion, as when the
//! console runs the line itself.
NOLINT_TEST_F(CommandDispatcherTest, PostedLinesRankCompletion)
{
  RunActive([&]() -> Co<> {
    dispatcher_.PostLine(
      "ngin.resume", CommandContext { .source = CommandSource::kLocalConsole });
    co_await Frame();

    const auto candidates = engine_.GetConsole().Complete("ngin.");
    EXPECT_FALSE(candidates.empty());
    if (!candidates.empty()) {
      EXPECT_EQ(candidates.front().token, "ngin.resume");
    }
  });
}

//! `ngin.step 2` replies once its two step frames have run.
NOLINT_TEST_F(CommandDispatcherTest, StepRepliesAfterItsFrames)
{
  auto result = std::optional<CommandResult> {};
  RunActive([&]() -> Co<> {
    OXCO_WITH_NURSERY(n)
    {
      n.Start([&]() -> Co<> {
        result = co_await dispatcher_.Run(
          Bind("ngin.step", json { { "frames", 2 } }), CommandContext {});
      });
      co_await Frame(); // the awaiter posts the job during this frame
      co_await Frame(); // starts the job; the request applies next frame
      co_await Frame(); // first step frame
      EXPECT_FALSE(result.has_value());
      co_await Frame(); // second step frame
      EXPECT_FALSE(result.has_value());
      co_await Frame(); // the second step frame has run
      co_return oxygen::co::kJoin;
    };
  });

  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->status, ExecutionStatus::kOk);
  EXPECT_EQ(result->data["frames"], 2);
  EXPECT_EQ(result->data["ran"], 2);
}

//! `ngin.resume` posted after `ngin.step` in the same frame acts after it,
//! cancelling every step: jobs take effect in arrival order.
NOLINT_TEST_F(CommandDispatcherTest, ResumeCancelsStep)
{
  auto result = std::optional<CommandResult> {};
  RunActive([&]() -> Co<> {
    OXCO_WITH_NURSERY(n)
    {
      n.Start([&]() -> Co<> {
        result = co_await dispatcher_.Run(
          Bind("ngin.step", json { { "frames", 5 } }), CommandContext {});
      });
      co_await Frame();
      dispatcher_.Post(Bind("ngin.resume"), CommandContext {});
      co_await Frame();
      co_await Frame();
      co_await Frame();
      co_return oxygen::co::kJoin;
    };
  });

  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->status, ExecutionStatus::kCancelled);
  EXPECT_EQ(result->data["ran"], 0);
}

//! Pausing again in the frame the steps were cancelled still ends the wait:
//! the engine's pending step count, not the pause state, decides.
NOLINT_TEST_F(CommandDispatcherTest, StepEndsWhenResumedAndPausedAgain)
{
  auto result = std::optional<CommandResult> {};
  RunActive([&]() -> Co<> {
    OXCO_WITH_NURSERY(n)
    {
      n.Start([&]() -> Co<> {
        result = co_await dispatcher_.Run(
          Bind("ngin.step", json { { "frames", 5 } }), CommandContext {});
      });
      co_await Frame();
      dispatcher_.Post(Bind("ngin.resume"), CommandContext {});
      dispatcher_.Post(Bind("ngin.pause"), CommandContext {});
      for (int i = 0; i < 4; ++i) {
        co_await Frame();
      }
      co_return oxygen::co::kJoin;
    };
  });

  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->status, ExecutionStatus::kCancelled);
}

//! `cmd.cancel` cancels a running job, whose awaiter gets `kCancelled`.
NOLINT_TEST_F(CommandDispatcherTest, CancelStopsARunningJob)
{
  static_cast<void>(engine_.GetCommands().Register(CommandDefinition {
    .name = "test.forever",
    .label = "Forever",
    .help = "Never finishes",
    .async_handler = [](CommandArgs, CommandContext) -> Co<CommandResult> {
      // Never triggered: only cancellation ends the job.
      oxygen::co::Event never;
      co_await never;
      co_return CommandResult::Success("unreachable");
    },
  }));
  auto result = std::optional<CommandResult> {};
  RunActive([&]() -> Co<> {
    OXCO_WITH_NURSERY(n)
    {
      n.Start([&]() -> Co<> {
        result = co_await dispatcher_.Run(Bind("test.forever"), {});
      });
      co_await Frame();
      dispatcher_.Post(Bind("cmd.cancel", json { { "job", 1 } }), {});
      co_await Frame();
      co_await Frame();
      co_return oxygen::co::kJoin;
    };
  });

  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->status, ExecutionStatus::kCancelled);
}

} // namespace
