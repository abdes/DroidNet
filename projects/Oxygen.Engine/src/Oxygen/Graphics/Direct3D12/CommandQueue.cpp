//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <limits>
#include <string_view>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/NoStd.h>
#include <Oxygen/Base/Windows/ComError.h>
#include <Oxygen/Graphics/Common/CommandList.h>
#include <Oxygen/Graphics/Common/ObjectRelease.h>
#include <Oxygen/Graphics/Common/Types/Queues.h>
#include <Oxygen/Graphics/Direct3D12/CommandList.h>
#include <Oxygen/Graphics/Direct3D12/CommandQueue.h>
#include <Oxygen/Graphics/Direct3D12/Detail/dx12_utils.h>
#include <Oxygen/Graphics/Direct3D12/Graphics.h>

using oxygen::graphics::d3d12::CommandQueue;
using oxygen::windows::ThrowOnFailed;

CommandQueue::CommandQueue(
  std::string_view name, QueueRole role, const Graphics* gfx)
  : Base(name)
  , queue_role_(role)
  , gfx_(gfx)
{
  DCHECK_NOTNULL_F(gfx_, "Graphics context cannot be null!");

  CreateCommandQueue(role, name);
  LOG_F(INFO, "D3D12 Command queue [name=`{}`, role=`{}`] created", name,
    nostd::to_string(role));

  const auto fence_name = fmt::format("Fence ({})", name);
  CreateFence(fence_name, 0ULL);
  LOG_F(INFO, "D3D12 Fence [name=`{}`] created", fence_name);
}

CommandQueue::~CommandQueue() noexcept
{
  if (command_queue_ == nullptr) {
    return;
  }
  DCHECK_NOTNULL_F(fence_);

  // Flush the command queue to ensure all commands are completed before
  // destruction.
  Wait(GetCurrentValue());

  // Get the command queue debug name (from the previously set private data)
  // for logging.
  const auto queue_name = GetObjectName(command_queue_, "Command Queue");

  ReleaseFence();
  LOG_F(INFO, "D3D12 Fence [name=`Fence ({})`] destroyed", queue_name);

  ReleaseCommandQueue();
  LOG_F(INFO, "D3D12 Command Queue [name=`{}`] destroyed", queue_name);
}

auto CommandQueue::CurrentDevice() const -> dx::IDevice*
{
  return gfx_->GetCurrentDevice();
}

void CommandQueue::CreateCommandQueue(
  QueueRole role, const std::string_view queue_name)
{
  auto* const device = CurrentDevice();
  DCHECK_NOTNULL_F(device);

  D3D12_COMMAND_LIST_TYPE d3d12_type; // NOLINT(*-init-variables)
  switch (role) // NOLINT(clang-diagnostic-switch-enum) - these are the only
                // valid values
  {
  case QueueRole::kGraphics:
    d3d12_type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    break;
  case QueueRole::kCompute:
    d3d12_type = D3D12_COMMAND_LIST_TYPE_COMPUTE;
    break;
  case QueueRole::kTransfer:
    d3d12_type = D3D12_COMMAND_LIST_TYPE_COPY;
    break;
  case QueueRole::kPresent:
    d3d12_type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    break;
  default:
    throw std::runtime_error(
      fmt::format("Unsupported CommandQueue role: {}", nostd::to_string(role)));
  }

  const D3D12_COMMAND_QUEUE_DESC queue_desc = { .Type = d3d12_type,
    .Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL,
    .Flags = D3D12_COMMAND_QUEUE_FLAG_NONE,
    .NodeMask = 0 };

  ThrowOnFailed(
    device->CreateCommandQueue(&queue_desc, IID_PPV_ARGS(&command_queue_)),
    fmt::format("could not create `{}` Command Queue", nostd::to_string(role)));
  NameObject(command_queue_, queue_name);
}

void CommandQueue::ReleaseCommandQueue() noexcept
{
  ObjectRelease(command_queue_);
}

void CommandQueue::CreateFence(
  const std::string_view fence_name, const uint64_t initial_value)
{
  DCHECK_NOTNULL_F(command_queue_);
  DCHECK_EQ_F(fence_, nullptr);

  current_value_ = initial_value;
  dx::IFence* raw_fence = nullptr;
  ThrowOnFailed(CurrentDevice()->CreateFence(initial_value,
                  D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&raw_fence)),
    "Could not create a Fence");
  fence_ = raw_fence;

  fence_event_ = CreateEvent(nullptr, FALSE, FALSE, nullptr);
  if (fence_event_ == nullptr) {
    DLOG_F(ERROR, "Failed to create fence event");
    ReleaseFence();
    windows::WindowsException::ThrowFromLastError();
  }
  NameObject(fence_, fence_name);
}

