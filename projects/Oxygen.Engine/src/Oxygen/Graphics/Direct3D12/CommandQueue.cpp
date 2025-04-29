//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <exception>
#include <stdexcept>
#include <string>
#include <string_view>

#include <combaseapi.h>
#include <d3d12.h>
#include <fmt/format.h>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/NoStd.h>
#include <Oxygen/Base/Windows/ComError.h>
#include <Oxygen/Graphics/Common/CommandList.h>
#include <Oxygen/Graphics/Common/Types/Queues.h>
#include <Oxygen/Graphics/Direct3D12/CommandList.h>
#include <Oxygen/Graphics/Direct3D12/CommandQueue.h>
#include <Oxygen/Graphics/Direct3D12/Detail/dx12_utils.h>
#include <Oxygen/Graphics/Direct3D12/Maestro/Fence.h>
#include <Oxygen/Graphics/Direct3D12/Resources/DeferredObjectRelease.h>
#include <memory>

namespace {

auto GetNameForType(const D3D12_COMMAND_LIST_TYPE list_type) -> std::string
{
    auto list_type_string { "" };
    switch (list_type) {
    case D3D12_COMMAND_LIST_TYPE_DIRECT:
        list_type_string = "Graphics ";
        break;
    case D3D12_COMMAND_LIST_TYPE_COMPUTE:
        list_type_string = "Compute ";
        break;
    case D3D12_COMMAND_LIST_TYPE_COPY:
        list_type_string = "Copy ";
        break;
    case D3D12_COMMAND_LIST_TYPE_BUNDLE:
    case D3D12_COMMAND_LIST_TYPE_VIDEO_DECODE:
    case D3D12_COMMAND_LIST_TYPE_VIDEO_PROCESS:
    case D3D12_COMMAND_LIST_TYPE_VIDEO_ENCODE:
    case D3D12_COMMAND_LIST_TYPE_NONE:
        list_type_string = " ";
    }

    return std::string { list_type_string } + "Command Queue";
}

} // namespace

using oxygen::graphics::d3d12::CommandQueue;
using oxygen::graphics::d3d12::detail::GetGraphics;
using oxygen::windows::ThrowOnFailed;

CommandQueue::CommandQueue(QueueRole type, std::string_view name)
    : Base(type, name)
{
    const auto device = GetGraphics().GetCurrentDevice();
    DCHECK_NOTNULL_F(device);

    D3D12_COMMAND_LIST_TYPE d3d12_type;
    switch (GetQueueType()) // NOLINT(clang-diagnostic-switch-enum) - these are the only valid values
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
            fmt::format("Unsupported CommandListType: {}",
                nostd::to_string(GetQueueType())));
    }

    const D3D12_COMMAND_QUEUE_DESC queue_desc = {
        .Type = d3d12_type,
        .Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL,
        .Flags = D3D12_COMMAND_QUEUE_FLAG_NONE,
        .NodeMask = 0
    };

    ThrowOnFailed(
        device->CreateCommandQueue(&queue_desc, IID_PPV_ARGS(&command_queue_)),
        fmt::format("could not create `{}` Command Queue", nostd::to_string(GetQueueType())));
    NameObject(command_queue_, GetNameForType(d3d12_type));
    LOG_F(INFO, "Command queue for `{}` created", nostd::to_string(GetQueueType()));

    try {
        fence_ = std::make_unique<Fence>(command_queue_);
    } catch (const std::exception& ex) {
        LOG_F(ERROR, "Failed to create Fence: {}", ex.what());
        ObjectRelease(command_queue_);
        throw;
    }
}

CommandQueue::~CommandQueue() noexcept
{
    ObjectRelease(command_queue_);
    LOG_F(INFO, "Command queue for `{}` destroyed", nostd::to_string(GetQueueType()));
}

void CommandQueue::SetName(std::string_view name) noexcept
{
    Base::SetName(name);
    NameObject(command_queue_, name);
}

void CommandQueue::Submit(graphics::CommandList& command_list)
{
    auto* d3d12_command_list = static_cast<CommandList*>(&command_list);
    d3d12_command_list->OnSubmitted();
    // TODO: replace with std::array
    ID3D12CommandList* command_lists[] = { d3d12_command_list->GetCommandList() };
    command_queue_->ExecuteCommandLists(_countof(command_lists), command_lists);
    d3d12_command_list->OnExecuted();
}
