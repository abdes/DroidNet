//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <d3d12.h>

#include <Oxygen/Graphics/Common/CommandQueue.h>
#include <Oxygen/Graphics/Direct3D12/Detail/Types.h>
#include <Oxygen/Graphics/Direct3D12/api_export.h>

namespace oxygen::graphics::d3d12 {

class CommandQueue final : public graphics::CommandQueue {
    using Base = graphics::CommandQueue;

public:
    OXYGEN_D3D12_API CommandQueue(std::string_view name, QueueRole role);

    OXYGEN_D3D12_API ~CommandQueue() noexcept override;
    OXYGEN_MAKE_NON_COPYABLE(CommandQueue);
    OXYGEN_MAKE_NON_MOVABLE(CommandQueue);

    [[nodiscard]] OXYGEN_D3D12_API auto GetQueueRole() const -> QueueRole override;

    OXYGEN_D3D12_API void Signal(uint64_t value) const override;
    [[nodiscard]] OXYGEN_D3D12_API auto Signal() const -> uint64_t override;
    OXYGEN_D3D12_API void Wait(uint64_t value, std::chrono::milliseconds timeout) const override;
    OXYGEN_D3D12_API void Wait(uint64_t value) const override;
    OXYGEN_D3D12_API void QueueSignalCommand(uint64_t value) override;
    OXYGEN_D3D12_API void QueueWaitCommand(uint64_t value) const override;
    [[nodiscard]] OXYGEN_D3D12_API auto GetCompletedValue() const -> uint64_t override;
    [[nodiscard]] OXYGEN_D3D12_API auto GetCurrentValue() const -> uint64_t override;

    OXYGEN_D3D12_API void Submit(graphics::CommandList& command_list) override;

    OXYGEN_D3D12_API void SetName(std::string_view name) noexcept override;

    [[nodiscard]] OXYGEN_D3D12_API auto GetCommandQueue() const -> dx::ICommandQueue*;
    [[nodiscard]] OXYGEN_D3D12_API auto GetFence() const -> dx::IFence*;
};

} // namespace oxygen::graphics::d3d12
