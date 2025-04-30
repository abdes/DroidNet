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

#include <MainModule.h>

using oxygen::Graphics;
using oxygen::GraphicsConfig;
using oxygen::Platform;
using oxygen::PlatformConfig;
using oxygen::graphics::BackendType;
using oxygen::graphics::d3d12::DeviceManager;
using oxygen::graphics::d3d12::DeviceManagerDesc;
using WindowEvent = oxygen::platform::window::Event;
using oxygen::examples::MainModule;

namespace {

struct MyEngine {
    std::shared_ptr<oxygen::Platform> platform;
    std::weak_ptr<Graphics> gfx_weak;
};

// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
bool is_running { false };
void EventLoopRun(const MyEngine& engine)
{
    // TODO: This is the game engine main loop.

    // Track the last render time
    auto last_render_time = std::chrono::steady_clock::now();

    while (is_running) {
        if (engine.gfx_weak.expired()) {
            LOG_F(ERROR, "Graphics backend is no longer available");
            is_running = false;
            break;
        }
        auto gfx = engine.gfx_weak.lock();

        // Physics
        // Sleep for a while to simulate physics
        std::this_thread::sleep_for(std::chrono::milliseconds(30));

        // Input Events
        engine.platform->Async().PollOne();
        engine.platform->Events().PollOne();

        // Game logic
        // Sleep for a while to simulate game logic updates
        std::this_thread::sleep_for(std::chrono::milliseconds(20));

        // Render (only if at least 1 second has passed since the last render)
        auto now = std::chrono::steady_clock::now();
        if (std::chrono::duration_cast<std::chrono::seconds>(now - last_render_time).count() >= 1) {
            gfx->Render();
            last_render_time = now;
        }

        // Check for Pause/Resume
    }
}
} // namespace

template <>
struct oxygen::co::EventLoopTraits<MyEngine> {
    static void Run(const MyEngine& engine) { EventLoopRun(engine); }
    static void Stop(MyEngine& /*engine*/) { is_running = false; }
    static auto IsRunning(const MyEngine& /*engine*/) -> bool { return is_running; }
    static auto EventLoopId(const MyEngine& engine) -> EventLoopID { return EventLoopID(&engine); }
};

namespace {

auto AsyncMain(
    std::shared_ptr<Platform> platform,
    std::weak_ptr<Graphics> gfx_weak,
    MainModule& main_module) -> oxygen::co::Co<int>
{
    // NOLINTNEXTLINE(*-capturing-lambda-coroutines, *-reference-coroutine-parameters)
    OXCO_WITH_NURSERY(n)
    {
        is_running = true;

        // Activate and run child live objects with our nursery.

        co_await n.Start(&Platform::ActivateAsync, std::ref(*platform));
        platform->Run();

        DCHECK_F(!gfx_weak.expired());
        auto gfx = gfx_weak.lock();
        co_await n.Start(&Graphics::ActivateAsync, std::ref(*gfx));
        gfx->Run();

        co_await n.Start(&MainModule::StartAsync, std::ref(main_module));
        main_module.Run();

        // Terminate the application when the last window (main window) is
        // closed
        n.Start([&platform, &gfx_weak, &n]() -> oxygen::co::Co<> {
            co_await platform->Windows().LastWindowClosed();
            LOG_F(INFO, "Last window is closed -> wrapping up");
            // Explicitly stop the child live objects. Although this is not
            // strictly required, it is a good practice to do so and ensures a
            // controlled shutdown.
            platform->Stop();

            // Stop the render thread
            DCHECK_F(!gfx_weak.expired());
            gfx_weak.lock()->Stop();

            // Cancel the main nursery to stop all background async tasks and
            // return control to `main()`
            n.Cancel();
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
    auto gfx_weak = loader.LoadBackend(BackendType::kDirect3D12, gfx_config);
    CHECK_F(!gfx_weak.expired()); // Expect a valid graphics backend, or abort

    // Create the application main module
    MainModule main_module(platform, gfx_weak);

    // Transfer control to the asynchronous main loop
    MyEngine engine { platform, gfx_weak };
    oxygen::co::Run(engine, AsyncMain(platform, gfx_weak, main_module));

    // Explicit destruction order due to dependencies.
    platform.reset();
    gfx_weak.reset();
}
