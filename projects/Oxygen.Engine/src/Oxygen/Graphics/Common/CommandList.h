//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <string_view>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/Macros.h>
#include <Oxygen/Composition/Composition.h>
#include <Oxygen/Composition/Named.h>
#include <Oxygen/Composition/ObjectMetaData.h>
#include <Oxygen/Graphics/Common/Types/Queues.h>
#include <Oxygen/Graphics/Common/api_export.h>

namespace oxygen::graphics {

class CommandRecorder;
class Renderer;

class CommandList : public Composition, public Named {
public:
    explicit CommandList(QueueRole type)
        : CommandList(type, "Command List")
    {
    }

    CommandList(QueueRole type, std::string_view name)
        : type_(type)
        , state_(State::kFree)
    {
        AddComponent<ObjectMetaData>(name);
        DLOG_F(INFO, "CommandList created: {}", name);
    }

    ~CommandList() override
    {
        DLOG_F(INFO, "CommandList destroyed: {}", GetComponent<ObjectMetaData>().GetName());
    }

    OXYGEN_MAKE_NON_COPYABLE(CommandList);
    OXYGEN_MAKE_NON_MOVABLE(CommandList);

    [[nodiscard]] auto GetQueueRole() const { return type_; }

    [[nodiscard]] auto GetName() const noexcept -> std::string_view override
    {
        return GetComponent<ObjectMetaData>().GetName();
    }

    void SetName(std::string_view name) noexcept override
    {
        GetComponent<ObjectMetaData>().SetName(name);
    }

protected:
    friend class oxygen::graphics::CommandRecorder;
    friend class oxygen::graphics::Renderer;

    enum class State : int8_t {
        kInvalid = -1, //<! Invalid state

        kFree = 0, //<! Free command list.
        kRecording = 1, //<! The command list is being recorded.
        kRecorded = 2, //<! The command list is recorded and ready to be submitted.
        kExecuting = 3, //<! The command list is being executed.
    };
    [[nodiscard]] auto GetState() const { return state_; }

    OXYGEN_GFX_API virtual void OnBeginRecording();
    OXYGEN_GFX_API virtual void OnEndRecording();
    OXYGEN_GFX_API virtual void OnSubmitted();
    OXYGEN_GFX_API virtual void OnExecuted();

private:
    QueueRole type_ { QueueRole::kNone };
    State state_ { State::kInvalid };
};

} // namespace oxygen::graphics
