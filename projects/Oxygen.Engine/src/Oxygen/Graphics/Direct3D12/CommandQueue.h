//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <d3d12.h>

#include "Oxygen/Graphics/Common/CommandQueue.h"

namespace oxygen::graphics::d3d12 {

class CommandQueue final : public graphics::CommandQueue {
    using Base = graphics::CommandQueue;

public:
    explicit CommandQueue(const CommandListType type)
        : Base(type)
    {
    }
    ~CommandQueue() override = default;

    OXYGEN_MAKE_NON_COPYABLE(CommandQueue);
    OXYGEN_MAKE_NON_MOVEABLE(CommandQueue);

    void Submit(const CommandListPtr& command_list) override;
    void Submit(const CommandLists& command_list);

    [[nodiscard]] ID3D12CommandQueue* GetCommandQueue() const { return command_queue_; }

protected:
    void InitializeCommandQueue() override;
    void ReleaseCommandQueue() noexcept override;

    [[nodiscard]] auto CreateSynchronizationCounter() -> std::unique_ptr<SynchronizationCounter> override;

private:
    ID3D12CommandQueue* command_queue_ {};
};

} // namespace oxygen::graphics::d3d12
