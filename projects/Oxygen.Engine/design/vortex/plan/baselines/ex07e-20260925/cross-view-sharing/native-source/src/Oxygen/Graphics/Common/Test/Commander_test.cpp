//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <array>
#include <chrono>
#include <memory>
#include <thread>

#include <Oxygen/Composition/Object.h>
#include <Oxygen/Core/Types/Frame.h>
#include <Oxygen/Graphics/Common/Buffer.h>
#include <Oxygen/Graphics/Common/CommandList.h>
#include <Oxygen/Graphics/Common/CommandQueue.h>
#include <Oxygen/Graphics/Common/CommandRecorder.h>
#include <Oxygen/Graphics/Common/CommandRecording.h>
#include <Oxygen/Graphics/Common/Detail/Barriers.h>
#include <Oxygen/Graphics/Common/Detail/DeferredReclaimer.h>
#include <Oxygen/Graphics/Common/Framebuffer.h>
#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/Graphics/Common/Internal/Commander.h>
#include <Oxygen/Graphics/Common/NativeObject.h>
#include <Oxygen/Graphics/Common/PipelineState.h>
#include <Oxygen/Graphics/Common/Queues.h>
#include <Oxygen/Graphics/Common/ResourceRegistry.h>
#include <Oxygen/Graphics/Common/Test/Fakes/FakeResource.h>
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
  MOCK_METHOD(std::uint64_t, GetCompletedValue, (), (const, override));
  MOCK_METHOD(std::uint64_t, GetCurrentValue, (), (const, override));
  MOCK_METHOD(void, Submit, (CommandListPtr), (override));
  MOCK_METHOD(void, Submit, (CommandListSpan), (override));
  MOCK_METHOD(oxygen::graphics::QueueRole, GetQueueRole, (), (const, override));
  // clang-format on
  // NOLINTEND

protected:
  auto SignalImmediate(std::uint64_t value) const -> void override
  {
    Signal(value);
  }
};

// The registry identifies this wrapper by address, while queues identify its
// backend resource by a separate handle, just as the D3D12 backend does.
class RegistryNativeResource final
  : public oxygen::graphics::RegisteredResource,
    public oxygen::Object {
  OXYGEN_TYPED(RegistryNativeResource)
public:
  using ViewDescriptionT = oxygen::graphics::testing::TestViewDesc;

  explicit RegistryNativeResource(const std::uint64_t native_id)
    : native_ { native_id, ClassTypeId() }
  {
  }

  [[nodiscard]] auto GetNativeResource() const
    -> oxygen::graphics::NativeResource
  {
    return native_;
  }

  [[nodiscard]] auto GetNativeView(
    const oxygen::graphics::DescriptorAllocationHandle& /*unused*/,
    const ViewDescriptionT& /*unused*/) const -> NativeView
  {
    return {};
  }

private:
  oxygen::graphics::NativeResource native_;
};

NOLINT_TEST(ResourceRegistryStateTest,
  UnregisterForgetsBackendStateBeforeReleasingTheWrapper)
{
  auto queue = NiceMock<MockCommandQueue>("state-cache");
  auto registry = oxygen::graphics::ResourceRegistry("state-cache");
  auto resource = std::make_shared<RegistryNativeResource>(1U);
  const auto native = resource->GetNativeResource();
  const auto states
    = std::array { CommandQueue::KnownResourceState { .resource = native,
      .state = oxygen::graphics::ResourceStates::kShaderResource } };
  queue.AdoptKnownResourceStates(states);
  registry.Register(resource);
  const auto weak = std::weak_ptr { resource };
  auto* wrapper = resource.get();
  resource.reset();
  auto notified = false;
  registry.SetResourceUnregisteredCallback([&](const auto& forgotten) -> void {
    notified = true;
    EXPECT_FALSE(weak.expired());
    EXPECT_EQ(forgotten, native);
    queue.ForgetKnownResourceState(forgotten);
  });

  registry.UnRegisterResource(*wrapper);

  EXPECT_TRUE(notified);
  EXPECT_TRUE(weak.expired());
  // Reuse the native handle for a newly allocated resource. Its initial state
  // must not inherit the shader-read state of the retired allocation.
  auto replacement = std::make_shared<RegistryNativeResource>(1U);
  EXPECT_FALSE(queue.TryGetKnownResourceState(replacement->GetNativeResource())
      .has_value());
}

