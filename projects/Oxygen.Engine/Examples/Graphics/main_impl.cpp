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
#include <RenderThread.h>

using oxygen::Graphics;
using oxygen::GraphicsConfig;
using oxygen::Platform;
using oxygen::PlatformConfig;
using oxygen::graphics::BackendType;
using oxygen::graphics::d3d12::DeviceManager;
using oxygen::graphics::d3d12::DeviceManagerDesc;
using WindowEvent = oxygen::platform::window::Event;
using oxygen::RenderThread;
using oxygen::examples::MainModule;

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

auto AsyncMain(
    std::shared_ptr<Platform> platform,
    std::weak_ptr<Graphics> gfx_weak,
    RenderThread& render_thread,
    MainModule& main_module) -> oxygen::co::Co<int>
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

        co_await n.Start(&MainModule::StartAsync, std::ref(main_module));
        main_module.Run();

        // Terminate the application when the last window (main window) is
        // closed
        n.Start([&platform, &render_thread, &n]() -> oxygen::co::Co<> {
            co_await platform->Windows().LastWindowClosed();
            LOG_F(INFO, "Last window is closed -> wrapping up");
            render_thread.Stop();
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

    MainModule main_module(platform, gfx_weak);

    // Start the render thread
    auto render_thread = std::make_unique<oxygen::RenderThread>(gfx_weak);

    oxygen::co::Run(*platform, AsyncMain(platform, gfx_weak, *render_thread, main_module));

    // Explicit destruction order due to dependencies.
    render_thread.reset();
    platform.reset();
    gfx_weak.reset();
}
