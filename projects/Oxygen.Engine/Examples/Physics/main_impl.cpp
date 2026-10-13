//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <memory>
#include <span>

#include "DemoShell/Runtime/DemoMain.h"
#include "DemoShell/Services/SettingsService.h"
#include "Physics/MainModule.h"

#include <Oxygen/Core/EngineModule.h>
#include <Oxygen/Engine/AsyncEngine.h>
#include <Oxygen/Host/HostConfig.h>
#include <Oxygen/Host/RunHost.h>
#include <Oxygen/PhysicsModule/PhysicsModule.h>

#ifdef OXYGEN_BUILD_UI_TESTS
#  include "DemoShell/test/UiTestSession.h"
#endif

namespace examples = oxygen::examples;
namespace host = oxygen::host;

extern "C" auto MainImpl(std::span<const char*> args) -> int
{
#ifdef OXYGEN_BUILD_UI_TESTS
  examples::testing::UiTestSession::InitializeSettings();
#endif
  examples::SettingsService::ForDemoApp();

  return examples::RunDemo(args,
    {
      .program = "physics",
      .about = "End-to-end physics integration demo",
      .application = "Physics Demo",
      .register_modules
      = [](oxygen::AsyncEngine& engine, host::HostContext& context) -> void {
        host::RegisterRequiredModule(engine,
          std::make_unique<oxygen::physics::PhysicsModule>(
            oxygen::engine::kPhysicsModulePriority));
        host::RegisterRequiredModule(engine,
          std::make_unique<examples::physics_demo::MainModule>(context));
        examples::RegisterDemoRenderer(engine, context);
      },
    });
}
