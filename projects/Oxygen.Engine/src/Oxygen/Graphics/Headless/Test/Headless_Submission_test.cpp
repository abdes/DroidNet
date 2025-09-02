//===----------------------------------------------------------------------===//
// Tests for deferred submission behavior in the headless graphics backend.
// Verifies that when a recorder is created with immediate_submission=false the
// recorded command list is not submitted until SubmitDeferredCommandLists()
// is called on the headless Graphics instance.
//===----------------------------------------------------------------------===//

#include <chrono>
#include <memory>
#include <thread>

#include <Oxygen/Testing/GTest.h>
#include <Oxygen/Testing/ScopedLogCapture.h>

using testing::NiceMock;
using testing::Return;
using testing::StrictMock;
using testing::Test;
using testing::Throw;

#include <Oxygen/Core/Types/Frame.h>
#include <Oxygen/Core/Types/Scissors.h>
#include <Oxygen/Core/Types/ViewPort.h>
#include <Oxygen/Graphics/Common/BackendModule.h>
#include <Oxygen/Graphics/Common/Buffer.h>
#include <Oxygen/Graphics/Common/CommandList.h>
#include <Oxygen/Graphics/Common/CommandQueue.h>
#include <Oxygen/Graphics/Common/CommandRecorder.h>
#include <Oxygen/Graphics/Common/Detail/DeferredReclaimer.h>
#include <Oxygen/Graphics/Common/Framebuffer.h>
#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/Graphics/Common/NativeObject.h>
#include <Oxygen/Graphics/Common/PipelineState.h>
#include <Oxygen/Graphics/Common/Queues.h>
#include <Oxygen/Graphics/Common/ResourceRegistry.h>
#include <Oxygen/Graphics/Common/Texture.h>
#include <Oxygen/Graphics/Common/Types/ClearFlags.h>
#include <Oxygen/Graphics/Headless/CommandRecorder.h>
#include <Oxygen/Graphics/Headless/Graphics.h>
#include <Oxygen/Graphics/Headless/Internal/Commander.h>

extern "C" auto GetGraphicsModuleApi() -> void*;

namespace {

using testing::Test;

using Role = oxygen::graphics::QueueRole;
using Alloc = oxygen::graphics::QueueAllocationPreference;
using Share = oxygen::graphics::QueueSharingPreference;
using oxygen::graphics::CommandList;
using oxygen::graphics::CommandQueue;
using oxygen::graphics::QueueKey;
using oxygen::graphics::QueueSpecification;
using oxygen::graphics::detail::DeferredReclaimer;

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
  MOCK_METHOD(void, Submit, (std::shared_ptr<CommandList>), (override));
  MOCK_METHOD(void, Submit, (std::span<std::shared_ptr<CommandList>>), (override));
  MOCK_METHOD(oxygen::graphics::QueueRole, GetQueueRole, (), (const, override));
  // clang-format on
  // NOLINTEND
};

// Mock CommandRecorder that can simulate End() failures
// ReSharper disable once CppClassCanBeFinal - mocks cannot be final
class MockCommandRecorder : public oxygen::graphics::CommandRecorder {
public:
  explicit MockCommandRecorder(std::shared_ptr<CommandList> command_list,
    const oxygen::observer_ptr<CommandQueue> queue)
    : CommandRecorder(std::move(command_list), queue)
  {
  }

