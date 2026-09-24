//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <atomic>
#include <limits>
#include <string_view>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/NoStd.h>
#include <Oxygen/Base/Windows/ComError.h>
#include <Oxygen/Base/Windows/Exceptions.h>
#include <Oxygen/Graphics/Common/CommandList.h>
#include <Oxygen/Graphics/Common/Internal/QueueSubmission.h>
#include <Oxygen/Graphics/Common/ObjectRelease.h>
#include <Oxygen/Graphics/Common/Types/QueueRole.h>
#include <Oxygen/Graphics/Direct3D12/CommandList.h>
#include <Oxygen/Graphics/Direct3D12/CommandQueue.h>
#include <Oxygen/Graphics/Direct3D12/Detail/dx12_utils.h>
#include <Oxygen/Graphics/Direct3D12/Devices/DebugLayer.h>
#include <Oxygen/Graphics/Direct3D12/Graphics.h>
#include <Oxygen/Profiling/CpuProfileScope.h>
#include <Oxygen/Tracy/D3D12.h>

using oxygen::graphics::d3d12::CommandQueue;
using oxygen::windows::ThrowOnFailed;

struct CommandQueue::PreparedSubmission final
  : graphics::internal::NativeSubmission {
  struct Group {
    size_t first;
    size_t count;
    std::vector<graphics::CommandList::SubmitQueueAction> actions;
  };
  struct Dependency {
    Microsoft::WRL::ComPtr<ID3D12Fence> fence;
    uint64_t value;
  };
  CommandQueue* owner;
  std::vector<ID3D12CommandList*> lists;
  std::vector<Group> groups;
  std::vector<Dependency> dependencies;
  std::optional<uint64_t> private_marker;
  std::optional<uint64_t> legacy_marker;
  bool fail_before_private_marker { false };
  bool fail_after_first_list { false };

  explicit PreparedSubmission(CommandQueue& queue)
    : owner(&queue)
  {
  }
  auto Execute(graphics::internal::NativeSubmissionProgress& progress)
    -> void override
  {
    for (const auto& dependency : dependencies) {
      ThrowOnFailed(
        owner->command_queue_->Wait(dependency.fence.Get(), dependency.value),
        "queue wait for producer completion");
    }
    for (const auto& group : groups) {
      for (const auto& action : group.actions) {
        if (action.kind
          == graphics::CommandList::SubmitQueueActionKind::kWait) {
          owner->QueueWaitImmediate(action.value);
        }
      }
      owner->command_queue_->ExecuteCommandLists(
        static_cast<UINT>(group.count), lists.data() + group.first);
      progress.issued_lists += group.count;
      if (fail_after_first_list) {
        throw graphics::SubmissionException(
          { graphics::SubmissionOutcome::kExecutionUncertain, {} });
      }
      for (const auto& action : group.actions) {
        if (action.kind
          == graphics::CommandList::SubmitQueueActionKind::kSignal) {
          owner->SignalImmediate(action.value);
          progress.last_legacy_signal = action.value;
        }
      }
    }
    if (legacy_marker) {
      owner->SignalImmediate(*legacy_marker);
      progress.last_legacy_signal = *legacy_marker;
    }
    if (fail_before_private_marker) {
      throw graphics::SubmissionException(
        { graphics::SubmissionOutcome::kExecutionUncertain, {} });
    }
    if (private_marker) {
      ThrowOnFailed(owner->command_queue_->Signal(
                      owner->private_fence_.Get(), *private_marker),
        "signal private submission completion");
      progress.private_marker_emitted = true;
    }
  }
};