NOLINT_TEST(ResourceRegistryStateTest,
  ReplaceForgetsOldBackendStateAndPreservesTheNewResource)
{
  for (const auto with_updater : { false, true }) {
    SCOPED_TRACE(with_updater);
    auto queue = NiceMock<MockCommandQueue>("replace-state");
    auto registry = oxygen::graphics::ResourceRegistry("replace-state");
    auto previous = std::make_shared<RegistryNativeResource>(1U);
    auto replacement = std::make_shared<RegistryNativeResource>(2U);
    const auto previous_native = previous->GetNativeResource();
    const auto replacement_native = replacement->GetNativeResource();
    const auto states = std::array {
      CommandQueue::KnownResourceState { .resource = previous_native,
        .state = oxygen::graphics::ResourceStates::kShaderResource },
      CommandQueue::KnownResourceState { .resource = replacement_native,
        .state = oxygen::graphics::ResourceStates::kDepthWrite },
    };
    queue.AdoptKnownResourceStates(states);
    registry.Register(previous);
    const auto weak = std::weak_ptr { previous };
    auto* wrapper = previous.get();
    previous.reset();
    registry.SetResourceUnregisteredCallback(
      [&](const auto& forgotten) -> void {
        EXPECT_FALSE(weak.expired());
        EXPECT_EQ(forgotten, previous_native);
        queue.ForgetKnownResourceState(forgotten);
      });

    if (with_updater) {
      registry.Replace(*wrapper, replacement,
        [](const RegistryNativeResource::ViewDescriptionT& desc)
          -> std::optional<RegistryNativeResource::ViewDescriptionT> {
          return std::optional { desc };
        });
    } else {
      registry.Replace(*wrapper, replacement, nullptr);
    }

    EXPECT_TRUE(weak.expired());
    EXPECT_FALSE(queue.TryGetKnownResourceState(previous_native).has_value());
    EXPECT_EQ(queue.TryGetKnownResourceState(replacement_native),
      oxygen::graphics::ResourceStates::kDepthWrite);
    EXPECT_TRUE(registry.Contains(*replacement));
  }
}

