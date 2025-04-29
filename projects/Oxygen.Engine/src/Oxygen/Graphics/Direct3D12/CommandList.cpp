//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Graphics/Direct3D12/CommandList.h>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/NoStd.h>
#include <Oxygen/Base/Windows/ComError.h>
#include <Oxygen/Graphics/Common/DeferredObjectRelease.h>
#include <Oxygen/Graphics/Direct3D12/Detail/dx12_utils.h>
#include <Oxygen/Graphics/Direct3D12/Graphics.h>
#include <Oxygen/Graphics/Direct3D12/Renderer.h>

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

    return std::string { list_type_string };
}

} // namespace

using oxygen::graphics::d3d12::CommandList;
using oxygen::graphics::d3d12::detail::GetGraphics;
using oxygen::windows::ThrowOnFailed;

void CommandList::ReleaseCommandList() noexcept
{
    // TODO: user of command list must ensure the command list is no longer used
    if (command_allocator_ != nullptr) {
        command_allocator_->Release();
        command_allocator_ = nullptr;
    }
    if (command_list_ != nullptr) {
        command_list_->Release();
        command_list_ = nullptr;
    }
}

CommandList::CommandList(QueueRole type, std::string_view name)
    : Base(type, name)
{
    // TODO: consider if we want to reuse command list objects

    D3D12_COMMAND_LIST_TYPE d3d12_type;

    switch (type) // NOLINT(clang-diagnostic-switch-enum) - these are the only valid values
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
        throw std::runtime_error(fmt::format("Unsupported CommandListType: {}", nostd::to_string(type)));
    }

    const auto device = GetGraphics().GetCurrentDevice();
    DCHECK_NOTNULL_F(device);

    try {
        ThrowOnFailed(
            device->CreateCommandAllocator(d3d12_type, IID_PPV_ARGS(&command_allocator_)),
            fmt::format("could not create {} Command Allocator", nostd::to_string(type)));
        NameObject(command_allocator_, GetNameForType(d3d12_type) + "Command Allocator");

        ThrowOnFailed(
            device->CreateCommandList(0, d3d12_type, command_allocator_, nullptr, IID_PPV_ARGS(&command_list_)),
            fmt::format("could not create {} Command List", nostd::to_string(type)));
        NameObject(command_list_, GetNameForType(d3d12_type) + "Command List");

        ThrowOnFailed(command_list_->Close(), "could not close command list after it was created");
    } catch (const std::exception& e) {
        LOG_F(ERROR, "Failed to create CommandList: {}", e.what());
        ReleaseCommandList();
        throw;
    }

    state_ = State::kFree;

    NameObject(command_list_, name);
}

CommandList::~CommandList()
{
    ReleaseCommandList();
}

void CommandList::SetName(std::string_view name) noexcept
{
    Base::SetName(name);
    NameObject(command_list_, name);
}

void CommandList::OnBeginRecording()
{
    if (state_ != State::kFree) {
        throw std::runtime_error("CommandList is not in a Free state");
    }
    ThrowOnFailed(command_allocator_->Reset(), "could not reset the command allocator");
    ThrowOnFailed(command_list_->Reset(command_allocator_, nullptr), "could not reset the command list");
    state_ = State::kRecording;
}

void CommandList::OnEndRecording()
{
    if (state_ != State::kRecording) {
        throw std::runtime_error("CommandList is not in a Recording state");
    }
    ThrowOnFailed(command_list_->Close(), "could not close the command list");
    state_ = State::kRecorded;
}

void CommandList::OnSubmitted()
{
    if (state_ != State::kRecorded) {
        throw std::runtime_error("CommandList is not in a Recorded state");
    }
    state_ = State::kExecuting;
}

void CommandList::OnExecuted()
{
    if (state_ != State::kExecuting) {
        throw std::runtime_error("CommandList is not in an Executing state");
    }
    state_ = State::kFree;
}
