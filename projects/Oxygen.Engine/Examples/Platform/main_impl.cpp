//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <chrono>
#include <cstdlib>
#include <memory>
#include <string>
#include <type_traits>
#include <vector>

#include <SDL3/SDL_events.h>

#ifdef _WIN32
#  include <windows.h>
#  include <wingdi.h>
#endif

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

//! Simple help text renderer using Windows GDI APIs
class HelpRenderer {
private:
#ifdef _WIN32
  HWND hwnd_ = nullptr;
#endif
  std::vector<std::string> help_lines_;
  bool show_help_ = false;

public:
  explicit HelpRenderer(void* native_window_handle)
  {
#ifdef _WIN32
    hwnd_ = static_cast<HWND>(native_window_handle);
    if (!hwnd_) {
      LOG_F(ERROR, "Invalid native window handle");
      return;
    }
#endif

    // Initialize help text lines
    help_lines_ = { "Oxygen Platform Example - Keyboard Controls", "",
      "Arrow Keys: Move window position by 10px",
      "X: Maximize  |  M: Minimize  |  R: Restore",
      "F: Enter Fullscreen  |  G: Exit Fullscreen",
      "Q: Request Close  |  A: Request Close (rejected)",
      "Z: Force Close  |  Y: Confirm close prompt",
      "H: Toggle this help overlay", "",
      "Press any key to test the controls..." };

    LOG_F(INFO, "Help renderer created successfully");
  }

  ~HelpRenderer() = default;

  void ToggleHelp()
  {
    show_help_ = !show_help_;
    if (show_help_) {
      DrawHelp();
    } else {
      ClearHelp();
    }
    LOG_F(INFO, "Help overlay: {}", show_help_ ? "ON" : "OFF");
  }

  void OnWindowResize()
  {
    // Always clear the window on resize, then redraw help if showing
    ClearHelp(); // This clears the entire window to black
    if (show_help_) {
      DrawHelp();
    }
  }

public:
  bool IsShowingHelp() const { return show_help_; }

private:
  void DrawHelp()
  {
#ifdef _WIN32
    if (!hwnd_)
      return;

    HDC hdc = GetDC(hwnd_);
    if (!hdc)
      return;

    // Get window dimensions
    RECT window_rect;
    GetClientRect(hwnd_, &window_rect);

    // Clear entire window surface with solid black
    HBRUSH black_brush = CreateSolidBrush(RGB(0, 0, 0));
    FillRect(hdc, &window_rect, black_brush);

    // Set text drawing properties for transparent background
    SetBkMode(hdc, TRANSPARENT);

    // Create readable font
    HFONT font = CreateFont(24, // Large height for readability
      0, // Width
      0, // Escapement
      0, // Orientation
      FW_BOLD, // Bold weight
      FALSE, // Italic
      FALSE, // Underline
      FALSE, // StrikeOut
      ANSI_CHARSET, // CharSet
      OUT_DEFAULT_PRECIS, // OutPrecision
      CLIP_DEFAULT_PRECIS, // ClipPrecision
      ANTIALIASED_QUALITY, // Quality
      DEFAULT_PITCH | FF_DONTCARE, // PitchAndFamily
      "Arial" // Font name
    );

    HGDIOBJ old_font = SelectObject(hdc, font);

    // Draw text directly on black surface with high contrast colors
    const int margin = 30;
    const int line_height = 35;

    for (size_t i = 0; i < help_lines_.size(); ++i) {
      if (help_lines_[i].empty())
        continue;

      // Use bright colors on black background
      if (i == 0) {
        SetTextColor(hdc, RGB(255, 255, 255)); // White header
      } else if (help_lines_[i].find(":") != std::string::npos) {
        SetTextColor(hdc, RGB(255, 255, 0)); // Yellow for key controls
      } else {
        SetTextColor(hdc, RGB(200, 200, 200)); // Light gray for descriptions
      }

      const std::string& line = help_lines_[i];
      TextOut(hdc, margin, margin + static_cast<int>(i) * line_height,
        line.c_str(), static_cast<int>(line.length()));
    }

    // Cleanup
    SelectObject(hdc, old_font);
    DeleteObject(font);
    DeleteObject(black_brush);
    ReleaseDC(hwnd_, hdc);
#endif
  }

