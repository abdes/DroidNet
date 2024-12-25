//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "Oxygen/Renderers/Direct3d12/Fence.h"

#include <stdexcept>

#include "Oxygen/Base/Windows/ComError.h"
#include "Oxygen/Renderers/Direct3d12/Detail/dx12_utils.h" // for GetMainDevice()
#include "Oxygen/Renderers/Direct3d12/Detail/FenceImpl.h"

using namespace oxygen::renderer::d3d12;

Fence::~Fence()
{
  Release();
}

void Fence::Initialize(const uint64_t initial_value)
{
  Release();
  current_value_ = initial_value;
  pimpl_->OnInitialize(initial_value);
  should_release_ = true;
}

void Fence::Release() noexcept
{
  if (!should_release_) return;
  pimpl_->OnRelease();
  should_release_ = false;
}

void Fence::Signal(const uint64_t value) const
{
  if (value <= current_value_) {
    DLOG_F(WARNING, "New value {} must be greater than the current value {}", value, current_value_);
    throw std::invalid_argument("New value must be greater than the current value");
  }
  pimpl_->Signal(value);
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
  pimpl_->Wait(value, static_cast<DWORD>(timeout.count()));
}

void Fence::Wait(const uint64_t value) const
{
  Wait(value, std::chrono::milliseconds(std::numeric_limits<DWORD>::max()));
}

void Fence::QueueWaitCommand(const uint64_t value) const
{
  pimpl_->QueueWaitCommand(value);
}

void Fence::QueueSignalCommand(const uint64_t value)
{
  pimpl_->QueueSignalCommand(value);
}

auto Fence::GetCompletedValue() const->uint64_t
{
  return pimpl_->GetCompletedValue();
}
