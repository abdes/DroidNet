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
#include <Oxygen/Base/Windows/Exceptions.h>
#include <Oxygen/Graphics/Common/CommandList.h>
#include <Oxygen/Graphics/Common/ObjectRelease.h>
#include <Oxygen/Graphics/Common/Types/QueueRole.h>
#include <Oxygen/Graphics/Direct3D12/CommandList.h>
#include <Oxygen/Graphics/Direct3D12/CommandQueue.h>
#include <Oxygen/Graphics/Direct3D12/Detail/dx12_utils.h>
#include <Oxygen/Graphics/Direct3D12/Graphics.h>

#include <Oxygen/Tracy/D3D12.h>

using oxygen::graphics::d3d12::CommandQueue;
using oxygen::windows::ThrowOnFailed;

namespace {

using oxygen::graphics::d3d12::CommandList;
using QueueStateEntry = oxygen::graphics::CommandQueue::KnownResourceState;

auto ToKnownStates(
  std::vector<oxygen::graphics::CommandList::RecordedResourceState>&& states)
  -> std::vector<QueueStateEntry>
{
  auto known_states = std::vector<QueueStateEntry> {};
  known_states.reserve(states.size());
  for (const auto& state : states) {
    known_states.push_back(
      { .resource = state.resource, .state = state.state });
  }
  return known_states;
}
} // namespace

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

#if defined(OXYGEN_WITH_TRACY)
  tracy_context_
    = oxygen::tracy::d3d12::CreateContext(CurrentDevice(), command_queue_);
  if (tracy_context_ != nullptr) {
    oxygen::tracy::d3d12::NameContext(tracy_context_, name);
  }
#endif
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

#if defined(OXYGEN_WITH_TRACY)
  if (tracy_context_ != nullptr) {
    oxygen::tracy::d3d12::AdvanceContextFrame(tracy_context_);
    oxygen::tracy::d3d12::CollectContext(tracy_context_);
    oxygen::tracy::d3d12::DestroyContext(tracy_context_);
    tracy_context_ = nullptr;
  }
#endif

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
  last_signaled_value_ = initial_value;
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
  current_value_ = value;
}

auto CommandQueue::Signal() const -> uint64_t
{
  ++current_value_;
  return current_value_;
}

void CommandQueue::SignalImmediate(const uint64_t value) const
{
  DCHECK_NOTNULL_F(fence_, "fence must be initialized");
  DCHECK_NOTNULL_F(command_queue_, "command queue must be valid");
  if (value <= last_signaled_value_) {
    DLOG_F(WARNING,
      "Immediate signal value {} must be greater than the last signaled value "
      "{}",
      value, last_signaled_value_);
    throw std::invalid_argument(
      "Immediate signal value must be greater than the last signaled value");
  }
  if (value > current_value_) {
    current_value_ = value;
  }

  DLOG_F(1,
    "CommandQueue[{}]::SignalImmediate({} / current={} completed={} "
    "last_signaled={})",
    GetName(), value, GetCurrentValue(), GetCompletedValue(),
    last_signaled_value_);
  ThrowOnFailed((command_queue_->Signal(fence_, value)),
    fmt::format("SignalImmediate({}) on fence failed", value));
  last_signaled_value_ = value;
}

void CommandQueue::QueueWaitImmediate(const uint64_t value) const
{
  DCHECK_NOTNULL_F(fence_, "fence must be initialized");
  DCHECK_NOTNULL_F(command_queue_, "command queue must be valid");
  DLOG_F(1, "CommandQueue[{}]::QueueWaitImmediate({})", GetName(), value);
  ThrowOnFailed(command_queue_->Wait(fence_, value),
    fmt::format("QueueWaitImmediate({}) on fence failed", value));
}

void CommandQueue::Wait(
  const uint64_t value, const std::chrono::milliseconds timeout) const
{
  DCHECK_F(timeout.count() <= (std::numeric_limits<DWORD>::max)(),
    "timeout value must fit in a DWORD");
  auto completed_value = fence_->GetCompletedValue();
  DLOG_F(2, "CommandQueue[{}]::Wait({} / current={})", GetName(), value,
    GetCurrentValue());
  if (completed_value < value) {
    ThrowOnFailed(fence_->SetEventOnCompletion(value, fence_event_),
      fmt::format("Wait({}) on fence failed", value));
    const auto wait_result
      = WaitForSingleObject(fence_event_, static_cast<DWORD>(timeout.count()));
    switch (wait_result) {
    case WAIT_OBJECT_0:
      completed_value = fence_->GetCompletedValue();
      break;
    case WAIT_TIMEOUT:
      throw std::runtime_error(
        fmt::format("Wait({}) timed out after {} ms", value, timeout.count()));
    case WAIT_FAILED:
      windows::WindowsException::ThrowFromLastError();
    default:
      throw std::runtime_error(fmt::format(
        "Wait({}) returned unexpected result {}", value, wait_result));
    }
    DLOG_F(2, "CommandQueue[{}] reached {}", GetName(), value);
  }
  DLOG_F(2, "CommandQueue[{}] at completed value: {} (current={})", GetName(),
    completed_value, GetCurrentValue());
}