NOLINT_TEST(
  ResourceRegistryStateTest, RemovingOnlyViewsPreservesTheBackendResourceState)
{
  auto queue = NiceMock<MockCommandQueue>("view-state");
  auto registry = oxygen::graphics::ResourceRegistry("view-state");
  auto resource = std::make_shared<RegistryNativeResource>(1U);
  const auto native = resource->GetNativeResource();
  const auto states
    = std::array { CommandQueue::KnownResourceState { .resource = native,
      .state = oxygen::graphics::ResourceStates::kShaderResource } };
  queue.AdoptKnownResourceStates(states);
  registry.Register(resource);
  registry.SetResourceUnregisteredCallback([&](const auto& forgotten) -> void {
    queue.ForgetKnownResourceState(forgotten);
  });

  registry.UnRegisterViews(*resource);

  EXPECT_EQ(queue.TryGetKnownResourceState(native),
    oxygen::graphics::ResourceStates::kShaderResource);
}

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
  MOCK_METHOD(CommandListPtr, End, (), (noexcept, override));
  MOCK_METHOD(void, BeginEvent, (std::string_view), (override));
  MOCK_METHOD(void, EndEvent, (), (override));
  MOCK_METHOD(void, SetMarker, (std::string_view), (override));
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
  MOCK_METHOD(void, ExecuteIndirect, (const oxygen::graphics::Buffer&, const oxygen::graphics::CommandRecorder::IndirectCommandDesc&, const oxygen::graphics::CommandRecorder::IndirectExecutionDesc&), (override));
  MOCK_METHOD(void, SetVertexBuffers, (std::uint32_t, const std::shared_ptr<oxygen::graphics::Buffer>*, const std::uint32_t*), (const, override));
  MOCK_METHOD(void, BindIndexBuffer, (const oxygen::graphics::Buffer&, oxygen::Format), (override));
  MOCK_METHOD(void, BindFrameBuffer, (const oxygen::graphics::Framebuffer&), (override));
  MOCK_METHOD(void, ClearDepthStencilView, (const oxygen::graphics::Texture&, const oxygen::graphics::NativeView&, oxygen::graphics::ClearFlags, float, std::uint8_t), (override));
  MOCK_METHOD(void, ClearDepthStencilView, (const oxygen::graphics::Texture&, const oxygen::graphics::NativeView&, oxygen::graphics::ClearFlags, float, std::uint8_t, std::span<const oxygen::Scissors>), (override));
  MOCK_METHOD(void, ClearFramebuffer, (const oxygen::graphics::Framebuffer&, std::optional<std::vector<std::optional<oxygen::graphics::Color>>>, std::optional<float>, std::optional<std::uint8_t>), (override));
  MOCK_METHOD(void, CopyBuffer, (oxygen::graphics::Buffer&, std::size_t, const oxygen::graphics::Buffer&, std::size_t, std::size_t), (override));
  MOCK_METHOD(void, CopyBufferToTexture, (const oxygen::graphics::Buffer&, const oxygen::graphics::TextureUploadRegion&, oxygen::graphics::Texture&), (override));
  MOCK_METHOD(void, CopyBufferToTexture, (const oxygen::graphics::Buffer&, std::span<const oxygen::graphics::TextureUploadRegion>, oxygen::graphics::Texture&), (override));
  MOCK_METHOD(void, CopyTextureToBuffer, (oxygen::graphics::Buffer&, const oxygen::graphics::Texture&, const oxygen::graphics::TextureBufferCopyRegion&), (override));
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

using TestCommander = oxygen::graphics::internal::Commander;

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

    auto list = std::make_shared<NiceMock<MockCommandList>>(name);
    ON_CALL(*list, OnSubmitted()).WillByDefault([list = list.get()] -> void {
      list->CommandList::OnSubmitted();
    });
    ON_CALL(*list, OnExecuted()).WillByDefault([list = list.get()] -> void {
      list->CommandList::OnExecuted();
    });
    return list;
  }

  //! Factory method to create a mock command recorder with standard setup
  auto CreateMockCommandRecorder(
    CommandListPtr command_list, const std::shared_ptr<MockCommandQueue>& queue)
    -> std::unique_ptr<MockCommandRecorder>
  {

    std::unique_ptr<MockCommandRecorder> recorder;
    // Convert shared_ptr<MockCommandQueue> to observer_ptr<CommandQueue>
    const oxygen::observer_ptr<CommandQueue> obs_queue { queue.get() };
    recorder = std::make_unique<NiceMock<MockCommandRecorder>>(
      std::move(command_list), obs_queue);

    ON_CALL(*recorder, Begin())
      .WillByDefault([pointer = recorder.get()]() -> void {
        pointer->CommandRecorder::Begin();
      });
    ON_CALL(*recorder, End())
      .WillByDefault([pointer = recorder.get()]() -> CommandListPtr {
        return pointer->CommandRecorder::End();
      });

    return recorder;
  }

private:
  //! Setup default behaviors for primary mock queues
  auto SetupDefaultQueueBehaviors() -> void
  {
    constexpr auto kSecondaryFence = 100U;
    constexpr auto kPrimaryFence = 200U;
    // Use ON_CALL for both NiceMock and StrictMock compatibility
    ON_CALL(*secondary_q, GetQueueRole())
      .WillByDefault(Return(Role::kGraphics));
    ON_CALL(*primary_q, GetQueueRole()).WillByDefault(Return(Role::kCompute));
    ON_CALL(*secondary_q, GetCurrentValue())
      .WillByDefault(Return(kSecondaryFence));
    ON_CALL(*primary_q, GetCurrentValue()).WillByDefault(Return(kPrimaryFence));
    ON_CALL(*secondary_q, GetCompletedValue())
      .WillByDefault(Return(kSecondaryFence));
    ON_CALL(*primary_q, GetCompletedValue())
      .WillByDefault(Return(kPrimaryFence));
  }

