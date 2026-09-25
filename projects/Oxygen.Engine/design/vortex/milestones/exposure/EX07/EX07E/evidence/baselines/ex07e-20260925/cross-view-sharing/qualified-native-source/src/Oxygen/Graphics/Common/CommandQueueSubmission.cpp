//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at <https://opensource.org/licenses/BSD-3-Clause>.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#if defined(OXYGEN_WITH_TRACY)
#  include <Oxygen/Profiling/CpuProfileScope.h>
#endif

#include <algorithm>
#include <array>
#include <atomic>
#include <cassert>
#include <limits>
#include <stdexcept>
#include <utility>

#include <Oxygen/Composition/ObjectMetadata.h>
#include <Oxygen/Graphics/Common/CommandQueue.h>
#include <Oxygen/Graphics/Common/Internal/QueueSubmission.h>
#include <Oxygen/Graphics/Common/Internal/SubmissionFaultTestAccess.h>
#include <Oxygen/Graphics/Common/ResourceRegistry.h>

namespace oxygen::graphics {
using internal::SubmittedWork;
namespace {
  std::atomic<uint64_t> next_queue_identity { 1 };
  auto AllocateQueueIdentity() -> QueueIdentity
  {
    auto value = next_queue_identity.load(std::memory_order_relaxed);
    for (;;) {
      if (value == (std::numeric_limits<uint64_t>::max)()) {
        throw std::overflow_error("Queue identities exhausted");
      }
      if (next_queue_identity.compare_exchange_weak(value, value + 1)) {
        return QueueIdentity { value };
      }
    }
  }
  auto Append(std::unique_ptr<SubmittedWork>& head, SubmittedWork*& tail,
    std::unique_ptr<SubmittedWork> work) noexcept -> void
  {
    auto* next_tail = work.get();
    if (tail) {
      tail->next = std::move(work);
    } else {
      head = std::move(work);
    }
    tail = next_tail;
  }
}

struct CommandQueue::SubmissionState {
  std::shared_ptr<void> native_lifetime;
  std::shared_ptr<BackendLifetime> lifetime;
  QueueIdentity identity { AllocateQueueIdentity() };
  mutable std::mutex mutex;
  uint64_t next_private { 1 };
  internal::SubmissionFailurePoint failure {
    internal::SubmissionFailurePoint::kNone
  };
  std::atomic<uint64_t> last_private_emitted { 0 };
  uint64_t last_legacy_emitted { 0 };
  SubmissionCounters counters;
  std::unordered_map<NativeResource, ResourceStates> staged_states;
  std::unique_ptr<SubmittedWork> in_flight;
  SubmittedWork* in_flight_tail { nullptr };
  std::unique_ptr<SubmittedWork> quarantine;
  SubmittedWork* quarantine_tail { nullptr };
};

auto internal::SubmissionFaultTestAccess::LoseDevice(
  CommandQueue& queue) noexcept -> void
{
  queue.StopAfterSubmissionFailure();
}

auto internal::SubmissionFaultTestAccess::FailNext(
  CommandQueue& queue, SubmissionFailurePoint point) -> void
{
  std::lock_guard lock(queue.submission_->mutex);
  queue.submission_->failure = point;
}

CommandQueue::CommandQueue(std::string_view name)
  : submission_(std::make_unique<SubmissionState>())
{
  AddComponent<ObjectMetadata>(name);
}
CommandQueue::~CommandQueue()
{
  // Backends drain or stop their native executor before Common tears down pins.
  ReleaseUsesAfterDeviceLoss();
}

auto CommandQueue::InspectSubmissionCounters() const -> SubmissionCounters
{
  std::lock_guard lock(submission_->mutex);
  return submission_->counters;
}
auto CommandQueue::Identity() const noexcept -> QueueIdentity
{
  return submission_->identity;
}
auto CommandQueue::BackendLifetimeState() const noexcept
  -> std::shared_ptr<BackendLifetime>
{
  return submission_->lifetime;
}
auto CommandQueue::BindBackend(std::shared_ptr<BackendLifetime> lifetime,
  std::shared_ptr<void> native_lifetime) -> void
{
  std::lock_guard lock(submission_->mutex);
  if (submission_->lifetime && submission_->lifetime != lifetime) {
    throw std::logic_error("Queue belongs to another backend incarnation");
  }
  submission_->lifetime = std::move(lifetime);
  submission_->native_lifetime = std::move(native_lifetime);
}

auto CommandQueue::EnqueueLegacyMarker(uint64_t value) -> void
{
  SignalImmediate(value);
}
auto CommandQueue::EmitCompletionMarker() -> uint64_t
{
  std::lock_guard lock(submission_->mutex);
  const auto value = Signal();
  try {
    EnqueueLegacyMarker(value);
  } catch (...) {
    if (submission_->lifetime) {
      submission_->lifetime->MarkSubmissionFault();
    }
    throw;
  }
  submission_->last_legacy_emitted = value;
  return value;
}
auto CommandQueue::SignalSubmittedWork() -> uint64_t
{
  const auto admission = submission_->lifetime
    ? submission_->lifetime->AcquireOperation()
    : BackendOperation {};
  return EmitCompletionMarker();
}
void CommandQueue::Flush() const
{
  auto& self = const_cast<CommandQueue&>(*this);
  const auto marker = self.EmitCompletionMarker();
  Wait(marker);
  if (GetCompletedValue() == (std::numeric_limits<uint64_t>::max)()) {
    throw std::runtime_error("Device lost while draining queue");
  }
  self.PollCompletedUses();
}

auto CommandQueue::PrepareNativeSubmission(
  const internal::NativeSubmissionRequest&,
  std::unique_ptr<internal::NativeSubmission>)
  -> std::unique_ptr<internal::NativeSubmission>
{
  throw std::logic_error("Backend does not implement prepared submission");
}
auto CommandQueue::QueryPrivateCompletion() const noexcept -> uint64_t
{
  return 0;
}
auto CommandQueue::WaitPrivateCompletion(uint64_t) const -> void
{
  throw std::logic_error("Backend has no completion timeline");
}
auto CommandQueue::TearDownUncertainDevice() noexcept -> void { }
auto CommandQueue::WaitForNativeStop() noexcept -> void { }

auto CommandQueue::PrepareStateAdoption(const SubmittedWork& work) -> void
{
  auto& staged = submission_->staged_states;
  staged.clear();
  for (const auto& state : work.final_states) {
    if (state.resource->IsValid() && state.state != ResourceStates::kUnknown
      && !known_resource_states_.contains(state.resource)) {
      staged.insert_or_assign(state.resource, state.state);
    }
  }
  const auto needed = known_resource_states_.size() + staged.size();
  if (needed > known_resource_states_.bucket_count()
      * known_resource_states_.max_load_factor()) {
    known_resource_states_.reserve(needed);
  }
}
auto CommandQueue::CommitStateAdoption(
  const SubmittedWork& work, size_t issued_lists) noexcept -> void
{
  const auto count = issued_lists == 0 ? 0 : work.state_ends[issued_lists - 1];
  for (size_t index = 0; index < count; ++index) {
    const auto& state = work.final_states[index];
    if (!state.resource->IsValid() || state.state == ResourceStates::kUnknown) {
      continue;
    }
    auto known = known_resource_states_.find(state.resource);
    if (known == known_resource_states_.end()) {
      auto node = submission_->staged_states.extract(state.resource);
      assert(!node.empty());
      known = known_resource_states_.insert(std::move(node)).position;
    }
    known->second = state.state;
  }
  submission_->staged_states.clear();
}

auto CommandQueue::Submit(std::shared_ptr<CommandList> list) -> void
{
  const std::array lists { std::move(list) };
  const auto result = SubmitPrepared(lists, false);
  if (result.outcome != SubmissionOutcome::kSubmitted) {
    throw SubmissionException(result);
  }
}
auto CommandQueue::Submit(std::span<std::shared_ptr<CommandList>> lists) -> void
{
  const auto result = SubmitPrepared(lists, false);
  if (result.outcome != SubmissionOutcome::kSubmitted) {
    throw SubmissionException(result);
  }
}
auto CommandQueue::SubmitWithReceipt(std::shared_ptr<CommandList> list) noexcept
  -> SubmissionResult
{
  const std::array lists { std::move(list) };
  return SubmitPrepared(lists, true);
}

auto CommandQueue::SubmitPrepared(
  std::span<const std::shared_ptr<CommandList>> lists,
  bool request_receipt) noexcept -> SubmissionResult
{
  SubmissionResult result;
  std::unique_ptr<SubmittedWork> work;
  SubmittedWork* retained = nullptr;
  try {
    {
      const auto admission = submission_->lifetime
        ? submission_->lifetime->AcquireOperation()
        : BackendOperation {};
      std::unique_lock lock(submission_->mutex, std::defer_lock);
#if defined(OXYGEN_WITH_TRACY)
      {
        static const profiling::CpuProfileScopeDesc kWait { .label
          = "Graphics.Queue.Submission.LockWait",
          .category = profiling::ProfileCategory::kSynchronization };
        const profiling::CpuProfileScope wait(kWait);
        lock.lock();
      }
      static const profiling::CpuProfileScopeDesc kHeld { .label
        = "Graphics.Queue.Submission.CriticalSection",
        .category = profiling::ProfileCategory::kSynchronization };
      const profiling::CpuProfileScope held(kHeld);
#else
      lock.lock();
#endif
      if (lists.empty()) {
        return result;
      }
      auto last_signal = submission_->last_legacy_emitted;
      for (size_t list_index = 0; list_index < lists.size(); ++list_index) {
        const auto& list = lists[list_index];
        if (!list || !list->IsClosed() || list->GetQueueRole() != GetQueueRole()
          || !list->RecordingIsCurrent() || !list->Uses().Validate()) {
          return result;
        }
        if (list->backend_bound_
          && (!submission_->lifetime
            || !list->Uses().MatchesQueue(
              submission_->lifetime->Id(), Identity()))) {
          return result;
        }
        for (size_t earlier = 0; earlier < list_index; ++earlier) {
          if (lists[earlier] == list) {
            return result;
          }
        }
        const auto prior_signal = last_signal;
        request_receipt = request_receipt || list->Uses().NeedsCompletion();
        for (const auto& action : list->SubmitActions()) {
          if (action.value == 0
            || action.value == (std::numeric_limits<uint64_t>::max)()) {
            return result;
          }
          if (action.kind == CommandList::SubmitQueueActionKind::kSignal) {
            if (action.value <= last_signal) {
              return result;
            }
            last_signal = action.value;
          } else if (action.value > prior_signal) {
            return result;
          }
        }
      }
      if (request_receipt
        && (!submission_->lifetime || submission_->lifetime->Id().get() == 0
          || submission_->next_private
            == (std::numeric_limits<uint64_t>::max)())) {
        return result;
      }
      work = lists.front()->TakeRetirementStorage();
      work->lists.assign(lists.begin(), lists.end());
      work->final_states.clear();
      work->state_ends.clear();
      work->dependencies.clear();
      for (const auto& list : lists) {
        const auto states = list->RecordedStates();
        work->final_states.insert(
          work->final_states.end(), states.begin(), states.end());
        work->state_ends.push_back(work->final_states.size());
        for (const auto receipt : list->Uses().Dependencies()) {
          const auto producer = submission_->lifetime
            ? submission_->lifetime->FindQueue(receipt.Queue().get())
            : nullptr;
          if (!producer
            || producer->QueryCompletion(receipt) == CompletionStatus::kInvalid
            || producer->QueryCompletion(receipt)
              == CompletionStatus::kDeviceLost) {
            throw std::logic_error(
              "Dependency was not emitted by this backend");
          }
          if (producer.get() != this) {
            work->dependencies.push_back({ producer.get(), receipt });
          }
        }
      }
      const auto marker = request_receipt
        ? std::optional<uint64_t>(submission_->next_private++)
        : std::nullopt;
      const auto failure = std::exchange(
        submission_->failure, internal::SubmissionFailurePoint::kNone);
      if (failure == internal::SubmissionFailurePoint::kBeforeIssue) {
        throw SubmissionException({});
      }
      work->native = PrepareNativeSubmission(
        { work->lists, work->dependencies, marker, {},
          failure == internal::SubmissionFailurePoint::kAfterIssueBeforeMarker,
          failure == internal::SubmissionFailurePoint::kAfterFirstList },
        std::move(work->native));
      if (!work->native) {
        throw std::runtime_error("Native preparation failed");
      }
      std::lock_guard states_lock(known_resource_states_mutex_);
      PrepareStateAdoption(*work);
      internal::NativeSubmissionProgress progress { .last_legacy_signal
        = submission_->last_legacy_emitted };
      bool accepted = false;
      try {
        work->native->Execute(progress);
        accepted = progress.issued_lists == lists.size()
          && (!marker || progress.private_marker_emitted);
      } catch (...) {
      }
      submission_->last_legacy_emitted = progress.last_legacy_signal;
      work->issued_lists = progress.issued_lists;
      if (accepted) {
        ++submission_->counters.accepted_batches;
        submission_->counters.command_lists += lists.size();
        submission_->counters.completion_signals += marker ? 1U : 0U;
        submission_->counters.dependency_waits += work->dependencies.size();
        CommitStateAdoption(*work, lists.size());
        result.outcome = SubmissionOutcome::kSubmitted;
        if (marker) {
          submission_->last_private_emitted.store(
            *marker, std::memory_order_release);
          result.receipt = CompletionReceipt { submission_->lifetime->Id(),
            Identity(), *marker };
        }
        for (const auto& list : lists) {
          list->state_ = CommandList::State::kSubmitted;
        }
      } else if (progress.issued_lists != 0) {
        result.outcome = SubmissionOutcome::kExecutionUncertain;
        for (const auto& list : lists) {
          list->state_ = CommandList::State::kExecutionUncertain;
        }
        if (submission_->lifetime) {
          submission_->lifetime->MarkSubmissionFault();
        }
      }
      work->result = result;
      work->published = false;
      if (result.outcome == SubmissionOutcome::kExecutionUncertain) {
        retained = work.get();
        Append(submission_->quarantine, submission_->quarantine_tail,
          std::move(work));
      } else if (result.receipt) {
        retained = work.get();
        Append(
          submission_->in_flight, submission_->in_flight_tail, std::move(work));
      }
    }
    // Resolve opaque ownership outside both locks. Collection waits for
    // publication.
    for (const auto& list : lists) {
      list->Uses().ResolveSubmission(result);
    }
    if (retained) {
      std::lock_guard lock(submission_->mutex);
      retained->published = true;
    }
  } catch (...) {
    // Never turn accepted/possibly issued work into a discard.
    if (retained) {
      for (const auto& list : lists) {
        list->Uses().ResolveSubmission(result);
      }
      std::lock_guard lock(submission_->mutex);
      retained->published = true;
    }
  }
  if (work) {
    RecycleWork(std::move(work),
      result.outcome == SubmissionOutcome::kDiscarded
        ? std::optional(UseReleaseReason::kDiscarded)
        : std::nullopt);
  }
  if (submission_->lifetime
    && submission_->lifetime->State() == BackendLifecycle::kRetiring) {
    ReleaseUsesAfterDeviceLoss();
  }
  return result;
}

auto CommandQueue::RecycleWork(std::unique_ptr<SubmittedWork> work,
  std::optional<UseReleaseReason> completion) noexcept -> void
{
  if (work->lists.empty()) {
    return;
  }
  auto first = work->lists.front();
  for (const auto& list : work->lists) {
    if (completion) {
      list->Uses().Release(*completion);
      if (*completion == UseReleaseReason::kDeviceLost) {
        list->Invalidate();
      } else {
        list->state_ = CommandList::State::kFree;
      }
    } else {
      list->Uses().Release(UseReleaseReason::kAdmissionResolved);
    }
  }
  work->lists.clear();
  work->final_states.clear();
  work->state_ends.clear();
  work->dependencies.clear();
  work->result = {};
  work->issued_lists = 0;
  work->published = false;
  assert(!work->next);
  first->RestoreRetirementStorage(std::move(work));
}

auto CommandQueue::QueryCompletion(CompletionReceipt receipt) const noexcept
  -> CompletionStatus
{
  if (!receipt.IsValid() || !submission_->lifetime
    || receipt.Backend() != submission_->lifetime->Id()
    || receipt.Queue() != Identity()
    || receipt.Value()
      > submission_->last_private_emitted.load(std::memory_order_acquire)) {
    return CompletionStatus::kInvalid;
  }
  const auto completed = QueryPrivateCompletion();
  if (completed == (std::numeric_limits<uint64_t>::max)()) {
    return CompletionStatus::kDeviceLost;
  }
  return completed >= receipt.Value() ? CompletionStatus::kComplete
                                      : CompletionStatus::kPending;
}
auto CommandQueue::PollCompletedUses() -> void
{
  for (;;) {
    std::unique_ptr<SubmittedWork> ready;
    {
      std::lock_guard lock(submission_->mutex);
      const auto* head = submission_->in_flight.get();
      if (!head || !head->published) {
        return;
      }
      const auto status = QueryCompletion(*head->result.receipt);
      if (status == CompletionStatus::kDeviceLost) {
        if (submission_->lifetime) {
          submission_->lifetime->MarkSubmissionFault();
        }
        return;
      }
      if (status != CompletionStatus::kComplete) {
        return;
      }
      ready = std::move(submission_->in_flight);
      submission_->in_flight = std::move(ready->next);
      if (!submission_->in_flight) {
        submission_->in_flight_tail = nullptr;
      }
    }
    RecycleWork(std::move(ready), UseReleaseReason::kCompleted);
  }
}

auto CommandQueue::HasStateConflictWith(const CommandQueue& other) const -> bool
{
  if (&other == this) {
    return false;
  }
  std::scoped_lock lock(
    known_resource_states_mutex_, other.known_resource_states_mutex_);
  for (const auto& [resource, state] : known_resource_states_) {
    const auto found = other.known_resource_states_.find(resource);
    if (found != other.known_resource_states_.end() && found->second != state) {
      return true;
    }
  }
  return false;
}
auto CommandQueue::ReconcileQuarantinedStates() -> bool
{
  std::lock_guard lock(submission_->mutex);
  std::lock_guard states_lock(known_resource_states_mutex_);
  for (auto* work = submission_->quarantine.get(); work;
    work = work->next.get()) {
    if (!work->published) {
      return false;
    }
    // Retain every pin until all queues agree on restored states. A failed
    // allocation leaves quarantine intact and the backend faulted.
    PrepareStateAdoption(*work);
    CommitStateAdoption(*work, work->issued_lists);
  }
  return true;
}

auto CommandQueue::RecoverQuarantinedWork(ResourceRegistry& registry) -> bool
{
  // The Graphics owner has first drained every queue and excluded new issue.
  for (;;) {
    std::unique_ptr<SubmittedWork> ready;
    {
      std::lock_guard lock(submission_->mutex);
      if (!submission_->quarantine) {
        return true;
      }
      if (!submission_->quarantine->published) {
        return false;
      }
      ready = std::move(submission_->quarantine);
      submission_->quarantine = std::move(ready->next);
      if (!submission_->quarantine) {
        submission_->quarantine_tail = nullptr;
      }
    }
    for (const auto& list : ready->lists) {
      list->Uses().InvalidateRegistrations(registry);
    }
    RecycleWork(std::move(ready), UseReleaseReason::kCompleted);
  }
}
auto CommandQueue::StopAfterSubmissionFailure() noexcept -> void
{
  if (submission_->lifetime) {
    submission_->lifetime->MarkSubmissionFault();
  }
  TearDownUncertainDevice();
  WaitForNativeStop();
  ReleaseUsesAfterDeviceLoss();
}

auto CommandQueue::ReleaseUsesAfterDeviceLoss() noexcept -> void
{
  for (auto* head : { &submission_->in_flight, &submission_->quarantine }) {
    for (;;) {
      std::unique_ptr<SubmittedWork> work;
      {
        std::lock_guard lock(submission_->mutex);
        if (!*head || !(*head)->published) {
          break;
        }
        work = std::move(*head);
        *head = std::move(work->next);
        if (!*head) {
          if (head == &submission_->in_flight) {
            submission_->in_flight_tail = nullptr;
          } else {
            submission_->quarantine_tail = nullptr;
          }
        }
      }
      RecycleWork(std::move(work), UseReleaseReason::kDeviceLost);
    }
  }
}
} // namespace oxygen::graphics
