//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "oxygen/renderer-d3d12/command.h"

using namespace oxygen::renderer::direct3d12;

namespace {
  enum class ObjectType
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

Command::Command(ID3D12Device9* device, const D3D12_COMMAND_LIST_TYPE type)
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

    for (size_t index = 0; index < kFrameBufferCount; ++index) {
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

Command::~Command()
{
  Release();
}
