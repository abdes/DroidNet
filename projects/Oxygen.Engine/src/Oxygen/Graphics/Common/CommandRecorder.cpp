//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Graphics/Common/CommandRecorder.h>
#include <Oxygen/Graphics/Common/CommandList.h>

using oxygen::graphics::CommandRecorder;

 void CommandRecorder::Begin()
{
}

 auto CommandRecorder::End() -> std::shared_ptr<graphics::CommandList>
{
    if (command_list_ == nullptr) {
        return {};
    }

    try {
        command_list_->OnEndRecording();
        // Return a non-owning pointer (wrapped in unique_ptr)
        return std::shared_ptr<graphics::CommandList>(command_list_, [](graphics::CommandList*) { });
    } catch (const std::exception& e) {
        LOG_F(ERROR, "Recording failed: {}", e.what());
        return {};
    }
}
