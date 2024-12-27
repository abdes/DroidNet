//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "Oxygen/Renderers/Direct3d12/CommandQueue.h"

#include "Oxygen/Base/Logging.h"
#include "Oxygen/Renderers/Direct3d12/CommandList.h"
#include "Oxygen/Renderers/Direct3d12/DeferredObjectRelease.h"
#include "Oxygen/Renderers/Direct3d12/Detail/dx12_utils.h"
#include "Oxygen/Renderers/Direct3d12/Detail/FenceImpl.h"
#include "Oxygen/Renderers/Direct3d12/Fence.h"
#include "Oxygen/Renderers/Direct3d12/Types.h"

namespace {

  auto GetNameForType(const D3D12_COMMAND_LIST_TYPE list_type) -> std::wstring
  {
    auto list_type_string{ L"" };
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

    return std::wstring{ list_type_string } + L"Command Queue";
  }

}  // namespace

using namespace oxygen::renderer::d3d12;
using detail::DeferredObjectRelease;

void CommandQueue::OnInitialize()
{
  const auto device = GetMainDevice();
  DCHECK_NOTNULL_F(device);

  D3D12_COMMAND_LIST_TYPE d3d12_type;
  switch (GetQueueType())  // NOLINT(clang-diagnostic-switch-enum) - these are the only valid values
  {
  case CommandListType::kGraphics:
    d3d12_type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    break;
  case CommandListType::kCompute:
    d3d12_type = D3D12_COMMAND_LIST_TYPE_COMPUTE;
    break;
  case CommandListType::kCopy:
    d3d12_type = D3D12_COMMAND_LIST_TYPE_COPY;
    break;
  default:
    throw std::runtime_error(fmt::format("Unsupported CommandListType: {}", nostd::to_string(GetQueueType())));
  }

  const D3D12_COMMAND_QUEUE_DESC queue_desc = {
    .Type = d3d12_type,
    .Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL,
    .Flags = D3D12_COMMAND_QUEUE_FLAG_NONE,
    .NodeMask = 0
  };

  ThrowOnFailed(
    device->CreateCommandQueue(&queue_desc, IID_PPV_ARGS(&command_queue_)),
    fmt::format("could not create {} Command Queue", nostd::to_string(GetQueueType())));
  NameObject(command_queue_, GetNameForType(d3d12_type));

  ShouldRelease(true);
}

void CommandQueue::Submit(const CommandListPtr& command_list) {
  const auto d3d12_command_list = dynamic_cast<CommandList*>(command_list.get());
  d3d12_command_list->OnSubmitted();
  ID3D12CommandList* command_lists[] = { d3d12_command_list->GetCommandList() };
  command_queue_->ExecuteCommandLists(_countof(command_lists), command_lists);
  d3d12_command_list->OnExecuted();
}

void CommandQueue::Flush() {
  const auto fence = dynamic_cast<Fence*>(fence_.get());
  CHECK_NOTNULL_F(fence);
  fence->Flush();
}

void CommandQueue::OnRelease()
{
  LOG_F(INFO, "Command Queue released (deferred)");
  DeferredObjectRelease(command_queue_);
}

auto CommandQueue::CreateSynchronizationCounter() -> std::unique_ptr<ISynchronizationCounter>
{
  auto fence_impl = std::make_unique<detail::FenceImpl>(command_queue_);
  return std::unique_ptr<Fence>(new Fence(std::move(fence_impl)));
}