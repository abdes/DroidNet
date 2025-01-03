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
#include "Oxygen/Graphics/Common/Graphics.h"
#include "Oxygen/Graphics/Common/GraphicsModule.h"
#include "Oxygen/Graphics/Loader/GraphicsBackendLoader.h"
#include "Oxygen/Platform/SDL/platform.h"
#include "oxygen/core/engine.h"
#include "oxygen/core/version.h"

using namespace std::chrono_literals;

using oxygen::graphics::BackendType;
using oxygen::graphics::LoadBackend;
using oxygen::graphics::UnloadBackend;

auto main([[maybe_unused]] int argc, [[maybe_unused]] char* argv[]) -> int
{
  auto status { EXIT_SUCCESS };

  // Optional, but useful to time-stamp the start of the log.
  // Will also detect verbosity level on command line as -v.
  loguru::init(argc, argv);

  LOG_F(INFO, "{}", oxygen::version::NameVersion());

  // We want to control the destruction order of the important objects in the
  // system. For example, destroy the core before we destroy the platform.
  std::shared_ptr<oxygen::Platform> platform {};
  std::shared_ptr<oxygen::Engine> engine {};
  std::shared_ptr<oxygen::Graphics> graphics {};

  try {
    // 1- The platform abstraction layer
    platform = std::make_shared<oxygen::platform::sdl::Platform>();
    // 2- The graphics backend module
    const oxygen::GraphicsBackendProperties backend_props {
      .enable_debug = true,
      .enable_validation = false,
      // We don't want a renderer
      .renderer_props = {},
    };

    graphics = LoadBackend(oxygen::graphics::BackendType::kDirect3D12).lock();
    DCHECK_NOTNULL_F(graphics);
    graphics->Initialize(platform, backend_props);

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
    const auto my_window {
      platform->MakeWindow("Oxygen Input System Example", window_size, window_flags)
    };

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

    engine = std::make_shared<oxygen::Engine>(platform, graphics, props);
    const auto my_module = std::make_shared<MainModule>(engine);
    engine->AttachModule(my_module);

    engine->Initialize();
    engine->Run();
    engine->Shutdown();
    graphics->Shutdown();

    LOG_F(INFO, "Exiting application");
  } catch (std::exception const& err) {
    LOG_F(ERROR, "A fatal error occurred: {}", err.what());
    status = EXIT_FAILURE;
  }

  // Explicit destruction order due to dependencies.
  graphics.reset();
  engine.reset();
  platform.reset();

  return status;
}
