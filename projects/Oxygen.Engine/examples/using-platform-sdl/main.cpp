//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cstdlib>
#include <exception>
#include <memory>
#include <span>

#include "oxygen/base/logging.h"
#include "oxygen/platform-sdl/platform.h"

using oxygen::platform::Display;
using oxygen::platform::sdl::Platform;

// We're just being lazy here and not disconnecting the signals from their
// handlers, because we are keeping the connection until the program terminates.

// NOLINTBEGIN(*-unused-return-value)

auto main([[maybe_unused]] int argc, [[maybe_unused]] char *argv[]) -> int {

  try {
    // Optional, but useful to time-stamp the start of the log.
    // Will also detect verbosity level on command line as -v.
    loguru::init(argc, argv);

    const auto platform = std::make_shared<Platform>();
    // auto core = std::make_shared<oxygen::Engine>(platform);
    // auto instance =
    // std::make_shared<oxygen::graphics::instance>(*core);

    const auto window_weak = platform->MakeWindow("Oxygen Window Playground",
                                                  {.width = 800, .height = 600},
                                                  {
                                                      //.maximized = true,
                                                      .resizable = true,
                                                      //.borderless = true
                                                  });

    if (const auto window = window_weak.lock()) {
      window->Show();
    }

    bool continue_running = true;
    platform->OnLastWindowClosed().connect(
        [&continue_running]() { continue_running = false; });

    while (continue_running) {
      if (const auto event = platform->PollEvent()) {
        if (event->GetType() == oxygen::platform::InputEventType::kKeyEvent) {
          const auto &key_event =
              dynamic_cast<const oxygen::platform::KeyEvent &>(*event);
          if (key_event.GetButtonState() ==
              oxygen::platform::ButtonState::kPressed) {

            constexpr int translate_by = 10;

            switch (key_event.GetKeyCode()) {
            case oxygen::platform::Key::kLeftArrow: {
              if (const auto window = window_weak.lock()) {
                if (window->IsMaximized()) {
                  window->Restore();
                }
                const auto [pos_x, pos_y] = window->Position();
                window->Position({pos_x - translate_by, pos_y});
              }
            } break;
            case oxygen::platform::Key::kRightArrow: {
              if (const auto window = window_weak.lock()) {
                if (window->IsMaximized()) {
                  window->Restore();
                }
                const auto [pos_x, pos_y] = window->Position();
                window->Position({pos_x + translate_by, pos_y});
              }
            } break;
            case oxygen::platform::Key::kUpArrow: {
              if (const auto window = window_weak.lock()) {
                if (window->IsMaximized()) {
                  window->Restore();
                }
                const auto [pos_x, pos_y] = window->Position();
                window->Position({pos_x, pos_y - translate_by});
              }
            } break;
            case oxygen::platform::Key::kDownArrow: {
              if (const auto window = window_weak.lock()) {
                if (window->IsMaximized()) {
                  window->Restore();
                }
                const auto [pos_x, pos_y] = window->Position();
                window->Position({pos_x, pos_y + translate_by});
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
  } catch (const std::exception &err) {
    LOG_F(ERROR, "A fatal error occurred: {}", err.what());
  }

  return EXIT_SUCCESS;
}
// NOLINTEND(*-unused-return-value)
