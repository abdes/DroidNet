//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/OxCo/Executor.h>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/OxCo/EventLoop.h>

using oxygen::co::EventLoopID;
using oxygen::co::Executor;

namespace {

class ExecutorTest : public testing::Test {
protected:
  // NOLINTBEGIN(*-non-private-member-variables-in-classes)
  struct Loops {
    int main { 1 };
    int other { 2 };
  } loops_;
  EventLoopID main_loop_id_ { &loops_.main };
  EventLoopID other_loop_id_ { &loops_.other };
  Executor executor_ = Executor(main_loop_id_);
  Executor another_executor_ = Executor(other_loop_id_);
  // NOLINTEND(*-non-private-member-variables-in-classes)

  void SetUp() override { testing::internal::CaptureStderr(); }

  void TearDown() override
  {
    const auto captured_stderr = testing::internal::GetCapturedStderr();
    std::cout << "Captured stderr:\n" << captured_stderr << '\n';
  }
};

void TestFunction(int* value) noexcept
{
  DLOG_F(1, "TestFunction ({})", *value);
  (*value)++;
}

struct Context {
  int* value { nullptr };
  Executor* executor { nullptr };
};

void RunTestFunction(Context* context) noexcept
{
  DLOG_F(1, "RunTestFunction");
  EXPECT_NE(context, nullptr);
  EXPECT_NE(context->value, nullptr);
  EXPECT_NE(context->executor, nullptr);
  context->executor->RunSoon(TestFunction, context->value);
}

void OuterFunction(Context* context) noexcept
{
  DLOG_F(1, "OuterFunction");
  EXPECT_NE(context, nullptr);
  EXPECT_NE(context->value, nullptr);
  EXPECT_NE(context->executor, nullptr);
  context->executor->RunSoon(RunTestFunction, context);
}

NOLINT_TEST_F(ExecutorTest, RunSoonExecutesTaskImmediately)
{
  int value = 0;
  executor_.RunSoon(TestFunction, &value);
  EXPECT_EQ(value, 1);
}

NOLINT_TEST_F(ExecutorTest, ScheduleDefersTaskExecution)
{
  int value = 0;
  executor_.Schedule(TestFunction, &value);
  EXPECT_EQ(value, 0);
  executor_.RunSoon();
  EXPECT_EQ(value, 1);
}

NOLINT_TEST_F(ExecutorTest, MultipleTasksExecuteInOrder)
{
  int value = 0;
  executor_.Schedule(TestFunction, &value);
  executor_.Schedule(TestFunction, &value);
  EXPECT_EQ(value, 0);
  executor_.RunSoon();
  EXPECT_EQ(value, 2);
}

NOLINT_TEST_F(ExecutorTest, CaptureExecutesTasksInCapturedList)
{
  int value = 0;
  executor_.Capture([&] {
    executor_.Schedule(TestFunction, &value);
    EXPECT_EQ(value, 0);
  });
  EXPECT_EQ(value, 1);
}

NOLINT_TEST_F(ExecutorTest, NestedRunSoonDoesNotCauseInfiniteLoop)
{
  int value = 0;
  Context context = { .value = &value, .executor = &executor_ };
  executor_.RunSoon(OuterFunction, &context);
  EXPECT_EQ(value, 1);
}

NOLINT_TEST_F(ExecutorTest, RunSoonFromAnotherExecutor)
{
  int value = 0;
  Context other_context = { .value = &value, .executor = &another_executor_ };
  Context this_context = { .value = &value, .executor = &executor_ };

  another_executor_.Schedule(OuterFunction, &other_context);
  executor_.Schedule(RunTestFunction, &this_context);
  executor_.RunSoon(
    +[](const Context* context) noexcept { context->executor->RunSoon(); },
    &other_context);
  EXPECT_EQ(value, 2);
}

NOLINT_TEST_F(ExecutorTest, DrainWhenRunning)
{
  int value = 0;
  executor_.Schedule(TestFunction, &value);

  const Context this_context = { .value = &value, .executor = &executor_ };

  executor_.RunSoon(
    +[](const Context* context) noexcept { context->executor->Drain(); },
    &this_context);
}

NOLINT_TEST_F(ExecutorTest, DrainWhenEmpty)
{
  executor_.Drain();
  SUCCEED(); // If no exception is thrown, the test passes
}

NOLINT_TEST_F(ExecutorTest, NestedRunSoonFromAnotherExecutor)
{
  int value = 0;
  Context this_context = { .value = &value, .executor = &executor_ };
  another_executor_.RunSoon(OuterFunction, &this_context);
  EXPECT_EQ(value, 1);
}

NOLINT_TEST_F(ExecutorTest, MultipleExecutorsRunIndependently)
{
  int value1 = 0;
  int value2 = 0;
  executor_.Schedule(TestFunction, &value1);
  another_executor_.Schedule(TestFunction, &value2);

  executor_.RunSoon();
  EXPECT_EQ(value1, 1);
  EXPECT_EQ(value2, 0);

  another_executor_.RunSoon();
  EXPECT_EQ(value2, 1);
}

NOLINT_TEST_F(ExecutorTest, RunSoonWhileDraining)
{
  int value = 0;
  Context this_context = { .value = &value, .executor = &executor_ };
  executor_.Schedule(OuterFunction, &this_context);

  executor_.RunSoon(
    +[](const Context* context) noexcept { context->executor->Drain(); },
    &this_context);
  EXPECT_EQ(value, 1);
}

NOLINT_TEST_F(ExecutorTest, ScheduleWhileDraining)
{
  int value = 0;
  const Context this_context = { .value = &value, .executor = &executor_ };
  executor_.Schedule(
    +[](const Context* context) noexcept {
      context->executor->Schedule(TestFunction, context->value);
    },
    &this_context);

  executor_.RunSoon();
  EXPECT_EQ(value, 1);
}

} // namespace
