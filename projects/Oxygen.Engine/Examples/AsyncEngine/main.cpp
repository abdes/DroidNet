//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <chrono>
#include <memory>

#include "Oxygen/Base/Logging.h"
#include "Oxygen/OxCo/Co.h"
#include "Oxygen/OxCo/Nursery.h"
#include "Oxygen/OxCo/Run.h"

#include "AsyncEngineRunner.h"
#include "Platform.h"
#include "PlatformEvent.h"
#include "SignalAwaitable.h"

using namespace std::chrono_literals;

namespace {

struct MyEvent {
    int value { 1 };
};

using oxygen::Platform;
using oxygen::platform::PlatformEvent;

auto AsyncMain(std::shared_ptr<oxygen::Platform> platform, std::shared_ptr<AsyncEngine> engine) -> Co<int>
{
    OXCO_WITH_NURSERY(n)
    {
        auto stop = [&]() -> Co<> {
            // TODO: await termination conditions such as last window closed
            co_await platform->Async().SleepFor(5s);

            n.Cancel();
        };
        n.Start(stop);

        // Active the live objects with our nursery, making it available for the
        // lifetime of the nursery.
        co_await n.Start(&Platform::Start, std::ref(*platform));
        platform->Run();
        co_await n.Start(&AsyncEngine::Start, std::ref(*engine));
        engine->Run();

        // Wait for all tasks to complete
        co_return kJoin;
    };
    co_return EXIT_SUCCESS;
}

} // namespace

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
    // loguru::g_preamble_time = false;
    loguru::g_preamble_uptime = false;
    loguru::g_preamble_thread = false;
    loguru::g_preamble_header = false;
    loguru::g_stderr_verbosity = loguru::Verbosity_0;
    loguru::g_colorlogtostderr = true;
    // Optional, but useful to time-stamp the start of the log.
    // Will also detect verbosity level on command line as -v.
    loguru::init(argc, argv);

    auto platform = std::make_shared<Platform>();
    auto engine = std::make_shared<AsyncEngine>(platform);
    try {
        oxygen::co::Run(*engine, AsyncMain(platform, engine));
    } catch (const std::exception& e) {
        LOG_F(ERROR, "Uncaught exception: {}", e.what());
        status = EXIT_FAILURE;
    } catch (...) {
        LOG_F(ERROR, "Uncaught exception of unknown type");
        status = EXIT_FAILURE;
    }
    // Explicit destruction order due to dependencies.
    engine.reset();

    LOG_F(INFO, "Exit with status: {}", status);
    loguru::shutdown();
    return status;
}

//// Listen for the last window closed event
// auto last_window_closed_con = GetPlatform().OnLastWindowClosed().connect(
//     [this] {
//         DLOG_F(INFO, "Last window closed -> stopping engine");
//         this->Stop();
//     });
