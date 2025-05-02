//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <string_view>

#include <d3d12.h>
#include <fmt/format.h>

#include "CommandQueue.h"
#include <Oxygen/Base/NoStd.h>
#include <Oxygen/Base/Windows/ComError.h>
#include <Oxygen/Graphics/Common/CommandList.h>
#include <Oxygen/Graphics/Common/Types/Queues.h>
#include <Oxygen/Graphics/Direct3D12/Detail/SynchronizedCommandQueue.h>
#include <Oxygen/Graphics/Direct3D12/Resources/DeferredObjectRelease.h>

using oxygen::graphics::d3d12::CommandQueue;
using oxygen::graphics::d3d12::detail::GetGraphics;
using oxygen::windows::ThrowOnFailed;

CommandQueue::CommandQueue(std::string_view name, QueueRole type)
    : Base(name)
{
    AddComponent<detail::SynchronizedCommandQueue>(name, type);
}

CommandQueue::~CommandQueue() noexcept = default;

void CommandQueue::SetName(const std::string_view name) noexcept
{
    Base::SetName(name);
    GetComponent<detail::SynchronizedCommandQueue>().SetCommandQueueName(name);
}

void CommandQueue::Submit(graphics::CommandList& command_list)
{
    GetComponent<detail::SynchronizedCommandQueue>().Submit(command_list);
}

auto CommandQueue::GetCommandQueue() const -> dx::ICommandQueue*
{
    return GetComponent<detail::SynchronizedCommandQueue>().GetCommandQueue();
}

auto CommandQueue::GetFence() const -> dx::IFence*
{
    return GetComponent<detail::SynchronizedCommandQueue>().GetFence();
}
void CommandQueue::Signal(uint64_t value) const
{
    GetComponent<detail::SynchronizedCommandQueue>().Signal(value);
}

auto CommandQueue::Signal() const -> uint64_t
{
    return GetComponent<detail::SynchronizedCommandQueue>().Signal();
}

void CommandQueue::Wait(uint64_t value, std::chrono::milliseconds timeout) const
{
    GetComponent<detail::SynchronizedCommandQueue>().Wait(value, timeout);
}

void CommandQueue::Wait(uint64_t value) const
{
    GetComponent<detail::SynchronizedCommandQueue>().Wait(value);
}

void CommandQueue::QueueSignalCommand(uint64_t value)
{
    GetComponent<detail::SynchronizedCommandQueue>().QueueSignalCommand(value);
}

void CommandQueue::QueueWaitCommand(uint64_t value) const
{
    GetComponent<detail::SynchronizedCommandQueue>().QueueWaitCommand(value);
}

auto CommandQueue::GetCompletedValue() const -> uint64_t
{
    return GetComponent<detail::SynchronizedCommandQueue>().GetCompletedValue();
}

auto CommandQueue::GetCurrentValue() const -> uint64_t
{
    return GetComponent<detail::SynchronizedCommandQueue>().GetCurrentValue();
}

auto CommandQueue::GetQueueRole() const -> QueueRole
{
    return GetComponent<detail::SynchronizedCommandQueue>().GetQueueRole();
}
