//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "Oxygen/Base/Logging.h"
#include "Oxygen/Renderers/Common/Disposable.h"

using namespace oxygen::renderer;

Disposable::~Disposable()
{
  if (should_release_) {
    LOG_F(ERROR, "You should call Release() before the Disposable object is destroyed!");
    const auto stack_trace = loguru::stacktrace();
    if (!stack_trace.empty())
      DRAW_LOG_F(ERROR, "{}", stack_trace.c_str());
    ABORT_F("Cannot continue!");
  }
}
