//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "MainModule.h"

#include <chrono>
#include <memory>
#include <span>

#include "Oxygen/Base/logging.h"
#include "oxygen/core/engine.h"
#include "oxygen/core/version.h"
#include "Oxygen/Platform/SDL/platform.h"

using namespace std::chrono_literals;

using oxygen::Engine;

auto main([[maybe_unused]] int argc, [[maybe_unused]] char* argv[]) -> int
{
  auto status { EXIT_SUCCESS };

  // Optional, but useful to time-stamp the start of the log.
  // Will also detect verbosity level on command line as -v.
  loguru::init(argc, argv);

  LOG_F(INFO, "{}", oxygen::version::NameVersion());

  // We want to control the destruction order of the important objects in the
  // system. For example, destroy the core before we destroy the platform.
  std::shared_ptr<oxygen::Platform> platform;
  std::shared_ptr<Engine> engine;

  try {
    platform = std::make_shared<oxygen::platform::sdl::Platform>();

    Engine::Properties props {
      .application = {
        .name = "Triangle",
        .version = 0x0001'0000,
      },
      .extensions = {},
      .max_fixed_update_duration = 10ms,
      .enable_imgui_layer = false,
    };

    // Engine with no renderer
    std::shared_ptr<oxygen::Renderer> renderer {};

    engine = std::make_shared<Engine>(platform, renderer, props);
    const auto my_module = std::make_shared<MainModule>(engine);
    engine->AttachModule(my_module);

    engine->Initialize();
    engine->Run();
    engine->Shutdown();

    LOG_F(INFO, "Exiting application");
  } catch (std::exception const& err) {
    LOG_F(ERROR, "A fatal error occurred: {}", err.what());
    status = EXIT_FAILURE;
  }

  // Explicit destruction order due to dependencies.
  engine.reset();
  platform.reset();

  return status;
}
