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
#include "Oxygen/OxCo/Co.h"
#include "Oxygen/Platform/SDL/platform.h"

#include "AsyncEngineRunner.h"
#include "Awaitables.h"
#include "SimpleModule.h"

using namespace std::chrono_literals;

template <typename Callable, typename... Args>
    requires(std::is_invocable_v<Callable, Args...>)
class SignalAwaitable {
    sigslot::signal<Args...>& signal_; // NOLINT(cppcoreguidelines-avoid-const-or-ref-data-members)
    sigslot::connection conn_;
    Callable callable_;

public:
    SignalAwaitable(sigslot::signal<Args...>& signal, Callable callable)
        : signal_(signal)
        , callable_(std::move(callable))
    {
    }

    // ReSharper disable CppMemberFunctionMayBeStatic
    [[nodiscard]] auto await_ready() const noexcept -> bool { return false; }
    void await_suspend(std::coroutine_handle<> h)
    {
        conn_ = signal_.connect(
            [this, h](Args&&... args) {
                // Invoke the callable method on obj_ with the arguments
                // received from the signal.
                std::invoke(callable_, std::forward<Args>(args)...);
                // Disconnect the signal after the first emission.
                conn_.disconnect();
                h.resume();
            });
    }
    void await_resume() { /* no return value*/ }
    auto await_cancel(std::coroutine_handle<> /*h*/)
    {
        conn_.disconnect();
        return std::true_type {};
    }
    // ReSharper restore CppMemberFunctionMayBeStatic
};

namespace {

auto AsyncMain(std::shared_ptr<oxygen::Engine> engine) -> oxygen::co::Co<int>
{
    auto& platform = engine->GetPlatform();
    co_await SignalAwaitable(
        platform.OnLastWindowClosed(),
        [&engine] {
            engine->Stop();
        });

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

        oxygen::co::Run(*engine, AsyncMain(engine));

    } catch (std::exception const& err) {
        LOG_F(ERROR, "A fatal error occurred: {}", err.what());
        status = EXIT_FAILURE;
    }

    // Explicit destruction order due to dependencies.
    engine.reset();
    platform.reset();

    loguru::shutdown();

    LOG_F(INFO, "Exit with status: {}", status);
    return status;
}

//// Listen for the last window closed event
// auto last_window_closed_con = GetPlatform().OnLastWindowClosed().connect(
//     [this] {
//         DLOG_F(INFO, "Last window closed -> stopping engine");
//         this->Stop();
//     });
