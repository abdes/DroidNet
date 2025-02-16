//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "Algorithms.h"

#include <cstdlib>
#include <exception>
#include <memory>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/OxCo/Run.h>
#include <Oxygen/Platform/Platform.h>
#include <Oxygen/Platform/Window.h>

// Force link the DLL containing the InitializeTypeRegistry function.
extern "C" auto InitializeTypeRegistry() -> oxygen::TypeRegistry*;
namespace {
[[maybe_unused]] auto* ts_registry_unused = InitializeTypeRegistry();
} // namespace

using oxygen::Platform;
using oxygen::platform::Display;
using oxygen::platform::Window;
using WindowProps = oxygen::platform::window::Properties;
using WindowEvent = oxygen::platform::window::Event;

namespace {

bool is_running { false };
void EventLoopRun(const Platform& platform)
{
    while (is_running) {
        platform.Async().PollOne();
        platform.Events().PollOne();
    }
}

auto AsyncMain(std::shared_ptr<oxygen::Platform> platform) -> oxygen::co::Co<int>
{
    OXCO_WITH_NURSERY(n)
    {
        is_running = true;

        // Activate the live objects with our nursery, making it available for the
        // lifetime of the nursery.
        co_await n.Start(&Platform::Start, std::ref(*platform));
        platform->Run();

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

        n.Start([window_weak]() -> oxygen::co::Co<> {
            bool not_destroyed { true };
            while (not_destroyed && !window_weak.expired()) {
                auto window = window_weak.lock();
                if (const auto [from, to] = co_await window->Events().UntilChanged();
                    to == WindowEvent::kDestroyed) {
                    LOG_F(INFO, "My window is destroyed");
                    not_destroyed = false;
                } else {
                    if (to == WindowEvent::kExposed) {
                        LOG_F(INFO, "My window is exposed");
                    }
                }
            }
        });

        n.Start([window_weak, platform]() -> oxygen::co::Co<> {
            while (!window_weak.expired()) {
                auto window = window_weak.lock();
                co_await window->CloseRequested();
                DLOG_F(WARNING, "Press 'y' to close the window, you have 3 seconds...");
                // Wait for the user to press 'y'
                // 3 seconds to elapse
                auto [double_close, _] = co_await oxygen::co::AnyOf(
                    [&platform]() -> oxygen::co::Co<bool> {
                        while (true) {
                            auto& event = co_await platform->Events().NextEvent();
                            auto _ = co_await platform->Events().Lock();
                            auto sdl_event = event.NativeEventAs<SDL_Event>();
                            if (sdl_event->type == SDL_EVENT_KEY_DOWN) {
                                if (sdl_event->key.key == SDLK_Y) {
                                    co_return true;
                                }
                            }
                        }
                    },
                    platform->Async().SleepFor(std::chrono::seconds(3)));
                if (!window_weak.expired()) {
                    if (!double_close) {
                        window_weak.lock()->VoteNotToClose();
                    } else {
                        window_weak.lock()->VoteToClose();
                    }
                }
            }
        });

        n.Start([&platform, &n]() -> oxygen::co::Co<> {
            co_await platform->Windows().LastWindowClosed();
            LOG_F(INFO, "Last window is closed -> wrapping up");
            n.Cancel();
        });

        // Wait for all tasks to complete
        co_return oxygen::co::kJoin;
    };
    co_return EXIT_SUCCESS;
}

} // namespace

template <>
struct oxygen::co::EventLoopTraits<Platform> {
    static void Run(const Platform& platform) { EventLoopRun(platform); }
    static void Stop(Platform& /*platform*/) { is_running = false; }
    static auto IsRunning(const Platform& /*platform*/) -> bool { return is_running; }
    static auto EventLoopId(const Platform& platform) -> EventLoopID { return EventLoopID(&platform); }
};

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
    loguru::g_stderr_verbosity = loguru::Verbosity_0;
    loguru::g_colorlogtostderr = true;
    // Optional, but useful to time-stamp the start of the log.
    // Will also detect verbosity level on command line as -v.
    loguru::init(argc, argv);

    auto platform = std::make_shared<Platform>();
    try {
        oxygen::co::Run(*platform, AsyncMain(platform));
    } catch (const std::exception& e) {
        LOG_F(ERROR, "Uncaught exception: {}", e.what());
        status = EXIT_FAILURE;
    } catch (...) {
        LOG_F(ERROR, "Uncaught exception of unknown type");
        status = EXIT_FAILURE;
    }

    // Explicit destruction order due to dependencies.
    platform.reset();

    LOG_F(INFO, "Exit with status: {}", status);
    loguru::shutdown();
    return status;
}

