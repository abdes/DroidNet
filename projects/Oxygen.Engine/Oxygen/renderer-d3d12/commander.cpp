//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "oxygen/renderer-d3d12/Commander.h"

#include <stdexcept>

#include "oxygen/base/logging.h"
#include "oxygen/base/win_errors.h"
#include "oxygen/renderer/types.h"
#include "oxygen/renderer-d3d12/detail/dx12_utils.h"

using namespace oxygen::renderer::direct3d12;
using oxygen::CheckResult;

// Anonymous namespace for object naming helper functions
namespace {
  enum class ObjectType : uint8_t
  {
    kCommandQueue,
    kCommandAllocator,
    kCommandList
  };

  auto GetNameForType(const D3D12_COMMAND_LIST_TYPE list_type, const ObjectType object_type) -> std::wstring
  {
    LPCWSTR object_string{ nullptr };
    switch (object_type) {
    case ObjectType::kCommandQueue:
      object_string = L"Command Queue";
      break;
    case ObjectType::kCommandAllocator:
      object_string = L"Command Allocator";
      break;
    case ObjectType::kCommandList:
      object_string = L"Command List";
      break;
    }

    LPCWSTR list_type_string{ nullptr };
    switch (list_type) {
    case D3D12_COMMAND_LIST_TYPE_DIRECT:
      list_type_string = L"Graphics ";
      break;
    case D3D12_COMMAND_LIST_TYPE_COMPUTE:
      list_type_string = L"Compute ";
      break;
    case D3D12_COMMAND_LIST_TYPE_COPY:
      list_type_string = L"Copy ";
      break;
    case D3D12_COMMAND_LIST_TYPE_BUNDLE:
    case D3D12_COMMAND_LIST_TYPE_VIDEO_DECODE:
    case D3D12_COMMAND_LIST_TYPE_VIDEO_PROCESS:
    case D3D12_COMMAND_LIST_TYPE_VIDEO_ENCODE:
    case D3D12_COMMAND_LIST_TYPE_NONE:
      list_type_string = L" ";
    }

    return std::wstring{ list_type_string } + object_string;
  }

  auto GetIndexNameForType(const D3D12_COMMAND_LIST_TYPE list_type, const ObjectType object_type, const size_t index) -> std::wstring
  {
    return GetNameForType(list_type, object_type) + L" [" + std::to_wstring(index) + L"]";
  }

}  // namespace

// Anonymous namespace for command frame management
namespace {

  struct CommandFrame
  {
    ID3D12CommandAllocator* command_allocator{ nullptr };
    uint64_t fence_value{ 0 };

    void Wait(const HANDLE fence_event, ID3D12Fence1* fence) const
    {
      DCHECK_F(fence && fence_event);

      if (fence->GetCompletedValue() < fence_value) {
        CheckResult(fence->SetEventOnCompletion(fence_value, fence_event));
        WaitForSingleObject(fence_event, INFINITE);
      }
    }

    void Release() noexcept
    {
      ObjectRelease(command_allocator);
      fence_value = 0;
    }
  };

  size_t current_frame_index{ 0 };

}  // namespace

namespace oxygen::renderer::direct3d12 {
  [[nodiscard]] auto CurrentFrameIndex() -> size_t { return current_frame_index; }
}  // namespace oxygen::renderer::direct3d12

// Commander implementation details
namespace oxygen::renderer::direct3d12::detail {

  class CommanderImpl final
  {
  public:
    CommanderImpl(ID3D12Device9* device, D3D12_COMMAND_LIST_TYPE type);
    ~CommanderImpl() { Release(); }

    OXYGEN_MAKE_NON_COPYABLE(CommanderImpl);
    OXYGEN_MAKE_NON_MOVEABLE(CommanderImpl);

    void Release() noexcept;

    [[nodiscard]] auto CommandQueue() const noexcept -> ID3D12CommandQueue* { return command_queue_; }
    [[nodiscard]] auto CommandList() const noexcept -> ID3D12GraphicsCommandList7* { return command_list_; }

    void BeginFrame() const;
    void EndFrame();

    void Flush();

  private:
    bool is_released_{ false };

    ID3D12CommandQueue* command_queue_{ nullptr };
    ID3D12GraphicsCommandList7* command_list_{ nullptr };
    CommandFrame frames_[kFrameBufferCount]{};

    ID3D12Fence1* fence_{ nullptr };
    uint64_t fence_value_{ 0 };
    HANDLE fence_event_{ nullptr };
  };

