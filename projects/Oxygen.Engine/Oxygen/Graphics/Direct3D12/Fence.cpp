//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "Oxygen/Graphics/Direct3d12/Fence.h"

#include <stdexcept>

#include "Oxygen/Base/Windows/ComError.h"
#include "Oxygen/Graphics/Direct3d12/Detail/dx12_utils.h" // for GetMainDevice()

#include "Renderer.h"

using namespace oxygen::renderer::d3d12;

void Fence::InitializeSynchronizationObject(const uint64_t initial_value)
{
  DCHECK_EQ_F(fence_, nullptr);
  current_value_ = initial_value;
  ID3DFenceV* raw_fence = nullptr;
  ThrowOnFailed(GetMainDevice()->CreateFence(initial_value,
                                             D3D12_FENCE_FLAG_NONE,
                                             IID_PPV_ARGS(&raw_fence)),
                "Could not create a Fence");
  fence_ = raw_fence;

  fence_event_ = CreateEvent(nullptr, FALSE, FALSE, nullptr);
  if (!fence_event_) {
    DLOG_F(ERROR, "Failed to create fence event");
    ReleaseSynchronizationObject();
    windows::WindowsException::ThrowFromLastError();
  }
}

void Fence::ReleaseSynchronizationObject() noexcept
{
  if (fence_event_) {
    if (!CloseHandle(fence_event_))
    {
      DLOG_F(WARNING, "Failed to close fence event handle");
    }
    fence_event_ = nullptr;
  }
  ObjectRelease(fence_);
  command_queue_ = nullptr;
}

void Fence::Signal(const uint64_t value) const
{
  if (value <= current_value_) {
    DLOG_F(WARNING, "New value {} must be greater than the current value {}", value, current_value_);
    throw std::invalid_argument("New value must be greater than the current value");
  }
  DCHECK_NOTNULL_F(fence_, "fence must be initialized");
  DCHECK_NOTNULL_F(command_queue_, "command queue must be valid");

  DCHECK_GT_F(value, fence_->GetCompletedValue(), "New value must be greater than the current value");
  ThrowOnFailed((command_queue_->Signal(fence_, value)),
                fmt::format("Signal({}) on fence failed", value));
  current_value_ = value;
}

auto Fence::Signal() const -> uint64_t
{
  Signal(current_value_ + 1);
  // Increment only if the signal was successful
  return current_value_;
}

void Fence::Wait(const uint64_t value, const std::chrono::milliseconds timeout) const
{
  DCHECK_LE_F(timeout.count(), std::numeric_limits<DWORD>::max());
  if (fence_->GetCompletedValue() < value) {
    ThrowOnFailed(fence_->SetEventOnCompletion(value, fence_event_),
                  fmt::format("Wait({}) on fence failed", value));
    WaitForSingleObject(fence_event_, static_cast<DWORD>(timeout.count()));
  }
}

void Fence::Wait(const uint64_t value) const
{
  Wait(value, std::chrono::milliseconds(std::numeric_limits<DWORD>::max()));
}

void Fence::QueueWaitCommand(const uint64_t value) const
{
  DCHECK_NOTNULL_F(command_queue_, "command queue must be valid");
  DCHECK_NOTNULL_F(fence_, "fence must be initialized");

  ThrowOnFailed(command_queue_->Wait(fence_, value),
                fmt::format("QueueWaitCommand({}) on fence failed", value));
}

void Fence::QueueSignalCommand(const uint64_t value)
{
  DCHECK_NOTNULL_F(command_queue_, "command queue must be valid");
  DCHECK_NOTNULL_F(fence_, "fence must be initialized");
  ThrowOnFailed(command_queue_->Signal(fence_, value),
                fmt::format("QueueSignalCommand({}) on fence failed", value));
}

auto Fence::GetCompletedValue() const->uint64_t
{
  return fence_->GetCompletedValue();
}
