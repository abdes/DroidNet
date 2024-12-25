#include "FenceImpl.h"

#include "Oxygen/Base/Logging.h"
#include "Oxygen/Base/Windows/ComError.h"

using oxygen::windows::ThrowOnFailed;
using oxygen::renderer::d3d12::detail::FenceImpl;

FenceImpl::FenceImpl(ID3D12CommandQueue* command_queue)
  : command_queue_{ command_queue }
{
  DCHECK_NOTNULL_F(command_queue, "Command queue must be valid");
}

FenceImpl::~FenceImpl()
{
  DCHECK_F(!fence_,
           "Fence object was not released"
           "The Fence objects needs to handle release from destructor");
}

void FenceImpl::OnInitialize(const uint64_t initial_value)
{
  DCHECK_F(!fence_, "not already initialized");

  FenceType* raw_fence = nullptr;
  windows::ThrowOnFailed(GetMainDevice()->CreateFence(initial_value,
                                                      D3D12_FENCE_FLAG_NONE,
                                                      IID_PPV_ARGS(&raw_fence)),
                         "Could not create a Fence");
  fence_.reset(raw_fence);

  fence_event_ = CreateEvent(nullptr, FALSE, FALSE, nullptr);
  if (!fence_event_) {
    DLOG_F(ERROR, "Failed to create fence event");
    OnRelease();
    windows::WindowsException::ThrowFromLastError();
  }
}

void FenceImpl::OnRelease() noexcept
{
  if (fence_event_) {
    if (!CloseHandle(fence_event_))
    {
      DLOG_F(WARNING, "Failed to close fence event handle");
    }
    fence_event_ = nullptr;
  }
  fence_.reset();
  command_queue_ = nullptr;
}

void FenceImpl::Signal(const uint64_t value) const
{
  DCHECK_NOTNULL_F(fence_, "fence must be initialized");
  DCHECK_NOTNULL_F(command_queue_, "command queue must be valid");

  DCHECK_GT_F(value, fence_->GetCompletedValue(), "New value must be greater than the current value");
  windows::ThrowOnFailed((command_queue_->Signal(fence_.get(), value)),
                         fmt::format("Signal({}) on fence failed", value));
}

void FenceImpl::Wait(const uint64_t value,
                     const DWORD milliseconds) const
{
  if (fence_->GetCompletedValue() < value) {
    windows::ThrowOnFailed(fence_->SetEventOnCompletion(value, fence_event_),
                           fmt::format("Wait({}) on fence failed", value));
    WaitForSingleObject(fence_event_, milliseconds);
  }
}

void FenceImpl::QueueWaitCommand(uint64_t value) const
{
  DCHECK_NOTNULL_F(command_queue_, "command queue must be valid");
  DCHECK_NOTNULL_F(fence_, "fence must be initialized");

  ThrowOnFailed(command_queue_->Wait(fence_.get(), value),
                fmt::format("QueueWaitCommand({}) on fence failed", value));
}

void FenceImpl::QueueSignalCommand(uint64_t value) const
{
  DCHECK_NOTNULL_F(command_queue_, "command queue must be valid");
  DCHECK_NOTNULL_F(fence_, "fence must be initialized");
  ThrowOnFailed(command_queue_->Signal(fence_.get(), value),
                fmt::format("QueueSignalCommand({}) on fence failed", value));
}

auto FenceImpl::GetCompletedValue() const -> uint64_t
{
  return fence_->GetCompletedValue();
}
