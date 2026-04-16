//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Composition/ObjectMetadata.h>
#include <Oxygen/Graphics/Common/CommandList.h>

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
  state_ = State::kFree;
  DLOG_F(WARNING, "'{}' errored, and its state will be force reset to 'Free'",
    GetName());
}
