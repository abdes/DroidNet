//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <chrono>
#include <memory>
#include <thread>

#include <Oxygen/Core/Types/Frame.h>
#include <Oxygen/Graphics/Common/Buffer.h>
#include <Oxygen/Graphics/Common/CommandList.h>
#include <Oxygen/Graphics/Common/CommandQueue.h>
#include <Oxygen/Graphics/Common/CommandRecorder.h>
#include <Oxygen/Graphics/Common/Detail/Barriers.h>
#include <Oxygen/Graphics/Common/Detail/DeferredReclaimer.h>
#include <Oxygen/Graphics/Common/Framebuffer.h>
#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/Graphics/Common/Internal/Commander.h>
#include <Oxygen/Graphics/Common/NativeObject.h>
#include <Oxygen/Graphics/Common/PipelineState.h>
#include <Oxygen/Graphics/Common/Queues.h>
#include <Oxygen/Graphics/Common/ResourceRegistry.h>
#include <Oxygen/Graphics/Common/Texture.h>
#include <Oxygen/Graphics/Common/Types/ClearFlags.h>
#include <Oxygen/Testing/GTest.h>
#include <Oxygen/Testing/ScopedLogCapture.h>

//=== External Dependencies ===-----------------------------------------------//

// Consolidated GoogleTest/GoogleMock using aliases for tests
using testing::NiceMock;
using testing::Return;
using testing::Test;
using testing::Throw;

using Role = oxygen::graphics::QueueRole;
using oxygen::graphics::CommandList;
using oxygen::graphics::CommandQueue;
using oxygen::graphics::NativeView;
using oxygen::graphics::QueueKey;
using oxygen::graphics::detail::DeferredReclaimer;
using CommandListPtr = std::shared_ptr<CommandList>;
using CommandListSpan = std::span<CommandListPtr>;

namespace {

//=== Mock Classes for Commander Testing ===----------------------------------//

// Mock CommandQueue that can simulate submission failures
// ReSharper disable once CppClassCanBeFinal - mocks cannot be final
class MockCommandQueue : public CommandQueue {
public:
  explicit MockCommandQueue(const std::string_view name)
    : CommandQueue(name)
  {
  }

  // NOLINTBEGIN
  // clang-format off
  MOCK_METHOD(void, Signal, (std::uint64_t), (const, override));
  MOCK_METHOD(std::uint64_t, Signal, (), (const, override));
  MOCK_METHOD(void, Wait, (std::uint64_t, std::chrono::milliseconds), (const, override));
  MOCK_METHOD(void, Wait, (std::uint64_t), (const, override));
  MOCK_METHOD(void, QueueSignalCommand, (std::uint64_t), (override));
  MOCK_METHOD(void, QueueWaitCommand, (std::uint64_t), (const, override));
  MOCK_METHOD(std::uint64_t, GetCompletedValue, (), (const, override));
  MOCK_METHOD(std::uint64_t, GetCurrentValue, (), (const, override));
  MOCK_METHOD(void, Submit, (CommandListPtr), (override));
  MOCK_METHOD(void, Submit, (CommandListSpan), (override));
  MOCK_METHOD(oxygen::graphics::QueueRole, GetQueueRole, (), (const, override));
  // clang-format on
  // NOLINTEND
};

// Mock CommandRecorder that can simulate End() failures
// ReSharper disable once CppClassCanBeFinal - mocks cannot be final
class MockCommandRecorder : public oxygen::graphics::CommandRecorder {
public:
  explicit MockCommandRecorder(
    CommandListPtr command_list, const oxygen::observer_ptr<CommandQueue> queue)
    : CommandRecorder(std::move(command_list), queue)
  {
  }

  // NOLINTBEGIN
  // clang-format off
  MOCK_METHOD(void, Begin, (), (override));
  MOCK_METHOD(CommandListPtr, End, (), (override));
  MOCK_METHOD(void, RecordQueueSignal, (std::uint64_t), (override));
  MOCK_METHOD(void, RecordQueueWait, (std::uint64_t), (override));
  MOCK_METHOD(void, SetPipelineState, (oxygen::graphics::GraphicsPipelineDesc), (override));
  MOCK_METHOD(void, SetPipelineState, (oxygen::graphics::ComputePipelineDesc), (override));
  MOCK_METHOD(void, SetGraphicsRootConstantBufferView, (std::uint32_t, std::uint64_t), (override));
  MOCK_METHOD(void, SetComputeRootConstantBufferView, (std::uint32_t, std::uint64_t), (override));
  MOCK_METHOD(void, SetGraphicsRoot32BitConstant, (std::uint32_t, std::uint32_t, std::uint32_t), (override));
  MOCK_METHOD(void, SetComputeRoot32BitConstant, (std::uint32_t, std::uint32_t, std::uint32_t), (override));
  MOCK_METHOD(void, SetRenderTargets, (std::span<oxygen::graphics::NativeView>, std::optional<oxygen::graphics::NativeView>), (override));
  MOCK_METHOD(void, SetViewport, (const oxygen::ViewPort&), (override));
  MOCK_METHOD(void, SetScissors, (const oxygen::Scissors&), (override));
  MOCK_METHOD(void, Draw, (std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t), (override));
  MOCK_METHOD(void, Dispatch, (std::uint32_t, std::uint32_t, std::uint32_t), (override));
  MOCK_METHOD(void, SetVertexBuffers, (std::uint32_t, const std::shared_ptr<oxygen::graphics::Buffer>*, const std::uint32_t*), (const, override));
  MOCK_METHOD(void, BindIndexBuffer, (const oxygen::graphics::Buffer&, oxygen::Format), (override));
  MOCK_METHOD(void, BindFrameBuffer, (const oxygen::graphics::Framebuffer&), (override));
  MOCK_METHOD(void, ClearDepthStencilView, (const oxygen::graphics::Texture&, const oxygen::graphics::NativeView&, oxygen::graphics::ClearFlags, float, std::uint8_t), (override));
  MOCK_METHOD(void, ClearFramebuffer, (const oxygen::graphics::Framebuffer&, std::optional<std::vector<std::optional<oxygen::graphics::Color>>>, std::optional<float>, std::optional<std::uint8_t>), (override));
  MOCK_METHOD(void, CopyBuffer, (oxygen::graphics::Buffer&, std::size_t, const oxygen::graphics::Buffer&, std::size_t, std::size_t), (override));
  MOCK_METHOD(void, CopyBufferToTexture, (const oxygen::graphics::Buffer&, const oxygen::graphics::TextureUploadRegion&, oxygen::graphics::Texture&), (override));
  MOCK_METHOD(void, CopyBufferToTexture, (const oxygen::graphics::Buffer&, std::span<const oxygen::graphics::TextureUploadRegion>, oxygen::graphics::Texture&), (override));
  MOCK_METHOD(void, CopyTexture, (const oxygen::graphics::Texture&, const oxygen::graphics::TextureSlice&, const oxygen::graphics::TextureSubResourceSet&, oxygen::graphics::Texture&, const oxygen::graphics::TextureSlice&, const oxygen::graphics::TextureSubResourceSet&), (override));
  MOCK_METHOD(void, ExecuteBarriers, (std::span<const oxygen::graphics::detail::Barrier>), (override));
  // clang-format on
  // NOLINTEND
};

// Mock CommandList for testing
// ReSharper disable once CppClassCanBeFinal - mocks cannot be final
class MockCommandList : public CommandList {
public:
  explicit MockCommandList(const std::string_view name)
    : CommandList(name, oxygen::graphics::QueueRole::kGraphics)
  {
  }

