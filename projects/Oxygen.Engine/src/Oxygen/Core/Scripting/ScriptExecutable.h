//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <span>

#include <Oxygen/Base/Macros.h>

namespace oxygen::scripting {

class ScriptExecutable {
public:
  ScriptExecutable() = default;
  virtual ~ScriptExecutable() = default;

  OXYGEN_DEFAULT_COPYABLE(ScriptExecutable)
  OXYGEN_DEFAULT_MOVABLE(ScriptExecutable)

  virtual auto Run() const noexcept -> void = 0;

  [[nodiscard]] virtual auto BytecodeView() const noexcept
    -> std::span<const uint8_t>
  {
    return {};
  }

  [[nodiscard]] virtual auto ContentHash() const noexcept -> uint64_t
  {
    return 0;
  }
};

} // namespace oxygen::scripting
