//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "Oxygen/Renderers/Direct3d12/Fence.h"

#include <stdexcept>

#include "Oxygen/Base/Windows/ComError.h"
#include "Oxygen/Renderers/Direct3d12/Detail/dx12_utils.h" // for GetMainDevice()

using namespace oxygen::renderer::d3d12;

Fence::~Fence()
{
  Release();
}

void Fence::Initialize(const uint64_t initial_value)
{
  Release();

  current_value_ = initial_value;

  FenceType* raw_fence = nullptr;
  ThrowOnFailed(GetMainDevice()->CreateFence(current_value_, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&raw_fence)));
  fence_.reset(raw_fence);

  fence_event_ = CreateEvent(nullptr, FALSE, FALSE, nullptr);
  if (!fence_event_) {
    Release();
    throw std::runtime_error("Failed to create fence event");
  }

  should_release_ = true;
}

void Fence::Release() noexcept
{
  if (!should_release_) return;

  if (fence_event_) {
    CloseHandle(fence_event_);
    fence_event_ = nullptr;
  }
  fence_.reset(); // Will automatically call Release() on the fence object
  should_release_ = false;
}

void Fence::Signal(const uint64_t value)
{
  if (value <= current_value_) {
    DLOG_F(WARNING, "New value {} must be greater than the current value {}", value, current_value_);
    throw std::invalid_argument("New value must be greater than the current value");
  }
  ThrowOnFailed(fence_->Signal(value));
  current_value_ = value;
}

void Fence::Wait(const uint64_t value) const
{
  if (fence_->GetCompletedValue() < value) {
    ThrowOnFailed(fence_->SetEventOnCompletion(value, fence_event_));
    WaitForSingleObject(fence_event_, INFINITE);
  }
}

auto Fence::GetCompletedValue() const->uint64_t
{
  return fence_->GetCompletedValue();
}

void Fence::Reset(const uint64_t value)
{
  current_value_ = value;
  ThrowOnFailed(fence_->Signal(current_value_));
}
