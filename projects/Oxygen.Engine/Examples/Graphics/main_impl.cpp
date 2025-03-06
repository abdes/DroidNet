//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cstdlib>
#include <memory>
#include <span>
#include <type_traits>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Config/GraphicsConfig.h>
#include <Oxygen/Config/PlatformConfig.h>
#include <Oxygen/Graphics/Common/BackendModule.h>
#include <Oxygen/Graphics/Direct3D12/Devices/DeviceManager.h>
#include <Oxygen/Loader/GraphicsBackendLoader.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/OxCo/EventLoop.h>
#include <Oxygen/OxCo/Nursery.h>
#include <Oxygen/OxCo/Run.h>
#include <Oxygen/Platform/Platform.h>

using oxygen::GraphicsConfig;
using oxygen::Platform;
using oxygen::PlatformConfig;
using oxygen::graphics::BackendType;
using oxygen::graphics::d3d12::DeviceManager;
using oxygen::graphics::d3d12::DeviceManagerDesc;

namespace {
bool is_running { false };
void EventLoopRun(const Platform& platform)
{
    while (is_running) {
        platform.Async().PollOne();
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

auto AsyncMain(std::shared_ptr<oxygen::Platform> platform) -> oxygen::co::Co<int>
{
    OXCO_WITH_NURSERY(n)
    {
        is_running = true;

        // Activate the live objects with our nursery, making it available for the
        // lifetime of the nursery.
        co_await n.Start(&Platform::StartAsync, std::ref(*platform));
        platform->Run();

        // Add a termination signal handler
        n.Start([&]() -> oxygen::co::Co<> {
            co_await platform->Async().OnTerminate();
            LOG_F(INFO, "terminating...");
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
    auto platform = std::make_shared<Platform>(PlatformConfig { .headless = true });

    // Load the graphics backend
    GraphicsConfig gfx_config {
        .enable_debug = true,
        .enable_validation = false,
        .headless = true,
        .extra = {},
    };
    auto& loader = oxygen::GraphicsBackendLoader::GetInstance();
    auto gfx = loader.LoadBackend(BackendType::kDirect3D12, gfx_config);
    DCHECK_F(!gfx.expired());

    oxygen::co::Run(*platform, AsyncMain(platform));

    // Explicit destruction order due to dependencies.
    platform.reset();
    gfx.reset();
}
