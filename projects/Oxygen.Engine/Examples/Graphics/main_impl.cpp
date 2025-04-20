//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cstdlib>
#include <memory>
#include <span>
#include <type_traits>

#include <SDL3/SDL_events.h>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Config/GraphicsConfig.h>
#include <Oxygen/Config/PlatformConfig.h>
#include <Oxygen/Graphics/Common/BackendModule.h>
#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/Graphics/Direct3D12/Devices/DeviceManager.h>
#include <Oxygen/Loader/GraphicsBackendLoader.h>
#include <Oxygen/OxCo/Algorithms.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/OxCo/EventLoop.h>
#include <Oxygen/OxCo/Nursery.h>
#include <Oxygen/OxCo/Run.h>
#include <Oxygen/Platform/Platform.h>

using oxygen::Graphics;
using oxygen::GraphicsConfig;
using oxygen::Platform;
using oxygen::PlatformConfig;
using oxygen::graphics::BackendType;
using oxygen::graphics::d3d12::DeviceManager;
using oxygen::graphics::d3d12::DeviceManagerDesc;
using oxygen::platform::Window;
using WindowProps = oxygen::platform::window::Properties;
using WindowEvent = oxygen::platform::window::Event;

namespace {
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
bool is_running { false };
void EventLoopRun(const Platform& platform)
{
    // TODO: This is the game engine main loop.
    while (is_running) {
        platform.Async().PollOne();
        platform.Events().PollOne();
    }
}
} // namespace

template <>
struct oxygen::co::EventLoopTraits<Platform> {
    static void Run(const Platform& platform) { EventLoopRun(platform); }
    static void Stop(Platform& /*platform*/) { is_running = false; }
    static auto IsRunning(const Platform& /*platform*/) -> bool { return is_running; }
    static auto EventLoopId(const Platform& platform) -> EventLoopID { return EventLoopID(&platform); }
};

namespace {

auto AsyncMain(std::shared_ptr<Platform> platform,
    std::weak_ptr<Graphics> gfx_weak) -> oxygen::co::Co<int>
{
    // NOLINTNEXTLINE(*-capturing-lambda-coroutines, *-reference-coroutine-parameters)
    OXCO_WITH_NURSERY(n)
    {
        is_running = true;

        // Activate and run child live objects with our nursery.

        co_await n.Start(&Platform::StartAsync, std::ref(*platform));
        platform->Run();

        DCHECK_F(!gfx_weak.expired());
        auto gfx = gfx_weak.lock();
        co_await n.Start(&Graphics::StartAsync, std::ref(*gfx));
        gfx->Run();

        // Setup the main window
        WindowProps props("Oxygen Window Playground");
        props.extent = { .width = 800, .height = 600 };
        props.flags = {
            .hidden = false,
            .always_on_top = false,
            .full_screen = false,
            .maximized = false,
            .minimized = false,
            .resizable = true,
            .borderless = false
        };
        const auto window_weak = platform->Windows().MakeWindow(props);
        if (const auto window = window_weak.lock()) {
            LOG_F(INFO, "My window {} is created", window->Id());
        }

        // Immediately accept the close request for the main window
        n.Start([window_weak, &platform]() -> oxygen::co::Co<> {
            while (!window_weak.expired()) {
                auto window = window_weak.lock();
                co_await window->CloseRequested();
                window_weak.lock()->VoteToClose();
            }
        });

        // Terminate the application when the last window (main window) is
        // closed
        n.Start([&platform, &n]() -> oxygen::co::Co<> {
            co_await platform->Windows().LastWindowClosed();
            LOG_F(INFO, "Last window is closed -> wrapping up");
            n.Cancel();
        });

        // Add a termination signal handler
        n.Start([window_weak, &platform]() -> oxygen::co::Co<> {
            co_await platform->Async().OnTerminate();
            LOG_F(INFO, "terminating...");
            // Terminate the application by requesting the main window to close
            window_weak.lock()->RequestClose();
        });

        // Wait for all tasks to complete
        co_return oxygen::co::kJoin;
    };
    co_return EXIT_SUCCESS;
}

} // namespace

extern "C" void MainImpl(std::span<const char*> /*args*/)
{
    // Create the platform
    auto platform = std::make_shared<Platform>(PlatformConfig { .headless = false });

    // Load the graphics backend
    GraphicsConfig gfx_config {
        .enable_debug = true,
        .enable_validation = false,
        .headless = false,
        .extra = {},
    };
    auto& loader = oxygen::GraphicsBackendLoader::GetInstance();
    auto gfx = loader.LoadBackend(BackendType::kDirect3D12, gfx_config);
    CHECK_F(!gfx.expired()); // Expect a valid graphics backend, or abort

    oxygen::co::Run(*platform, AsyncMain(platform, gfx));

    // Explicit destruction order due to dependencies.
    platform.reset();
    gfx.reset();
}
