//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "Oxygen/OxCo/Detail/GetAwaitable.h"
#include "Oxygen/OxCo/Detail/MuxHelper.h"
#include "Utils/MockAwaitable.h"
#include "Utils/MockMux.h"

using namespace oxygen::co;
using namespace oxygen::co::detail;
using namespace oxygen::co::testing;

using ::testing::_; // NOLINT(bugprone-reserved-identifier)
using ::testing::An;
using ::testing::IsNull;
using ::testing::Return;

namespace {

class TestMuxHelper : public MuxHelper<MockMux, MockAwaitable> {
public:
    using MuxHelper::GetState;
    using MuxHelper::InState;
    using MuxHelper::MuxHelper;
};

class BaseFixture : public ::testing::Test {
public:
    void SetUp() override
    {
        ::testing::internal::CaptureStderr();
    }

    void TearDown() override
    {
        const auto captured_stderr = ::testing::internal::GetCapturedStderr();
        std::cout << "Captured stderr:\n"
                  << captured_stderr << '\n';
    }
};
class StartWithoutCancellation : public BaseFixture { };
class CancellationBeforeStart : public BaseFixture { };
class CancellationPending : public BaseFixture { };
class CancellationAfterRunning : public BaseFixture { };
class CancellationPendingAwaitableResumes : public BaseFixture { };
class ReportImmediateResult : public BaseFixture { };
class ResultRetrieval : public BaseFixture { };

// 1. Starting the Awaitable Without Cancellation:
//   1a. Immediate completion:
//    State is kNotStarted
//    Awaitable is ready: awaitable.await_ready() returns true
//    => co_await immediately completes with result
//    -> MuxHelper transitions to State::kSucceeded.
//    -> await_ready() is called and returns true.
//    -> await_resume() is called and returns the result.
//    -> Invoke(nullptr) is called (with ex = nullptr).
TEST_F(StartWithoutCancellation, ImmediateCompletion)
{
    MockAwaitable awaitable;
    MockMux mux;

    EXPECT_CALL(*awaitable.Mock(), await_ready()).Times(2).WillRepeatedly(Return(true));
    EXPECT_CALL(*awaitable.Mock(), await_resume())
        .WillOnce(Return(41));

    EXPECT_CALL(*mux.Mock(), Invoke(IsNull())).WillOnce(Return());

    TestMuxHelper helper(std::move(awaitable));
    EXPECT_TRUE(helper.InState(TestMuxHelper::State::kNotStarted));
    EXPECT_TRUE(helper.IsReady()); // calls await_ready()

    helper.Bind(mux);
    helper.Suspend();
    EXPECT_TRUE(helper.InState(TestMuxHelper::State::kSucceeded));
    EXPECT_TRUE(helper.IsReady());
    EXPECT_EQ(41, std::move(helper).Result());
}

// 1. Starting the Awaitable Without Cancellation:
//   1b. Suspension Required:
//     State is kNotStarted
//     Awaitable is not ready: awaitable.await_ready() returns false
//     => awaitable needs to suspend, await_suspend() is called
//     -> MuxHelper transitions to State::kRunning initially
//     -> await_ready() is called and returns false.
//     -> await_suspend() is called with the coroutine handle.
//     When awaitable resumes:
//     -> await_resume() is called and returns the result.
//     -> MuxHelper transitions to State::kSucceeded after resuming.
//     -> Invoke(nullptr) is called (with ex = nullptr).
TEST_F(StartWithoutCancellation, SuspensionRequired)
{
    MockAwaitable awaitable;
    MockMux mux;

    // Expect await_ready() to return false, indicating we need to suspend
    EXPECT_CALL(*awaitable.Mock(), await_ready()).Times(2).WillRepeatedly(Return(false));
    // Expect await_suspend() to be called with the coroutine handle
    EXPECT_CALL(*awaitable.Mock(), await_suspend(_))
        .WillOnce([](const std::coroutine_handle<> h) {
            // Simulate the awaitable will resume immediately
            h.resume();
        });
    // Expect await_resume() to be called when resumed
    EXPECT_CALL(*awaitable.Mock(), await_resume()).WillOnce(Return(42));
    // Expect Mux Invoke to be called with nullptr exception (success)
    EXPECT_CALL(*mux.Mock(), Invoke(IsNull())).WillOnce(Return());

    TestMuxHelper helper(std::move(awaitable));
    EXPECT_TRUE(helper.InState(TestMuxHelper::State::kNotStarted));
    EXPECT_FALSE(helper.IsReady()); // calls await_ready()

    helper.Bind(mux);
    helper.Suspend();
    EXPECT_TRUE(helper.InState(TestMuxHelper::State::kSucceeded));
    EXPECT_TRUE(helper.IsReady());
    EXPECT_EQ(42, std::move(helper).Result());
}

// 2. Cancellation Before Starting:
//   2a. Early Cancellation Succeeds:
//     State is kNotStarted
//     awaitable.await_early_cancel() returns true
//     => Cancellation succeeds before starting
//     -> MuxHelper transitions to State::kCancelled.
//     -> await_early_cancel() is called and returns true.
//     -> Invoke(nullptr) is called (with ex = nullptr).
TEST_F(CancellationBeforeStart, EarlyCancellationSucceeds)
{
    MockAwaitable awaitable;
    MockMux mux;

    EXPECT_CALL(*awaitable.Mock(), await_ready()).WillOnce(Return(true));

    // Expect await_early_cancel() to be called and return true
    EXPECT_CALL(*awaitable.Mock(), await_early_cancel())
        .WillOnce(Return(true));
    // Expect Mux Invoke to be called with nullptr exception (cancelled)
    EXPECT_CALL(*mux.Mock(), Invoke(IsNull())).WillOnce(Return());

    TestMuxHelper helper(std::move(awaitable));
    helper.Bind(mux);
    EXPECT_TRUE(helper.InState(TestMuxHelper::State::kNotStarted));
    EXPECT_TRUE(helper.IsReady()); // calls await_ready()

    const bool cancelled = helper.Cancel();
    EXPECT_TRUE(cancelled);
    EXPECT_TRUE(helper.InState(TestMuxHelper::State::kCancelled));
    EXPECT_FALSE(helper.IsReady());
}

// 2. Cancellation Before Starting:
//   2b. Early Cancellation Fails:
//     State is kNotStarted
//     awaitable.await_early_cancel() returns false
//     => Cancellation pending; cannot cancel before start
//     -> MuxHelper transitions to State::kCancellationPending.
//     -> await_early_cancel() is called and returns false.
//     -> No methods are called on Mux at this point.
TEST_F(CancellationBeforeStart, EarlyCancellationFails)
{
    MockAwaitable awaitable;
    MockMux mux;

    EXPECT_CALL(*awaitable.Mock(), await_ready()).WillOnce(Return(true));

    // Expect await_early_cancel() to be called and return false
    EXPECT_CALL(*awaitable.Mock(), await_early_cancel())
        .WillOnce(Return(false));

    TestMuxHelper helper(std::move(awaitable));
    helper.Bind(mux);
    EXPECT_TRUE(helper.InState(TestMuxHelper::State::kNotStarted));
    EXPECT_TRUE(helper.IsReady()); // calls await_ready()

    const bool cancelled = helper.Cancel();
    EXPECT_FALSE(cancelled);
    EXPECT_TRUE(helper.InState(TestMuxHelper::State::kCancellationPending));
}

// 3. Starting After Cancellation Pending:
//   3a. Awaitable Ready Immediately, Must Not Resume:
//     State is kCancellationPending
//     awaitable.await_ready() returns true
//     awaitable.await_must_resume() returns false
//     => MuxHelper cancels the operation immediately
//     -> MuxHelper transitions to State::kCancelled.
//     -> await_ready() is called and returns true.
//     -> await_must_resume() is called and returns false.
//     -> Invoke(nullptr) is called (with ex = nullptr).
TEST_F(CancellationPending, AwaitableReadyMustNotResume)
{
    MockAwaitable awaitable;
    MockMux mux;

    // Simulate early cancellation failure
    EXPECT_CALL(*awaitable.Mock(), await_early_cancel())
        .WillOnce(Return(false));

    // Simulate await_ready() returning true
    EXPECT_CALL(*awaitable.Mock(), await_ready())
        .WillOnce(Return(true));
    // await_must_resume() returns false
    EXPECT_CALL(*awaitable.Mock(), await_must_resume())
        .WillOnce(Return(false));

    // Expect Mux Invoke to be called with nullptr exception (cancelled)
    EXPECT_CALL(*mux.Mock(), Invoke(IsNull())).WillOnce(Return());

    TestMuxHelper helper(std::move(awaitable));
    helper.Bind(mux);

    // Cancel before starting
    helper.Cancel();
    EXPECT_TRUE(helper.InState(TestMuxHelper::State::kCancellationPending));

    // Suspend after cancellation pending
    helper.Suspend();
    EXPECT_TRUE(helper.InState(TestMuxHelper::State::kCancelled));
}

// 3. Starting After Cancellation Pending:
//   3b. Awaitable Ready Immediately, Must Resume:
//     State is kCancellationPending
//     awaitable.await_ready() returns true
//     awaitable.await_must_resume() returns true
//     => MuxHelper must resume the awaitable to get the result
//     -> MuxHelper transitions to State::kSucceeded.
//     -> await_ready() is called and returns true.
//     -> await_must_resume() is called and returns true.
//     -> await_resume() is called and returns the result.
//     -> Invoke(nullptr) is called (with ex = nullptr).
TEST_F(CancellationPending, AwaitableReadyMustResume)
{
    MockAwaitable awaitable;
    MockMux mux;

    // Simulate early cancellation failure
    EXPECT_CALL(*awaitable.Mock(), await_early_cancel())
        .WillOnce(Return(false));

    // Simulate await_ready() returning true
    EXPECT_CALL(*awaitable.Mock(), await_ready())
        .WillOnce(Return(true));
    // await_must_resume() returns true
    EXPECT_CALL(*awaitable.Mock(), await_must_resume())
        .WillOnce(Return(true));
    // Expect await_resume() to be called and succeed
    EXPECT_CALL(*awaitable.Mock(), await_resume())
        .WillOnce(Return(43));

    // Expect Mux Invoke to be called with nullptr exception (success)
    EXPECT_CALL(*mux.Mock(), Invoke(IsNull())).WillOnce(Return());

    TestMuxHelper helper(std::move(awaitable));
    helper.Bind(mux);

    // Cancel before starting
    helper.Cancel();
    EXPECT_TRUE(helper.InState(TestMuxHelper::State::kCancellationPending));
    EXPECT_FALSE(helper.IsReady()); // does not call await_ready()

    // Suspend after cancellation pending
    helper.Suspend();
    EXPECT_TRUE(helper.InState(TestMuxHelper::State::kSucceeded));
    EXPECT_TRUE(helper.IsReady());
    EXPECT_EQ(43, std::move(helper).Result());
}

// 3. Starting After Cancellation Pending:
//   3c. Awaitable Needs Suspension:
//     State is kCancellationPending
//     awaitable.await_ready() returns false
//     => awaitable needs to suspend
//     -> MuxHelper transitions to State::kCancelling.
//     -> await_ready() is called and returns false.
//     -> await_suspend() is called.
//     When awaitable resumes:
//     -> await_must_resume() is called and returns false.
//     -> MuxHelper transitions to State::kCancelled.
//     -> Invoke(nullptr) is called (with ex = nullptr).
TEST_F(CancellationPending, AwaitableNeedsSuspension)
{
    MockAwaitable awaitable;
    MockMux mux;

    // Simulate early cancellation failure
    EXPECT_CALL(*awaitable.Mock(), await_early_cancel())
        .WillOnce(Return(false));

    // Simulate await_ready() returning false
    EXPECT_CALL(*awaitable.Mock(), await_ready())
        .WillOnce(Return(false));
    // Expect await_suspend() to be called
    EXPECT_CALL(*awaitable.Mock(), await_suspend(_))
        .WillOnce([](const std::coroutine_handle<> h) {
            // Simulate awaitable resumes after some time
            h.resume();
        });
    // await_must_resume() called during Invoke()
    EXPECT_CALL(*awaitable.Mock(), await_must_resume())
        .WillOnce(Return(false));

    // Expect Mux Invoke to be called with nullptr exception (cancelled)
    EXPECT_CALL(*mux.Mock(), Invoke(IsNull())).WillOnce(Return());

    TestMuxHelper helper(std::move(awaitable));
    helper.Bind(mux);

    // Cancel before starting
    helper.Cancel();
    EXPECT_TRUE(helper.InState(TestMuxHelper::State::kCancellationPending));
    EXPECT_FALSE(helper.IsReady()); // does not call await_ready()

    // Suspend after cancellation pending
    helper.Suspend();
    // After awaitable resumes, state should be Cancelled
    EXPECT_TRUE(helper.InState(TestMuxHelper::State::kCancelled));
}

// 4. Cancellation After Running:
//   4a. Immediate Cancellation Succeeds:
//     State is kRunning
//     awaitable.await_cancel() returns true
//     => Cancellation succeeds immediately after running
//     -> MuxHelper transitions to State::kCancelled.
//     -> await_cancel() is called and returns true.
//     -> Invoke(nullptr) is called (with ex = nullptr).
TEST_F(CancellationAfterRunning, ImmediateCancellationSucceeds)
{
    MockAwaitable awaitable;
    MockMux mux;

    // Simulate await_ready() returning false
    EXPECT_CALL(*awaitable.Mock(), await_ready())
        .WillOnce(Return(false));
    // Expect await_suspend() to be called
    EXPECT_CALL(*awaitable.Mock(), await_suspend(_))
        .Times(1);

    // Simulate immediate cancellation success
    EXPECT_CALL(*awaitable.Mock(), await_cancel(_))
        .WillOnce(Return(true));

    // Expect Mux Invoke to be called with nullptr exception (cancelled)
    EXPECT_CALL(*mux.Mock(), Invoke(IsNull())).WillOnce(Return());

    TestMuxHelper helper(std::move(awaitable));
    helper.Bind(mux);
    helper.Suspend();

    EXPECT_TRUE(helper.InState(TestMuxHelper::State::kRunning));

    const bool cancelled = helper.Cancel();
    EXPECT_TRUE(cancelled);
    EXPECT_TRUE(helper.InState(TestMuxHelper::State::kCancelled));
    EXPECT_FALSE(helper.IsReady()); // does not call await_ready()
}

// 4. Cancellation After Running:
//   4b. Cancellation Pending Until Resume:
//     State is kRunning
//     awaitable.await_cancel() returns false
//     => Cancellation is pending until awaitable resumes
//     -> MuxHelper transitions to State::kCancelling.
//     -> await_cancel() is called and returns false.
//     -> No methods are called on Mux at this point.
TEST_F(CancellationAfterRunning, CancellationPendingUntilResume)
{
    MockAwaitable awaitable;
    MockMux mux;

    // Simulate await_ready() returning false
    EXPECT_CALL(*awaitable.Mock(), await_ready())
        .WillOnce(Return(false));
    // Expect await_suspend() to be called
    EXPECT_CALL(*awaitable.Mock(), await_suspend(_))
        .WillOnce([](std::coroutine_handle<>) {
            // Simulate awaitable resumes later
        });

    // Simulate await_cancel() returning false (cancellation pending)
    EXPECT_CALL(*awaitable.Mock(), await_cancel(_))
        .WillOnce(Return(false));

    TestMuxHelper helper(std::move(awaitable));
    helper.Bind(mux);
    helper.Suspend();

    EXPECT_TRUE(helper.InState(TestMuxHelper::State::kRunning));

    const bool cancelled = helper.Cancel();
    EXPECT_FALSE(cancelled);
    EXPECT_TRUE(helper.InState(TestMuxHelper::State::kCancelling));
}

// 5. Awaitable Resumes After Cancellation Pending:
//   5a. Must Not Resume Evaluation:
//     State is kCancelling
//     awaitable resumes and Invoke() is called
//     awaitable.await_must_resume() returns false
//     => MuxHelper cancels the operation without resuming awaitable
//     -> MuxHelper transitions to State::kCancelled.
//     -> await_must_resume() is called and returns false.
//     -> Invoke(nullptr) is called (with ex = nullptr).
TEST_F(CancellationPendingAwaitableResumes, MustNotResume)
{
    MockAwaitable awaitable;
    MockMux mux;

    // Simulate await_ready() returning false
    EXPECT_CALL(*awaitable.Mock(), await_ready())
        .WillOnce(Return(false));
    // Expect await_suspend() to be called
    EXPECT_CALL(*awaitable.Mock(), await_suspend(_))
        .WillOnce([](std::coroutine_handle<>) {
            // Simulate awaitable resumes later
        });

    // Simulate await_cancel() returning false (cancellation pending)
    EXPECT_CALL(*awaitable.Mock(), await_cancel(_))
        .WillOnce(Return(false));

    // Simulate await_must_resume() returning false
    EXPECT_CALL(*awaitable.Mock(), await_must_resume())
        .WillOnce(Return(false));

    // Expect Mux Invoke to be called with nullptr exception (cancelled)
    EXPECT_CALL(*mux.Mock(), Invoke(IsNull())).WillOnce(Return());

    TestMuxHelper helper(std::move(awaitable));
    helper.Bind(mux);
    helper.Suspend();

    helper.Cancel();
    EXPECT_TRUE(helper.InState(TestMuxHelper::State::kCancelling));

    // Simulate awaitable resumes and invokes
    helper.resume_fn(&helper);
    EXPECT_TRUE(helper.InState(TestMuxHelper::State::kCancelled));
    EXPECT_FALSE(helper.IsReady()); // does not call await_ready()
}

// 5. Awaitable Resumes After Cancellation Pending:
//   5b. Resuming to Get Result:
//     State is kCancelling
//     awaitable resumes and Invoke() is called
//     awaitable.await_must_resume() returns true
//     awaitable.await_resume() succeeds
//     => MuxHelper completes successfully by resuming awaitable
//     -> MuxHelper transitions to State::kSucceeded.
//     -> await_must_resume() is called and returns true.
//     -> await_resume() is called and returns the result.
//     -> Invoke(nullptr) is called (with ex = nullptr).
TEST_F(CancellationPendingAwaitableResumes, MustResume)
{
    MockAwaitable awaitable;
    MockMux mux;

    // Simulate await_ready() returning false
    EXPECT_CALL(*awaitable.Mock(), await_ready())
        .WillOnce(Return(false));
    // Expect await_suspend() to be called
    EXPECT_CALL(*awaitable.Mock(), await_suspend(_))
        .WillOnce([](std::coroutine_handle<>) {
            // Simulate awaitable resumes later
        });

    // Simulate await_cancel() returning false (cancellation pending)
    EXPECT_CALL(*awaitable.Mock(), await_cancel(_))
        .WillOnce(Return(false));

    // Simulate await_must_resume() returning true
    EXPECT_CALL(*awaitable.Mock(), await_must_resume())
        .WillOnce(Return(true));
    // Expect await_resume() to be called and succeed
    EXPECT_CALL(*awaitable.Mock(), await_resume())
        .WillOnce(Return(45));

    // Expect Mux Invoke to be called with nullptr exception (success)
    EXPECT_CALL(*mux.Mock(), Invoke(IsNull())).WillOnce(Return());

    TestMuxHelper helper(std::move(awaitable));
    helper.Bind(mux);
    helper.Suspend();

    helper.Cancel();
    EXPECT_TRUE(helper.InState(TestMuxHelper::State::kCancelling));

    // Simulate awaitable resumes and invokes
    helper.resume_fn(&helper);
    EXPECT_TRUE(helper.InState(TestMuxHelper::State::kSucceeded));
    EXPECT_TRUE(helper.IsReady());
    EXPECT_EQ(45, std::move(helper).Result());
}

// 5. Awaitable Resumes After Cancellation Pending:
//   5c. Exception During Resume:
//     State is kCancelling
//     awaitable resumes and Invoke() is called
//     awaitable.await_must_resume() returns true
//     awaitable.await_resume() throws an exception
//     => MuxHelper transitions to State::kFailed.
//     -> await_must_resume() is called and returns true.
//     -> await_resume() is called and throws.
//     -> Invoke(exception_ptr) is called with the exception.
TEST_F(CancellationPendingAwaitableResumes, AwaitResumeThrows)
{
    MockAwaitable awaitable;
    MockMux mux;

    // Simulate await_ready() returning false
    EXPECT_CALL(*awaitable.Mock(), await_ready())
        .WillOnce(Return(false));
    // Expect await_suspend() to be called
    EXPECT_CALL(*awaitable.Mock(), await_suspend(_))
        .WillOnce([](std::coroutine_handle<>) {
            // Simulate awaitable resumes later
        });

    // Simulate await_cancel() returning false (cancellation pending)
    EXPECT_CALL(*awaitable.Mock(), await_cancel(_))
        .WillOnce(Return(false));

    // Simulate await_must_resume() returning true
    EXPECT_CALL(*awaitable.Mock(), await_must_resume())
        .WillOnce(Return(true));
    // Expect await_resume() to throw an exception
    EXPECT_CALL(*awaitable.Mock(), await_resume())
        .WillOnce([]() -> int {
            throw std::runtime_error("Error during await_resume");
        });

    // Expect Mux Invoke to be called with exception_ptr
    EXPECT_CALL(*mux.Mock(), Invoke(An<const std::exception_ptr&>()))
        .WillOnce(Return());

    TestMuxHelper helper(std::move(awaitable));
    helper.Bind(mux);
    helper.Suspend();

    helper.Cancel();
    EXPECT_TRUE(helper.InState(TestMuxHelper::State::kCancelling));

    // Simulate awaitable resumes and invokes
    helper.resume_fn(&helper);
    EXPECT_TRUE(helper.InState(TestMuxHelper::State::kFailed));
    EXPECT_TRUE(helper.IsReady()); // does not call await_ready()
}

// 6. Reporting Immediate Result:
//   6a. Cancellation Pending, Must Not Resume:
//     State is kCancellationPending
//     awaitable.await_must_resume() returns false
//     => MuxHelper cancels the operation immediately
//     -> MuxHelper transitions to State::kCancelled.
//     -> await_must_resume() is called and returns false.
//     -> Invoke(nullptr) is called (with ex = nullptr).
TEST_F(ReportImmediateResult, CancellationPendingMustNotResume)
{
    MockAwaitable awaitable;
    MockMux mux;

    // Simulate early cancellation failure
    EXPECT_CALL(*awaitable.Mock(), await_early_cancel())
        .WillOnce(Return(false));

    // Simulate await_must_resume() returning false
    EXPECT_CALL(*awaitable.Mock(), await_must_resume())
        .WillOnce(Return(false));

    // Expect Mux Invoke to be called with nullptr exception (cancelled)
    EXPECT_CALL(*mux.Mock(), Invoke(IsNull())).WillOnce(Return());

    TestMuxHelper helper(std::move(awaitable));
    helper.Bind(mux);

    // Cancel before starting
    helper.Cancel();
    EXPECT_TRUE(helper.InState(TestMuxHelper::State::kCancellationPending));

    helper.ReportImmediateResult();

    EXPECT_TRUE(helper.InState(TestMuxHelper::State::kCancelled));
    EXPECT_FALSE(helper.IsReady()); // does not call await_ready()
}

// 6. Reporting Immediate Result:
//   6b. Awaitable Is Ready:
//     State is kNotStarted
//     awaitable.await_ready() returns true
//     => MuxHelper proceeds to get the result immediately
//     -> MuxHelper transitions to State::kSucceeded.
//     -> await_ready() is called and returns true.
//     -> await_resume() is called and returns the result.
//     -> Invoke(nullptr) is called (with ex = nullptr).
TEST_F(ReportImmediateResult, AwaitableIsReady)
{
    MockAwaitable awaitable;
    MockMux mux;

    // Simulate await_ready() returning true
    EXPECT_CALL(*awaitable.Mock(), await_ready())
        .WillOnce(Return(true));
    // Expect await_resume() to be called and succeed
    EXPECT_CALL(*awaitable.Mock(), await_resume())
        .WillOnce(Return(46));

    // Expect Mux Invoke to be called with nullptr exception (success)
    EXPECT_CALL(*mux.Mock(), Invoke(IsNull())).WillOnce(Return());

    TestMuxHelper helper(std::move(awaitable));
    helper.Bind(mux);

    helper.ReportImmediateResult();

    EXPECT_TRUE(helper.InState(TestMuxHelper::State::kSucceeded));
    EXPECT_TRUE(helper.IsReady());
    EXPECT_EQ(46, std::move(helper).Result());
}

// 6. Reporting Immediate Result:
//   6c. Awaitable Not Ready, Cancelled:
//     State is kNotStarted
//     awaitable.await_ready() returns false
//     awaitable.await_early_cancel() returns true
//     => MuxHelper cancels the operation before starting
//     -> MuxHelper transitions to State::kCancelled.
//     -> await_ready() is called and returns false.
//     -> await_early_cancel() is called and returns true.
//     -> Invoke(nullptr) is called (with ex = nullptr).
TEST_F(ReportImmediateResult, AwaitableNotReadyCancelled)
{
    MockAwaitable awaitable;
    MockMux mux;

    // Simulate await_ready() returning false
    EXPECT_CALL(*awaitable.Mock(), await_ready())
        .WillOnce(Return(false));
    // Simulate await_early_cancel() returning true
    EXPECT_CALL(*awaitable.Mock(), await_early_cancel())
        .WillOnce(Return(true));

    // Expect Mux Invoke to be called with nullptr exception (cancelled)
    EXPECT_CALL(*mux.Mock(), Invoke(IsNull())).WillOnce(Return());

    TestMuxHelper helper(std::move(awaitable));
    helper.Bind(mux);

    helper.ReportImmediateResult();

    EXPECT_TRUE(helper.InState(TestMuxHelper::State::kCancelled));
    EXPECT_FALSE(helper.IsReady()); // does not call await_ready()
}

// 7. Obtaining Results:
//   7a. Retrieving the Result After Success:
//     State is kSucceeded
//     => Result() can be called to retrieve the stored result
//     -> No state change occurs.
//     -> No additional methods are called.
TEST_F(ResultRetrieval, RetrieveResultAfterSuccess)
{
    MockAwaitable awaitable;
    MockMux mux;

    // Simulate await_ready() returning true
    EXPECT_CALL(*awaitable.Mock(), await_ready())
        .WillOnce(Return(true));
    // Expect await_resume() to be called and return a value
    EXPECT_CALL(*awaitable.Mock(), await_resume())
        .WillOnce(Return(47));

    // Expect Mux Invoke to be called with nullptr exception (success)
    EXPECT_CALL(*mux.Mock(), Invoke(IsNull())).WillOnce(Return());

    TestMuxHelper helper(std::move(awaitable));
    helper.Bind(mux);
    helper.Suspend();

    EXPECT_TRUE(helper.InState(TestMuxHelper::State::kSucceeded));
    EXPECT_TRUE(helper.IsReady()); // does not call await_ready()

    const int result = std::move(helper).Result();
    EXPECT_EQ(result, 47);
}

// 7. Obtaining Results:
//   7b. Checking for Optional Result After Cancellation:
//     State is kCancelled
//     => AsOptional() can be called to check for result
//     -> Returns an empty optional.
//     -> No state change occurs.
//     -> No additional methods are called.
TEST_F(ResultRetrieval, AsOptionalAfterCancellation)
{
    MockAwaitable awaitable;
    MockMux mux;

    // Simulate early cancellation success
    EXPECT_CALL(*awaitable.Mock(), await_early_cancel())
        .WillOnce(Return(true));
    // Expect Mux Invoke to be called with nullptr exception (cancelled)
    EXPECT_CALL(*mux.Mock(), Invoke(IsNull())).WillOnce(Return());

    TestMuxHelper helper(std::move(awaitable));
    helper.Bind(mux);

    helper.Cancel();
    EXPECT_TRUE(helper.InState(TestMuxHelper::State::kCancelled));
    EXPECT_FALSE(helper.IsReady()); // does not call await_ready()

    const auto opt_result = std::move(helper).AsOptional();
    EXPECT_FALSE(opt_result.has_value());
}

} // namespace
