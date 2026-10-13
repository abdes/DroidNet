//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include <Oxygen/Control/EventLog.h>
#include <Oxygen/Testing/GTest.h>

namespace {

using nlohmann::json;
using oxygen::control::ClassifyLogLine;
using oxygen::control::EventLog;

//! Events are numbered across topics and stamped with the current frame.
NOLINT_TEST(EventLog, NumbersAndStampsEvents)
{
  EventLog log;
  log.SetFrame(7U);

  log.Publish("lifecycle", json { { "state", "paused" } });
  log.Publish("command", json { { "job", 1 } });

  const auto batch = log.Since(0U);
  ASSERT_EQ(batch.events.size(), 2U);
  EXPECT_EQ(batch.events[0].seq, 1U);
  EXPECT_EQ(batch.events[1].seq, 2U);
  EXPECT_EQ(batch.events[1].frame, 7U);
  EXPECT_EQ(batch.next_seq, 3U);
  EXPECT_EQ(log.LatestSeq(), 2U);
}

//! Catch-up filters by topic and limit, and reports what the buffer dropped.
NOLINT_TEST(EventLog, CatchUpFiltersLimitsAndReportsDrops)
{
  EventLog log(3U);
  for (int i = 0; i < 5; ++i) {
    log.Publish(i % 2 == 0 ? "a" : "b", json { { "i", i } });
  }

  const auto all = log.Since(0U);
  const auto only_a = log.Since(0U, std::vector<std::string> { "a" });
  const auto limited = log.Since(2U, {}, 1U);

  EXPECT_EQ(all.dropped, 2U);
  ASSERT_EQ(all.events.size(), 3U);
  EXPECT_EQ(all.events.front().seq, 3U);
  EXPECT_EQ(only_a.events.size(), 2U);
  ASSERT_EQ(limited.events.size(), 1U);
  EXPECT_EQ(limited.events.front().seq, 3U);
  EXPECT_EQ(limited.next_seq, 4U);
}

//! Interest is counted per topic.
NOLINT_TEST(EventLog, CountsInterest)
{
  EventLog log;

  log.AddInterest("gpu_work");
  log.AddInterest("gpu_work");
  log.RemoveInterest("gpu_work");
  const bool after_one = log.HasInterest("gpu_work");
  log.RemoveInterest("gpu_work");

  EXPECT_TRUE(after_one);
  EXPECT_FALSE(log.HasInterest("gpu_work"));
}

//! The published handler runs after every event.
NOLINT_TEST(EventLog, CallsThePublishedHandler)
{
  EventLog log;
  int calls = 0;
  log.SetPublishedHandler([&calls] -> void { ++calls; });

  log.Publish("a", json::object());
  log.Publish("b", json::object());

  EXPECT_EQ(calls, 2);
}

//! Script output goes to channel `script`, other lines to their file stem.
NOLINT_TEST(EventLog, ClassifiesLogLines)
{
  const auto script = ClassifyLogLine(
    "H:/Oxygen/Scripting/LogBindings.cpp", "[lua.info] hello world");
  const auto engine
    = ClassifyLogLine("H:\\Oxygen\\Engine\\AsyncEngine.cpp", "frame start");

  EXPECT_EQ(script.channel, "script");
  EXPECT_EQ(script.text, "hello world");
  EXPECT_EQ(engine.channel, "AsyncEngine");
  EXPECT_EQ(engine.text, "frame start");
}

} // namespace
