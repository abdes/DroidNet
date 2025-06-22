//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <chrono>
#include <memory>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Core/Engine.h>
#include <Oxygen/Core/Version.h>
#include <Oxygen/Platform/SDL/Platform.h>

#include "MainModule.h"

using namespace std::chrono_literals;

extern "C" void MainImpl(std::span<const char*> /*args*/)
{
  // We want to control the destruction order of the important objects in the
  // system. For example, destroy the core before we destroy the platform.
  std::shared_ptr<oxygen::Platform> platform {};
  std::shared_ptr<oxygen::Engine> engine {};

  try {
    // The platform abstraction layer
    platform = std::make_shared<oxygen::platform::sdl::Platform>();

    // Create a window.
    constexpr oxygen::PixelExtent window_size { 1900, 1200 };
    constexpr oxygen::platform::Window::InitialFlags window_flags {
      .hidden = false,
      .always_on_top = false,
      .full_screen = false,
      .maximized = false,
      .minimized = false,
      .resizable = true,
      .borderless = false,
    };
    const auto my_window { platform->MakeWindow(
      "Oxygen Input System Example", window_size, window_flags) };

    oxygen::Engine::Properties props {
            .application = {
                .name = "Input System",
                .version = 0x0001'0000,
            },
            .extensions = {},
            .max_fixed_update_duration = 10ms,
            .enable_imgui_layer = true,
            .main_window_id = my_window.lock()->Id(),
        };

    // No graphics
    engine = std::make_shared<oxygen::Engine>(
      platform, oxygen::GraphicsPtr {}, props);

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
}