protected:
  // Test fixture state is intentionally shared with derived test cases.
  // NOLINTBEGIN(cppcoreguidelines-non-private-member-variables-in-classes)
  std::unique_ptr<DeferredReclaimer> real_reclaimer;
  std::unique_ptr<TestCommander> commander;
  std::shared_ptr<MockCommandQueue> secondary_q;
  std::shared_ptr<MockCommandQueue> primary_q;
  // NOLINTEND(cppcoreguidelines-non-private-member-variables-in-classes)
};

using oxygen::graphics::CommandRecording;
using oxygen::graphics::SubmissionOutcome;
using oxygen::graphics::SubmissionPolicy;

//! Normal scope exit submits once and retains native work until retirement.
NOLINT_TEST_F(CommanderTestBase, ScopeExitSubmitsAndRetiresAfterTheFrame)
{
  // Arrange
  auto list = CreateMockCommandList("automatic");
  auto recorder = CreateMockCommandRecorder(list, secondary_q);
  EXPECT_CALL(*secondary_q, Submit(testing::A<CommandListPtr>())).Times(1);
  EXPECT_CALL(*list, OnSubmitted()).Times(1);
  EXPECT_CALL(*list, OnExecuted()).Times(0);

  // Act
  {
    auto recording = commander->PrepareCommandRecorder(
      std::move(recorder), SubmissionPolicy::kOnScopeExit);
    EXPECT_TRUE(recording);
    EXPECT_TRUE(list->IsRecording());
  }

  // Assert
  EXPECT_TRUE(list->IsSubmitted());
  testing::Mock::VerifyAndClearExpectations(list.get());
  EXPECT_CALL(*list, OnExecuted()).Times(1);
  SimulateFrameCompletion();
  EXPECT_TRUE(list->IsFree());
}

//! Explicit recordings remain unsubmitted until their retained owner submits.
NOLINT_TEST_F(
  CommanderTestBase, ExplicitSubmissionPublishesAfterNativeAcceptance)
{
  // Arrange
  auto list = CreateMockCommandList("explicit");
  auto recorder = CreateMockCommandRecorder(list, secondary_q);
  std::optional<SubmissionOutcome> publication;
  EXPECT_CALL(*secondary_q, Submit(testing::A<CommandListPtr>()))
    .WillOnce(
      [&](const auto&) -> void { EXPECT_FALSE(publication.has_value()); });
  auto recording = commander->PrepareCommandRecorder(
    std::move(recorder), SubmissionPolicy::kExplicit);
  recording->OnSubmission(
    [&](const auto result) -> void { publication = result; });

  // Act
  EXPECT_FALSE(list->IsSubmitted());
  const auto submitted = recording.Submit();

  // Assert
  EXPECT_TRUE(submitted);
  EXPECT_FALSE(recording);
  EXPECT_TRUE(list->IsSubmitted());
  EXPECT_EQ(publication, SubmissionOutcome::kSubmitted);
}

//! Destruction of an unsubmitted explicit recording cancels its publication.
NOLINT_TEST_F(CommanderTestBase, ExplicitScopeExitDiscards)
{
  // Arrange
  auto list = CreateMockCommandList("discard");
  auto recorder = CreateMockCommandRecorder(list, secondary_q);
  std::optional<SubmissionOutcome> publication;
  EXPECT_CALL(*secondary_q, Submit(testing::A<CommandListPtr>())).Times(0);

  // Act
  {
    auto recording = commander->PrepareCommandRecorder(
      std::move(recorder), SubmissionPolicy::kExplicit);
    recording->OnSubmission(
      [&](const auto result) -> void { publication = result; });
  }

  // Assert
  EXPECT_TRUE(list->IsFree());
  EXPECT_EQ(publication, SubmissionOutcome::kDiscarded);
}

