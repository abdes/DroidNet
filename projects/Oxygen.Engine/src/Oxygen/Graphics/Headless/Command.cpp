//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Graphics/Common/CommandQueue.h>
#include <Oxygen/Graphics/Headless/Command.h>
#include <Oxygen/Graphics/Headless/CommandContext.h>

namespace oxygen::graphics::headless {

void Command::Execute(CommandContext& ctx)
{
  // Sanity checks on the context
  DCHECK_NOTNULL_F(ctx.queue);

  DLOG_SCOPE_F(3, GetName());
  DLOG_F(3, "submission : {}", ctx.submission_id);
  DLOG_F(3, "queue      : {}", ctx.queue->GetName());

  DoExecute(ctx);
}

} // namespace oxygen::graphics::headless
