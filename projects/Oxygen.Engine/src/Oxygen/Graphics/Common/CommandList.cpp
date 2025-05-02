//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Graphics/Common/CommandList.h>

using oxygen::graphics::CommandList;

void CommandList::OnBeginRecording()
{
    if (state_ != State::kFree) {
        throw std::runtime_error("CommandList is not in a Free state");
    }
    state_ = State::kRecording;
}

void CommandList::OnEndRecording()
{
    if (state_ != State::kRecording) {
        throw std::runtime_error("CommandList is not in a Recording state");
    }
    state_ = State::kRecorded;
}

void CommandList::OnSubmitted()
{
    if (state_ != State::kRecorded) {
        throw std::runtime_error("CommandList is not in a Recorded state");
    }
    state_ = State::kExecuting;
}

void CommandList::OnExecuted()
{
    if (state_ != State::kExecuting) {
        throw std::runtime_error("CommandList is not in an Executing state");
    }
    state_ = State::kFree;
}
