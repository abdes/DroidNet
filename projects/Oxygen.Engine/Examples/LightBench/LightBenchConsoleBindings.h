//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//
#pragma once

#include <array>
#include <functional>
#include <string>

#include "LightBench/LightBenchSettings.h"

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Console/Command.h>

namespace oxygen::console {
class Console;
}

namespace oxygen::examples::light_bench {

//! Local commands invoking the same application actions as the preset bar.
class LightBenchConsoleBindings final {
public:
  struct Actions {
    std::function<void(LightBenchPreset)> select;
    std::function<void()> reset;
    std::function<std::string()> inspect;
  };
  LightBenchConsoleBindings(console::Console& console, Actions actions);
  ~LightBenchConsoleBindings();
  OXYGEN_MAKE_NON_COPYABLE(LightBenchConsoleBindings)
  OXYGEN_MAKE_NON_MOVABLE(LightBenchConsoleBindings)

private:
  auto Unregister() -> void;
  console::Console& console_;
  Actions actions_;
  std::array<console::CommandHandle, 3> handles_ {};
};

} // namespace oxygen::examples::light_bench