auto CommandQueue::PrepareNativeSubmission(
  const graphics::internal::NativeSubmissionRequest& request,
  std::unique_ptr<graphics::internal::NativeSubmission> reusable)
  -> std::unique_ptr<graphics::internal::NativeSubmission>
{
  auto prepared = reusable
    ? std::unique_ptr<PreparedSubmission>(
        static_cast<PreparedSubmission*>(reusable.release()))
    : std::make_unique<PreparedSubmission>(*this);
  prepared->owner = this;
  prepared->lists.clear();
  prepared->groups.clear();
  prepared->dependencies.clear();
  prepared->lists.reserve(request.lists.size());
  prepared->groups.reserve(request.lists.size());
  prepared->dependencies.reserve(request.dependencies.size());
  prepared->private_marker = request.private_marker;
  prepared->legacy_marker = request.legacy_marker;
  prepared->fail_before_private_marker = request.fail_before_private_marker;
  prepared->fail_after_first_list = request.fail_after_first_list;
  for (const auto& dependency : request.dependencies) {
    // Backend identity was validated before native preparation.
    const auto& producer
      = static_cast<const CommandQueue&>(*dependency.producer);
    prepared->dependencies.push_back(
      { producer.private_fence_, dependency.receipt.Value() });
  }
  for (const auto& list : request.lists) {
    auto* native = static_cast<CommandList*>(list.get());
    const auto actions = list->SubmitActions();
    const auto first = prepared->lists.size();
    prepared->lists.push_back(native->GetCommandList());
    if (!request.fail_after_first_list && actions.empty()
      && !prepared->groups.empty() && prepared->groups.back().actions.empty()) {
      ++prepared->groups.back().count;
    } else {
      prepared->groups.push_back(
        { first, 1, { actions.begin(), actions.end() } });
    }
  }
  return prepared;
}
auto CommandQueue::QueryPrivateCompletion() const noexcept -> uint64_t
{
  return private_fence_->GetCompletedValue();
}
auto CommandQueue::WaitPrivateCompletion(uint64_t value) const -> void
{
  const auto completed = private_fence_->GetCompletedValue();
  if (completed == (std::numeric_limits<uint64_t>::max)()) {
    throw std::runtime_error(
      "Device lost while waiting for submission completion");
  }
  if (completed < value) {
    ThrowOnFailed(private_fence_->SetEventOnCompletion(value, private_event_),
      "wait for private completion fence");
    if (WaitForSingleObject(private_event_, INFINITE) != WAIT_OBJECT_0
      || private_fence_->GetCompletedValue()
        == (std::numeric_limits<uint64_t>::max)()) {
      throw std::runtime_error("Private completion wait failed");
    }
  }
}
auto CommandQueue::TearDownUncertainDevice() noexcept -> void
{
  Microsoft::WRL::ComPtr<ID3D12Device5> device;
  if (SUCCEEDED(device_.As(&device))) {
    device->RemoveDevice();
  }
}

namespace {

using oxygen::graphics::d3d12::CommandList;
auto ReportDeviceRemovalIfPresent(
  oxygen::graphics::d3d12::dx::IDevice* device) noexcept -> void
{
  static std::atomic_bool reported { false };
  if (device == nullptr) {
    return;
  }

  const HRESULT reason = device->GetDeviceRemovedReason();
  if (SUCCEEDED(reason)) {
    return;
  }
  bool expected = false;
  if (!reported.compare_exchange_strong(expected, true)) {
    return;
  }

  LOG_F(ERROR, "D3D12 device removed while waiting for GPU work: {}",
    oxygen::windows::ComError {
      static_cast<oxygen::windows::ComErrorEnum>(reason) }
      .what());
  oxygen::graphics::d3d12::DebugLayer::NotifyDeviceRemoved();
  oxygen::graphics::d3d12::DebugLayer::PrintDredReport(device);
}
} // namespace

