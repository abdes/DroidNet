//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Base/Logging.h>
#include <limits>
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
  const auto completed = GetCompletedValue();
  // Some backends (e.g., D3D12) return UINT64_MAX when the device is removed.
  // In that case, bail out gracefully instead of wrapping to 0 and throwing.
  if (completed == (std::numeric_limits<uint64_t>::max)()) {
    DLOG_F(WARNING,
      "CommandQueue[{}] flush skipped: completed value is UINT64_MAX (device removed)",
      GetName());
    return;
  }

  // Ensure monotonic advancement: the next value must be > current.
  const auto current = GetCurrentValue();
  auto next = completed + 1;
  if (next <= current) {
    next = current + 1;
  }

  Signal(next);
  // Wait for the value we just signaled to ensure GPU caught up.
  Wait(next);
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
