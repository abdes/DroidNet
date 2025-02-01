//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <chrono>
#include <memory>

#include "Oxygen/Base/Logging.h"
#include "Oxygen/Core/Engine.h"
#include "Oxygen/Core/Version.h"
#include "Oxygen/Platform/SDL/platform.h"

#include "SimpleModule.h"

using namespace std::chrono_literals;

auto main([[maybe_unused]] int argc, [[maybe_unused]] char* argv[]) -> int
{
    auto status { EXIT_SUCCESS };

#if defined(_MSC_VER)
    // Enable memory leak detection in debug mode
    _CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
#endif

    loguru::g_preamble_date = false;
    loguru::g_preamble_file = true;
    loguru::g_preamble_verbose = false;
    loguru::g_preamble_time = false;
    loguru::g_preamble_uptime = false;
    loguru::g_preamble_thread = false;
    loguru::g_preamble_header = false;
    loguru::g_stderr_verbosity = loguru::Verbosity_1;
    // Optional, but useful to time-stamp the start of the log.
    // Will also detect verbosity level on command line as -v.
    loguru::init(argc, argv);

    LOG_F(INFO, "{}", oxygen::version::NameVersion());

    // We want to control the destruction order of the important objects in the
    // system. For example, destroy the core before we destroy the platform.
    std::shared_ptr<oxygen::Platform> platform {};
    std::shared_ptr<oxygen::Engine> engine {};

    try {
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
            .enable_imgui_layer = false,
            .main_window_id = my_window.lock()->Id(),
        };

        engine = std::make_shared<oxygen::Engine>(platform, oxygen::GraphicsPtr {}, props);

        const auto simple_module = std::make_shared<SimpleModule>(engine);
        engine->AttachModule(simple_module);

        engine->Run();

        LOG_F(INFO, "Exiting application");
    } catch (std::exception const& err) {
        LOG_F(ERROR, "A fatal error occurred: {}", err.what());
        status = EXIT_FAILURE;
    }

    // Explicit destruction order due to dependencies.
    engine.reset();
    platform.reset();

    loguru::shutdown();
    return status;
}