  // NOLINTBEGIN
  // clang-format off
  MOCK_METHOD(void, Begin, (), (override));
  MOCK_METHOD(std::shared_ptr<CommandList>, End, (), (override));
  MOCK_METHOD(void, RecordQueueSignal, (std::uint64_t), (override));
  MOCK_METHOD(void, RecordQueueWait, (std::uint64_t), (override));
  MOCK_METHOD(void, SetPipelineState, (oxygen::graphics::GraphicsPipelineDesc), (override));
  MOCK_METHOD(void, SetPipelineState, (oxygen::graphics::ComputePipelineDesc), (override));
  MOCK_METHOD(void, SetGraphicsRootConstantBufferView, (std::uint32_t, std::uint64_t), (override));
  MOCK_METHOD(void, SetComputeRootConstantBufferView, (std::uint32_t, std::uint64_t), (override));
  MOCK_METHOD(void, SetGraphicsRoot32BitConstant, (std::uint32_t, std::uint32_t, std::uint32_t), (override));
  MOCK_METHOD(void, SetComputeRoot32BitConstant, (std::uint32_t, std::uint32_t, std::uint32_t), (override));
  MOCK_METHOD(void, SetRenderTargets, (std::span<oxygen::graphics::NativeObject>, std::optional<oxygen::graphics::NativeObject>), (override));
  MOCK_METHOD(void, SetViewport, (const oxygen::ViewPort&), (override));
  MOCK_METHOD(void, SetScissors, (const oxygen::Scissors&), (override));
  MOCK_METHOD(void, Draw, (std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t), (override));
  MOCK_METHOD(void, DrawIndexed, (std::uint32_t, std::uint32_t, std::uint32_t, std::int32_t, std::uint32_t), (override));
  MOCK_METHOD(void, Dispatch, (std::uint32_t, std::uint32_t, std::uint32_t), (override));
  MOCK_METHOD(void, SetVertexBuffers, (std::uint32_t, const std::shared_ptr<oxygen::graphics::Buffer>*, const std::uint32_t*), (const, override));
  MOCK_METHOD(void, BindIndexBuffer, (const oxygen::graphics::Buffer&, oxygen::Format), (override));
  MOCK_METHOD(void, BindFrameBuffer, (const oxygen::graphics::Framebuffer&), (override));
  MOCK_METHOD(void, ClearDepthStencilView, (const oxygen::graphics::Texture&, const oxygen::graphics::NativeObject&, oxygen::graphics::ClearFlags, float, std::uint8_t), (override));
  MOCK_METHOD(void, ClearFramebuffer, (const oxygen::graphics::Framebuffer&, std::optional<std::vector<std::optional<oxygen::graphics::Color>>>, std::optional<float>, std::optional<std::uint8_t>), (override));
  MOCK_METHOD(void, CopyBuffer, (oxygen::graphics::Buffer&, std::size_t, const oxygen::graphics::Buffer&, std::size_t, std::size_t), (override));
  MOCK_METHOD(void, CopyBufferToTexture, (const oxygen::graphics::Buffer&, const oxygen::graphics::TextureUploadRegion&, oxygen::graphics::Texture&), (override));
  MOCK_METHOD(void, CopyBufferToTexture, (const oxygen::graphics::Buffer&, std::span<const oxygen::graphics::TextureUploadRegion>, oxygen::graphics::Texture&), (override));
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
class TestCommander final
  : public oxygen::graphics::headless::internal::Commander {
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

//=== Queue Allocation Strategy ===-------------------------------------------//

// Create default queues using a small local strategy similar to the smoke
// test so we can find by name.
class MultiQueueStrategy : public oxygen::graphics::QueuesStrategy {
public:
  [[nodiscard]] auto Specifications() const
    -> std::vector<QueueSpecification> override
  {
    return {
      QueueSpecification {
        .key = QueueKey { "multi-gfx" },
        .role = Role::kGraphics,
        .allocation_preference = Alloc::kDedicated,
        .sharing_preference = Share::kNamed,
      },
      QueueSpecification {
        .key = QueueKey { "multi-cpu" },
        .role = Role::kCompute,
        .allocation_preference = Alloc::kDedicated,
        .sharing_preference = Share::kNamed,
      },
    };
  }

  [[nodiscard]] auto KeyFor(const Role role) const -> QueueKey override
  {
    switch (role) {
    case Role::kGraphics:
    case Role::kTransfer:
    case Role::kPresent:
      return QueueKey { "multi-gfx" };
    case Role::kCompute:
      return QueueKey { "multi-cpu" };
    case oxygen::graphics::QueueRole::kMax:;
    }
    return QueueKey { "__invalid__" };
  }

  [[nodiscard]] auto Clone() const -> std::unique_ptr<QueuesStrategy> override
  {
    return std::make_unique<MultiQueueStrategy>(*this);
  }
};

//=== Base Test Fixture ===---------------------------------------------------//

class SubmissionTestBase : public Test {
protected:
  auto SetUp() -> void override
  {
    module_ptr = static_cast<oxygen::graphics::GraphicsModuleApi*>(
      GetGraphicsModuleApi());
    ASSERT_NE(module_ptr, nullptr);

    constexpr oxygen::SerializedBackendConfig cfg {
      .json_data = "{}",
      .size = 2,
    };
    backend = module_ptr->CreateBackend(cfg);
    ASSERT_NE(backend, nullptr);

    headless = static_cast<oxygen::graphics::headless::Graphics*>(backend);
    ASSERT_NE(headless, nullptr);

    queue_strategy = std::make_unique<MultiQueueStrategy>();
    headless->CreateCommandQueues(*queue_strategy);

    gfx_queue
      = headless->GetCommandQueue(queue_strategy->KeyFor(Role::kGraphics));
    cpu_queue
      = headless->GetCommandQueue(queue_strategy->KeyFor(Role::kCompute));
    ASSERT_NE(gfx_queue, nullptr);
    ASSERT_NE(cpu_queue, nullptr);
  }

  auto TearDown() -> void override
  {
    gfx_queue.reset();
    cpu_queue.reset();
    if (module_ptr && backend) {
      module_ptr->DestroyBackend();
    }
    backend = nullptr;
    headless = nullptr;
    module_ptr = nullptr;
    queue_strategy.reset();
  }

  // Helpers
  auto AcquireList(const oxygen::graphics::QueueRole role,
    const char* name) const -> std::shared_ptr<CommandList>
  {
    return headless->AcquireCommandList(role, name);
  }

  [[nodiscard]] auto AcquireRecorder(const std::shared_ptr<CommandQueue>& q,
    std::shared_ptr<CommandList> list, const bool immediate = false) const
  {
    return headless->AcquireCommandRecorder(
      oxygen::observer_ptr { q.get() }, std::move(list), immediate);
  }

  // Members
  oxygen::graphics::GraphicsModuleApi* module_ptr { nullptr };
  void* backend { nullptr };
  oxygen::graphics::headless::Graphics* headless { nullptr };
  std::unique_ptr<MultiQueueStrategy> queue_strategy;
  std::shared_ptr<CommandQueue> gfx_queue;
  std::shared_ptr<CommandQueue> cpu_queue;
};

//=== Immediate Submission Test Cases ===-------------------------------------//

// Immediate-submission fixture: same setup but used to mark tests that
// exercise immediate submission semantics.
class ImmediateSubmissionTest : public SubmissionTestBase { };

//! Verifies immediate submission bypasses deferred queue and executes
//! immediately
NOLINT_TEST_F(ImmediateSubmissionTest, ImmediateSubmission_BypassesDeferred)
{
  const auto list = AcquireList(gfx_queue->GetQueueRole(), "immediate-list");
  ASSERT_NE(list, nullptr);
  EXPECT_TRUE(list->IsFree());

  const auto before_value = gfx_queue->GetCurrentValue();
  const auto done = before_value + 1;

  {
    const auto rec = AcquireRecorder(gfx_queue, list, true);
    ASSERT_NE(rec, nullptr);
    rec->RecordQueueSignal(done);
  }

  EXPECT_TRUE(list->IsSubmitted());

  gfx_queue->Wait(done);
  headless->BeginFrame(
    oxygen::frame::SequenceNumber { 0 }, oxygen::frame::Slot { 0 });

  EXPECT_TRUE(list->IsFree());
  EXPECT_GE(gfx_queue->GetCompletedValue(), done);
}

//! Verifies empty command list with immediate submission follows lifecycle
NOLINT_TEST_F(ImmediateSubmissionTest, EmptyList_ImmediateFollowsLifecycle)
{
  const auto list
    = AcquireList(gfx_queue->GetQueueRole(), "empty-list-immediate");
  ASSERT_NE(list, nullptr);

  const auto before = gfx_queue->GetCurrentValue();

  {
    const auto rec = AcquireRecorder(gfx_queue, list, true);
    ASSERT_NE(rec, nullptr);
  }

  EXPECT_TRUE(list->IsSubmitted());
  EXPECT_EQ(gfx_queue->GetCompletedValue(), before);

  headless->BeginFrame(
    oxygen::frame::SequenceNumber { 0 }, oxygen::frame::Slot { 0 });

  EXPECT_TRUE(list->IsFree());
}

//=== Deferred Submission Test Cases ===--------------------------------------//

// Deferred-submission fixture: uses the base setup and defaults to deferred
class DeferredSubmissionTest : public SubmissionTestBase { };

//! Verifies complete deferred submission lifecycle: Free -> Recording -> Closed
//! -> Submitted -> Free
NOLINT_TEST_F(DeferredSubmissionTest, DeferredLifecycle_CompleteFlow)
{
  const auto cmd_list
    = AcquireList(gfx_queue->GetQueueRole(), "deferred-cmd-list");
  ASSERT_NE(cmd_list, nullptr);
  EXPECT_TRUE(cmd_list->IsFree());

  const auto before_value = gfx_queue->GetCurrentValue();
  const auto completion_value = before_value + 1;

  {
    const auto recorder
      = AcquireRecorder(gfx_queue, cmd_list, /*immediate=*/false);
    ASSERT_NE(recorder, nullptr);
    recorder->RecordQueueSignal(completion_value);
  }

  EXPECT_TRUE(cmd_list->IsClosed());
  EXPECT_LT(gfx_queue->GetCompletedValue(), completion_value);

  headless->SubmitDeferredCommandLists();

  EXPECT_TRUE(cmd_list->IsSubmitted());

  gfx_queue->Wait(completion_value);
  headless->BeginFrame(
    oxygen::frame::SequenceNumber { 0 }, oxygen::frame::Slot { 0 });

  EXPECT_TRUE(cmd_list->IsFree());
  EXPECT_GE(gfx_queue->GetCompletedValue(), completion_value);
}

//! Verifies multiple deferred lists from different queues work independently
NOLINT_TEST_F(DeferredSubmissionTest, MultipleRecorders_DifferentQueues)
{
  auto list_gfx = AcquireList(gfx_queue->GetQueueRole(), "defer-gfx");
  auto list_cpu = AcquireList(cpu_queue->GetQueueRole(), "defer-cpu");
  ASSERT_NE(list_gfx, nullptr);
  ASSERT_NE(list_cpu, nullptr);
  EXPECT_TRUE(list_gfx->IsFree());
  EXPECT_TRUE(list_cpu->IsFree());

  const auto v_gfx_done = gfx_queue->GetCurrentValue() + 1;
  const auto v_cpu_done = cpu_queue->GetCurrentValue() + 1;

  {
    auto r_gfx = AcquireRecorder(gfx_queue, list_gfx, false);
    auto r_cpu = AcquireRecorder(cpu_queue, list_cpu, false);
    ASSERT_NE(r_gfx, nullptr);
    ASSERT_NE(r_cpu, nullptr);
    r_gfx->RecordQueueSignal(v_gfx_done);
    r_cpu->RecordQueueSignal(v_cpu_done);
  }

  EXPECT_TRUE(list_gfx->IsClosed());
  EXPECT_TRUE(list_cpu->IsClosed());
  EXPECT_LT(gfx_queue->GetCompletedValue(), v_gfx_done);
  EXPECT_LT(cpu_queue->GetCompletedValue(), v_cpu_done);

  headless->SubmitDeferredCommandLists();

  EXPECT_TRUE(list_gfx->IsSubmitted());
  EXPECT_TRUE(list_cpu->IsSubmitted());

  gfx_queue->Wait(v_gfx_done);
  cpu_queue->Wait(v_cpu_done);
  headless->BeginFrame(
    oxygen::frame::SequenceNumber { 0 }, oxygen::frame::Slot { 0 });

  EXPECT_TRUE(list_gfx->IsFree());
  EXPECT_TRUE(list_cpu->IsFree());
  EXPECT_GE(gfx_queue->GetCompletedValue(), v_gfx_done);
  EXPECT_GE(cpu_queue->GetCompletedValue(), v_cpu_done);
}

//! Verifies SubmitDeferredCommandLists() is idempotent when no lists pending
NOLINT_TEST_F(DeferredSubmissionTest, SubmitDeferred_Idempotent)
{
  headless->SubmitDeferredCommandLists();
  headless->SubmitDeferredCommandLists();
}

//! Verifies multiple deferred lists on same queue are batched together
NOLINT_TEST_F(DeferredSubmissionTest, MultipleLists_SameQueue_Batched)
{
  const auto list_a = AcquireList(gfx_queue->GetQueueRole(), "batch-a");
  const auto list_b = AcquireList(gfx_queue->GetQueueRole(), "batch-b");
  ASSERT_NE(list_a, nullptr);
  ASSERT_NE(list_b, nullptr);

  const auto before = gfx_queue->GetCurrentValue();
  const auto v_a = before + 1;
  const auto v_b = before + 2;

  {
    const auto r_a = AcquireRecorder(gfx_queue, list_a, false);
    const auto r_b = AcquireRecorder(gfx_queue, list_b, false);
    ASSERT_NE(r_a, nullptr);
    ASSERT_NE(r_b, nullptr);
    r_a->RecordQueueSignal(v_a);
    r_b->RecordQueueSignal(v_b);
  }

  headless->SubmitDeferredCommandLists();

  EXPECT_TRUE(list_a->IsSubmitted());
  EXPECT_TRUE(list_b->IsSubmitted());

  gfx_queue->Wait(v_b);
  headless->BeginFrame(
    oxygen::frame::SequenceNumber { 0 }, oxygen::frame::Slot { 0 });

  EXPECT_TRUE(list_a->IsFree());
  EXPECT_TRUE(list_b->IsFree());
  EXPECT_GE(gfx_queue->GetCompletedValue(), v_b);
}

//! Verifies immediate and deferred submission work together without
//! interference
NOLINT_TEST_F(DeferredSubmissionTest, ImmediateAndDeferred_Interleaved)
{
  const auto list_def
    = AcquireList(gfx_queue->GetQueueRole(), "interleaved-def");
  const auto list_imm
    = AcquireList(gfx_queue->GetQueueRole(), "interleaved-imm");
  ASSERT_NE(list_def, nullptr);
  ASSERT_NE(list_imm, nullptr);

  const auto before = gfx_queue->GetCurrentValue();
  const auto imm = before + 1;
  const auto def = before + 2;

  {
    const auto r_def = AcquireRecorder(gfx_queue, list_def, false);
    ASSERT_NE(r_def, nullptr);
    r_def->RecordQueueSignal(def);
  }

  {
    const auto r_imm = AcquireRecorder(gfx_queue, list_imm, true);
    ASSERT_NE(r_imm, nullptr);
    r_imm->RecordQueueSignal(imm);
  }

  EXPECT_TRUE(list_imm->IsSubmitted());

  gfx_queue->Wait(imm);
  headless->BeginFrame(
    oxygen::frame::SequenceNumber { 0 }, oxygen::frame::Slot { 0 });

  EXPECT_TRUE(list_imm->IsFree());

  headless->SubmitDeferredCommandLists();

  EXPECT_TRUE(list_def->IsSubmitted());

  gfx_queue->Wait(def);
  headless->BeginFrame(
    oxygen::frame::SequenceNumber { 0 }, oxygen::frame::Slot { 0 });

  EXPECT_TRUE(list_def->IsFree());
  EXPECT_GE(gfx_queue->GetCompletedValue(), def);
}

//! Verifies empty deferred command list follows normal lifecycle
NOLINT_TEST_F(DeferredSubmissionTest, EmptyList_FollowsNormalLifecycle)
{
  const auto list = AcquireList(gfx_queue->GetQueueRole(), "empty-list");
  ASSERT_NE(list, nullptr);

  const auto before = gfx_queue->GetCurrentValue();

  {
    const auto rec = AcquireRecorder(gfx_queue, list, false);
    ASSERT_NE(rec, nullptr);
  }

  EXPECT_TRUE(list->IsClosed());

  headless->SubmitDeferredCommandLists();

  EXPECT_TRUE(list->IsSubmitted());
  EXPECT_EQ(gfx_queue->GetCompletedValue(), before);

  headless->BeginFrame(
    oxygen::frame::SequenceNumber { 0 }, oxygen::frame::Slot { 0 });

  EXPECT_TRUE(list->IsFree());
}

//! Verifies multiple SubmitDeferredCommandLists() calls don't double-submit
NOLINT_TEST_F(DeferredSubmissionTest, NoDoubleSubmit_PerList)
{
  const auto list = AcquireList(gfx_queue->GetQueueRole(), "single-submit");
  ASSERT_NE(list, nullptr);

  const auto before = gfx_queue->GetCurrentValue();
  const auto done = before + 1;

  {
    const auto rec = AcquireRecorder(gfx_queue, list, false);
    ASSERT_NE(rec, nullptr);
    rec->RecordQueueSignal(done);
  }

  headless->SubmitDeferredCommandLists();
  headless->SubmitDeferredCommandLists();

  EXPECT_TRUE(list->IsSubmitted());

  gfx_queue->Wait(done);
  headless->BeginFrame(
    oxygen::frame::SequenceNumber { 0 }, oxygen::frame::Slot { 0 });

  EXPECT_TRUE(list->IsFree());
  EXPECT_GE(gfx_queue->GetCompletedValue(), done);
}

//! Verifies command list transitions from Submitted back to Free after
//! execution completes
NOLINT_TEST_F(DeferredSubmissionTest, SubmittedToFree_AfterExecution)
{
  const auto list = AcquireList(gfx_queue->GetQueueRole(), "submitted-exec");
  ASSERT_NE(list, nullptr);

  const auto before = gfx_queue->GetCurrentValue();
  const auto done = before + 1;

  {
    const auto rec = AcquireRecorder(gfx_queue, list, false);
    ASSERT_NE(rec, nullptr);
    rec->RecordQueueSignal(done);
  }

  headless->SubmitDeferredCommandLists();

  EXPECT_TRUE(list->IsSubmitted());

  gfx_queue->Wait(done);
  headless->BeginFrame(
    oxygen::frame::SequenceNumber { 0 }, oxygen::frame::Slot { 0 });

  EXPECT_TRUE(list->IsFree());
  EXPECT_GE(gfx_queue->GetCompletedValue(), done);
}

//=== Commander Error Testing with Mocks ===----------------------------------//

// Test fixture for testing Commander error scenarios with mocked dependencies
class CommanderErrorTest : public Test {
protected:
  auto SetUp() -> void override
  {
    // Create real DeferredReclaimer for testing
    real_reclaimer = std::make_unique<DeferredReclaimer>();

    // Create mocked dependencies
    mock_queue = std::make_shared<StrictMock<MockCommandQueue>>("test-queue");
    mock_command_list
      = std::make_shared<NiceMock<MockCommandList>>("test-list");
    mock_recorder = std::make_unique<NiceMock<MockCommandRecorder>>(
      mock_command_list, oxygen::observer_ptr { mock_queue.get() });

    // Set up default behaviors for mocks
    ON_CALL(*mock_queue, GetQueueRole()).WillByDefault(Return(Role::kGraphics));
    ON_CALL(*mock_recorder, Begin()).WillByDefault(Return());

    // Create TestCommander with real DeferredReclaimer injection
    commander = std::make_unique<TestCommander>(*real_reclaimer);
  }

  auto TearDown() -> void override
  {
    commander.reset();
    mock_recorder.reset();
    mock_command_list.reset();
    mock_queue.reset();
    real_reclaimer.reset();
  }

  std::unique_ptr<DeferredReclaimer> real_reclaimer;
  std::unique_ptr<TestCommander> commander;
  std::shared_ptr<StrictMock<MockCommandQueue>> mock_queue;
  std::shared_ptr<NiceMock<MockCommandList>> mock_command_list;
  std::unique_ptr<NiceMock<MockCommandRecorder>> mock_recorder;
};

//! Tests deferred submission failure propagates as exception
NOLINT_TEST_F(CommanderErrorTest, DeferredSubmission_QueueFailure_Throws)
{
  EXPECT_CALL(*mock_recorder, End()).WillOnce(Return(mock_command_list));

  {
    auto deleter = commander->PrepareCommandRecorder(
      std::move(mock_recorder), mock_command_list, false);
  }

  EXPECT_CALL(
    *mock_queue, Submit(testing::A<std::span<std::shared_ptr<CommandList>>>()))
    .WillOnce(Throw(std::runtime_error("Queue submission failed")));

  NOLINT_EXPECT_THROW(
    commander->SubmitDeferredCommandLists(), std::runtime_error);
}

//! Tests immediate submission queue failure is logged but doesn't throw
NOLINT_TEST_F(
  CommanderErrorTest, ImmediateSubmission_QueueFailure_LoggedNotThrown)
{
  EXPECT_CALL(*mock_recorder, End()).WillOnce(Return(mock_command_list));

  EXPECT_CALL(*mock_queue, Submit(testing::A<std::shared_ptr<CommandList>>()))
    .WillOnce(Throw(std::runtime_error("Immediate queue submission failed")));

  NOLINT_EXPECT_NO_THROW({
    auto deleter = commander->PrepareCommandRecorder(
      std::move(mock_recorder), mock_command_list, true);
  });
}

//! Tests recorder End() failure during deferred submission logs but doesn't
//! throw
NOLINT_TEST_F(CommanderErrorTest, RecorderEnd_Failure_LoggedNotThrown)
{
  EXPECT_CALL(*mock_recorder, End())
    .WillOnce(Throw(std::runtime_error("Recorder end failed")));

  NOLINT_EXPECT_NO_THROW({
    auto deleter = commander->PrepareCommandRecorder(
      std::move(mock_recorder), mock_command_list, false);
  });
}

//! Tests null command list from recorder End() is handled gracefully
NOLINT_TEST_F(CommanderErrorTest, NoRecordedList_HandledGracefully)
{
  EXPECT_CALL(*mock_recorder, End()).WillOnce(Return(nullptr));

  NOLINT_EXPECT_NO_THROW({
    auto deleter = commander->PrepareCommandRecorder(
      std::move(mock_recorder), mock_command_list, false);
  });
}

//! Tests null recorder parameter triggers death test
NOLINT_TEST_F(CommanderErrorTest, NullRecorder_TriggersDeathTest)
{
  NOLINT_EXPECT_DEATH(
    {
      auto deleter
        = commander->PrepareCommandRecorder(nullptr, mock_command_list, false);
    },
    "CHECK FAILED.*recorder != nullptr");
}

//! Tests null command list parameter triggers death test
NOLINT_TEST_F(CommanderErrorTest, NullCommandList_TriggersDeathTest)
{
  NOLINT_EXPECT_DEATH(
    {
      auto deleter = commander->PrepareCommandRecorder(
        std::move(mock_recorder), nullptr, false);
    },
    "CHECK FAILED.*command_list != nullptr");
}

//! Tests multiple deferred lists with queue submission failure
NOLINT_TEST_F(
  CommanderErrorTest, MultipleDeferredLists_PartialFailure_HandledProperly)
{
  EXPECT_CALL(*mock_recorder, End()).WillOnce(Return(mock_command_list));

  {
    auto deleter1 = commander->PrepareCommandRecorder(
      std::move(mock_recorder), mock_command_list, false);
  }

  auto list2 = std::make_shared<NiceMock<MockCommandList>>("list-2");
  auto recorder2 = std::make_unique<NiceMock<MockCommandRecorder>>(
    list2, oxygen::observer_ptr { mock_queue.get() });

  ON_CALL(*recorder2, Begin()).WillByDefault(Return());
  EXPECT_CALL(*recorder2, End()).WillOnce(Return(list2));

  {
    auto deleter2
      = commander->PrepareCommandRecorder(std::move(recorder2), list2, false);
  }

  EXPECT_CALL(
    *mock_queue, Submit(testing::A<std::span<std::shared_ptr<CommandList>>>()))
    .WillOnce(Throw(std::runtime_error("Queue submission failed")));

  NOLINT_EXPECT_THROW(
    commander->SubmitDeferredCommandLists(), std::runtime_error);
}

//! Tests error recovery after queue failure
NOLINT_TEST_F(CommanderErrorTest, ErrorRecovery_SubsequentSubmissions_Work)
{
  EXPECT_CALL(*mock_recorder, End()).WillOnce(Return(mock_command_list));

  {
    auto deleter = commander->PrepareCommandRecorder(
      std::move(mock_recorder), mock_command_list, false);
  }

  EXPECT_CALL(
    *mock_queue, Submit(testing::A<std::span<std::shared_ptr<CommandList>>>()))
    .WillOnce(Throw(std::runtime_error("First submission failed")));

  NOLINT_EXPECT_THROW(
    commander->SubmitDeferredCommandLists(), std::runtime_error);

  auto recovery_queue
    = std::make_shared<StrictMock<MockCommandQueue>>("test-queue");
  auto recovery_list
    = std::make_shared<NiceMock<MockCommandList>>("recovery-list");
  auto recovery_recorder = std::make_unique<NiceMock<MockCommandRecorder>>(
    recovery_list, oxygen::observer_ptr { recovery_queue.get() });

  ON_CALL(*recovery_queue, GetQueueRole())
    .WillByDefault(Return(Role::kGraphics));
  ON_CALL(*recovery_recorder, Begin()).WillByDefault(Return());
  EXPECT_CALL(*recovery_recorder, End()).WillOnce(Return(recovery_list));

  EXPECT_CALL(*recovery_queue,
    Submit(testing::A<std::span<std::shared_ptr<CommandList>>>()))
    .WillOnce(Return());

  {
    auto deleter = commander->PrepareCommandRecorder(
      std::move(recovery_recorder), recovery_list, false);
  }

  NOLINT_EXPECT_NO_THROW(commander->SubmitDeferredCommandLists());
}

//=== Comprehensive Commander Queue Error Testing ===------------------------//

//! Tests successive immediate submissions on different queues
NOLINT_TEST_F(
  CommanderErrorTest, SuccessiveImmediateSubmissions_DifferentQueues_AllSucceed)
{
  auto queue2 = std::make_shared<NiceMock<MockCommandQueue>>("queue-2");
  auto list2 = std::make_shared<NiceMock<MockCommandList>>("list-2");
  auto recorder2 = std::make_unique<NiceMock<MockCommandRecorder>>(
    list2, oxygen::observer_ptr { queue2.get() });

  ON_CALL(*queue2, GetQueueRole()).WillByDefault(Return(Role::kCompute));
  ON_CALL(*recorder2, Begin()).WillByDefault(Return());

  EXPECT_CALL(*mock_recorder, End()).WillOnce(Return(mock_command_list));
  EXPECT_CALL(*recorder2, End()).WillOnce(Return(list2));

  EXPECT_CALL(*mock_queue, Submit(testing::A<std::shared_ptr<CommandList>>()))
    .WillOnce(Return());
  EXPECT_CALL(*queue2, Submit(testing::A<std::shared_ptr<CommandList>>()))
    .WillOnce(Return());

  NOLINT_EXPECT_NO_THROW({
    auto deleter1 = commander->PrepareCommandRecorder(
      std::move(mock_recorder), mock_command_list, true);
  });

  NOLINT_EXPECT_NO_THROW({
    auto deleter2
      = commander->PrepareCommandRecorder(std::move(recorder2), list2, true);
  });
}

//! Tests immediate submission on same queue after failure (destructor safety)
NOLINT_TEST_F(
  CommanderErrorTest, ImmediateSubmission_SameQueueAfterFailure_Works)
{
  auto list1 = std::make_shared<NiceMock<MockCommandList>>("fail-list");
  auto recorder1 = std::make_unique<NiceMock<MockCommandRecorder>>(
    list1, oxygen::observer_ptr { mock_queue.get() });
  ON_CALL(*recorder1, Begin()).WillByDefault(Return());
  EXPECT_CALL(*recorder1, End()).WillOnce(Return(list1));

  EXPECT_CALL(*mock_queue, Submit(testing::A<std::shared_ptr<CommandList>>()))
    .WillOnce(Throw(std::runtime_error("First submission failed")))
    .WillOnce(Return());

  NOLINT_EXPECT_NO_THROW({
    auto deleter1
      = commander->PrepareCommandRecorder(std::move(recorder1), list1, true);
  });

  auto list2 = std::make_shared<NiceMock<MockCommandList>>("success-list");
  auto recorder2 = std::make_unique<NiceMock<MockCommandRecorder>>(
    list2, oxygen::observer_ptr { mock_queue.get() });
  ON_CALL(*recorder2, Begin()).WillByDefault(Return());
  EXPECT_CALL(*recorder2, End()).WillOnce(Return(list2));

  NOLINT_EXPECT_NO_THROW({
    auto deleter2
      = commander->PrepareCommandRecorder(std::move(recorder2), list2, true);
  });
}

//! Tests immediate submission on different queue after failure
NOLINT_TEST_F(
  CommanderErrorTest, ImmediateSubmission_DifferentQueueAfterFailure_Works)
{
  auto list1 = std::make_shared<NiceMock<MockCommandList>>("fail-list");
  auto recorder1 = std::make_unique<NiceMock<MockCommandRecorder>>(
    list1, oxygen::observer_ptr { mock_queue.get() });
  ON_CALL(*recorder1, Begin()).WillByDefault(Return());
  EXPECT_CALL(*recorder1, End()).WillOnce(Return(list1));

  EXPECT_CALL(*mock_queue, Submit(testing::A<std::shared_ptr<CommandList>>()))
    .WillOnce(Throw(std::runtime_error("First submission failed")));

  NOLINT_EXPECT_NO_THROW({
    auto deleter1
      = commander->PrepareCommandRecorder(std::move(recorder1), list1, true);
  });

  auto queue2 = std::make_shared<NiceMock<MockCommandQueue>>("queue-2");
  auto list2 = std::make_shared<NiceMock<MockCommandList>>("success-list");
  auto recorder2 = std::make_unique<NiceMock<MockCommandRecorder>>(
    list2, oxygen::observer_ptr { queue2.get() });

  ON_CALL(*queue2, GetQueueRole()).WillByDefault(Return(Role::kCompute));
  ON_CALL(*recorder2, Begin()).WillByDefault(Return());
  EXPECT_CALL(*recorder2, End()).WillOnce(Return(list2));
  EXPECT_CALL(*queue2, Submit(testing::A<std::shared_ptr<CommandList>>()))
    .WillOnce(Return());

  NOLINT_EXPECT_NO_THROW({
    auto deleter2
      = commander->PrepareCommandRecorder(std::move(recorder2), list2, true);
  });
}

//! Tests deferred submissions with two different queues
NOLINT_TEST_F(
  CommanderErrorTest, DeferredSubmissions_TwoDifferentQueues_AllSuccessful)
{
  const auto queue2 = std::make_shared<NiceMock<MockCommandQueue>>("queue-2");
  auto list2 = std::make_shared<NiceMock<MockCommandList>>("list-2");
  auto recorder2 = std::make_unique<NiceMock<MockCommandRecorder>>(
    list2, oxygen::observer_ptr { queue2.get() });

  ON_CALL(*queue2, GetQueueRole()).WillByDefault(Return(Role::kCompute));
  ON_CALL(*recorder2, Begin()).WillByDefault(Return());

  EXPECT_CALL(*mock_recorder, End()).WillOnce(Return(mock_command_list));
  EXPECT_CALL(*recorder2, End()).WillOnce(Return(list2));

  {
    auto deleter1 = commander->PrepareCommandRecorder(
      std::move(mock_recorder), mock_command_list, false);
    auto deleter2
      = commander->PrepareCommandRecorder(std::move(recorder2), list2, false);
  }

  EXPECT_CALL(
    *mock_queue, Submit(testing::A<std::span<std::shared_ptr<CommandList>>>()))
    .WillOnce(Return());
  EXPECT_CALL(
    *queue2, Submit(testing::A<std::span<std::shared_ptr<CommandList>>>()))
    .WillOnce(Return());

  EXPECT_CALL(*mock_command_list, OnSubmitted()).WillOnce(Return());
  EXPECT_CALL(*list2, OnSubmitted()).WillOnce(Return());

  NOLINT_EXPECT_NO_THROW(commander->SubmitDeferredCommandLists());
}

//! Tests deferred submissions with first queue failing, second succeeding
NOLINT_TEST_F(CommanderErrorTest,
  DeferredSubmissions_TwoDifferentQueues_FirstFailsSecondSucceeds)
{
  const auto queue2 = std::make_shared<NiceMock<MockCommandQueue>>("queue-2");
  auto list2 = std::make_shared<NiceMock<MockCommandList>>("list-2");
  auto recorder2 = std::make_unique<NiceMock<MockCommandRecorder>>(
    list2, oxygen::observer_ptr { queue2.get() });

  ON_CALL(*queue2, GetQueueRole()).WillByDefault(Return(Role::kCompute));
  ON_CALL(*recorder2, Begin()).WillByDefault(Return());

  EXPECT_CALL(*mock_recorder, End()).WillOnce(Return(mock_command_list));
  EXPECT_CALL(*recorder2, End()).WillOnce(Return(list2));

  {
    auto deleter1 = commander->PrepareCommandRecorder(
      std::move(mock_recorder), mock_command_list, false);
    auto deleter2
      = commander->PrepareCommandRecorder(std::move(recorder2), list2, false);
  }

  EXPECT_CALL(
    *mock_queue, Submit(testing::A<std::span<std::shared_ptr<CommandList>>>()))
    .WillOnce(Throw(std::runtime_error("First queue failed")));
  EXPECT_CALL(
    *queue2, Submit(testing::A<std::span<std::shared_ptr<CommandList>>>()))
    .WillOnce(Return());

  EXPECT_CALL(*list2, OnSubmitted()).Times(1);

  NOLINT_EXPECT_THROW(
    commander->SubmitDeferredCommandLists(), std::runtime_error);
}

//! Tests deferred submissions with first queue succeeding, second failing
NOLINT_TEST_F(CommanderErrorTest,
  DeferredSubmissions_TwoDifferentQueues_FirstSucceedsSecondFails)
{
  const auto queue2 = std::make_shared<NiceMock<MockCommandQueue>>("queue-2");
  auto list2 = std::make_shared<NiceMock<MockCommandList>>("list-2");
  auto recorder2 = std::make_unique<NiceMock<MockCommandRecorder>>(
    list2, oxygen::observer_ptr { queue2.get() });

  ON_CALL(*queue2, GetQueueRole()).WillByDefault(Return(Role::kCompute));
  ON_CALL(*recorder2, Begin()).WillByDefault(Return());

  EXPECT_CALL(*mock_recorder, End()).WillOnce(Return(mock_command_list));
  EXPECT_CALL(*recorder2, End()).WillOnce(Return(list2));

  {
    auto deleter1 = commander->PrepareCommandRecorder(
      std::move(mock_recorder), mock_command_list, false);
    auto deleter2
      = commander->PrepareCommandRecorder(std::move(recorder2), list2, false);
  }

  EXPECT_CALL(
    *mock_queue, Submit(testing::A<std::span<std::shared_ptr<CommandList>>>()))
    .WillOnce(Return());
  EXPECT_CALL(
    *queue2, Submit(testing::A<std::span<std::shared_ptr<CommandList>>>()))
    .WillOnce(Throw(std::runtime_error("Second queue failed")));

  EXPECT_CALL(*mock_command_list, OnSubmitted()).Times(1);

  NOLINT_EXPECT_THROW(
    commander->SubmitDeferredCommandLists(), std::runtime_error);
}

//! Tests deferred submissions with both queues failing
NOLINT_TEST_F(
  CommanderErrorTest, DeferredSubmissions_TwoDifferentQueues_BothFail)
{
  const auto queue2 = std::make_shared<NiceMock<MockCommandQueue>>("queue-2");
  auto list2 = std::make_shared<NiceMock<MockCommandList>>("list-2");
  auto recorder2 = std::make_unique<NiceMock<MockCommandRecorder>>(
    list2, oxygen::observer_ptr { queue2.get() });

  ON_CALL(*queue2, GetQueueRole()).WillByDefault(Return(Role::kCompute));
  ON_CALL(*recorder2, Begin()).WillByDefault(Return());

  EXPECT_CALL(*mock_recorder, End()).WillOnce(Return(mock_command_list));
  EXPECT_CALL(*recorder2, End()).WillOnce(Return(list2));

  {
    auto deleter1 = commander->PrepareCommandRecorder(
      std::move(mock_recorder), mock_command_list, false);
    auto deleter2
      = commander->PrepareCommandRecorder(std::move(recorder2), list2, false);
  }

  EXPECT_CALL(
    *mock_queue, Submit(testing::A<std::span<std::shared_ptr<CommandList>>>()))
    .WillOnce(Throw(std::runtime_error("First queue failed")));
  EXPECT_CALL(
    *queue2, Submit(testing::A<std::span<std::shared_ptr<CommandList>>>()))
    .WillOnce(Throw(std::runtime_error("Second queue failed")));

  NOLINT_EXPECT_THROW(
    commander->SubmitDeferredCommandLists(), std::runtime_error);
}

//! Tests error logging format for deferred submission failures
NOLINT_TEST_F(CommanderErrorTest, DeferredSubmission_ErrorLogging_VerifyFormat)
{
  EXPECT_CALL(*mock_recorder, End()).WillOnce(Return(mock_command_list));

  {
    auto deleter = commander->PrepareCommandRecorder(
      std::move(mock_recorder), mock_command_list, false);
  }

  EXPECT_CALL(
    *mock_queue, Submit(testing::A<std::span<std::shared_ptr<CommandList>>>()))
    .WillOnce(Throw(std::runtime_error("Queue submission failed")));

  const oxygen::testing::ScopedLogCapture capture(
    "TestCapture", loguru::Verbosity_ERROR);

  NOLINT_EXPECT_THROW(
    commander->SubmitDeferredCommandLists(), std::runtime_error);

  EXPECT_TRUE(
    capture.Contains("-failed- 'test-list': Queue submission failed"));
}

//! Tests error logging format for immediate submission failures
NOLINT_TEST_F(CommanderErrorTest, ImmediateSubmission_ErrorLogging_VerifyFormat)
{
  EXPECT_CALL(*mock_recorder, End()).WillOnce(Return(mock_command_list));

  EXPECT_CALL(*mock_queue, Submit(testing::A<std::shared_ptr<CommandList>>()))
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

//! Verifies concurrent submission attempts are thread-safe
NOLINT_TEST_F(ConcurrencyTest, ConcurrentSubmission_ThreadSafe)
{
  const auto list_a = AcquireList(gfx_queue->GetQueueRole(), "concurrent-a");
  const auto list_b = AcquireList(gfx_queue->GetQueueRole(), "concurrent-b");
  ASSERT_NE(list_a, nullptr);
  ASSERT_NE(list_b, nullptr);

  const auto before = gfx_queue->GetCurrentValue();
  const auto v_a = before + 1;
  const auto v_b = before + 2;

  {
    const auto r_a = AcquireRecorder(gfx_queue, list_a, false);
    const auto r_b = AcquireRecorder(gfx_queue, list_b, false);
    ASSERT_NE(r_a, nullptr);
    ASSERT_NE(r_b, nullptr);
    r_a->RecordQueueSignal(v_a);
    r_b->RecordQueueSignal(v_b);
  }

  EXPECT_TRUE(list_a->IsClosed());
  EXPECT_TRUE(list_b->IsClosed());

  std::vector<std::thread> threads;
  std::atomic<int> submission_count { 0 };

  threads.reserve(3);
  for (int i = 0; i < 3; ++i) {
    threads.emplace_back([this, &submission_count]() {
      try {
        headless->SubmitDeferredCommandLists();
        submission_count.fetch_add(1);
      } catch (const std::exception& e) {
        FAIL() << "Concurrent submission threw: " << e.what();
      }
    });
  }

  for (auto& t : threads) {
    t.join();
  }

  EXPECT_EQ(submission_count.load(), 3);

  EXPECT_TRUE(list_a->IsSubmitted());
  EXPECT_TRUE(list_b->IsSubmitted());

  gfx_queue->Wait(v_b);
  headless->BeginFrame(
    oxygen::frame::SequenceNumber { 0 }, oxygen::frame::Slot { 0 });

  EXPECT_TRUE(list_a->IsFree());
  EXPECT_TRUE(list_b->IsFree());
}

} // anonymous namespace for additional tests
