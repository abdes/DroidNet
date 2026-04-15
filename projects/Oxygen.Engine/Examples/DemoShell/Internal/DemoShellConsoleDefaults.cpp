//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "DemoShell/Internal/DemoShellConsoleDefaults.h"

#include <string_view>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Console/Command.h>
#include <Oxygen/Console/Console.h>

namespace oxygen::examples::internal {

namespace {

  constexpr std::string_view kCVarRendererGpuTimestamps = "rndr.gpu_timestamps";
  constexpr std::string_view kCVarRendererGpuTimestampViewer
    = "rndr.gpu_timestamps.viewer";

  auto TryAddBoolDefault(console::Console& console,
    console::ConsoleStartupPlan& startup_plan, const std::string_view cvar_name,
    const bool value) -> void
  {
    if (console.FindCVar(cvar_name) == nullptr) {
      LOG_F(
        WARNING, "DemoShell: '{}' is unavailable; default skipped", cvar_name);
      return;
    }
    startup_plan.Set(
      std::string(cvar_name), value, console::CVarValueOrigin::kAppDefault);
  }

} // namespace

auto ApplyDemoShellConsoleDefaults(console::Console& console) -> void
{
  auto startup_plan = console::ConsoleStartupPlan {};
  TryAddBoolDefault(console, startup_plan, kCVarRendererGpuTimestamps, true);
  TryAddBoolDefault(
    console, startup_plan, kCVarRendererGpuTimestampViewer, true);
  console.ApplyStartupPlan(startup_plan);
}

} // namespace oxygen::examples::internal
