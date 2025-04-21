//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <chrono>
#include <cstdint>
#include <span>
#include <string_view>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Composition/Composition.h>
#include <Oxygen/Composition/Named.h>
#include <Oxygen/Composition/ObjectMetaData.h>
#include <Oxygen/Graphics/Common/SynchronizationCounter.h>
#include <Oxygen/Graphics/Common/Types/Queues.h>

namespace oxygen::graphics {

class CommandList;

class CommandQueue : public Composition, public Named {
public:
    explicit CommandQueue(QueueRole type)
        : CommandQueue(type, "Command Queue")
    {
    }

    CommandQueue(QueueRole type, std::string_view name)
        : type_(type)
    {
        AddComponent<ObjectMetaData>(name);
    }

    ~CommandQueue() override = default;

    OXYGEN_MAKE_NON_COPYABLE(CommandQueue);
    OXYGEN_MAKE_NON_MOVABLE(CommandQueue);

    virtual void Submit(CommandList& command_list) = 0;

    virtual void Flush() { Wait(GetSynchronizationCounter().GetCurrentValue()); }

    [[nodiscard]] virtual auto GetQueueType() const -> QueueRole { return type_; }

    virtual void Signal(const uint64_t value)
    {
        GetSynchronizationCounter().Signal(value);
    }
    [[nodiscard]] uint64_t Signal() const
    {
        return GetSynchronizationCounter().Signal();
    }
    void Wait(const uint64_t value, const std::chrono::milliseconds timeout) const
    {
        GetSynchronizationCounter().Wait(value, timeout);
    }
    void Wait(const uint64_t value) const
    {
        GetSynchronizationCounter().Wait(value);
    }
    void QueueWaitCommand(const uint64_t value) const
    {
        GetSynchronizationCounter().QueueWaitCommand(value);
    }
    void QueueSignalCommand(const uint64_t value) const
    {
        GetSynchronizationCounter().QueueSignalCommand(value);
    }

    [[nodiscard]] auto GetName() const noexcept -> std::string_view override
    {
        return GetComponent<ObjectMetaData>().GetName();
    }

    void SetName(std::string_view name) noexcept override
    {
        GetComponent<ObjectMetaData>().SetName(name);
    }

protected:
    virtual auto GetSynchronizationCounter() const -> SynchronizationCounter& = 0;

private:
    QueueRole type_ { QueueRole::kNone };
};

} // namespace oxygen::graphics