CommandQueue::CommandQueue(
  std::string_view name, QueueRole role, const Graphics* gfx)
  : Base(name)
  , queue_role_(role)
  , native_lifetime_(gfx->GetNativeLifetimeToken())
  , device_(gfx->GetCurrentDevice())
{
  DCHECK_NOTNULL_F(device_, "Graphics device cannot be null!");

  try {
    CreateCommandQueue(role, name);
    LOG_F(INFO, "D3D12 Command queue [name=`{}`, role=`{}`] created", name,
      nostd::to_string(role));

    const auto fence_name = fmt::format("Fence ({})", name);
    CreateFence(fence_name, 0ULL);
    ThrowOnFailed(CurrentDevice()->CreateFence(0, D3D12_FENCE_FLAG_NONE,
                    IID_PPV_ARGS(private_fence_.GetAddressOf())),
      "create private completion fence");
    private_event_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (!private_event_) {
      windows::WindowsException::ThrowFromLastError();
    }
    LOG_F(INFO, "D3D12 Fence [name=`{}`] created", fence_name);

#if defined(OXYGEN_WITH_TRACY)
    tracy_context_
      = oxygen::tracy::d3d12::CreateContext(CurrentDevice(), command_queue_);
    if (tracy_context_ != nullptr) {
      oxygen::tracy::d3d12::NameContext(tracy_context_, name);
    }
#endif
  } catch (...) {
    if (private_event_) {
      CloseHandle(private_event_);
      private_event_ = nullptr;
    }
    ReleaseFence();
    ReleaseCommandQueue();
    throw;
  }
}

CommandQueue::~CommandQueue() noexcept
{
  if (command_queue_ == nullptr) {
    return;
  }
  DCHECK_NOTNULL_F(fence_);

  // Flush the command queue to ensure all commands are completed before
  // destruction.
  // Close already drained or stopped this incarnation. Late CPU owner release
  // must not start another native drain on the releasing thread.
  const auto lifetime = BackendLifetimeState();
  if (!lifetime || lifetime->State() == graphics::BackendLifecycle::kActive) {
    try {
      Flush();
    } catch (...) {
      TearDownUncertainDevice();
    }
  }
  ReleaseUsesAfterDeviceLoss();

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

  if (private_event_) {
    CloseHandle(private_event_);
    private_event_ = nullptr;
  }
  private_fence_.Reset();
  ReleaseFence();
  LOG_F(INFO, "D3D12 Fence [name=`Fence ({})`] destroyed", queue_name);

  ReleaseCommandQueue();
  LOG_F(INFO, "D3D12 Command Queue [name=`{}`] destroyed", queue_name);
}

auto CommandQueue::CurrentDevice() const -> dx::IDevice*
{
  return device_.Get();
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
  std::lock_guard lock(timeline_mutex_);
  if (value <= current_value_
    || value == (std::numeric_limits<uint64_t>::max)()) {
    throw std::invalid_argument("Invalid queue signal reservation");
  }
  current_value_ = value;
}
auto CommandQueue::Signal() const -> uint64_t
{
  std::lock_guard lock(timeline_mutex_);
  if (current_value_ >= (std::numeric_limits<uint64_t>::max)() - 1) {
    throw std::overflow_error("Queue timeline exhausted");
  }
  return ++current_value_;
}
void CommandQueue::SignalImmediate(const uint64_t value) const
{
  std::lock_guard lock(timeline_mutex_);
  if (value <= last_signaled_value_) {
    throw std::invalid_argument("Queue signal went backwards");
  }
  current_value_ = std::max(current_value_, value);
  ThrowOnFailed(
    command_queue_->Signal(fence_, value), "Queue completion signal failed");
  last_signaled_value_ = value;
}
void CommandQueue::QueueWaitImmediate(const uint64_t value) const
{
  ThrowOnFailed(command_queue_->Wait(fence_, value), "Queue wait failed");
}

void CommandQueue::Wait(
  const uint64_t value, const std::chrono::milliseconds timeout) const
{
  oxygen::profiling::CpuProfileScope cpu_scope(
    "D3D12.FenceWait", oxygen::profiling::ProfileCategory::kSynchronization);
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
  if (completed_value == (std::numeric_limits<uint64_t>::max)()) {
    ReportDeviceRemovalIfPresent(CurrentDevice());
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
  if (GetCompletedValue() == (std::numeric_limits<uint64_t>::max)()) {
    ReportDeviceRemovalIfPresent(CurrentDevice());
  }
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
    oxygen::tracy::d3d12::CollectContext(tracy_context_);
  }
#endif
}

void CommandQueue::SetName(const std::string_view name) noexcept
{
  Base::SetName(name);
  NameObject(command_queue_, name);
}

auto CommandQueue::GetQueueRole() const -> QueueRole { return queue_role_; }
