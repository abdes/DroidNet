//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <d3d12.h>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Composition/ComponentMacros.h>
#include <Oxygen/Composition/Composition.h>
#include <Oxygen/Graphics/Common/Types/Queues.h>
#include <Oxygen/Graphics/Direct3D12/Detail/Types.h>

namespace oxygen::graphics {

class CommandList;

namespace d3d12::detail {

    class SynchronizedCommandQueue final : public Component {
        OXYGEN_COMPONENT(SynchronizedCommandQueue)

    public:
        SynchronizedCommandQueue(std::string_view name, QueueRole role);
        ~SynchronizedCommandQueue() noexcept override;

        OXYGEN_MAKE_NON_COPYABLE(SynchronizedCommandQueue)
        OXYGEN_MAKE_NON_MOVABLE(SynchronizedCommandQueue)

        void Signal(uint64_t value) const;
        [[nodiscard]] auto Signal() const -> uint64_t;
        void Wait(uint64_t value, std::chrono::milliseconds timeout) const;
        void Wait(uint64_t value) const;
        void QueueSignalCommand(uint64_t value);
        void QueueWaitCommand(uint64_t value) const;
        [[nodiscard]] auto GetCompletedValue() const -> uint64_t;
        [[nodiscard]] auto GetCurrentValue() const -> uint64_t { return current_value_; }

        void Submit(graphics::CommandList& command_list) const;

        void SetCommandQueueName(std::string_view name) const noexcept;

        [[nodiscard]] auto GetQueueRole() const -> QueueRole;
        [[nodiscard]] auto GetCommandQueue() const -> dx::ICommandQueue* { return command_queue_; }
        [[nodiscard]] auto GetFence() const -> dx::IFence* { return fence_; }

    private:
        void CreateCommandQueue(QueueRole type, std::string_view queue_name);
        void CreateFence(std::string_view fence_name, uint64_t initial_value);
        void ReleaseCommandQueue() noexcept;
        void ReleaseFence() noexcept;

        QueueRole queue_role_; //<! The cached role of the command queue.
        dx::ICommandQueue* command_queue_ { nullptr };

        dx::IFence* fence_ { nullptr };
        mutable uint64_t current_value_ { 0 };
        HANDLE fence_event_ { nullptr };
    };

} // namespace d3d12::detail

} // namespace oxygen::graphics
