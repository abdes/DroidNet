//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Graphics/Common/CommandQueue.h>

using oxygen::graphics::CommandQueue;

void CommandQueue::Flush() const
{
    DLOG_F(1, "CommandQueue[{}] flushed", GetName());
    Signal(GetCompletedValue() + 1);
    Wait(GetCurrentValue());
}
