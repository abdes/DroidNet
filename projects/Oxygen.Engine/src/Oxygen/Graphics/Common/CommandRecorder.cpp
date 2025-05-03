//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Graphics/Common/CommandList.h>
#include <Oxygen/Graphics/Common/CommandRecorder.h>

using oxygen::graphics::CommandRecorder;

CommandRecorder::CommandRecorder(CommandList* command_list, CommandQueue* target_queue)
    : command_list_(command_list)
    , target_queue_(target_queue)
{
    CHECK_NOTNULL_F(command_list_);
}

void CommandRecorder::Begin()
{
    if (command_list_ != nullptr) {
        DCHECK_EQ_F(command_list_->GetState(), CommandList::State::kFree);
        command_list_->OnBeginRecording();
    }
}

auto CommandRecorder::End() -> graphics::CommandList*
{
    if (command_list_ == nullptr) {
        return {};
    }

    try {
        command_list_->OnEndRecording();
        return command_list_;
    } catch (const std::exception& e) {
        LOG_F(ERROR, "Recording failed: {}", e.what());
        return {};
    }
}
