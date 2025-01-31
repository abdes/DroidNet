//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "Oxygen/OxCo/Detail/MuxBase.h"

using namespace oxygen::co::detail;

namespace {

// Custom coroutine handle to detect when resume is called
class TestCoroutine {
public:
    struct promise_type {
        auto get_return_object() -> TestCoroutine { return TestCoroutine { this }; }
        // ReSharper disable CppMemberFunctionMayBeStatic
        // NOLINTNEXTLINE(*-convert-member-functions-to-static)
        auto initial_suspend() noexcept -> std::suspend_never { return {}; }
        // NOLINTNEXTLINE(*-convert-member-functions-to-static)
        auto final_suspend() noexcept -> std::suspend_always { return {}; }
        // NOLINTNEXTLINE(*-convert-member-functions-to-static)
        [[noreturn]] auto unhandled_exception() -> void { std::terminate(); }
        void return_void() { }
        // ReSharper restore CppMemberFunctionMayBeStatic
    };

    explicit TestCoroutine(promise_type* p)
        : handle_(std::coroutine_handle<promise_type>::from_promise(*p))
    {
    }
    ~TestCoroutine() { handle_.destroy(); }

    OXYGEN_MAKE_NON_COPYABLE(TestCoroutine)
    OXYGEN_DEFAULT_MOVABLE(TestCoroutine)

    std::coroutine_handle<promise_type> handle_;
};

// TestMux class without overriding any MuxBase methods
template <bool Skippable = false, bool Abortable = false>
class TestMux final : public MuxBase<TestMux<Skippable, Abortable>> {
public:
    static constexpr auto IsSkippable() noexcept -> bool { return Skippable; }
    static constexpr auto IsAbortable() noexcept -> bool { return Abortable; }

    TestMux(const size_t size, const size_t min_ready, const bool internal_cancel_result = false)
        : internal_cancel_called_(0)
        , size_(size)
        , min_ready_(min_ready)
        , internal_cancel_result_(internal_cancel_result)
    {
    }

    ~TestMux() = default;
    OXYGEN_MAKE_NON_COPYABLE(TestMux)
    OXYGEN_DEFAULT_MOVABLE(TestMux)

    // Implementing the Multiplexer concept
    [[nodiscard]] auto Size() const -> size_t { return size_; }
    [[nodiscard]] auto MinReady() const -> size_t { return min_ready_; }
    auto InternalCancel()
    {
        ++internal_cancel_called_;
        if (internal_cancel_result_) {
            // Simulate synchronous cancel success for the remaining awaitables
            size_ = min_ready_;
        }
        return internal_cancel_result_;
    }

    // Expose methods and members for testing
    using MuxBase<TestMux>::DoSuspend;
    using MuxBase<TestMux>::Invoke;
    using MuxBase<TestMux>::ReRaise;
    using MuxBase<TestMux>::HasException;

    // Expose count_ and parent_ for testing
    using MuxBase<TestMux>::count_;
    using MuxBase<TestMux>::parent_;

    size_t internal_cancel_called_;

private:
    size_t size_;
    size_t min_ready_;
    bool internal_cancel_result_;
};

class MuxBaseTest : public ::testing::Test {
public:
    void SetUp() override { ::testing::internal::CaptureStderr(); }

