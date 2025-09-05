//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Composition/ObjectMetadata.h>
#include <Oxygen/Graphics/Common/CommandQueue.h>

using oxygen::graphics::CommandQueue;

CommandQueue::CommandQueue(std::string_view name)
{
  AddComponent<ObjectMetadata>(name);
}

CommandQueue::~CommandQueue()
{
  DLOG_F(INFO, "CommandQueue destroyed: {}",
    GetComponent<ObjectMetadata>().GetName());
}

void CommandQueue::Flush() const
{
  DLOG_F(1, "CommandQueue[{}] flushed", GetName());
  Signal(GetCompletedValue() + 1);
  Wait(GetCurrentValue());
  DLOG_F(1, "CommandQueue[{}] fence current value: {}", GetName(),
    GetCurrentValue());
  DLOG_F(1, "CommandQueue[{}] fence completed value: {}", GetName(),
    GetCompletedValue());
}

auto CommandQueue::GetName() const noexcept -> std::string_view
{
  return GetComponent<ObjectMetadata>().GetName();
}

void CommandQueue::SetName(const std::string_view name) noexcept
{
  GetComponent<ObjectMetadata>().SetName(name);
}