#if 0
        bool continue_running = true;
        // platform->OnLastWindowClosed().connect(
        //     [&continue_running]() { continue_running = false; });

        while (continue_running) {
            if (const auto event = platform->PollEvent()) {
                if (event->GetType() == oxygen::platform::InputEventType::kKeyEvent) {
                    const auto& key_event = dynamic_cast<const oxygen::platform::KeyEvent&>(*event);
                    if (key_event.GetButtonState() == oxygen::platform::ButtonState::kPressed) {

                        constexpr int translate_by = 10;

                        switch (key_event.GetKeyCode()) {
                        case oxygen::platform::Key::kLeftArrow: {
                            if (const auto window = window_weak.lock()) {
                                if (window->IsMaximized()) {
                                    window->Restore();
                                }
                                const auto [pos_x, pos_y] = window->Position();
                                window->Position({ pos_x - translate_by, pos_y });
                            }
                        } break;
                        case oxygen::platform::Key::kRightArrow: {
                            if (const auto window = window_weak.lock()) {
                                if (window->IsMaximized()) {
                                    window->Restore();
                                }
                                const auto [pos_x, pos_y] = window->Position();
                                window->Position({ pos_x + translate_by, pos_y });
                            }
                        } break;
                        case oxygen::platform::Key::kUpArrow: {
                            if (const auto window = window_weak.lock()) {
                                if (window->IsMaximized()) {
                                    window->Restore();
                                }
                                const auto [pos_x, pos_y] = window->Position();
                                window->Position({ pos_x, pos_y - translate_by });
                            }
                        } break;
                        case oxygen::platform::Key::kDownArrow: {
                            if (const auto window = window_weak.lock()) {
                                if (window->IsMaximized()) {
                                    window->Restore();
                                }
                                const auto [pos_x, pos_y] = window->Position();
                                window->Position({ pos_x, pos_y + translate_by });
                            }
                        } break;
                        case oxygen::platform::Key::kX: {
                            if (const auto window = window_weak.lock()) {
                                LOG_F(INFO, "Maximize()");
                                window->Maximize();
                            }
                        } break;
                        case oxygen::platform::Key::kM: {
                            if (const auto window = window_weak.lock()) {
                                LOG_F(INFO, "Minimize()");
                                window->Minimize();
                            }
                        } break;
                        case oxygen::platform::Key::kR: {
                            if (const auto window = window_weak.lock()) {
                                LOG_F(INFO, "Restore()");
                                window->Restore();
                            }
                        } break;
                        case oxygen::platform::Key::kF: {
                            if (const auto window = window_weak.lock()) {
                                LOG_F(INFO, "FullScreen(true)");
                                window->FullScreen(true);
                            }
                        } break;
                        case oxygen::platform::Key::kG: {
                            if (const auto window = window_weak.lock()) {
                                LOG_F(INFO, "FullScreen(false)");
                                window->FullScreen(false);
                            }
                        } break;
                        case oxygen::platform::Key::kQ: {
                            if (const auto window = window_weak.lock()) {
                                LOG_F(INFO, "RequestClose(force=false)");
                                window->RequestClose(false);
                            }
                        } break;
                        case oxygen::platform::Key::kA: {
                            if (const auto window = window_weak.lock()) {
                                LOG_F(INFO, "RequestClose(force=false) rejected");
                                auto connection = window->OnCloseRequested().connect(
                                    [&window](bool) { window->RequestNotToClose(); });
                                window->RequestClose(false);
                                window->OnCloseRequested().disconnect(connection);
                            }
                        } break;
                        case oxygen::platform::Key::kZ: {
                            if (const auto window = window_weak.lock()) {
                                LOG_F(INFO,
                                    "RequestClose(force=true) rejected - should still close");
                                auto connection = window->OnCloseRequested().connect(
                                    [&window](bool) { window->RequestNotToClose(); });
                                window->RequestClose(true);
                                window->OnCloseRequested().disconnect(connection);
                            }
                        } break;
                        default:
                            break;
                        }
                        if (const auto window = window_weak.lock()) {
                            LOG_F(INFO, "  {} | {}", nostd::to_string(window->Size()),
                                nostd::to_string(window->Position()));
                        }
                    }
                }
            }

            constexpr auto wait_for = std::chrono::milliseconds(10);
            std::this_thread::sleep_for(wait_for);
        }
#endif