    void TearDown() override
    {
        const auto captured_stderr = ::testing::internal::GetCapturedStderr();
        std::cout << "Captured stderr:\n"
                  << captured_stderr << '\n';
    }
};

// Scenario 1: Zero Awaitables.
// Verify that the multiplexer correctly handles the case with no awaitables.
// The multiplexer should not suspend and should complete immediately.
TEST_F(MuxBaseTest, ZeroAwaitables)
{
    TestMux<> mux(0, 0);

    // The multiplexer should not suspend
    EXPECT_FALSE(mux.DoSuspend(std::noop_coroutine()));
}

// Scenario 2: All Awaitables Complete Successfully
// Ensure that the multiplexer correctly detects when all awaitables have
// finished. For AllOf semantics, the multiplexer should resume the parent
// coroutine after all awaitables complete.
TEST_F(MuxBaseTest, AllAwaitablesCompleteSuccessfully)
{
    TestMux<> mux(3, 3);

    bool parent_resumed = false;
    // Create a custom coroutine to detect resumption
    const auto parent_coroutine = [](bool& parent_resumed) -> TestCoroutine {
        // Initially suspend the coroutine
        co_await std::suspend_always(); // Coroutine body does nothing
        parent_resumed = true;
        co_return; // Coroutine completes
    }(parent_resumed);

    // Replace parent_ with our custom coroutine handle
    mux.parent_ = parent_coroutine.handle_;

    // Suspend the mux
    EXPECT_TRUE(mux.DoSuspend(mux.parent_));

    // Simulate awaitables completing successfully
    mux.Invoke(nullptr); // First awaitable completes
    EXPECT_EQ(mux.count_, 1);

    mux.Invoke(nullptr); // Second awaitable completes
    EXPECT_EQ(mux.count_, 2);

    mux.Invoke(nullptr); // Third awaitable completes
    EXPECT_EQ(mux.count_, 3);

    // Since all awaitables have completed, parent coroutine should be resumed
    EXPECT_TRUE(parent_resumed);
}

// Scenario 3: Partial Completion with Minimum Ready
// Purpose: Test behavior when the minimum required awaitables have completed.
// For AnyOf semantics where MinReady() is less than Size(), the multiplexer should resume the parent coroutine when the condition is met.
TEST_F(MuxBaseTest, PartialCompletionWithMinReady)
{
    TestMux<> mux(3, 2, true); // Size is 3, MinReady is 2

    // Create a flag to detect resumption
    bool parent_resumed = false;

    // Define the parent coroutine
    const auto parent_coroutine = [](bool& parent_resumed) -> TestCoroutine {
        co_await std::suspend_always();
        parent_resumed = true;
        co_return;
    }(parent_resumed);

    // Assign the handle to mux.parent_
    mux.parent_ = parent_coroutine.handle_;

    // Suspend the mux
    EXPECT_TRUE(mux.DoSuspend(mux.parent_));

    // Simulate first awaitable completing
    mux.Invoke(nullptr);
    EXPECT_EQ(mux.count_, 1);
    EXPECT_FALSE(parent_resumed);

    // Simulate second awaitable completing (min_ready reached)
    mux.Invoke(nullptr);
    EXPECT_EQ(mux.count_, 2);

    // Parent coroutine should be resumed
    EXPECT_TRUE(parent_resumed);
}

// Scenario 4: Awaitable Throwing an Exception
// Purpose: Ensure exceptions are properly propagated.
// An awaitable throws an exception, and exception_ captures it. ReRaise()
// should rethrow it in await_resume().
TEST_F(MuxBaseTest, AwaitableThrowsException)
{
    TestMux<> mux(2, 2);

    // Suspend the mux
    EXPECT_TRUE(mux.DoSuspend(std::noop_coroutine()));

    // First awaitable throws an exception
    mux.Invoke(std::make_exception_ptr(std::runtime_error("Test exception")));
    EXPECT_TRUE(mux.HasException());

    // Second awaitable completes successfully
    mux.Invoke(nullptr);
    EXPECT_EQ(mux.count_, 2);

    // ReRaise the exception
    EXPECT_THROW(mux.ReRaise(), std::runtime_error);
}

// Scenario 5: Early Cancellation Success
// Purpose: Verify that early cancellation works as expected.
// await_early_cancel() is called, and all awaitables are successfully cancelled.
TEST_F(MuxBaseTest, EarlyCancellationSuccess)
{
    TestMux<false, false> mux(5, 1, true);

    const auto result = mux.await_early_cancel();

    // Since InternalCancel returns true, expect true result
    EXPECT_TRUE(result);
}

// Scenario 6: Early Cancellation Failure
// Purpose: Ensure proper handling when early cancellation is not possible. Not
// all awaitables can be cancelled during await_early_cancel(), and the
// multiplexer handles it accordingly.
TEST_F(MuxBaseTest, EarlyCancellationFailure)
{
    TestMux<false, false> mux(5, 1, false);

    const auto result = mux.await_early_cancel();

    // Since InternalCancel returns false, expect false result
    EXPECT_FALSE(result);
}

// Scenario 7: Cancellation After Some Awaitables Complete
// Purpose: Test cancellation logic when some awaitables have already finished.
// await_cancel() is called after partial completion, and remaining awaitables
// are attempted to be cancelled.
TEST_F(MuxBaseTest, CancellationAfterPartialCompletion)
{
    TestMux<> mux(5, 5, false); // Non-abortable, InternalCancel returns false
    EXPECT_TRUE(mux.DoSuspend(std::noop_coroutine()));

    // Simulate two awaitables completing
    mux.Invoke(nullptr);
    mux.Invoke(nullptr);
    EXPECT_EQ(mux.count_, 2);

    // Attempt to cancel
    const auto result = mux.await_cancel(std::noop_coroutine());

    // Cancellation should not be synchronous since some awaitables have already completed
    EXPECT_FALSE(result);

    // Simulate remaining awaitables completing
    mux.Invoke(nullptr);
    mux.Invoke(nullptr);
    mux.Invoke(nullptr);
    EXPECT_EQ(mux.count_, 5);
}

// Scenario 8: Abortable vs. Non-Abortable Behaviors (Abortable)
// Purpose: Check behavior when IsAbortable() is true. For an abortable
// multiplexer (like AnyOf), cancellation after the first completion should be
// synchronous.
TEST_F(MuxBaseTest, AwaitCancelAbortable)
{
    TestMux<true, true> mux(5, 1, true);

    const auto result = mux.await_cancel(std::noop_coroutine());

    EXPECT_TRUE(result);
}

// Scenario 8: Abortable vs. Non-Abortable Behaviors (Non-Abortable)
// Purpose: Check behavior when IsAbortable() is false.
// For a non-abortable multiplexer, cancellation may not be synchronous.
TEST_F(MuxBaseTest, AwaitCancelNonAbortable)
{
    TestMux<> mux(5, 1, false);

    const auto result = mux.await_cancel(std::noop_coroutine());

    EXPECT_FALSE(result);
    EXPECT_EQ(mux.internal_cancel_called_, 1);

    // Simulate count_ reaching Size()
    mux.count_ = 5;
}

// Scenario 9: Exception Handling in Invoke()
// Purpose: Ensure that exceptions from awaitables are correctly handled.
// Multiple awaitables throw exceptions; only the first is stored.
TEST_F(MuxBaseTest, InvokeExceptionHandling)
{
    TestMux<> mux(3, 3);
    EXPECT_TRUE(mux.DoSuspend(std::noop_coroutine()));

    // First awaitable throws an exception
    mux.Invoke(std::make_exception_ptr(std::runtime_error("First exception")));
    EXPECT_TRUE(mux.HasException());

    // Second awaitable throws a different exception
    mux.Invoke(std::make_exception_ptr(std::runtime_error("Second exception")));
    EXPECT_TRUE(mux.HasException());

    // Third awaitable completes successfully
    mux.Invoke(nullptr);

    // Ensure only the first exception is stored
    try {
        mux.ReRaise();
    } catch (const std::runtime_error& e) {
        EXPECT_STREQ(e.what(), "First exception");
    }
}

// Scenario 10: Synchronization of count_
// Purpose: Verify that count_ accurately tracks completion. Simulate concurrent
// completions and ensure count_ and parent_.resume() are correctly updated.
TEST_F(MuxBaseTest, CountSynchronization)
{
    TestMux<> mux(3, 3);

    // Create a flag to detect resumption
    bool parent_resumed = false;

    // Define the parent coroutine
    const auto parent_coroutine = [](bool& parent_resumed) -> TestCoroutine { // NOLINT(cppcoreguidelines-avoid-capturing-lambda-coroutines)
        co_await std::suspend_always();
        parent_resumed = true;
        co_return;
    }(parent_resumed);

    // Assign the handle
    mux.parent_ = parent_coroutine.handle_;

    // Suspend the mux
    EXPECT_TRUE(mux.DoSuspend(mux.parent_));

    // Simulate invoke calls
    mux.Invoke(nullptr);
    mux.Invoke(nullptr);
    mux.Invoke(nullptr);

    EXPECT_EQ(mux.count_, 3);

    // Parent coroutine should be resumed after all completions
    EXPECT_TRUE(parent_resumed);
}

// Scenario 11: Resumption of Parent Coroutine
// Purpose: Make sure the parent coroutine is resumed at the correct time.
// Parent coroutine is resumed when all awaitables have completed or when the minimum required awaitables have completed.
TEST_F(MuxBaseTest, ParentCoroutineResumption)
{
    // Covered in previous test cases:
    // - AllAwaitablesCompleteSuccessfully (Scenario 2)
    // - PartialCompletionWithMinReady (Scenario 3)
    // - CountSynchronization (Scenario 10)
    // - ParentResumption (already exists)

    // Since the resumption is already tested, we ensure it's explicitly checked in those test cases.
    // Adding a comment here for completeness.
}

// Scenario 13: Testing DoSuspend() Logic (With Awaitables)
// Purpose: Confirm that the multiplexer suspends when required.
TEST_F(MuxBaseTest, DoSuspendWithAwaitables)
{
    TestMux<> mux(5, 1);

    const auto result = mux.DoSuspend(std::noop_coroutine());

    EXPECT_TRUE(result);
}

// Scenario 13: Testing DoSuspend() Logic (No Awaitables)
// Purpose: Confirm that the multiplexer does not suspend when there are zero
// awaitables.
TEST_F(MuxBaseTest, DoSuspendNoAwaitables)
{
    TestMux<> mux(0, 0);

    const auto result = mux.DoSuspend(std::noop_coroutine());

    EXPECT_FALSE(result);
}

// Scenario 14: Ensure InternalCancel() Correctness (Success)
// Purpose: Test that InternalCancel() correctly cancels all pending awaitables
// and returns true.
TEST_F(MuxBaseTest, InternalCancelSuccess)
{
    TestMux<> mux(3, 3, true); // InternalCancel returns true
    EXPECT_TRUE(mux.InternalCancel());
    EXPECT_EQ(mux.internal_cancel_called_, 1);
}
// Scenario 14: Ensure InternalCancel() Correctness (Failure)
// Purpose: Test that InternalCancel() handles failure correctly and returns
// false.
TEST_F(MuxBaseTest, InternalCancelFailure)
{
    TestMux<> mux(3, 3, false); // InternalCancel returns false
    EXPECT_FALSE(mux.InternalCancel());
    EXPECT_EQ(mux.internal_cancel_called_, 1);
}

// Scenario 16: Testing IsSkippable() Conditions
// Purpose: Check that skippable multiplexers are handled correctly. If
// IsSkippable() is true, ensure that early cancellations are synchronous and
// assert checks pass.
TEST_F(MuxBaseTest, AwaitEarlyCancelSkippable)
{
    TestMux<true, false> mux(5, 1, true);
    // Since IsSkippable is true, expect std::true_type{}
    EXPECT_TRUE((std::is_same_v<decltype(mux.await_early_cancel()), std::true_type>));
}

} // namespace