//! Exception unwinding cancels automatic recordings instead of submitting them.
NOLINT_TEST_F(CommanderTestBase, UnwindingDiscardsAutomaticRecording)
{
  // Arrange
  auto list = CreateMockCommandList("unwind");
  auto recorder = CreateMockCommandRecorder(list, secondary_q);
  std::optional<SubmissionOutcome> publication;
  EXPECT_CALL(*secondary_q, Submit(testing::A<CommandListPtr>())).Times(0);

  // Act
  const auto work = [&]() -> void {
    auto recording = commander->PrepareCommandRecorder(
      std::move(recorder), SubmissionPolicy::kOnScopeExit);
    recording->OnSubmission(
      [&](const auto result) -> void { publication = result; });
    throw std::runtime_error("recording failed");
  };
  NOLINT_EXPECT_THROW(work(), std::runtime_error);

  // Assert
  EXPECT_EQ(publication, SubmissionOutcome::kDiscarded);
  EXPECT_TRUE(list->IsFree());
}

//! Explicit submission disarms automatic scope-exit submission and callbacks.
NOLINT_TEST_F(CommanderTestBase, SubmissionAndPublicationResolveExactlyOnce)
{
  // Arrange
  auto list = CreateMockCommandList("once");
  auto recorder = CreateMockCommandRecorder(list, secondary_q);
  auto calls = 0;
  EXPECT_CALL(*secondary_q, Submit(testing::A<CommandListPtr>())).Times(1);

  // Act
  {
    auto recording = commander->PrepareCommandRecorder(
      std::move(recorder), SubmissionPolicy::kOnScopeExit);
    recording->OnSubmission([&](const auto result) -> void {
      EXPECT_EQ(result, SubmissionOutcome::kSubmitted);
      ++calls;
    });
    EXPECT_TRUE(recording.Submit());
    EXPECT_TRUE(recording.Submit());
    recording.Discard();
  }

  // Assert
  EXPECT_EQ(calls, 1);
}

//! Explicit discard prevents later submission through either policy.
NOLINT_TEST_F(CommanderTestBase, DiscardIsIdempotentAndPreventsSubmission)
{
  // Arrange
  auto list = CreateMockCommandList("discard once");
  auto recorder = CreateMockCommandRecorder(list, secondary_q);
  auto calls = 0;
  EXPECT_CALL(*secondary_q, Submit(testing::A<CommandListPtr>())).Times(0);
  auto recording = commander->PrepareCommandRecorder(
    std::move(recorder), SubmissionPolicy::kOnScopeExit);
  recording->OnSubmission([&](const auto result) -> void {
    EXPECT_EQ(result, SubmissionOutcome::kDiscarded);
    ++calls;
  });

  // Act
  recording.Discard();
  recording.Discard();

  // Assert
  EXPECT_FALSE(recording.Submit());
  EXPECT_EQ(calls, 1);
  EXPECT_TRUE(list->IsFree());
}

//! Closing failure never submits or commits publication and does not retry End.
NOLINT_TEST_F(CommanderTestBase, CloseFailureDiscardsWithoutRetry)
{
  // Arrange
  auto list = CreateMockCommandList("close failure");
  auto recorder = CreateMockCommandRecorder(list, secondary_q);
  EXPECT_CALL(*recorder, End()).Times(1).WillOnce(Return(nullptr));
  EXPECT_CALL(*secondary_q, Submit(testing::A<CommandListPtr>())).Times(0);
  std::optional<SubmissionOutcome> publication;
  auto recording = commander->PrepareCommandRecorder(
    std::move(recorder), SubmissionPolicy::kExplicit);
  recording->OnSubmission(
    [&](const auto result) -> void { publication = result; });

  // Act
  const auto submitted = recording.Submit();

  // Assert
  EXPECT_FALSE(submitted);
  EXPECT_EQ(publication, SubmissionOutcome::kDiscarded);
  EXPECT_EQ(list->GetState(), CommandList::State::kInvalid);
}

