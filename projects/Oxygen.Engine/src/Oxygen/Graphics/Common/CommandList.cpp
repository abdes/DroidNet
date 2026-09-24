//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Composition/ObjectMetadata.h>
#include <Oxygen/Graphics/Common/CommandList.h>
#include <Oxygen/Graphics/Common/Internal/QueueSubmission.h>

using oxygen::graphics::CommandList;

CommandList::CommandList(std::string_view name, const QueueRole type)
  : type_(type)
  , state_(State::kFree)
{
  AddComponent<ObjectMetadata>(name);
  DLOG_F(1, "CommandList created: {}", name);
}

CommandList::~CommandList()
{
  DLOG_F(
    1, "CommandList destroyed: {}", GetComponent<ObjectMetadata>().GetName());
}

auto CommandList::GetName() const noexcept -> std::string_view
{
  return GetComponent<ObjectMetadata>().GetName();
}

void CommandList::SetName(const std::string_view name) noexcept
{
  GetComponent<ObjectMetadata>().SetName(name);
}

auto CommandList::QueueSubmitSignal(const uint64_t value) -> void
{
  submit_queue_actions_.push_back(
    { .kind = SubmitQueueActionKind::kSignal, .value = value });
}

auto CommandList::QueueSubmitWait(const uint64_t value) -> void
{
  submit_queue_actions_.push_back(
    { .kind = SubmitQueueActionKind::kWait, .value = value });
}

auto CommandList::HasSubmitQueueActions() const noexcept -> bool
{
  return !submit_queue_actions_.empty();
}

auto CommandList::TakeSubmitQueueActions() -> std::vector<SubmitQueueAction>
{
  auto actions = std::move(submit_queue_actions_);
  submit_queue_actions_.clear();
  return actions;
}

auto CommandList::SetRecordedResourceStates(
  std::vector<RecordedResourceState> states) -> void
{
  recorded_resource_states_ = std::move(states);
}

auto CommandList::TakeRecordedResourceStates()
  -> std::vector<RecordedResourceState>
{
  auto states = std::move(recorded_resource_states_);
  recorded_resource_states_.clear();
  return states;
}

void CommandList::OnBeginRecording()
{
  if (state_ != State::kFree) {
    throw std::runtime_error("CommandList is not in a Free state");
  }
  submit_queue_actions_.clear();
  recorded_resource_states_.clear();
  state_ = State::kRecording;
}

auto CommandList::BindBackend(
  std::shared_ptr<BackendLifetime> lifetime, QueueIdentity queue) -> void
{
  if (!IsFree()) {
    throw std::logic_error("Cannot rebind an active command list");
  }
  backend_lifetime_ = lifetime;
  backend_bound_ = lifetime != nullptr;
  recording_epoch_ = lifetime ? lifetime->RecordingEpoch() : 0;
  uses_.Bind(lifetime ? lifetime->Id() : BackendIncarnationId { 0 }, queue);
}
auto CommandList::RecordingIsCurrent() const noexcept -> bool
{
  const auto lifetime = backend_lifetime_.lock();
  return !backend_bound_
    || (lifetime && lifetime->State() == BackendLifecycle::kActive
      && !lifetime->IsFaulted()
      && lifetime->RecordingEpoch() == recording_epoch_);
}
auto CommandList::TakeRetirementStorage()
  -> std::unique_ptr<internal::SubmittedWork>
{
  if (!retirement_storage_) {
    retirement_storage_ = std::make_unique<internal::SubmittedWork>();
  }
  return std::move(retirement_storage_);
}
auto CommandList::RestoreRetirementStorage(
  std::unique_ptr<internal::SubmittedWork> storage) noexcept -> void
{
  retirement_storage_ = std::move(storage);
}

void CommandList::OnEndRecording()
{
  if (state_ != State::kRecording) {
    throw std::runtime_error("CommandList is not in a Recording state");
  }
  state_ = State::kClosed;
}

void CommandList::OnSubmitted()
{
  if (state_ != State::kClosed) {
    throw std::runtime_error("CommandList is not in a Recorded state");
  }
  state_ = State::kSubmitted;
  DLOG_F(3, "'{}' submitted", GetName());
}

void CommandList::OnExecuted()
{
  if (state_ != State::kSubmitted) {
    throw std::runtime_error("CommandList is not in an Executing state");
  }
  state_ = State::kFree;
}

void CommandList::OnFailed() noexcept
{
  submit_queue_actions_.clear();
  recorded_resource_states_.clear();
  if (state_ != State::kInvalid && state_ != State::kExecutionUncertain) {
    state_ = State::kFree;
  }
}

auto CommandList::Invalidate() noexcept -> void
{
  submit_queue_actions_.clear();
  recorded_resource_states_.clear();
  state_ = State::kInvalid;
}
