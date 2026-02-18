//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <Oxygen/Base/Macros.h>

namespace oxygen::scripting {

class ScriptExecutable {
public:
  ScriptExecutable() = default;
  virtual ~ScriptExecutable() = default;

  OXYGEN_DEFAULT_COPYABLE(ScriptExecutable)
  OXYGEN_DEFAULT_MOVABLE(ScriptExecutable)

  virtual auto Run() const noexcept -> void = 0;
};

} // namespace oxygen::scripting
