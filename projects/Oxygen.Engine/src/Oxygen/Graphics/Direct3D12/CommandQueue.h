//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <d3d12.h>

#include <Oxygen/Graphics/Common/CommandQueue.h>
#include <memory>

namespace oxygen::graphics::d3d12 {

class CommandQueue final : public graphics::CommandQueue {
    using Base = graphics::CommandQueue;

public:
    CommandQueue(QueueRole type)
        : CommandQueue(type, "Command List")
    {
    }

    CommandQueue(QueueRole type, std::string_view name);

    ~CommandQueue() noexcept override;
    OXYGEN_MAKE_NON_COPYABLE(CommandQueue);
    OXYGEN_MAKE_NON_MOVABLE(CommandQueue);

    void Submit(graphics::CommandList& command_list) override;

    [[nodiscard]] ID3D12CommandQueue* GetCommandQueue() const { return command_queue_; }

    void SetName(std::string_view name) noexcept override;

protected:
    auto GetSynchronizationCounter() const -> SynchronizationCounter& override { return *fence_; }

private:
    void ReleaseCommandQueue() noexcept;

    ID3D12CommandQueue* command_queue_ {};
    std::unique_ptr<SynchronizationCounter> fence_ {};
};

} // namespace oxygen::graphics::d3d12