  void ClearHelp()
  {
#ifdef _WIN32
    if (!hwnd_)
      return;

    HDC hdc = GetDC(hwnd_);
    if (!hdc)
      return;

    // Get window dimensions and fill with black
    RECT window_rect;
    GetClientRect(hwnd_, &window_rect);

    // Force solid black background
    HBRUSH black_brush = CreateSolidBrush(RGB(0, 0, 0));
    FillRect(hdc, &window_rect, black_brush);

    DeleteObject(black_brush);
    ReleaseDC(hwnd_, hdc);
#endif
  }
};

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

    // Start a lightweight frame tick task that calls Platform::OnFrameStart()
    // each frame. The Platform implementation now requires a per-frame
    // OnFrameStart() call so that internal window lifecycle and timers are
    // advanced; without this the window may never close. Run at ~60Hz.
    n.Start([platform]() -> oxygen::co::Co<> {
      const auto frame_period = std::chrono::milliseconds(16); // ~60 FPS
      while (oxygen::co::EventLoopTraits<Platform>::IsRunning(*platform)) {
        // Call OnFrameStart on the platform to advance per-frame state.
        try {
          platform->OnFrameStart();
        } catch (const std::exception& e) {
          DLOG_F(WARNING, "Platform::OnFrameStart() threw: {}", e.what());
        } catch (...) {
          DLOG_F(WARNING, "Platform::OnFrameStart() threw unknown exception");
        }

        co_await platform->Async().SleepFor(frame_period);
      }

      co_return;
    });

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
    std::shared_ptr<HelpRenderer> help_renderer;

    if (const auto window = window_weak.lock()) {
      LOG_F(INFO, "My window {} is created", window->Id());

      // Get native window handle and create help renderer
      const auto native_handles = window->Native();
      if (native_handles.window_handle) {
        try {
          help_renderer
            = std::make_shared<HelpRenderer>(native_handles.window_handle);
          LOG_F(INFO, "Help renderer initialized successfully");
        } catch (const std::exception& e) {
          LOG_F(ERROR, "Failed to create help renderer: {}", e.what());
        }
      }

      LOG_F(INFO, "H: Show detailed help ({})",
        help_renderer ? "On-screen + Console" : "Console only");
    }

    n.Start([window_weak, help_renderer]() -> oxygen::co::Co<> {
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
          } else if (to == WindowEvent::kResized) {
            LOG_F(INFO, "My window is resized");
            // Always clear and redraw the window on resize
            if (help_renderer) {
              help_renderer->OnWindowResize();
            }
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
    n.Start([window_weak, platform, &n, help_renderer]() -> oxygen::co::Co<> {
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

          // Help toggle
          case SDLK_H: {
            LOG_F(INFO, "=== HELP - Keyboard Controls ===");
            LOG_F(INFO, "Arrow Keys: Move window position by 10 pixels");
            LOG_F(INFO, "X: Maximize window");
            LOG_F(INFO, "M: Minimize window");
            LOG_F(INFO, "R: Restore window to normal state");
            LOG_F(INFO, "F: Enter fullscreen mode");
            LOG_F(INFO, "G: Exit fullscreen mode");
            LOG_F(INFO, "Q: Request window close (can be voted against)");
            LOG_F(INFO, "A: Request close but auto-reject (demo rejection)");
            LOG_F(INFO, "Z: Force close (bypasses voting mechanism)");
            LOG_F(INFO, "Y: Confirm close when prompted");
            LOG_F(INFO, "H: Show this help text");
            LOG_F(INFO, "================================");

            // Display help overlay on window if renderer is available
            if (help_renderer) {
              help_renderer->ToggleHelp();
              LOG_F(INFO, "Help overlay toggled");
            }
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