  MOCK_METHOD(void, OnSubmitted, (), (override));
  MOCK_METHOD(void, OnExecuted, (), (override));
};

// TestCommander class that allows dependency injection for testing
class TestCommander final : public oxygen::graphics::internal::Commander {
public:
  explicit TestCommander(DeferredReclaimer& reclaimer)
    : injected_reclaimer_(&reclaimer)
  {
    // Immediately set the reclaimer_ to our injected instance
    reclaimer_ = oxygen::observer_ptr<DeferredReclaimer>(injected_reclaimer_);
  }

protected:
  auto UpdateDependencies(const std::function<Component&(oxygen::TypeId)>&
    /*get_component*/) noexcept -> void override
  {
    // Override to inject our real DeferredReclaimer for testing
    // Instead of calling the parent's UpdateDependencies, we directly
    // set the reclaimer_ pointer to our injected instance
    reclaimer_ = oxygen::observer_ptr<DeferredReclaimer>(injected_reclaimer_);
  }

private:
  // Store pointer to the real DeferredReclaimer we want to inject
  DeferredReclaimer* injected_reclaimer_;
};

//=== Common Test Infrastructure ===------------------------------------------//

//! Base fixture providing common mock setup and utilities for Commander tests
class CommanderTestBase : public Test {
protected:
  auto SetUp() -> void override
  {
    // Create real DeferredReclaimer for testing
    real_reclaimer = std::make_unique<DeferredReclaimer>();

    secondary_q = std::make_shared<NiceMock<MockCommandQueue>>("gfx-queue");
    primary_q = std::make_shared<NiceMock<MockCommandQueue>>("cpu-queue");

    // Set up default behaviors for queues
    SetupDefaultQueueBehaviors();

    // Create TestCommander with real DeferredReclaimer injection
    commander = std::make_unique<TestCommander>(*real_reclaimer);
  }

  auto TearDown() -> void override
  {
    commander.reset();
    primary_q.reset();
    secondary_q.reset();
    real_reclaimer.reset();
  }

  //! Helper to simulate frame completion
  auto SimulateFrameCompletion() -> void
  {
    real_reclaimer->ProcessAllDeferredReleases();
  }

  //! Factory method to create a mock command list with standard setup
  auto CreateMockCommandList(const std::string& name)
    -> std::shared_ptr<MockCommandList>
  {

    return std::make_shared<NiceMock<MockCommandList>>(name);
  }

