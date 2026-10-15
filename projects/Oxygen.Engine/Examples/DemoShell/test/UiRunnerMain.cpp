//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <memory>
#include <span>

#include "DemoShell/Runtime/DemoMain.h"
#include "DemoShell/Services/SettingsService.h"
#include "DemoShell/test/UiRunnerModule.h"
#include "DemoShell/test/UiTestSession.h"

#include <Oxygen/Engine/AsyncEngine.h>
#include <Oxygen/Host/HostConfig.h>
#include <Oxygen/Host/RunHost.h>

namespace examples = oxygen::examples;
namespace host = oxygen::host;

//! Runs the DemoShell UI tests on an empty scene, without any example.
extern "C" auto MainImpl(std::span<const char*> args) -> int
{
  examples::testing::UiTestSession::InitializeSettings();
  examples::SettingsService::ForDemoApp();

  return examples::RunDemo(args,
    {
      .program = "demoshell-ui-runner",
      .about = "Runs the DemoShell UI tests",
      .application = "DemoShell UI Tests",
      .register_modules
      = [](oxygen::AsyncEngine& engine, host::HostContext& context) -> void {
        host::RegisterRequiredModule(
          engine, std::make_unique<examples::testing::UiRunnerModule>(context));
        examples::RegisterDemoRenderer(engine, context);
      },
    });
}
