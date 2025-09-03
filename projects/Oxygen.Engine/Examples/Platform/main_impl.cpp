//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cstdlib>
#include <memory>
#include <type_traits>

#include <SDL3/SDL_events.h>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Config/PlatformConfig.h>
#include <Oxygen/OxCo/Algorithms.h>
#include <Oxygen/OxCo/Run.h>
#include <Oxygen/Platform/Platform.h>
#include <Oxygen/Platform/Window.h>

using oxygen::Platform;
using oxygen::PlatformConfig;
using oxygen::platform::Window;
using WindowProps = oxygen::platform::window::Properties;
using WindowEvent = oxygen::platform::window::Event;

namespace {
bool is_running { false };
void EventLoopRun(Platform& platform)
{
  while (is_running) {
    platform.Async().PollOne();
    platform.Events().PollOne();
  }
}
} // namespace

template <> struct oxygen::co::EventLoopTraits<Platform> {
  static void Run(Platform& platform) { EventLoopRun(platform); }
  static void Stop(Platform& /*platform*/) { is_running = false; }
  static auto IsRunning(const Platform& /*platform*/) -> bool
  {
    return is_running;
  }
  static auto EventLoopId(const Platform& platform) -> EventLoopID
  {
    return EventLoopID(&platform);
  }
};

namespace {
auto AsyncMain(std::shared_ptr<oxygen::Platform> platform)
  -> oxygen::co::Co<int>
{
  OXCO_WITH_NURSERY(n)
  {
    is_running = true;

    // Activate the live objects with our nursery, making it available for the
    // lifetime of the nursery.
    co_await n.Start(&Platform::ActivateAsync, std::ref(*platform));
    platform->Run();

    WindowProps props("Oxygen Window Playground - Interactive Controls");
    props.extent = { .width = 800, .height = 600 };
    props.flags = { .hidden = false,
      .always_on_top = false,
      .full_screen = false,
      .maximized = false,
      .minimized = false,
      .resizable = true,
      .borderless = false };
    const auto window_weak = platform->Windows().MakeWindow(props);
    if (const auto window = window_weak.lock()) {
      LOG_F(INFO, "My window {} is created", window->Id());
      LOG_F(INFO, "=== Interactive Keyboard Controls ===");
      LOG_F(INFO, "Arrow Keys: Move window position");
      LOG_F(INFO, "X: Maximize | M: Minimize | R: Restore");
      LOG_F(INFO, "F: Enter Fullscreen | G: Exit Fullscreen");
      LOG_F(INFO,
        "Q: Request Close | A: Request Close (rejected) | Z: Force Close");
      LOG_F(INFO, "Y: Confirm window close when prompted");
      LOG_F(INFO, "=====================================");
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

    // Async keyboard input handler for window manipulation
    n.Start([window_weak, platform, &n]() -> oxygen::co::Co<> {
      while (!window_weak.expired()) {
        auto& event = co_await platform->Events().NextEvent();
        auto _ = co_await platform->Events().Lock();
        auto sdl_event = event.NativeEventAs<SDL_Event>();

        if (sdl_event->type == SDL_EVENT_KEY_DOWN) {
          const auto window = window_weak.lock();
          if (!window)
            continue;

          constexpr int translate_by = 10;

          switch (sdl_event->key.key) {
          // Arrow keys - Move window position
          case SDLK_LEFT: {
            if (window->Maximized()) {
              window->Restore();
            }
            const auto [pos_x, pos_y] = window->Position();
            window->MoveTo({ pos_x - translate_by, pos_y });
          } break;

          case SDLK_RIGHT: {
            if (window->Maximized()) {
              window->Restore();
            }
            const auto [pos_x, pos_y] = window->Position();
            window->MoveTo({ pos_x + translate_by, pos_y });
          } break;

          case SDLK_UP: {
            if (window->Maximized()) {
              window->Restore();
            }
            const auto [pos_x, pos_y] = window->Position();
            window->MoveTo({ pos_x, pos_y - translate_by });
          } break;

          case SDLK_DOWN: {
            if (window->Maximized()) {
              window->Restore();
            }
            const auto [pos_x, pos_y] = window->Position();
            window->MoveTo({ pos_x, pos_y + translate_by });
          } break;

          // Window state controls
          case SDLK_X: {
            LOG_F(INFO, "Maximize()");
            window->Maximize();
          } break;

          case SDLK_M: {
            LOG_F(INFO, "Minimize()");
            window->Minimize();
          } break;

          case SDLK_R: {
            LOG_F(INFO, "Restore()");
            window->Restore();
          } break;

          // Fullscreen controls
          case SDLK_F: {
            LOG_F(INFO, "EnterFullScreen()");
            window->EnterFullScreen();
          } break;

          case SDLK_G: {
            LOG_F(INFO, "ExitFullScreen()");
            window->ExitFullScreen();
          } break;

          // Close operations
          case SDLK_Q: {
            LOG_F(INFO, "RequestClose(force=false)");
            window->RequestClose(false);
          } break;

          case SDLK_A: {
            LOG_F(INFO, "RequestClose(force=false) rejected");
            // Start a task that will automatically vote against closing
            n.Start([window_weak]() -> oxygen::co::Co<> {
              if (auto window = window_weak.lock()) {
                co_await window->CloseRequested();
                LOG_F(INFO, "Auto-rejecting close request (A key behavior)");
                window->VoteNotToClose();
              }
            });
            window->RequestClose(false);
          } break;

          case SDLK_Z: {
            LOG_F(
              INFO, "RequestClose(force=true) rejected - should still close");
            // Start a task that will vote against closing, but force=true
            // should override it
            n.Start([window_weak]() -> oxygen::co::Co<> {
              if (auto window = window_weak.lock()) {
                co_await window->CloseRequested();
                LOG_F(INFO,
                  "Auto-rejecting close request (Z key behavior) - but "
                  "force=true should override");
                window->VoteNotToClose();
              }
            });
            window->RequestClose(
              true); // force=true should close despite rejection
          } break;

          default:
            break;
          }

          // Log current window state after any operation
          if (window) {
            const auto size = window->Size();
            const auto position = window->Position();
            LOG_F(INFO, "  Size: {}x{} | Position: {},{}", size.width,
              size.height, position.x, position.y);
          }
        }
      }
    });

    n.Start([&platform, &n]() -> oxygen::co::Co<> {
      co_await platform->Windows().LastWindowClosed();
      LOG_F(INFO, "Last window is closed -> wrapping up");
      platform->Stop();
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
  auto platform
    = std::make_shared<Platform>(PlatformConfig { .headless = false });

  oxygen::co::Run(*platform, AsyncMain(platform));

  // Explicit destruction order due to dependencies.
  platform.reset();
}