  //! Factory method to create a mock command recorder with standard setup
  auto CreateMockCommandRecorder(
    CommandListPtr command_list, const std::shared_ptr<MockCommandQueue> queue)
    -> std::unique_ptr<MockCommandRecorder>
  {

    std::unique_ptr<MockCommandRecorder> recorder;
    // Convert shared_ptr<MockCommandQueue> to observer_ptr<CommandQueue>
    const oxygen::observer_ptr<CommandQueue> obs_queue { queue.get() };
    recorder = std::make_unique<NiceMock<MockCommandRecorder>>(
      std::move(command_list), obs_queue);

    ON_CALL(*recorder, Begin()).WillByDefault(Return());

    return recorder;
  }

private:
  //! Setup default behaviors for primary mock queues
  auto SetupDefaultQueueBehaviors() -> void
  {
    // Use ON_CALL for both NiceMock and StrictMock compatibility
    ON_CALL(*secondary_q, GetQueueRole())
      .WillByDefault(Return(Role::kGraphics));
    ON_CALL(*primary_q, GetQueueRole()).WillByDefault(Return(Role::kCompute));
    ON_CALL(*secondary_q, GetCurrentValue()).WillByDefault(Return(100));
    ON_CALL(*primary_q, GetCurrentValue()).WillByDefault(Return(200));
    ON_CALL(*secondary_q, GetCompletedValue()).WillByDefault(Return(100));
    ON_CALL(*primary_q, GetCompletedValue()).WillByDefault(Return(200));
  }

protected:
  // Common members available to all derived fixtures
  std::unique_ptr<DeferredReclaimer> real_reclaimer;
  std::unique_ptr<TestCommander> commander;
  std::shared_ptr<MockCommandQueue> secondary_q;
  std::shared_ptr<MockCommandQueue> primary_q;
};

//=== Submission Test Fixtures ===--------------------------------------------//

//! Base fixture for submission-related tests using nice mocks
class SubmissionTestBase : public CommanderTestBase {
  // Inherits all functionality from CommanderTestBase with nice mocks
};

//=== Immediate Submission Test Cases ===-------------------------------------//

// Immediate-submission fixture: same setup but used to mark tests that
// exercise immediate submission semantics.
class ImmediateSubmissionTest : public SubmissionTestBase { };

//! Immediate path: GIVEN a recorder in immediate mode WHEN its deleter runs
//! THEN the command list is ended, submitted once to its target queue, and
//! OnSubmitted() is invoked immediately (execution completion deferred).
NOLINT_TEST_F(
  ImmediateSubmissionTest, ImmediateSubmission_CallsSubmitImmediately)
{

  // Create mock command list and recorder using factory methods
  auto mock_list = CreateMockCommandList("immediate-list");
  auto mock_recorder = CreateMockCommandRecorder(mock_list, secondary_q);

  // Set up expectations
  EXPECT_CALL(*mock_recorder, End()).WillOnce(Return(mock_list));
  EXPECT_CALL(*secondary_q, Submit(testing::A<CommandListPtr>()))
    .WillOnce(Return());
  EXPECT_CALL(*mock_list, OnSubmitted()).Times(1);

  {
    // Use TestCommander for immediate submission
    auto deleter = commander->PrepareCommandRecorder(
      std::move(mock_recorder), mock_list, true);
    // When deleter goes out of scope, immediate submission happens
  }

  // Process any deferred releases
  SimulateFrameCompletion();
}

//! Edge case: immediate submission of an otherwise "empty" recorder (no
//! commands recorded) SHOULD still end & submit the list and invoke
//! OnSubmitted(), proving the pathway does not special-case emptiness.
NOLINT_TEST_F(ImmediateSubmissionTest, EmptyList_ImmediateStillSubmits)
{

  auto mock_list = CreateMockCommandList("empty-list");
  auto mock_recorder = CreateMockCommandRecorder(mock_list, secondary_q);

  // Set up expectations for empty list submission
  EXPECT_CALL(*mock_recorder, End()).WillOnce(Return(mock_list));
  EXPECT_CALL(*secondary_q, Submit(testing::A<CommandListPtr>()))
    .WillOnce(Return());
  EXPECT_CALL(*mock_list, OnSubmitted()).Times(1);

  {
    // Use TestCommander for immediate submission of empty list
    auto deleter = commander->PrepareCommandRecorder(
      std::move(mock_recorder), mock_list, true);
    // Empty recorder - no commands recorded, but still submitted immediately
  }

  // Process any deferred releases
  SimulateFrameCompletion();
}

//! Immediate path OnExecuted: GIVEN an immediate submission WHEN we process
//! deferred releases THEN OnSubmitted fires once at submit time and OnExecuted
//! fires exactly once later (idempotent reprocessing safe).
NOLINT_TEST_F(ImmediateSubmissionTest, ImmediateSubmission_OnExecutedFiresOnce)
{
  auto mock_list = CreateMockCommandList("immediate-onexecuted");
  auto mock_recorder = CreateMockCommandRecorder(mock_list, secondary_q);

  testing::Sequence seq; // Enforce ordering: OnSubmitted before OnExecuted
  EXPECT_CALL(*mock_recorder, End()).WillOnce(Return(mock_list));
  EXPECT_CALL(*secondary_q, Submit(testing::A<CommandListPtr>()))
    .WillOnce(Return());
  EXPECT_CALL(*mock_list, OnSubmitted()).Times(1).InSequence(seq);
  EXPECT_CALL(*mock_list, OnExecuted()).Times(1).InSequence(seq);

  {
    auto deleter = commander->PrepareCommandRecorder(
      std::move(mock_recorder), mock_list, true);
  } // immediate submit happens

  // Not processed yet -> OnExecuted should not have fired (ordering ensures)
  // Process deferred releases to trigger OnExecuted
  SimulateFrameCompletion();

  // Second processing pass should NOT call OnExecuted again (Times(1) enforces)
  SimulateFrameCompletion();
}

//=== Deferred Submission Test Cases ===--------------------------------------//

// Deferred-submission fixture: uses the base setup and defaults to deferred
class DeferredSubmissionTest : public SubmissionTestBase { };

//! Deferred lifecycle: recorder destruction in deferred mode MUST NOT submit;
//! only an explicit SubmitDeferredCommandLists() groups & submits later.
NOLINT_TEST_F(DeferredSubmissionTest, DeferredLifecycle_WaitsForSubmitCall)
{
  auto mock_list = CreateMockCommandList("deferred-list");
  auto mock_recorder = CreateMockCommandRecorder(mock_list, secondary_q);

  // Set up expectations - Submit should be called when we explicitly submit
  EXPECT_CALL(*mock_recorder, End()).WillOnce(Return(mock_list));
  EXPECT_CALL(*secondary_q, Submit(testing::A<CommandListSpan>()))
    .WillOnce(Return());

  {
    // Use TestCommander for deferred submission
    auto deleter = commander->PrepareCommandRecorder(
      std::move(mock_recorder), mock_list, false);
    // Deferred recorder - commands are not submitted until explicit call
  }

  // Submit all deferred command lists
  commander->SubmitDeferredCommandLists();

  // Process any deferred releases
  SimulateFrameCompletion();
}

//! Batching: multiple deferred recorders targeting same queue SHOULD be
//! coalesced into one Submit(span) call preserving per-list OnSubmitted.
NOLINT_TEST_F(DeferredSubmissionTest, MultipleLists_SubmittedTogether)
{
  auto list_a = CreateMockCommandList("batch-a");
  auto list_b = CreateMockCommandList("batch-b");
  auto recorder_a = CreateMockCommandRecorder(list_a, secondary_q);
  auto recorder_b = CreateMockCommandRecorder(list_b, secondary_q);

  // Set up expectations for batched submission
  EXPECT_CALL(*recorder_a, End()).WillOnce(Return(list_a));
  EXPECT_CALL(*recorder_b, End()).WillOnce(Return(list_b));
  EXPECT_CALL(*secondary_q, Submit(testing::A<CommandListSpan>()))
    .WillOnce(Return());

  {
    auto deleter_a
      = commander->PrepareCommandRecorder(std::move(recorder_a), list_a, false);
    auto deleter_b
      = commander->PrepareCommandRecorder(std::move(recorder_b), list_b, false);
  }

  // Submit all deferred command lists (should batch together)
  commander->SubmitDeferredCommandLists();

  // Process any deferred releases
  SimulateFrameCompletion();
}

//! Idempotence: calling SubmitDeferredCommandLists() with an empty backlog
//! SHOULD be a no-op and remain safe when invoked repeatedly.
NOLINT_TEST_F(DeferredSubmissionTest, SubmitDeferred_Idempotent)
{
  // Should not crash or throw when called with no pending lists
  commander->SubmitDeferredCommandLists();
  commander->SubmitDeferredCommandLists(); // Second call should be safe
}

//! Mixed modes: an immediate list and a deferred list on the same queue—
//! immediate one submits right away; deferred waits until the batch submit.
NOLINT_TEST_F(DeferredSubmissionTest, ImmediateAndDeferred_WorkTogether)
{
  auto list_def = CreateMockCommandList("deferred");
  auto list_imm = CreateMockCommandList("immediate");
  auto recorder_def = CreateMockCommandRecorder(list_def, secondary_q);
  auto recorder_imm = CreateMockCommandRecorder(list_imm, secondary_q);

  // Set up expectations
  EXPECT_CALL(*recorder_def, End()).WillOnce(Return(list_def));
  EXPECT_CALL(*recorder_imm, End()).WillOnce(Return(list_imm));
  EXPECT_CALL(*secondary_q, Submit(testing::A<CommandListPtr>()))
    .WillOnce(Return()); // Immediate submission
  EXPECT_CALL(*secondary_q, Submit(testing::A<CommandListSpan>()))
    .WillOnce(Return()); // Deferred submission
  EXPECT_CALL(*list_imm, OnSubmitted()).Times(1);
  EXPECT_CALL(*list_def, OnSubmitted()).Times(1);

  {
    // Create deferred first, then immediate (immediate submits on destruction)
    auto deleter_def = commander->PrepareCommandRecorder(
      std::move(recorder_def), list_def, false);
    auto deleter_imm = commander->PrepareCommandRecorder(
      std::move(recorder_imm), list_imm, true);
  }

  // Submit deferred lists
  commander->SubmitDeferredCommandLists();

  // Process any deferred releases
  SimulateFrameCompletion();
}

//! Deferred path OnExecuted: GIVEN deferred lists WHEN we submit and process
//! releases THEN each list receives OnSubmitted once and OnExecuted once, with
//! ordering preserved per list.
NOLINT_TEST_F(DeferredSubmissionTest, DeferredSubmission_OnExecutedFiresOnce)
{
  auto list_a = CreateMockCommandList("deferred-a-onexecuted");
  auto list_b = CreateMockCommandList("deferred-b-onexecuted");
  auto recorder_a = CreateMockCommandRecorder(list_a, secondary_q);
  auto recorder_b = CreateMockCommandRecorder(list_b, secondary_q);

  testing::Sequence seq_a, seq_b;
  EXPECT_CALL(*recorder_a, End()).WillOnce(Return(list_a));
  EXPECT_CALL(*recorder_b, End()).WillOnce(Return(list_b));
  EXPECT_CALL(*secondary_q, Submit(testing::A<CommandListSpan>()))
    .WillOnce(Return());
  EXPECT_CALL(*list_a, OnSubmitted()).Times(1).InSequence(seq_a);
  EXPECT_CALL(*list_a, OnExecuted()).Times(1).InSequence(seq_a);
  EXPECT_CALL(*list_b, OnSubmitted()).Times(1).InSequence(seq_b);
  EXPECT_CALL(*list_b, OnExecuted()).Times(1).InSequence(seq_b);

  {
    auto deleter_a
      = commander->PrepareCommandRecorder(std::move(recorder_a), list_a, false);
    auto deleter_b
      = commander->PrepareCommandRecorder(std::move(recorder_b), list_b, false);
  }

  commander->SubmitDeferredCommandLists();
  SimulateFrameCompletion();
  SimulateFrameCompletion(); // Idempotency check
}

//! Uneven multi-queue batch: GIVEN two lists on primary queue and one on
//! secondary WHEN deferred submission occurs THEN two Submit(span) calls (one
//! per queue) and each valid list gets OnSubmitted & OnExecuted exactly once.
NOLINT_TEST_F(DeferredSubmissionTest, DeferredSubmission_UnevenMultiQueueBatch)
{
  auto list_p1 = CreateMockCommandList("primary-1");
  auto list_p2 = CreateMockCommandList("primary-2");
  auto list_s = CreateMockCommandList("secondary-1");
  auto rec_p1 = CreateMockCommandRecorder(list_p1, primary_q);
  auto rec_p2 = CreateMockCommandRecorder(list_p2, primary_q);
  auto rec_s = CreateMockCommandRecorder(list_s, secondary_q);

  testing::Sequence seq_p1, seq_p2, seq_s;
  EXPECT_CALL(*rec_p1, End()).WillOnce(Return(list_p1));
  EXPECT_CALL(*rec_p2, End()).WillOnce(Return(list_p2));
  EXPECT_CALL(*rec_s, End()).WillOnce(Return(list_s));
  EXPECT_CALL(*primary_q, Submit(testing::A<CommandListSpan>()))
    .WillOnce(Return());
  EXPECT_CALL(*secondary_q, Submit(testing::A<CommandListSpan>()))
    .WillOnce(Return());
  EXPECT_CALL(*list_p1, OnSubmitted()).Times(1).InSequence(seq_p1);
  EXPECT_CALL(*list_p1, OnExecuted()).Times(1).InSequence(seq_p1);
  EXPECT_CALL(*list_p2, OnSubmitted()).Times(1).InSequence(seq_p2);
  EXPECT_CALL(*list_p2, OnExecuted()).Times(1).InSequence(seq_p2);
  EXPECT_CALL(*list_s, OnSubmitted()).Times(1).InSequence(seq_s);
  EXPECT_CALL(*list_s, OnExecuted()).Times(1).InSequence(seq_s);

  {
    auto d1
      = commander->PrepareCommandRecorder(std::move(rec_p1), list_p1, false);
    auto d2
      = commander->PrepareCommandRecorder(std::move(rec_p2), list_p2, false);
    auto d3
      = commander->PrepareCommandRecorder(std::move(rec_s), list_s, false);
  }

  commander->SubmitDeferredCommandLists();
  SimulateFrameCompletion();
  SimulateFrameCompletion(); // Ensure no duplicate OnExecuted
}

//! Mixed valid + null in deferred batch: GIVEN one recorder whose End() returns
//! nullptr and another valid WHEN submitting THEN only the valid list is
//! submitted and receives OnSubmitted/OnExecuted.
NOLINT_TEST_F(DeferredSubmissionTest, DeferredSubmission_NullListSkipped)
{
  auto valid_list = CreateMockCommandList("valid-list");
  auto valid_rec = CreateMockCommandRecorder(valid_list, secondary_q);
  auto null_list = CreateMockCommandList("null-list-placeholder");
  auto null_rec = CreateMockCommandRecorder(null_list, secondary_q);

  // Force one recorder to produce nullptr
  EXPECT_CALL(*null_rec, End()).WillOnce(Return(nullptr));
  EXPECT_CALL(*valid_rec, End()).WillOnce(Return(valid_list));
  EXPECT_CALL(*secondary_q, Submit(testing::A<CommandListSpan>()))
    .WillOnce(Return());
  EXPECT_CALL(*valid_list, OnSubmitted()).Times(1);
  EXPECT_CALL(*valid_list, OnExecuted()).Times(1);
  EXPECT_CALL(*null_list, OnSubmitted()).Times(0);
  EXPECT_CALL(*null_list, OnExecuted()).Times(0);

  {
    auto d_null = commander->PrepareCommandRecorder(
      std::move(null_rec), null_list, false);
    auto d_valid = commander->PrepareCommandRecorder(
      std::move(valid_rec), valid_list, false);
  }

  commander->SubmitDeferredCommandLists();
  SimulateFrameCompletion();
}

//! OnExecuted idempotency: GIVEN a submitted list WHEN processing deferred
//! releases multiple times THEN OnExecuted is invoked only once.
NOLINT_TEST_F(DeferredSubmissionTest, OnExecuted_IdempotentAcrossFrames)
{
  auto list_a = CreateMockCommandList("idempotent-list");
  auto rec_a = CreateMockCommandRecorder(list_a, secondary_q);

  EXPECT_CALL(*rec_a, End()).WillOnce(Return(list_a));
  EXPECT_CALL(*secondary_q, Submit(testing::A<CommandListSpan>()))
    .WillOnce(Return());
  EXPECT_CALL(*list_a, OnSubmitted()).Times(1);
  EXPECT_CALL(*list_a, OnExecuted()).Times(1);

  {
    auto d = commander->PrepareCommandRecorder(std::move(rec_a), list_a, false);
  }

  commander->SubmitDeferredCommandLists();
  // First processing triggers OnExecuted
  SimulateFrameCompletion();
  // Subsequent processing should not retrigger
  SimulateFrameCompletion();
  SimulateFrameCompletion();
}

//=== Commander Error Testing with Mocks ===----------------------------------//

//! Test fixture for testing Commander error scenarios with strict mocks
class CommanderErrorTest : public CommanderTestBase {
protected:
  auto SetUp() -> void override
  {
    // Use strict mocks for precise error testing
    CommanderTestBase::SetUp();

    // Create additional mocked dependencies for error testing
    mock_command_list = CreateMockCommandList("test-list");
    mock_recorder = CreateMockCommandRecorder(mock_command_list, primary_q);

    // Set up default behaviors specific to error tests
    ON_CALL(*primary_q, GetQueueRole()).WillByDefault(Return(Role::kGraphics));
  }

