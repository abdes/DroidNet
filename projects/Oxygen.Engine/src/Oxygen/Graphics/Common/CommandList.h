//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <string_view>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Composition/Composition.h>
#include <Oxygen/Composition/Named.h>
#include <Oxygen/Graphics/Common/Types/Queues.h>
#include <Oxygen/Graphics/Common/api_export.h>

namespace oxygen::graphics {

class CommandRecorder;
class Renderer;

class CommandList : public Composition, public Named {
public:
    OXYGEN_GFX_API CommandList(QueueRole type, std::string_view name);

    //! Destroys the command list after releasing all graphics resources it was
    //! using.
    /*!
     \note It is the responsibility of the user to ensure the command list (or
     its associated resources) are not in use by ongoing GPU operations.
    */
    OXYGEN_GFX_API ~CommandList() override;

    OXYGEN_MAKE_NON_COPYABLE(CommandList)
    OXYGEN_MAKE_NON_MOVABLE(CommandList)

    [[nodiscard]] OXYGEN_GFX_API auto GetQueueRole() const { return type_; }

    [[nodiscard]] OXYGEN_GFX_API auto GetName() const noexcept -> std::string_view override;
    OXYGEN_GFX_API void SetName(std::string_view name) noexcept override;

protected:
    enum class State : int8_t {
        kInvalid = -1, //<! Invalid state

        kFree = 0, //<! Free command list.
        kRecording = 1, //<! The command list is being recorded.
        kRecorded = 2, //<! The command list is recorded and ready to be submitted.
        kExecuting = 3, //<! The command list is being executed.
    };
    [[nodiscard]] auto GetState() const { return state_; }

    friend class CommandRecorder;
    OXYGEN_GFX_API virtual void OnBeginRecording();
    OXYGEN_GFX_API virtual void OnEndRecording();

    friend class Renderer;
    OXYGEN_GFX_API virtual void OnSubmitted();
    OXYGEN_GFX_API virtual void OnExecuted();

private:
    QueueRole type_ { QueueRole::kNone };
    State state_ { State::kInvalid };
};

} // namespace oxygen::graphics
