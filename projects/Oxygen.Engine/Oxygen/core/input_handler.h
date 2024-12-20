//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include "oxygen/base/macros.h"
#include "oxygen/platform/types.h"

namespace oxygen::engine {

  class InputHandler
  {
  public:
    InputHandler() = default;
    virtual ~InputHandler() = default;

    OXYGEN_DEFAULT_COPYABLE(InputHandler);
    OXYGEN_DEFAULT_MOVABLE(InputHandler);

    // Called by the core, every frame, to give a chance to the system to
    // update its state.
    virtual void ProcessInput(const platform::InputEvent& event) = 0;
  };

}  // namespace oxygen::engine
