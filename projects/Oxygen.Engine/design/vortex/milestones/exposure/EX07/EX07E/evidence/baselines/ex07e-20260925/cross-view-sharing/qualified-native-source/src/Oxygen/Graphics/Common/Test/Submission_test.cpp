//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at <https://opensource.org/licenses/BSD-3-Clause>.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//
#include <array>
#include <optional>

#include <Oxygen/Graphics/Common/CommandQueue.h>
#include <Oxygen/Graphics/Common/Internal/QueueSubmission.h>
#include <Oxygen/Graphics/Common/Test/HeapAllocationFailure.h>
#include <Oxygen/Testing/GTest.h>

namespace {
using namespace oxygen::graphics;
class ControlledQueue final : public CommandQueue {
public:
  ControlledQueue()
    : CommandQueue("Controlled completion")
  {
  }
  mutable uint64_t legacy = 0;
  uint64_t completed = 0;
  bool private_marker_requested = false;
  void (*after_issue)(void*) noexcept = nullptr;
  void* context = nullptr;
  auto Signal(uint64_t value) const -> void override { legacy = value; }
  auto Signal() const -> uint64_t override { return ++legacy; }
  auto Wait(uint64_t) const -> void override { }
  auto Wait(uint64_t, std::chrono::milliseconds) const -> void override { }
  auto GetCompletedValue() const -> uint64_t override { return legacy; }
  auto GetCurrentValue() const -> uint64_t override { return legacy; }
  auto GetQueueRole() const -> QueueRole override
  {
    return QueueRole::kGraphics;
  }

private:
  struct Native final : internal::NativeSubmission {
    ControlledQueue* queue;
    size_t count;
    bool marker;
    auto Execute(internal::NativeSubmissionProgress& progress) -> void override
    {
      progress.issued_lists = count;
      if (queue->after_issue) {
        queue->after_issue(queue->context);
      }
      progress.private_marker_emitted = marker;
    }
  };
  auto PrepareNativeSubmission(const internal::NativeSubmissionRequest& request,
    std::unique_ptr<internal::NativeSubmission>)
    -> std::unique_ptr<internal::NativeSubmission> override
  {
    auto native = std::make_unique<Native>();
    native->queue = this;
    native->count = request.lists.size();
    native->marker = request.private_marker.has_value();
    private_marker_requested = native->marker;
    return native;
  }
  auto QueryPrivateCompletion() const noexcept -> uint64_t override
  {
    return completed;
  }
  auto SignalImmediate(uint64_t value) const -> void override
  {
    legacy = value;
  }
};
struct SubmissionTest : ::testing::Test {
  std::shared_ptr<BackendLifetime> lifetime
    = std::make_shared<BackendLifetime>();
  std::shared_ptr<ControlledQueue> queue = std::make_shared<ControlledQueue>();
  auto SetUp() -> void override
  {
    lifetime->Install(BackendIncarnationId { 42 }, {});
    queue->BindBackend(lifetime, {});
    lifetime->RegisterQueue(queue->Identity().get(), queue);
  }
  auto List() -> std::shared_ptr<CommandList>
  {
    auto list
      = std::make_shared<CommandList>("Use batch", QueueRole::kGraphics);
    list->BindBackend(lifetime, queue->Identity());
    list->OnBeginRecording();
    list->OnEndRecording();
    return list;
  }
};

NOLINT_TEST_F(
  SubmissionTest, OpaqueUseIsDeduplicatedAndRetiresOnlyAfterCompletion)
{
  struct Counts {
    int prepared = 0;
    int submitted = 0;
    int released = 0;
    UseReleaseReason reason {};
  } counts;
  OpaqueUseHooks hooks { .prepare
    = [](void* ptr, QueueIdentity) { ++static_cast<Counts*>(ptr)->prepared; },
    .submitted =
      [](void* ptr, QueueIdentity, const SubmissionResult&) noexcept {
        ++static_cast<Counts*>(ptr)->submitted;
      },
    .released =
      [](void* ptr, QueueIdentity, SubmissionOutcome,
        UseReleaseReason reason) noexcept {
        auto& value = *static_cast<Counts*>(ptr);
        ++value.released;
        value.reason = reason;
      } };
  auto list = List();
  auto owner = std::make_shared<int>(5);
  const std::weak_ptr<int> weak = owner;
  list->Uses().RetainOpaque(owner, 1, &counts, hooks);
  list->Uses().RetainOpaque(owner, 1, &counts, hooks);
  EXPECT_EQ(counts.prepared, 1);
  const auto result = queue->SubmitWithReceipt(list);
  ASSERT_TRUE(result.receipt);
  const auto counters = queue->InspectSubmissionCounters();
  EXPECT_EQ(counters.accepted_batches, 1U);
  EXPECT_EQ(counters.command_lists, 1U);
  EXPECT_EQ(counters.completion_signals, 1U);
  EXPECT_EQ(counters.dependency_waits, 0U);
  owner.reset();
  queue->PollCompletedUses();
  EXPECT_FALSE(weak.expired());
  EXPECT_EQ(counts.submitted, 1);
  EXPECT_EQ(counts.released, 0);
  queue->completed = result.receipt->Value();
  queue->PollCompletedUses();
  EXPECT_TRUE(weak.expired());
  EXPECT_EQ(counts.released, 1);
  EXPECT_EQ(counts.reason, UseReleaseReason::kCompleted);
  EXPECT_TRUE(list->IsFree());
}

NOLINT_TEST_F(SubmissionTest, BoundListCannotBeSubmittedOnAnotherQueue)
{
  auto other = std::make_shared<ControlledQueue>();
  other->BindBackend(lifetime, {});
  auto list = List();
  EXPECT_EQ(
    other->SubmitWithReceipt(list).outcome, SubmissionOutcome::kDiscarded);
  EXPECT_TRUE(list->IsClosed());
  EXPECT_EQ(
    queue->SubmitWithReceipt(list).outcome, SubmissionOutcome::kSubmitted);
}

NOLINT_TEST_F(SubmissionTest, InvalidDependencyCannotEnterRecording)
{
  auto list = List();
  EXPECT_THROW(list->Uses().RecordDependency({}), std::logic_error);
}

NOLINT_TEST_F(SubmissionTest, DuplicateListInOneIssueIsRejected)
{
  auto list = List();
  std::array lists { list, list };
  EXPECT_THROW(queue->Submit(lists), SubmissionException);
  EXPECT_TRUE(list->IsClosed());
}

NOLINT_TEST_F(SubmissionTest, CpuAdmissionGuardDoesNotAddAnUnneededPrivateFence)
{
  auto list = List();
  auto permission = std::make_shared<int>(1);
  list->Uses().RetainOpaque(permission, 1, permission.get(),
    { .valid =
        [](const void* value) noexcept {
          return *static_cast<const int*>(value) == 1;
        },
      .requires_completion = false });
  EXPECT_TRUE(list->Uses().HasUses());
  EXPECT_FALSE(list->Uses().NeedsCompletion());
  EXPECT_NO_THROW(queue->Submit(list));
  EXPECT_FALSE(queue->private_marker_requested);
  EXPECT_EQ(queue->InspectSubmissionCounters().completion_signals, 0U);
  EXPECT_FALSE(list->Uses().HasUses());
  list->OnExecuted();
}

#if defined(_MSC_VER) && defined(_DEBUG)
NOLINT_TEST_F(
  SubmissionTest, NoAllocationAfterNativeIssueOrDuringCompletedRetirement)
{
  using oxygen::graphics::testing::HeapAllocationFailure;
  auto list = List();
  list->SetRecordedResourceStates(
    { { NativeResource { uint64_t { 123 }, CommandList::ClassTypeId() },
      ResourceStates::kCopyDest } });
  auto owner = std::make_shared<int>(7);
  list->Uses().RetainOpaque(owner, 1);
  std::optional<HeapAllocationFailure> denied;
  queue->context = &denied;
  queue->after_issue = [](void* ptr) noexcept {
    static_cast<std::optional<HeapAllocationFailure>*>(ptr)->emplace();
  };
  const auto rejected_before = HeapAllocationFailure::RejectedCount();
  const auto result = queue->SubmitWithReceipt(list);
  denied.reset();
  ASSERT_EQ(result.outcome, SubmissionOutcome::kSubmitted);
  ASSERT_TRUE(result.receipt);
  EXPECT_EQ(HeapAllocationFailure::RejectedCount(), rejected_before);
  queue->completed = result.receipt->Value();
  {
    HeapAllocationFailure no_allocation;
    queue->PollCompletedUses();
  }
  EXPECT_TRUE(list->IsFree());
  EXPECT_EQ(HeapAllocationFailure::RejectedCount(), rejected_before);
}
#endif
} // namespace
