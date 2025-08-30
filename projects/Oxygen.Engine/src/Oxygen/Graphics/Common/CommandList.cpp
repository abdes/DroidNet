//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Composition/ObjectMetaData.h>
#include <Oxygen/Graphics/Common/CommandList.h>

using oxygen::graphics::CommandList;

CommandList::CommandList(std::string_view name, const QueueRole type)
  : type_(type)
  , state_(State::kFree)
{
  AddComponent<ObjectMetaData>(name);
  DLOG_F(INFO, "CommandList created: {}", name);
}

CommandList::~CommandList()
{
  DLOG_F(INFO, "CommandList destroyed: {}",
    GetComponent<ObjectMetaData>().GetName());
}

auto CommandList::GetName() const noexcept -> std::string_view
{
  return GetComponent<ObjectMetaData>().GetName();
}

void CommandList::SetName(const std::string_view name) noexcept
{
  GetComponent<ObjectMetaData>().SetName(name);
}

void CommandList::OnBeginRecording()
{
  if (state_ != State::kFree) {
    throw std::runtime_error("CommandList is not in a Free state");
  }
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
}

void CommandList::OnExecuted()
{
  if (state_ != State::kSubmitted) {
    throw std::runtime_error("CommandList is not in an Executing state");
  }
  state_ = State::kFree;
}