NOLINT_TEST_F(CommanderTestBase, NativeCloseExceptionDiscardsAndInvalidatesList)
{
  class ThrowingList final : public CommandList {
  public:
    ThrowingList()
      : CommandList("native close failure", Role::kGraphics)
    {
    }
    auto OnEndRecording() -> void override { throw 42; }
  };
  auto list = std::make_shared<ThrowingList>();
  auto recorder = CreateMockCommandRecorder(list, secondary_q);
  EXPECT_CALL(*secondary_q, Submit(testing::A<CommandListPtr>())).Times(0);
  std::optional<SubmissionOutcome> publication;
  {
    auto recording = commander->PrepareCommandRecorder(
      std::move(recorder), SubmissionPolicy::kExplicit);
    recording->OnSubmission([&](auto outcome) { publication = outcome; });
    EXPECT_NO_THROW(recording.Discard());
  }
  EXPECT_EQ(publication, SubmissionOutcome::kDiscarded);
  EXPECT_EQ(list->GetState(), CommandList::State::kInvalid);
}

//! Queue failure never acknowledges a recording as submitted.
NOLINT_TEST_F(CommanderTestBase, QueueFailureDiscardsPublication)
{
  // Arrange
  auto list = CreateMockCommandList("queue failure");
  auto recorder = CreateMockCommandRecorder(list, secondary_q);
  EXPECT_CALL(*secondary_q, Submit(testing::A<CommandListPtr>()))
    .WillOnce(Throw(std::runtime_error("queue failure")));
  EXPECT_CALL(*list, OnSubmitted()).Times(0);
  EXPECT_CALL(*list, OnExecuted()).Times(0);
  std::optional<SubmissionOutcome> publication;
  auto recording = commander->PrepareCommandRecorder(
    std::move(recorder), SubmissionPolicy::kExplicit);
  recording->OnSubmission(
    [&](const auto result) -> void { publication = result; });

  // Act
  const auto submitted = recording.Submit();
  SimulateFrameCompletion();

  // Assert
  EXPECT_FALSE(submitted);
  EXPECT_EQ(publication, SubmissionOutcome::kDiscarded);
  EXPECT_TRUE(list->IsFree());
}

//! Moving a recording transfers its single submission obligation.
NOLINT_TEST_F(CommanderTestBase, MoveTransfersOwnershipWithoutEarlySubmission)
{
  // Arrange
  auto list = CreateMockCommandList("move");
  auto recorder = CreateMockCommandRecorder(list, secondary_q);
  EXPECT_CALL(*secondary_q, Submit(testing::A<CommandListPtr>())).Times(1);
  auto first = commander->PrepareCommandRecorder(
    std::move(recorder), SubmissionPolicy::kExplicit);

  // Act
  auto second = std::move(first);

  // Assert
  EXPECT_TRUE(second);
  EXPECT_TRUE(second.Submit());
}

//! Empty recording values are inert and safe to move or discard.
NOLINT_TEST_F(CommanderTestBase, EmptyRecordingIsInert)
{
  // Arrange
  auto recording = CommandRecording {};

  // Act
  recording.Discard();
  auto moved = std::move(recording);

  // Assert
  EXPECT_FALSE(moved);
  EXPECT_FALSE(moved.Submit());
}

//! A failed publication observer cannot suppress subsequent observers.
NOLINT_TEST_F(CommanderTestBase, PublicationObserversResolveIndependently)
{
  // Arrange
  auto list = CreateMockCommandList("observers");
  auto recorder = CreateMockCommandRecorder(list, secondary_q);
  auto called = false;
  auto recording = commander->PrepareCommandRecorder(
    std::move(recorder), SubmissionPolicy::kExplicit);
  recording->OnSubmission(
    [](const auto) -> void { throw std::runtime_error("observer"); });
  recording->OnSubmission([&](const auto result) -> void {
    EXPECT_EQ(result, SubmissionOutcome::kSubmitted);
    called = true;
  });

  // Act
  const auto submitted = recording.Submit();

  // Assert
  EXPECT_TRUE(submitted);
  EXPECT_TRUE(called);
}

} // namespace