void CommandQueue::ReleaseFence() noexcept
{
  if (fence_event_ != nullptr) {
    if (CloseHandle(fence_event_) == 0) {
      DLOG_F(WARNING, "Failed to close fence event handle");
    }
    fence_event_ = nullptr;
  }
  ObjectRelease(fence_);
}

void CommandQueue::Signal(const uint64_t value) const
{
  if (value <= current_value_) {
    DLOG_F(WARNING, "New value {} must be greater than the current value {}",
      value, current_value_);
    throw std::invalid_argument(
      "New value must be greater than the current value");
  }
  DCHECK_NOTNULL_F(fence_, "fence must be initialized");
  DCHECK_NOTNULL_F(command_queue_, "command queue must be valid");

  DCHECK_GT_F(value, GetCurrentValue(),
    "New value must be greater than the current value");
  DLOG_F(2, "CommandQueue[{}]::Signal({} / current={})", GetName(), value,
    GetCurrentValue());
  ThrowOnFailed((command_queue_->Signal(fence_, value)),
    fmt::format("Signal({}) on fence failed", value));
  current_value_ = value;
}

auto CommandQueue::Signal() const -> uint64_t
{
  Signal(current_value_ + 1);
  // Incremented only if the signal was successful
  return current_value_;
}

void CommandQueue::Wait(
  const uint64_t value, const std::chrono::milliseconds timeout) const
{
  DCHECK_F(timeout.count() <= (std::numeric_limits<DWORD>::max)(),
    "timeout value must fit in a DWORD");
  const auto completed_value = fence_->GetCompletedValue();
  DLOG_F(2, "CommandQueue[{}]::Wait({} / current={})", GetName(), value,
    GetCurrentValue());
  if (completed_value < value) {
    ThrowOnFailed(fence_->SetEventOnCompletion(value, fence_event_),
      fmt::format("Wait({}) on fence failed", value));
    WaitForSingleObject(fence_event_, static_cast<DWORD>(timeout.count()));
    DLOG_F(2, "CommandQueue[{}] reached {}", GetName(), value);
  }
  DLOG_F(2, "CommandQueue[{}] at completed value: {} (current={})", GetName(),
    completed_value, GetCurrentValue());
}

void CommandQueue::Wait(const uint64_t value) const
{
  Wait(value, std::chrono::milliseconds((std::numeric_limits<DWORD>::max)()));
}

void CommandQueue::QueueWaitCommand(const uint64_t value) const
{
  DCHECK_NOTNULL_F(command_queue_, "command queue must be valid");
  DCHECK_NOTNULL_F(fence_, "fence must be initialized");

  ThrowOnFailed(command_queue_->Wait(fence_, value),
    fmt::format("QueueWaitCommand({}) on fence failed", value));
}

void CommandQueue::QueueSignalCommand(const uint64_t value)
{
  DCHECK_NOTNULL_F(command_queue_, "command queue must be valid");
  DCHECK_NOTNULL_F(fence_, "fence must be initialized");
  ThrowOnFailed(command_queue_->Signal(fence_, value),
    fmt::format("QueueSignalCommand({}) on fence failed", value));
}

auto CommandQueue::GetCompletedValue() const -> uint64_t
{
  return fence_->GetCompletedValue();
}

void CommandQueue::Submit(graphics::CommandList& command_list)
{
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-static-cast-downcast)
  const auto* d3d12_command_list = static_cast<CommandList*>(&command_list);
  ID3D12CommandList* command_lists[] = { d3d12_command_list->GetCommandList() };
  command_queue_->ExecuteCommandLists(_countof(command_lists), command_lists);
}

void CommandQueue::Submit(const std::span<graphics::CommandList*> command_lists)
{
  std::vector<ID3D12CommandList*> d3d12_lists;
  d3d12_lists.reserve(command_lists.size());
  for (auto* cl : command_lists) {
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-static-cast-downcast)
    const auto* d3d12_command_list = static_cast<CommandList*>(cl);
    d3d12_lists.push_back(d3d12_command_list->GetCommandList());
  }
  if (!d3d12_lists.empty()) {
    command_queue_->ExecuteCommandLists(
      static_cast<UINT>(d3d12_lists.size()), d3d12_lists.data());
  }
}

void CommandQueue::SetName(const std::string_view name) noexcept
{
  Base::SetName(name);
  NameObject(command_queue_, name);
}

auto CommandQueue::GetQueueRole() const -> QueueRole { return queue_role_; }
