//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <gtest/gtest.h>

#include "Oxygen/OxCo/Algorithms.h"
#include "Oxygen/OxCo/Awaitables.h"
#include "Oxygen/OxCo/Co.h"
#include "Oxygen/OxCo/Event.h"
#include "Oxygen/OxCo/Run.h"
#include "Utils/OxCoTestFixture.h"
#include "Utils/TestEventLoop.h"

using namespace oxygen::co;
using namespace oxygen::co::testing;
using namespace std::chrono_literals;

namespace {

// ReSharper disable CppClangTidyCppcoreguidelinesAvoidCapturingLambdaCoroutines

class BasicCancelTest : public OxCoTestFixture { };
TEST_F(BasicCancelTest, BasicCancel)
{
    ::Run(*el_, [this]() -> Co<> {
        bool started = false;
        auto task = [&]() -> Co<> {
            co_await el_->Sleep(1ms);
            started = true;
            co_await el_->Sleep(2ms);
            EXPECT_TRUE(false);
        };

        co_await AnyOf(task, el_->Sleep(2ms));
        EXPECT_TRUE(started);

        co_await el_->Sleep(5ms);
    });
}

class SelfCancelTest : public OxCoTestFixture { };
TEST_F(SelfCancelTest, SelfCancel)
{
    ::Run(*el_, [this]() -> Co<> {
        bool started = false;
        Event cancel_evt;
        auto outer = [&]() -> Co<> {
            auto work = [&]() -> Co<> {
                auto interrupt = [&]() -> Co<> {
                    started = true;
                    co_await el_->Sleep(1ms);
                    cancel_evt.Trigger();
                };
                co_await interrupt;
                co_await kYield;
                EXPECT_TRUE(false);
            };
            co_await AnyOf(work, cancel_evt);
        };
        co_await outer;
        EXPECT_TRUE(started);
    });
}

using TestFunction = std::function<Co<>(TestEventLoop&, bool&)>;

class NoCancelTest
    : public OxCoTestFixture,
      public ::testing::WithParamInterface<TestFunction> {
};

auto NoCancelNextCancellable(TestEventLoop& el, bool& resumed) -> Co<>
{
    co_await el.Sleep(5ms, kNonCancellable);
    LOG_F(1, "next-cancellable");
    resumed = true;
    co_await kYield;
    EXPECT_TRUE(false && "should not reach here");
}
auto NoCancelNextNonCancellable(TestEventLoop& el, bool& resumed) -> Co<>
{
    co_await el.Sleep(5ms, kNonCancellable);
    LOG_F(1, "next-non-cancellable");
    co_await el.Sleep(0ms, kNonCancellable);
    resumed = true;
}
auto NoCancelNextTask(TestEventLoop& el, bool& resumed) -> Co<>
{
    co_await el.Sleep(5ms, kNonCancellable);
    LOG_F(1, "next-task");
    co_await []() -> Co<> { co_return; };
    resumed = true;
}
auto NoCancelNextNonTrivial(TestEventLoop& el, bool& resumed) -> Co<>
{
    co_await el.Sleep(5ms, kNonCancellable);
    LOG_F(1, "next-nontrivial");
    co_await AnyOf(
        [&]() -> Co<> {
            co_await kYield;
            EXPECT_TRUE(false && "should not reach here");
            co_return;
        },
        UntilCancelledAnd([&]() -> Co<> {
            resumed = true;
            co_return;
        }));
}

TEST_P(NoCancelTest, NextTaskIs)
{
    ::Run(*el_, [this]() -> Co<> {
        bool resumed = false;
        auto task2 = [&]() -> Co<> {
            co_await AnyOf(
                GetParam()(*el_, resumed),
                el_->Sleep(2ms));
            EXPECT_EQ(el_->Now(), 5ms);
        };

        co_await task2;
        EXPECT_EQ(el_->Now(), 5ms);
        EXPECT_TRUE(resumed);
    });
}

INSTANTIATE_TEST_SUITE_P(
    NextTaskIs,
    NoCancelTest,
    ::testing::Values(
        &NoCancelNextCancellable,
        &NoCancelNextNonCancellable,
        &NoCancelNextTask,
        &NoCancelNextNonTrivial),
    [](const ::testing::TestParamInfo<NoCancelTest::ParamType>& info) {
        switch (info.index) {
        case 0:
            return "Cancellable";
        case 1:
            return "NonCancellable";
        case 2:
            return "Task";
        case 3:
            return "NonTrivial";
        default:
            return "Unknown";
        }
    });

class NonCancelableTaskTest : public OxCoTestFixture { };
TEST_F(NonCancelableTaskTest, Return)
{
    ::Run(*el_, [this]() -> Co<> {
        auto t1 = [&]() -> Co<> { co_await el_->Sleep(1ms); };
        auto t2 = [&] { return NonCancellable(t1()); };
        co_await t2();
        EXPECT_EQ(el_->Now(), 1ms);
    });
}

class ExceptionNoCancelTest : public OxCoTestFixture { };
TEST_F(ExceptionNoCancelTest, FromNestedTask)
{
    ::Run(*el_, [this]() -> Co<> {
        EXPECT_THROW(
            co_await AnyOf(
                el_->Sleep(1ms),
                [&]() -> Co<> {
                    co_await el_->Sleep(2ms, kNonCancellable);
                    co_await [&]() -> Co<> {
                        co_await el_->Sleep(1ms, kNonCancellable);
                        throw std::runtime_error("boo!");
                    };
                }),
            std::runtime_error);
    });
}
TEST_F(ExceptionNoCancelTest, FromMuxAllOf)
{
    ::Run(*el_, [this]() -> Co<> {
        EXPECT_THROW(
            co_await AnyOf(
                el_->Sleep(1ms),
                [&]() -> Co<> {
                    co_await el_->Sleep(2ms, kNonCancellable);
                    co_await AllOf(el_->Sleep(1ms), [&]() -> Co<> {
                        co_await el_->Sleep(1ms, kNonCancellable);
                        throw std::runtime_error("boo!");
                    });
                }),
            std::runtime_error);
    });
}

class RunOnCancelTest : public OxCoTestFixture { };
TEST_F(RunOnCancelTest, RunOnCancel)
{
    ::Run(*el_, [this]() -> Co<> {
        co_await AnyOf(
            el_->Sleep(2ms),
            UntilCancelledAnd(el_->Sleep(1ms)));
        EXPECT_EQ(el_->Now(), 3ms);
    });
}

} // namespace