  CommanderImpl::CommanderImpl(ID3D12Device9* device, const D3D12_COMMAND_LIST_TYPE type)
  {
    CHECK_NOTNULL_F(device);

    const D3D12_COMMAND_QUEUE_DESC queue_desc = {
      .Type = type,
      .Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL,
      .Flags = D3D12_COMMAND_QUEUE_FLAG_NONE,
      .NodeMask = 0,
    };
    try {
      CheckResult(device->CreateCommandQueue(&queue_desc, IID_PPV_ARGS(&command_queue_)));
      NameObject(command_queue_, GetNameForType(type, ObjectType::kCommandQueue));

      for (size_t index = 0; index < oxygen::kFrameBufferCount; ++index) {
        auto& command_allocator = frames_[index].command_allocator;
        CheckResult(device->CreateCommandAllocator(type, IID_PPV_ARGS(&command_allocator)));
        NameObject(command_allocator, GetIndexNameForType(type, ObjectType::kCommandAllocator, index));
      }

      CheckResult(device->CreateCommandList(
        0,
        type,
        frames_[0].command_allocator,
        nullptr,
        IID_PPV_ARGS(&command_list_)));
      NameObject(command_list_, GetNameForType(type, ObjectType::kCommandList));

      CheckResult(device->CreateFence(
        0,
        D3D12_FENCE_FLAG_NONE,
        IID_PPV_ARGS(&fence_)));
      NameObject(fence_, L"D3d12 Fence");

      fence_event_ = CreateEventEx(nullptr, nullptr, 0, EVENT_ALL_ACCESS);
      DCHECK_NOTNULL_F(fence_event_);
    }
    catch (const std::runtime_error& e) {
      LOG_F(ERROR, "Command queue creation failed: {}", e.what());
      Release();
      throw;
    }
  }

  void CommanderImpl::Release() noexcept
  {
    if (is_released_) return;

    Flush();

    ObjectRelease(command_queue_);
    ObjectRelease(command_list_);
    ObjectRelease(fence_);
    fence_value_ = 0;
    if (fence_event_) {
      CloseHandle(fence_event_);
      fence_event_ = nullptr;
    }
    for (auto& frame : frames_) {
      frame.Release();
    }

    is_released_ = true;
  }

  void CommanderImpl::BeginFrame() const
  {
    static bool first_time{ true };

    const auto& frame = frames_[current_frame_index];
    frame.Wait(fence_event_, fence_);
    if (!first_time) {
      CheckResult(frame.command_allocator->Reset());
      CheckResult(command_list_->Reset(frame.command_allocator, nullptr));
    }
    first_time = false;
  }

  void CommanderImpl::EndFrame()
  {
    CheckResult(command_list_->Close());
    ID3D12CommandList* command_lists[]{ command_list_ };
    command_queue_->ExecuteCommandLists(_countof(command_lists), command_lists);

    const CommandFrame& frame = frames_[current_frame_index];
    const uint64_t fence_value{ fence_value_ };
    ++fence_value_;
    CheckResult(command_queue_->Signal(fence_, fence_value));

    current_frame_index = (current_frame_index + 1) % oxygen::kFrameBufferCount;
  }

  void CommanderImpl::Flush()
  {
    for (auto& frame : frames_) frame.Wait(fence_event_, fence_);
    current_frame_index = 0;
  }

}  // namespace

namespace oxygen::renderer::direct3d12 {

  Commander::Commander(ID3D12Device9* device, D3D12_COMMAND_LIST_TYPE type)
    : pimpl_(std::make_unique<detail::CommanderImpl>(device, type))
  {
  }

  Commander::~Commander() = default;

  void Commander::Release() const noexcept
  {
    pimpl_->Release();
  }

  ID3D12CommandQueue* Commander::CommandQueue() const noexcept
  {
    return pimpl_->CommandQueue();
  }

  ID3D12GraphicsCommandList7* Commander::CommandList() const noexcept
  {
    return pimpl_->CommandList();
  }

  // ReSharper disable once CppMemberFunctionMayBeStatic
  auto Commander::CurrentFrameIndex() const noexcept -> size_t
  {
    return current_frame_index;
  }

  void Commander::BeginFrame() const
  {
    pimpl_->BeginFrame();
  }

  void Commander::EndFrame() const
  {
    pimpl_->EndFrame();
  }

  void Commander::Flush() const
  {
    pimpl_->Flush();
  }

}  // namespace oxygen::renderer::direct3d12
