//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Graphics/Common/Buffer.h>
#include <Oxygen/Graphics/Common/Detail/ResourceStateTracker.h>
#include <Oxygen/Graphics/Common/NativeObject.h>
#include <Oxygen/Testing/GTest.h>

using oxygen::graphics::Buffer;
using oxygen::graphics::NativeObject;
using oxygen::graphics::ResourceStates;
using oxygen::graphics::detail::Barrier;
using oxygen::graphics::detail::BufferBarrierDesc;
using oxygen::graphics::detail::ResourceStateTracker;

namespace {

// Minimal concrete Buffer for testing
class MinimalBuffer : public Buffer {
public:
    explicit MinimalBuffer(const uint64_t id)
        : Buffer("Test Buffer")
        , native_ { id, Buffer::ClassTypeId() }
    {
    }
    auto GetNativeResource() const -> NativeObject override { return native_; }
    void Bind() override { }
    auto Map() -> void* override { return nullptr; }
    void Unmap() override { }

private:
    NativeObject native_;
};

struct BufferStateTrackingTest : public ::testing::Test {
    ResourceStateTracker tracker;
    MinimalBuffer buffer1 { 1ULL }; // Use ULL suffix for uint64_t literals
    MinimalBuffer buffer2 { 2ULL }; // Use ULL suffix for uint64_t literals
};

// --- Tracking and Error Handling ---

NOLINT_TEST_F(BufferStateTrackingTest, BeginTracking_ThrowsIfAlreadyTracked)
{
    tracker.BeginTrackingResourceState(buffer1, ResourceStates::kCommon);
    NOLINT_EXPECT_THROW(tracker.BeginTrackingResourceState(buffer1, ResourceStates::kCommon), std::runtime_error);
}

NOLINT_TEST_F(BufferStateTrackingTest, RequireResourceState_ThrowsIfNotTracked)
{
    NOLINT_EXPECT_THROW(tracker.RequireResourceState(buffer1, ResourceStates::kCommon), std::runtime_error);
}

// --- State Transition Barriers ---

NOLINT_TEST_F(BufferStateTrackingTest, TransitionToDifferentState_CreatesBarrier)
{
    tracker.BeginTrackingResourceState(buffer1, ResourceStates::kCommon);
    tracker.RequireResourceState(buffer1, ResourceStates::kUnorderedAccess);
    const auto& barriers = tracker.GetPendingBarriers();
    ASSERT_EQ(barriers.size(), 1);
    EXPECT_EQ(barriers[0].GetResource().AsInteger(), buffer1.GetNativeResource().AsInteger());
}

NOLINT_TEST_F(BufferStateTrackingTest, TransitionToSameState_NoBarrier)
{
    tracker.BeginTrackingResourceState(buffer1, ResourceStates::kCommon);
    tracker.RequireResourceState(buffer1, ResourceStates::kCommon);
    EXPECT_TRUE(tracker.GetPendingBarriers().empty());
}

NOLINT_TEST_F(BufferStateTrackingTest, TransitionFromUAVToNonUAVState_CreatesTransitionBarrier)
{
    tracker.BeginTrackingResourceState(buffer1, ResourceStates::kUnorderedAccess);
    tracker.RequireResourceState(buffer1, ResourceStates::kCopyDest);
    const auto& barriers = tracker.GetPendingBarriers();
    ASSERT_FALSE(barriers.empty());
    auto desc = std::get<BufferBarrierDesc>(barriers[0].GetDescriptor());
    EXPECT_EQ(desc.before, ResourceStates::kUnorderedAccess);
    EXPECT_EQ(desc.after, ResourceStates::kCopyDest);
}

NOLINT_TEST_F(BufferStateTrackingTest, RedundantTransitions_MergeBarriers)
{
    // Pre-condition 1: Begin tracking in an initial state
    tracker.BeginTrackingResourceState(buffer1, ResourceStates::kCommon);

    // Pre-condition 2: First transition creates a buffer barrier (not a memory barrier)
    tracker.RequireResourceState(buffer1, ResourceStates::kUnorderedAccess);

    // Pre-condition 3: Second transition is for the same buffer, and no memory barrier in between
    tracker.RequireResourceState(buffer1, ResourceStates::kCopyDest);

    // Only one barrier should exist, and it should be a buffer barrier with merged states
    const auto& barriers = tracker.GetPendingBarriers();
    ASSERT_EQ(barriers.size(), 1);
    ASSERT_TRUE(std::holds_alternative<BufferBarrierDesc>(barriers[0].GetDescriptor()));
    auto desc = std::get<BufferBarrierDesc>(barriers[0].GetDescriptor());
    EXPECT_EQ(desc.before, ResourceStates::kCommon);
    EXPECT_EQ(desc.after, (ResourceStates::kUnorderedAccess | ResourceStates::kCopyDest));

    // Now, clear and insert a memory barrier (UAV to UAV)
    tracker.Clear();
    tracker.BeginTrackingResourceState(buffer1, ResourceStates::kUnorderedAccess);
    tracker.RequireResourceState(buffer1, ResourceStates::kUnorderedAccess);
    ASSERT_EQ(tracker.GetPendingBarriers().size(), 1);
    ASSERT_TRUE(tracker.GetPendingBarriers()[0].IsMemoryBarrier());

    // Now require a transition to a different state (should create a new buffer barrier, not merge)
    tracker.RequireResourceState(buffer1, ResourceStates::kCopyDest);
    const auto& barriers2 = tracker.GetPendingBarriers();
    // Should have two barriers: one memory, one buffer
    ASSERT_EQ(barriers2.size(), 2);
    EXPECT_TRUE(barriers2[0].IsMemoryBarrier());
    EXPECT_TRUE(std::holds_alternative<BufferBarrierDesc>(barriers2[1].GetDescriptor()));
    auto desc2 = std::get<BufferBarrierDesc>(barriers2[1].GetDescriptor());
    EXPECT_EQ(desc2.before, ResourceStates::kUnorderedAccess);
    EXPECT_EQ(desc2.after, ResourceStates::kCopyDest);
}

NOLINT_TEST_F(BufferStateTrackingTest, MultipleBuffers_TrackedIndependently)
{
    tracker.BeginTrackingResourceState(buffer1, ResourceStates::kCommon);
    tracker.BeginTrackingResourceState(buffer2, ResourceStates::kUnorderedAccess);
    tracker.RequireResourceState(buffer1, ResourceStates::kUnorderedAccess);
    const auto& barriers = tracker.GetPendingBarriers();
    ASSERT_EQ(barriers.size(), 1);
    EXPECT_EQ(barriers[0].GetResource().AsInteger(), buffer1.GetNativeResource().AsInteger());
}

// --- Auto Memory Barrier Insertion (Auto Mode) ---

NOLINT_TEST_F(BufferStateTrackingTest, AutoMemoryBarriers_FirstUAVAccess_CreatesBarrier)
{
    tracker.BeginTrackingResourceState(buffer1, ResourceStates::kUnorderedAccess);
    tracker.EnableAutoMemoryBarriers(buffer1);
    tracker.RequireResourceState(buffer1, ResourceStates::kUnorderedAccess);
    EXPECT_EQ(tracker.GetPendingBarriers().size(), 1);
}

NOLINT_TEST_F(BufferStateTrackingTest, AutoMemoryBarriers_SubsequentUAVAccess_CreatesBarrier)
{
    tracker.BeginTrackingResourceState(buffer1, ResourceStates::kUnorderedAccess);
    tracker.EnableAutoMemoryBarriers(buffer1);
    tracker.RequireResourceState(buffer1, ResourceStates::kUnorderedAccess);
    tracker.RequireResourceState(buffer1, ResourceStates::kUnorderedAccess);
    EXPECT_EQ(tracker.GetPendingBarriers().size(), 2);
}

// --- Manual Memory Barrier Insertion (Manual Mode) ---

NOLINT_TEST_F(BufferStateTrackingTest, ManualMemoryBarriers_FirstUAVAccess_CreatesBarrier)
{
    tracker.BeginTrackingResourceState(buffer1, ResourceStates::kUnorderedAccess);
    tracker.DisableAutoMemoryBarriers(buffer1);
    tracker.RequireResourceState(buffer1, ResourceStates::kUnorderedAccess);
    EXPECT_FALSE(tracker.GetPendingBarriers().empty());
}

NOLINT_TEST_F(BufferStateTrackingTest, ManualMemoryBarriers_SubsequentUAVAccess_NoBarrier)
{
    tracker.BeginTrackingResourceState(buffer1, ResourceStates::kUnorderedAccess);
    tracker.DisableAutoMemoryBarriers(buffer1);
    tracker.RequireResourceState(buffer1, ResourceStates::kUnorderedAccess);
    tracker.RequireResourceState(buffer1, ResourceStates::kUnorderedAccess);
    EXPECT_EQ(tracker.GetPendingBarriers().size(), 1);
}

// --- Manual Memory Barrier Toggle (Simulate Manual Insertion) ---

NOLINT_TEST_F(BufferStateTrackingTest, ManualMemoryBarrier_ToggleAutoMode_AllowsBarrierAgain)
{
    tracker.BeginTrackingResourceState(buffer1, ResourceStates::kUnorderedAccess);
    tracker.DisableAutoMemoryBarriers(buffer1);
    tracker.RequireResourceState(buffer1, ResourceStates::kUnorderedAccess);
    tracker.RequireResourceState(buffer1, ResourceStates::kUnorderedAccess);
    EXPECT_EQ(tracker.GetPendingBarriers().size(), 1);
    tracker.EnableAutoMemoryBarriers(buffer1);
    tracker.RequireResourceState(buffer1, ResourceStates::kUnorderedAccess);
    EXPECT_EQ(tracker.GetPendingBarriers().size(), 2);
}

// --- Clear and Reset ---

NOLINT_TEST_F(BufferStateTrackingTest, Clear_RemovesAllTrackingAndBarriers)
{
    tracker.BeginTrackingResourceState(buffer1, ResourceStates::kCommon);
    tracker.RequireResourceState(buffer1, ResourceStates::kUnorderedAccess);
    EXPECT_FALSE(tracker.GetPendingBarriers().empty());
    tracker.Clear();
    EXPECT_TRUE(tracker.GetPendingBarriers().empty());
    // Should throw if not tracked anymore
    NOLINT_EXPECT_THROW(tracker.RequireResourceState(buffer1, ResourceStates::kCommon), std::runtime_error);
}

// --- Permanent State ---

NOLINT_TEST_F(BufferStateTrackingTest, RequireResourceState_WithIsPermanentTrue_DoesNotThrow)
{
    tracker.BeginTrackingResourceState(buffer1, ResourceStates::kCommon);
    NOLINT_EXPECT_NO_THROW(tracker.RequireResourceStateFinal(buffer1, ResourceStates::kUnorderedAccess));
    const auto& barriers = tracker.GetPendingBarriers();
    EXPECT_FALSE(barriers.empty());
}

NOLINT_TEST_F(BufferStateTrackingTest, PermanentState_BlocksFurtherStateChanges)
{
    tracker.BeginTrackingResourceState(buffer1, ResourceStates::kCommon);
    // Set permanent state
    NOLINT_EXPECT_NO_THROW(tracker.RequireResourceStateFinal(buffer1, ResourceStates::kUnorderedAccess));
    // Further call the same state is allowed
    NOLINT_EXPECT_NO_THROW(tracker.RequireResourceState(buffer1, ResourceStates::kUnorderedAccess));
    // Attempt to change state should throw
    NOLINT_EXPECT_THROW(tracker.RequireResourceState(buffer1, ResourceStates::kCopyDest), std::runtime_error);
}

// --- Restore Initial State ---

NOLINT_TEST_F(BufferStateTrackingTest, RestoreInitialState_AfterNonPermanentTransition_AndKeepInitialStateTrue)
{
    // Begin tracking with keep_initial_state = true
    tracker.BeginTrackingResourceState(buffer1, ResourceStates::kCommon, true);
    tracker.RequireResourceState(buffer1, ResourceStates::kUnorderedAccess);
    // Simulate command list close (should restore to initial state)
    tracker.OnCommandListClosed();
    const auto& barriers = tracker.GetPendingBarriers();
    // There should be two barriers: one for the transition to UAV, one for the restore
    ASSERT_EQ(barriers.size(), 2);
    // The last barrier should restore to initial state
    auto desc = std::get<BufferBarrierDesc>(barriers[1].GetDescriptor());
    EXPECT_EQ(desc.before, ResourceStates::kUnorderedAccess);
    EXPECT_EQ(desc.after, ResourceStates::kCommon);
}

NOLINT_TEST_F(BufferStateTrackingTest, NoRestoreInitialState_AfterPermanentTransition)
{
    // Begin tracking with keep_initial_state = true
    tracker.BeginTrackingResourceState(buffer1, ResourceStates::kCommon, true);
    tracker.RequireResourceStateFinal(buffer1, ResourceStates::kUnorderedAccess);
    // Simulate command list close (should NOT restore to initial state)
    tracker.OnCommandListClosed();
    const auto& barriers = tracker.GetPendingBarriers();
    // Only one barrier for the permanent transition
    ASSERT_EQ(barriers.size(), 1);
    // The barrier should be to the permanent state
    auto desc = std::get<BufferBarrierDesc>(barriers[0].GetDescriptor());
    EXPECT_EQ(desc.before, ResourceStates::kCommon);
    EXPECT_EQ(desc.after, ResourceStates::kUnorderedAccess);
}

NOLINT_TEST_F(BufferStateTrackingTest, NoRestoreInitialState_IfKeepInitialStateFalse)
{
    // Begin tracking with keep_initial_state = false
    tracker.BeginTrackingResourceState(buffer1, ResourceStates::kCommon, false);
    tracker.RequireResourceState(buffer1, ResourceStates::kUnorderedAccess);
    tracker.OnCommandListClosed();
    const auto& barriers = tracker.GetPendingBarriers();
    // Only one barrier for the transition, no restore
    ASSERT_EQ(barriers.size(), 1);
    auto desc = std::get<BufferBarrierDesc>(barriers[0].GetDescriptor());
    EXPECT_EQ(desc.before, ResourceStates::kCommon);
    EXPECT_EQ(desc.after, ResourceStates::kUnorderedAccess);
}

} // namespace
