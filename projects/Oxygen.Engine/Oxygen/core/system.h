//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include "Oxygen/Base/Macros.h"
#include "Oxygen/Base/Time.h"

namespace oxygen::engine {

struct SystemUpdateContext {
  Duration time_since_start {};
  Duration delta_time {};
};

class System
{
 public:
  System() = default;
  virtual ~System() = default;

  OXYGEN_DEFAULT_COPYABLE(System);
  OXYGEN_DEFAULT_MOVABLE(System);

  // Called by the core, every frame, to give a chance to the system to
  // update its state.
  virtual void Update(const SystemUpdateContext& update_context) = 0;
};

} // namespace oxygen::engine
