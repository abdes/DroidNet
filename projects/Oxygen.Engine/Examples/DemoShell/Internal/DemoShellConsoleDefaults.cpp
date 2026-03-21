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

  constexpr std::string_view kCVarRendererGpuTimestamps
    = "rndr.gpu_timestamps";
  constexpr std::string_view kCVarRendererGpuTimestampViewer
    = "rndr.gpu_timestamps.viewer";

  auto TrySetBoolDefault(console::Console& console,
    const std::string_view cvar_name, const bool value) -> void
  {
    if (console.FindCVar(cvar_name) == nullptr) {
      LOG_F(WARNING,
        "DemoShell: '{}' is unavailable; default skipped", cvar_name);
      return;
    }

    const auto result = console.SetCVarFromText({
      .name = cvar_name,
      .text = value ? "true" : "false",
    });
    if (result.status != console::ExecutionStatus::kOk) {
      LOG_F(WARNING, "DemoShell: failed to set '{}' default: {} ({})",
        cvar_name, result.error, console::to_string(result.status));
    }
  }

} // namespace

auto ApplyDemoShellConsoleDefaults(console::Console& console) -> void
{
  TrySetBoolDefault(console, kCVarRendererGpuTimestamps, true);
  TrySetBoolDefault(console, kCVarRendererGpuTimestampViewer, true);
}

} // namespace oxygen::examples::internal