void CommandQueue::Wait(const uint64_t value) const
{
  Wait(value, std::chrono::milliseconds((std::numeric_limits<DWORD>::max)()));
}

auto CommandQueue::GetCompletedValue() const -> uint64_t
{
  return fence_->GetCompletedValue();
}

auto CommandQueue::TryGetTimestampFrequency(uint64_t& out_hz) const -> bool
{
  DCHECK_NOTNULL_F(command_queue_, "command queue must be valid");

  UINT64 frequency_hz = 0U;
  if (FAILED(command_queue_->GetTimestampFrequency(&frequency_hz))
    || frequency_hz == 0U) {
    return false;
  }

  out_hz = frequency_hz;
  return true;
}

auto CommandQueue::Flush() const -> void
{
  Base::Flush();
#if defined(OXYGEN_WITH_TRACY)
  if (tracy_context_ != nullptr) {
    oxygen::tracy::d3d12::CollectContext(tracy_context_);
  }
#endif
}

auto CommandQueue::BeginProfilingFrame() const -> void
{
#if defined(OXYGEN_WITH_TRACY)
  if (tracy_context_ != nullptr) {
    oxygen::tracy::d3d12::AdvanceContextFrame(tracy_context_);
  }
#endif
}

void CommandQueue::Submit(std::shared_ptr<graphics::CommandList> command_list)
{
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-static-cast-downcast)
  auto* d3d12_command_list = static_cast<CommandList*>(command_list.get());
  const auto submit_actions = d3d12_command_list->TakeSubmitQueueActions();
  for (const auto& action : submit_actions) {
    if (action.kind == graphics::CommandList::SubmitQueueActionKind::kWait) {
      QueueWaitImmediate(action.value);
    }
  }

  ID3D12CommandList* d3d12_lists[] = { d3d12_command_list->GetCommandList() };
  command_queue_->ExecuteCommandLists(_countof(d3d12_lists), d3d12_lists);

  for (const auto& action : submit_actions) {
    if (action.kind == graphics::CommandList::SubmitQueueActionKind::kSignal) {
      SignalImmediate(action.value);
    }
  }

  const auto known_states
    = ToKnownStates(command_list->TakeRecordedResourceStates());
  AdoptKnownResourceStates(known_states);
}

void CommandQueue::Submit(
  const std::span<std::shared_ptr<graphics::CommandList>> command_lists)
{
  std::vector<ID3D12CommandList*> d3d12_lists;
  std::vector<std::shared_ptr<graphics::CommandList>> pending_lists;
  d3d12_lists.reserve(command_lists.size());
  pending_lists.reserve(command_lists.size());
  for (const auto& cl : command_lists) {
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-static-cast-downcast)
    auto* d3d12_command_list = static_cast<CommandList*>(cl.get());
    if (d3d12_command_list->HasSubmitQueueActions()) {
      if (!d3d12_lists.empty()) {
        command_queue_->ExecuteCommandLists(
          static_cast<UINT>(d3d12_lists.size()), d3d12_lists.data());
        for (auto& pending : pending_lists) {
          const auto known_states
            = ToKnownStates(pending->TakeRecordedResourceStates());
          AdoptKnownResourceStates(known_states);
        }
        d3d12_lists.clear();
        pending_lists.clear();
      }
      const auto submit_actions = d3d12_command_list->TakeSubmitQueueActions();
      for (const auto& action : submit_actions) {
        if (action.kind
          == graphics::CommandList::SubmitQueueActionKind::kWait) {
          QueueWaitImmediate(action.value);
        }
      }

      ID3D12CommandList* d3d12_list[]
        = { d3d12_command_list->GetCommandList() };
      command_queue_->ExecuteCommandLists(_countof(d3d12_list), d3d12_list);

      for (const auto& action : submit_actions) {
        if (action.kind
          == graphics::CommandList::SubmitQueueActionKind::kSignal) {
          SignalImmediate(action.value);
        }
      }
      const auto known_states = ToKnownStates(cl->TakeRecordedResourceStates());
      AdoptKnownResourceStates(known_states);
      continue;
    }
    d3d12_lists.push_back(d3d12_command_list->GetCommandList());
    pending_lists.push_back(cl);
  }
  if (!d3d12_lists.empty()) {
    command_queue_->ExecuteCommandLists(
      static_cast<UINT>(d3d12_lists.size()), d3d12_lists.data());
    for (auto& pending : pending_lists) {
      const auto known_states
        = ToKnownStates(pending->TakeRecordedResourceStates());
      AdoptKnownResourceStates(known_states);
    }
  }
}

void CommandQueue::SetName(const std::string_view name) noexcept
{
  Base::SetName(name);
  NameObject(command_queue_, name);
}

auto CommandQueue::GetQueueRole() const -> QueueRole { return queue_role_; }