  // Convenience accessors for error test-specific members
  std::shared_ptr<MockCommandList> mock_command_list;
  std::unique_ptr<MockCommandRecorder> mock_recorder;
};

//! Deferred failure: GIVEN a deferred list WHEN queue->Submit(span) throws
//! THEN Commander aggregates errors and rethrows, with no OnSubmitted().
NOLINT_TEST_F(CommanderErrorTest, DeferredSubmission_QueueFailure_Throws)
{
  EXPECT_CALL(*mock_recorder, End()).WillOnce(Return(mock_command_list));
  EXPECT_CALL(*mock_command_list, OnSubmitted()).Times(0);

  {
    auto deleter = commander->PrepareCommandRecorder(
      std::move(mock_recorder), mock_command_list, false);
  }

  EXPECT_CALL(*primary_q, Submit(testing::A<CommandListSpan>()))
    .WillOnce(Throw(std::runtime_error("Queue submission failed")));

  NOLINT_EXPECT_THROW(
    commander->SubmitDeferredCommandLists(), std::runtime_error);
}

//! Immediate failure: GIVEN immediate mode WHEN queue submission throws
//! THEN exception is swallowed (logged) and no lifecycle callbacks fire.
NOLINT_TEST_F(
  CommanderErrorTest, ImmediateSubmission_QueueFailure_LoggedNotThrown)
{
  EXPECT_CALL(*mock_recorder, End()).WillOnce(Return(mock_command_list));

  EXPECT_CALL(*primary_q, Submit(testing::A<CommandListPtr>()))
    .WillOnce(Throw(std::runtime_error("Immediate queue submission failed")));
  EXPECT_CALL(*mock_command_list, OnSubmitted()).Times(0);
  EXPECT_CALL(*mock_command_list, OnExecuted()).Times(0);

  NOLINT_EXPECT_NO_THROW({
    auto deleter = commander->PrepareCommandRecorder(
      std::move(mock_recorder), mock_command_list, true);
  });

  // Ensure no deferred OnExecuted sneaks in
  SimulateFrameCompletion();
}

//! Immediate End() nullptr: recorder produces no list => no Submit, no
//! callbacks, proves defensive handling of null product.
NOLINT_TEST_F(CommanderErrorTest, ImmediateSubmission_EndReturnsNull_NoSubmit)
{
  EXPECT_CALL(*mock_recorder, End()).WillOnce(Return(nullptr));
  EXPECT_CALL(*primary_q, Submit(testing::A<CommandListPtr>())).Times(0);
  EXPECT_CALL(*mock_command_list, OnSubmitted()).Times(0);
  EXPECT_CALL(*mock_command_list, OnExecuted()).Times(0);
  NOLINT_EXPECT_NO_THROW({
    auto deleter = commander->PrepareCommandRecorder(
      std::move(mock_recorder), mock_command_list, true);
  });
  SimulateFrameCompletion();
}

//! Immediate End() exception: End() throws pre-submit => error logged and
//! no Submit/OnSubmitted/OnExecuted invocations occur.
NOLINT_TEST_F(CommanderErrorTest, ImmediateSubmission_EndThrows_NoSubmit)
{
  EXPECT_CALL(*mock_recorder, End())
    .WillOnce(Throw(std::runtime_error("End failed")));
  EXPECT_CALL(*primary_q, Submit(testing::A<CommandListPtr>())).Times(0);
  EXPECT_CALL(*mock_command_list, OnSubmitted()).Times(0);
  EXPECT_CALL(*mock_command_list, OnExecuted()).Times(0);
  NOLINT_EXPECT_NO_THROW({
    auto deleter = commander->PrepareCommandRecorder(
      std::move(mock_recorder), mock_command_list, true);
  });
  SimulateFrameCompletion();
}

//! Deferred End() exception: End() throws (deferred path) => destruction
//! absorbs error, nothing queued, no Submit attempt.
NOLINT_TEST_F(CommanderErrorTest, RecorderEnd_Failure_LoggedNotThrown)
{
  EXPECT_CALL(*mock_recorder, End())
    .WillOnce(Throw(std::runtime_error("Recorder end failed")));

  NOLINT_EXPECT_NO_THROW({
    auto deleter = commander->PrepareCommandRecorder(
      std::move(mock_recorder), mock_command_list, false);
  });
}

//! Deferred End() nullptr: End() returns nullptr => entry skipped, no
//! submission later, no callbacks.
NOLINT_TEST_F(CommanderErrorTest, NoRecordedList_HandledGracefully)
{
  EXPECT_CALL(*mock_recorder, End()).WillOnce(Return(nullptr));

  NOLINT_EXPECT_NO_THROW({
    auto deleter = commander->PrepareCommandRecorder(
      std::move(mock_recorder), mock_command_list, false);
  });
}

//! API contract: null recorder argument MUST trigger death check.
NOLINT_TEST_F(CommanderErrorTest, NullRecorder_TriggersDeathTest)
{
  NOLINT_EXPECT_DEATH(
    {
      auto deleter
        = commander->PrepareCommandRecorder(nullptr, mock_command_list, false);
    },
    "CHECK FAILED.*recorder != nullptr");
}

//! API contract: null command list argument MUST trigger death check.
NOLINT_TEST_F(CommanderErrorTest, NullCommandList_TriggersDeathTest)
{
  NOLINT_EXPECT_DEATH(
    {
      auto deleter = commander->PrepareCommandRecorder(
        std::move(mock_recorder), nullptr, false);
    },
    "CHECK FAILED.*command_list != nullptr");
}

//! Deferred multi-list failure: batching two lists on one queue that throws
//! should yield aggregated error and zero OnSubmitted().
NOLINT_TEST_F(
  CommanderErrorTest, MultipleDeferredLists_PartialFailure_HandledProperly)
{
  EXPECT_CALL(*mock_recorder, End()).WillOnce(Return(mock_command_list));
  EXPECT_CALL(*mock_command_list, OnSubmitted()).Times(0);

  {
    auto deleter1 = commander->PrepareCommandRecorder(
      std::move(mock_recorder), mock_command_list, false);
  }

  auto list2 = CreateMockCommandList("list-2");
  auto recorder2 = CreateMockCommandRecorder(list2, primary_q);

  EXPECT_CALL(*recorder2, End()).WillOnce(Return(list2));
  EXPECT_CALL(*list2, OnSubmitted()).Times(0);

  {
    auto deleter2
      = commander->PrepareCommandRecorder(std::move(recorder2), list2, false);
  }

  EXPECT_CALL(*primary_q, Submit(testing::A<CommandListSpan>()))
    .WillOnce(Throw(std::runtime_error("Queue submission failed")));

  NOLINT_EXPECT_THROW(
    commander->SubmitDeferredCommandLists(), std::runtime_error);
}

//! Recovery: after a failed deferred submission, new deferred list on other
//! queue still submits successfully proving internal state reset.
NOLINT_TEST_F(CommanderErrorTest, ErrorRecovery_SubsequentSubmissions_Work)
{
  EXPECT_CALL(*mock_recorder, End()).WillOnce(Return(mock_command_list));
  EXPECT_CALL(*mock_command_list, OnSubmitted())
    .Times(0); // first submission fails

  {
    auto deleter = commander->PrepareCommandRecorder(
      std::move(mock_recorder), mock_command_list, false);
  }

  EXPECT_CALL(*primary_q, Submit(testing::A<CommandListSpan>()))
    .WillOnce(Throw(std::runtime_error("First submission failed")));

  NOLINT_EXPECT_THROW(
    commander->SubmitDeferredCommandLists(), std::runtime_error);

  auto recovery_list = CreateMockCommandList("recovery-list");
  auto recovery_recorder
    = CreateMockCommandRecorder(recovery_list, secondary_q);

  EXPECT_CALL(*recovery_recorder, End()).WillOnce(Return(recovery_list));

  EXPECT_CALL(*secondary_q, Submit(testing::A<CommandListSpan>()))
    .WillOnce(Return());
  EXPECT_CALL(*recovery_list, OnSubmitted()).Times(1);

  {
    auto deleter = commander->PrepareCommandRecorder(
      std::move(recovery_recorder), recovery_list, false);
  }

  NOLINT_EXPECT_NO_THROW(commander->SubmitDeferredCommandLists());
}

//=== Comprehensive Commander Queue Error Testing ===-------------------------//

//! Immediate multi-queue: two independent immediate submissions on two
//! queues should each submit & invoke OnSubmitted() once.
NOLINT_TEST_F(
  CommanderErrorTest, SuccessiveImmediateSubmissions_DifferentQueues_AllSucceed)
{
  // Use fixture-provided  secondary_q instead of creating a new one
  auto list2 = CreateMockCommandList("list-2");
  auto recorder2 = CreateMockCommandRecorder(list2, secondary_q);

  EXPECT_CALL(*mock_recorder, End()).WillOnce(Return(mock_command_list));
  EXPECT_CALL(*recorder2, End()).WillOnce(Return(list2));

  EXPECT_CALL(*primary_q, Submit(testing::A<CommandListPtr>()))
    .WillOnce(Return());
  EXPECT_CALL(*secondary_q, Submit(testing::A<CommandListPtr>()))
    .WillOnce(Return());
  EXPECT_CALL(*mock_command_list, OnSubmitted()).Times(1);
  EXPECT_CALL(*list2, OnSubmitted()).Times(1);

  NOLINT_EXPECT_NO_THROW({
    auto deleter1 = commander->PrepareCommandRecorder(
      std::move(mock_recorder), mock_command_list, true);
  });

  NOLINT_EXPECT_NO_THROW({
    auto deleter2
      = commander->PrepareCommandRecorder(std::move(recorder2), list2, true);
  });
}

//! Immediate retry same queue: first immediate submit throws, second list on
//! same queue still succeeds showing deleter resilience.
NOLINT_TEST_F(
  CommanderErrorTest, ImmediateSubmission_SameQueueAfterFailure_Works)
{
  auto list1 = CreateMockCommandList("fail-list");
  auto recorder1 = CreateMockCommandRecorder(list1, primary_q);
  EXPECT_CALL(*recorder1, End()).WillOnce(Return(list1));

  EXPECT_CALL(*primary_q, Submit(testing::A<CommandListPtr>()))
    .WillOnce(Throw(std::runtime_error("First submission failed")))
    .WillOnce(Return());
  EXPECT_CALL(*list1, OnSubmitted()).Times(0); // failed immediate submit

  NOLINT_EXPECT_NO_THROW({
    auto deleter1
      = commander->PrepareCommandRecorder(std::move(recorder1), list1, true);
  });

  auto list2 = CreateMockCommandList("success-list");
  EXPECT_CALL(*list2, OnSubmitted()).Times(1);
  auto recorder2 = CreateMockCommandRecorder(list2, primary_q);
  EXPECT_CALL(*recorder2, End()).WillOnce(Return(list2));

  NOLINT_EXPECT_NO_THROW({
    auto deleter2
      = commander->PrepareCommandRecorder(std::move(recorder2), list2, true);
  });
}

//! Immediate retry different queue: failure on primary queue does not taint
//! subsequent immediate submission on a different queue.
NOLINT_TEST_F(
  CommanderErrorTest, ImmediateSubmission_DifferentQueueAfterFailure_Works)
{
  auto list1 = CreateMockCommandList("fail-list");
  auto recorder1 = CreateMockCommandRecorder(list1, primary_q);
  EXPECT_CALL(*recorder1, End()).WillOnce(Return(list1));

  EXPECT_CALL(*primary_q, Submit(testing::A<CommandListPtr>()))
    .WillOnce(Throw(std::runtime_error("First submission failed")));
  EXPECT_CALL(*list1, OnSubmitted()).Times(0);

  NOLINT_EXPECT_NO_THROW({
    auto deleter1
      = commander->PrepareCommandRecorder(std::move(recorder1), list1, true);
  });

  // Use fixture-provided  secondary_q instead of creating a new one
  auto list2 = CreateMockCommandList("success-list");
  auto recorder2 = CreateMockCommandRecorder(list2, secondary_q);

  EXPECT_CALL(*recorder2, End()).WillOnce(Return(list2));
  EXPECT_CALL(*secondary_q, Submit(testing::A<CommandListPtr>()))
    .WillOnce(Return());
  EXPECT_CALL(*list2, OnSubmitted()).Times(1);

  NOLINT_EXPECT_NO_THROW({
    auto deleter2
      = commander->PrepareCommandRecorder(std::move(recorder2), list2, true);
  });
}

//! Deferred multi-queue success: lists targeting two queues produce two
//! Submit(span) calls, each list OnSubmitted() exactly once.
NOLINT_TEST_F(
  CommanderErrorTest, DeferredSubmissions_TwoDifferentQueues_AllSuccessful)
{
  // NOTE: Submission order between queues is not guaranteed; grouping logic
  // iterates map keyed by queue pointer. Tests assert per-queue effects only.
  // Create second queue and related mocks using factory method
  auto list2 = CreateMockCommandList("list-2");
  auto recorder2 = CreateMockCommandRecorder(list2, secondary_q);

  EXPECT_CALL(*mock_recorder, End()).WillOnce(Return(mock_command_list));
  EXPECT_CALL(*recorder2, End()).WillOnce(Return(list2));

  {
    auto deleter1 = commander->PrepareCommandRecorder(
      std::move(mock_recorder), mock_command_list, false);
    auto deleter2
      = commander->PrepareCommandRecorder(std::move(recorder2), list2, false);
  }

  EXPECT_CALL(*primary_q, Submit(testing::A<CommandListSpan>()))
    .WillOnce(Return());
  EXPECT_CALL(*secondary_q, Submit(testing::A<CommandListSpan>()))
    .WillOnce(Return());

  EXPECT_CALL(*mock_command_list, OnSubmitted()).WillOnce(Return());
  EXPECT_CALL(*list2, OnSubmitted()).WillOnce(Return());

  NOLINT_EXPECT_NO_THROW(commander->SubmitDeferredCommandLists());
}

//! Deferred partial failure (first fails): failing queue lists skipped for
//! OnSubmitted(); succeeding queue lists still marked submitted.
NOLINT_TEST_F(CommanderErrorTest,
  DeferredSubmissions_TwoDifferentQueues_FirstFailsSecondSucceeds)
{
  auto list2 = CreateMockCommandList("list-2");
  auto recorder2 = CreateMockCommandRecorder(list2, secondary_q);

  EXPECT_CALL(*mock_recorder, End()).WillOnce(Return(mock_command_list));
  EXPECT_CALL(*recorder2, End()).WillOnce(Return(list2));

  {
    auto deleter1 = commander->PrepareCommandRecorder(
      std::move(mock_recorder), mock_command_list, false);
    auto deleter2
      = commander->PrepareCommandRecorder(std::move(recorder2), list2, false);
  }

  EXPECT_CALL(*primary_q, Submit(testing::A<CommandListSpan>()))
    .WillOnce(Throw(std::runtime_error("First queue failed")));
  EXPECT_CALL(*secondary_q, Submit(testing::A<CommandListSpan>()))
    .WillOnce(Return());

  EXPECT_CALL(*list2, OnSubmitted()).Times(1);
  EXPECT_CALL(*mock_command_list, OnSubmitted()).Times(0);

  NOLINT_EXPECT_THROW(
    commander->SubmitDeferredCommandLists(), std::runtime_error);
}

//! Deferred partial failure (second fails): first queue succeeds (lists
//! OnSubmitted()), second queue failure triggers aggregated throw.
NOLINT_TEST_F(CommanderErrorTest,
  DeferredSubmissions_TwoDifferentQueues_FirstSucceedsSecondFails)
{
  auto list2 = CreateMockCommandList("list-2");
  auto recorder2 = CreateMockCommandRecorder(list2, secondary_q);

  EXPECT_CALL(*mock_recorder, End()).WillOnce(Return(mock_command_list));
  EXPECT_CALL(*recorder2, End()).WillOnce(Return(list2));

  {
    auto deleter1 = commander->PrepareCommandRecorder(
      std::move(mock_recorder), mock_command_list, false);
    auto deleter2
      = commander->PrepareCommandRecorder(std::move(recorder2), list2, false);
  }

  EXPECT_CALL(*primary_q, Submit(testing::A<CommandListSpan>()))
    .WillOnce(Return());
  EXPECT_CALL(*secondary_q, Submit(testing::A<CommandListSpan>()))
    .WillOnce(Throw(std::runtime_error("Second queue failed")));

  EXPECT_CALL(*mock_command_list, OnSubmitted()).Times(1);
  EXPECT_CALL(*list2, OnSubmitted()).Times(0);

  NOLINT_EXPECT_THROW(
    commander->SubmitDeferredCommandLists(), std::runtime_error);
}

//! Deferred dual failure: both queues throw; all lists produce error entries
//! and no OnSubmitted() calls occur.
NOLINT_TEST_F(
  CommanderErrorTest, DeferredSubmissions_TwoDifferentQueues_BothFail)
{
  auto list2 = CreateMockCommandList("list-2");
  auto recorder2 = CreateMockCommandRecorder(list2, secondary_q);

  EXPECT_CALL(*mock_recorder, End()).WillOnce(Return(mock_command_list));
  EXPECT_CALL(*recorder2, End()).WillOnce(Return(list2));

  {
    auto deleter1 = commander->PrepareCommandRecorder(
      std::move(mock_recorder), mock_command_list, false);
    auto deleter2
      = commander->PrepareCommandRecorder(std::move(recorder2), list2, false);
  }

  EXPECT_CALL(*primary_q, Submit(testing::A<CommandListSpan>()))
    .WillOnce(Throw(std::runtime_error("First queue failed")));
  EXPECT_CALL(*secondary_q, Submit(testing::A<CommandListSpan>()))
    .WillOnce(Throw(std::runtime_error("Second queue failed")));

  EXPECT_CALL(*mock_command_list, OnSubmitted()).Times(0);
  EXPECT_CALL(*list2, OnSubmitted()).Times(0);

  NOLINT_EXPECT_THROW(
    commander->SubmitDeferredCommandLists(), std::runtime_error);
}

//! Logging (deferred failure): verifies error lines contain list name and
//! propagated queue exception text, and throw occurs after logging.
NOLINT_TEST_F(CommanderErrorTest, DeferredSubmission_ErrorLogging_VerifyFormat)
{
  EXPECT_CALL(*mock_recorder, End()).WillOnce(Return(mock_command_list));

  {
    auto deleter = commander->PrepareCommandRecorder(
      std::move(mock_recorder), mock_command_list, false);
  }

  EXPECT_CALL(*primary_q, Submit(testing::A<CommandListSpan>()))
    .WillOnce(Throw(std::runtime_error("Queue submission failed")));

  const oxygen::testing::ScopedLogCapture capture(
    "TestCapture", loguru::Verbosity_ERROR);

  NOLINT_EXPECT_THROW(
    commander->SubmitDeferredCommandLists(), std::runtime_error);

  EXPECT_TRUE(capture.Contains("Queue submission failed"));
}

//! Logging (immediate failure): verifies immediate path logs formatted
//! failure line and retains original exception text without rethrow.
NOLINT_TEST_F(CommanderErrorTest, ImmediateSubmission_ErrorLogging_VerifyFormat)
{
  EXPECT_CALL(*mock_recorder, End()).WillOnce(Return(mock_command_list));

  EXPECT_CALL(*primary_q, Submit(testing::A<CommandListPtr>()))
    .WillOnce(Throw(std::runtime_error("Immediate queue submission failed")));

  const oxygen::testing::ScopedLogCapture capture(
    "TestCapture", loguru::Verbosity_ERROR);

  NOLINT_EXPECT_NO_THROW({
    auto deleter = commander->PrepareCommandRecorder(
      std::move(mock_recorder), mock_command_list, true);
  });

  EXPECT_TRUE(capture.Contains("-failed- 'test-list' :"));
  EXPECT_TRUE(capture.Contains("Immediate queue submission failed"));
}

//=== Concurrency Test Cases ===----------------------------------------------//

// Concurrency fixture: uses the base setup for testing thread safety
class ConcurrencyTest : public SubmissionTestBase { };

//! Concurrency: multiple threads racing to call SubmitDeferredCommandLists()
//! should result in exactly one actual Submit(span) and no exceptions.
NOLINT_TEST_F(ConcurrencyTest, ConcurrentSubmission_ThreadSafe)
{
  // Arrange: Create command recorders to simulate submission workload
  auto mock_list_a = CreateMockCommandList("concurrent-a");
  auto mock_recorder_a = CreateMockCommandRecorder(mock_list_a, secondary_q);
  auto mock_list_b = CreateMockCommandList("concurrent-b");
  auto mock_recorder_b = CreateMockCommandRecorder(mock_list_b, secondary_q);
  EXPECT_CALL(*mock_recorder_a, End()).WillOnce(Return(mock_list_a));
  EXPECT_CALL(*mock_recorder_b, End()).WillOnce(Return(mock_list_b));
  EXPECT_CALL(*secondary_q, Submit(testing::A<CommandListSpan>())).Times(1);
  EXPECT_CALL(*mock_list_a, OnSubmitted()).Times(1);
  EXPECT_CALL(*mock_list_b, OnSubmitted()).Times(1);

  {
    auto deleter_a = commander->PrepareCommandRecorder(
      std::move(mock_recorder_a), mock_list_a, false);
    auto deleter_b = commander->PrepareCommandRecorder(
      std::move(mock_recorder_b), mock_list_b, false);
  }

  std::vector<std::thread> threads;
  std::atomic<int> submission_count { 0 };

  // Act: Run concurrent submissions
  threads.reserve(3);
  for (int i = 0; i < 3; ++i) {
    threads.emplace_back([this, &submission_count]() {
      try {
        commander->SubmitDeferredCommandLists();
        submission_count.fetch_add(1);
      } catch (const std::exception& e) {
        FAIL() << "Concurrent submission threw: " << e.what();
      }
    });
  }

  for (auto& t : threads) {
    t.join();
  }

  // Assert: All submissions completed successfully
  EXPECT_EQ(submission_count.load(), 3);

  // Process any deferred work
  real_reclaimer->ProcessAllDeferredReleases();
}

} // anonymous namespace for additional tests
