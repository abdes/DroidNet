//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "main_module.h"

#include <memory>

#include "Oxygen/Base/Compilers.h"
#include "Oxygen/Base/Logging.h"
#include "Oxygen/Core/Engine.h"
#include "Oxygen/Core/Version.h"
#include "Oxygen/Platform-SDL/Platform.h"
#include "Oxygen/Renderers/Common/Renderer.h"
#include "Oxygen/Renderers/Loader/RendererLoader.h"

using namespace std::chrono_literals;

using oxygen::Engine;
using oxygen::graphics::CreateRenderer;
using oxygen::graphics::DestroyRenderer;
using oxygen::graphics::GraphicsBackendType;

auto main([[maybe_unused]] int argc, [[maybe_unused]] char* argv[]) -> int
{
  auto status { EXIT_SUCCESS };

#if defined(_DEBUG) && defined(OXYGEN_MSVC_VERSION)
  _CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
#endif

  // Optional, but useful to time-stamp the start of the log.
  // Will also detect verbosity level on command line as -v.
  loguru::init(argc, argv);

  LOG_F(INFO, "{}", oxygen::version::NameVersion());

  // We want to control the destruction order of the important objects in the
  // system. For example, destroy the core before we destroy the platform.
  std::shared_ptr<oxygen::Platform> platform;
  std::shared_ptr<oxygen::Engine> engine;

  try {
    platform = std::make_shared<oxygen::platform::sdl::Platform>();

    constexpr oxygen::RendererProperties renderer_props {
#ifdef _DEBUG
      .enable_debug = true,
#endif
      .enable_validation = false,
    };
    CreateRenderer(GraphicsBackendType::kDirect3D12, platform, renderer_props);
    auto renderer = oxygen::graphics::GetRenderer().lock();
    DCHECK_NOTNULL_F(renderer);

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
      platform->MakeWindow("Oxygen Renderer Example", window_size, window_flags)
    };

    Engine::Properties props {
      .application = {
        .name = "Triangle",
        .version = 0x0001'0000,
      },
      .extensions = {},
      .max_fixed_update_duration = 10ms,
      .enable_imgui_layer = true,
      .main_window_id = my_window.lock()->Id(),
    };

    engine = std::make_shared<Engine>(platform, renderer, props);
    const auto my_module = std::make_shared<MainModule>(platform, engine, my_window);
    engine->AttachModule(my_module);

    engine->Initialize();
    engine->Run();
    engine->Shutdown();

    LOG_F(INFO, "Exiting application");
    DestroyRenderer();
  } catch (std::exception const& err) {
    LOG_F(ERROR, "A fatal error occurred: {}", err.what());
    status = EXIT_FAILURE;
  }

  // Explicit destruction order due to dependencies.
  engine.reset();
  platform.reset();

  return status;
}
