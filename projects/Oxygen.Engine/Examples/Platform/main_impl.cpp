//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <array>
#include <chrono>
#include <cstdlib>
#include <exception>
#include <functional>
#include <memory>
#include <span>
#include <string_view>

#include <SDL3/SDL_events.h>
#include <SDL3/SDL_keycode.h>

#ifdef _WIN32
#  include <windows.h> // IWYU pragma: keep

#  include <minwindef.h>
#  include <windef.h>
#  include <wingdi.h>
#  include <winuser.h>
#endif

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/Macros.h>
#include <Oxygen/Clap/Cli.h>
#include <Oxygen/Clap/Command.h>
#include <Oxygen/Clap/Fluent/DSL.h>
#include <Oxygen/Clap/Fluent/OptionValueBuilder.h>
#include <Oxygen/Clap/Option.h>
#include <Oxygen/Config/PlatformConfig.h>
#include <Oxygen/Host/RuntimeCli.h>
#include <Oxygen/OxCo/Algorithms.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/OxCo/EventLoop.h>
#include <Oxygen/OxCo/Nursery.h>
#include <Oxygen/OxCo/Run.h>
#include <Oxygen/Platform/Platform.h>
#include <Oxygen/Platform/Window.h>

using oxygen::Platform;
using oxygen::PlatformConfig;
using oxygen::platform::Window;
using WindowProps = oxygen::platform::window::Properties;
using WindowEvent = oxygen::platform::window::Event;

namespace {
//! Run flag shared by the event loop traits and the async main.
class RunState {
public:
  static auto IsRunning() noexcept -> bool { return running_; }
  static void SetRunning(const bool running) noexcept { running_ = running; }

private:
  static inline bool running_ { false };
};

//! Keyboard controls help, shared by the on-screen overlay and the console.
constexpr std::array<std::string_view, 10> kHelpLines {
  "Oxygen Platform Example - Keyboard Controls",
  "",
  "Arrow Keys: Move window position by 10px",
  "X: Maximize  |  M: Minimize  |  R: Restore",
  "F: Enter Fullscreen  |  G: Exit Fullscreen",
  "Q: Request Close  |  A: Request Close (rejected)",
  "Z: Force Close  |  Y: Confirm close prompt",
  "H: Toggle this help overlay",
  "",
  "Press any key to test the controls...",
};

//! Log the keyboard controls help to the console.
void PrintHelpToLog()
{
  LOG_F(INFO, "=== HELP - Keyboard Controls ===");
  for (size_t i = 1; i < kHelpLines.size(); ++i) {
    if (!kHelpLines[i].empty()) {
      LOG_F(INFO, "{}", kHelpLines[i]);
    }
  }
  LOG_F(INFO, "================================");
}

//! Simple help text renderer using Windows GDI APIs
class HelpRenderer {
private:
  void* native_window_handle_ = nullptr;
  bool show_help_ = false;

public:
  explicit HelpRenderer(void* native_window_handle)
    : native_window_handle_(native_window_handle)
  {
    if (!native_window_handle_) {
      LOG_F(ERROR, "Invalid native window handle");
      return;
    }

    LOG_F(INFO, "Help renderer created successfully");
  }

  ~HelpRenderer() = default;

  OXYGEN_MAKE_NON_COPYABLE(HelpRenderer)
  OXYGEN_MAKE_NON_MOVABLE(HelpRenderer)

  //! Show the overlay unless it is already showing.
  void ShowHelp() { SetVisible(true); }

  //! Show or hide the help overlay on the window.
  void ToggleHelp() { SetVisible(!show_help_); }

  void OnWindowResize()
  {
    // Always clear the window on resize, then redraw help if showing
    ClearHelp(); // This clears the entire window to black
    if (show_help_) {
      DrawHelp();
    }
  }

private:
#ifdef _WIN32
  [[nodiscard]] auto Window() const -> HWND
  {
    return static_cast<HWND>(native_window_handle_);
  }
#endif

  void SetVisible(bool visible)
  {
    if (visible == show_help_) {
      return;
    }

    show_help_ = visible;
    if (show_help_) {
      DrawHelp();
    } else {
      ClearHelp();
    }
    LOG_F(INFO, "Help overlay: {}", show_help_ ? "ON" : "OFF");
  }

  void DrawHelp()
  {
#ifdef _WIN32
    const HWND hwnd = Window();
    if (!hwnd) {
      return;
    }

    HDC hdc = GetDC(hwnd);
    if (!hdc) {
      return;
    }

    // Get window dimensions
    RECT window_rect;
    GetClientRect(hwnd, &window_rect);

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

    for (size_t i = 0; i < kHelpLines.size(); ++i) {
      const auto line = kHelpLines[i];
      if (line.empty()) {
        continue;
      }

      // Use bright colors on black background
      if (i == 0) {
        SetTextColor(hdc, RGB(255, 255, 255)); // White header
      } else if (line.contains(':')) {
        SetTextColor(hdc, RGB(255, 255, 0)); // Yellow for key controls
      } else {
        SetTextColor(hdc, RGB(200, 200, 200)); // Light gray for descriptions
      }

      TextOut(hdc, margin, margin + (static_cast<int>(i) * line_height),
        line.data(), static_cast<int>(line.size()));
    }

    // Cleanup
    SelectObject(hdc, old_font);
    DeleteObject(font);
    DeleteObject(black_brush);
    ReleaseDC(hwnd, hdc);
#endif
  }

  void ClearHelp()
  {
#ifdef _WIN32
    const HWND hwnd = Window();
    if (!hwnd) {
      return;
    }

    HDC hdc = GetDC(hwnd);
    if (!hdc) {
      return;
    }

    // Get window dimensions and fill with black
    RECT window_rect;
    GetClientRect(hwnd, &window_rect);

    // Force solid black background
    HBRUSH black_brush = CreateSolidBrush(RGB(0, 0, 0));
    FillRect(hdc, &window_rect, black_brush);

    DeleteObject(black_brush);
    ReleaseDC(hwnd, hdc);
#endif
  }
};

void EventLoopRun(Platform& platform)
{
  while (RunState::IsRunning()) {
    platform.Async().PollOne();
    platform.Events().PollOne();
  }
}
} // namespace

template <> struct oxygen::co::EventLoopTraits<Platform> {
  static void Run(Platform& platform) { EventLoopRun(platform); }
  static void Stop(Platform& /*platform*/) { RunState::SetRunning(false); }
  static auto IsRunning(const Platform& /*platform*/) -> bool
  {
    return RunState::IsRunning();
  }
  static auto EventLoopId(const Platform& platform) -> EventLoopID
  {
    return EventLoopID(&platform);
  }
};

namespace {
auto AsyncMain(std::shared_ptr<oxygen::Platform> platform,
  const bool start_fullscreen, const bool show_help_overlay)
  -> oxygen::co::Co<int>
{
  OXCO_WITH_NURSERY(n)
  {
    RunState::SetRunning(true);

    // Activate the live objects with our nursery, making it available for the
    // lifetime of the nursery.
    co_await n.Start(&Platform::ActivateAsync, std::ref(*platform));
    platform->Run();

    // Start a lightweight frame tick task that drives both frame lifecycle
    // hooks each frame. Window closure is discovered at frame end and applied
    // at the next frame start, so both hooks must run continuously. Run at
    // ~60Hz.
    n.Start([platform] -> oxygen::co::Co<> {
      const auto frame_period = std::chrono::milliseconds(16); // ~60 FPS
      while (oxygen::co::EventLoopTraits<Platform>::IsRunning(*platform)) {
        // Advance per-frame state and process pending closures from the
        // previous frame.
        try {
          platform->OnFrameStart();
        } catch (const std::exception& e) {
          LOG_F(WARNING, "Platform::OnFrameStart() threw: {}", e.what());
        } catch (...) {
          LOG_F(WARNING, "Platform::OnFrameStart() threw unknown exception");
        }

        // Scan for new close requests that should be processed on the next
        // frame start.
        try {
          platform->OnFrameEnd();
        } catch (const std::exception& e) {
          LOG_F(WARNING, "Platform::OnFrameEnd() threw: {}", e.what());
        } catch (...) {
          LOG_F(WARNING, "Platform::OnFrameEnd() threw unknown exception");
        }

        co_await platform->Async().SleepFor(frame_period);
      }

      co_return;
    });

    WindowProps props("Oxygen Window Playground - Interactive Controls");
    props.extent = { .width = 800, .height = 600 };
    props.flags = {
      .hidden = false,
      .always_on_top = false,
      .full_screen = start_fullscreen,
      .maximized = false,
      .minimized = false,
      .resizable = true,
      .borderless = false,
    };
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
        } catch (const std::exception& e) {
          LOG_F(ERROR, "Failed to create help renderer: {}", e.what());
        }
      }

      // The help overlay is shown by default; --show-help false hides it.
      if (help_renderer && show_help_overlay) {
        help_renderer->ShowHelp();
      }

      LOG_F(INFO, "H: Toggle detailed help ({})",
        help_renderer ? "On-screen + Console" : "Console only");
    }

    n.Start([window_weak, help_renderer] -> oxygen::co::Co<> {
      bool not_destroyed { true };
      while (not_destroyed && !window_weak.expired()) {
        const auto window = window_weak.lock();
        if (!window) {
          break;
        }

        if (const auto [from, to] = co_await window->Events().UntilChanged();
          to == WindowEvent::kDestroyed) {
          LOG_F(INFO, "My window is destroyed");
          not_destroyed = false;
        } else if (to == WindowEvent::kExposed) {
          LOG_F(INFO, "My window is exposed");
        } else if (to == WindowEvent::kResized) {
          LOG_F(INFO, "My window is resized");
          // Always clear and redraw the window on resize
          if (help_renderer) {
            help_renderer->OnWindowResize();
          }
        }
      }
    });

    n.Start([window_weak, platform] -> oxygen::co::Co<> {
      while (!window_weak.expired()) {
        const auto window = window_weak.lock();
        if (!window) {
          break;
        }

        co_await window->CloseRequested();
        DLOG_F(WARNING, "Press 'y' to close the window, you have 3 seconds...");
        // Wait for the user to press 'y' or for 3 seconds to elapse
        auto [double_close, _] = co_await oxygen::co::AnyOf(
          [&platform] -> oxygen::co::Co<bool> {
            while (true) {
              const auto& event = co_await platform->Events().NextEvent();
              auto _ = co_await platform->Events().Lock();
              auto* sdl_event = event.NativeEventAs<SDL_Event>();
              if (sdl_event->type == SDL_EVENT_KEY_DOWN) {
                if (sdl_event->key.key == SDLK_Y) {
                  co_return true;
                }
              }
            }
          },
          platform->Async().SleepFor(std::chrono::seconds(3)));
        if (!window_weak.expired()) {
          if (double_close) {
            window->VoteToClose();
            break;
          }

          window->VoteNotToClose();
        }
      }
    });

    // Async keyboard input handler for window manipulation
    n.Start([window_weak, platform, &n, help_renderer] -> oxygen::co::Co<> {
      while (!window_weak.expired()) {
        const auto& event = co_await platform->Events().NextEvent();
        auto _ = co_await platform->Events().Lock();
        auto* sdl_event = event.NativeEventAs<SDL_Event>();

        if (sdl_event->type == SDL_EVENT_KEY_DOWN) {
          const auto window = window_weak.lock();
          if (!window) {
            continue;
          }

          constexpr int translate_by = 10;

          // Restore before moving so arrow keys always reposition the window.
          const auto move_window = [&window](int dx, int dy) {
            if (window->Maximized()) {
              window->Restore();
            }

            const auto [pos_x, pos_y] = window->Position();
            window->MoveTo({ .x = pos_x + dx, .y = pos_y + dy });
          };

          switch (sdl_event->key.key) {
          // Arrow keys - Move window position
          case SDLK_LEFT: {
            move_window(-translate_by, 0);
          } break;

          case SDLK_RIGHT: {
            move_window(translate_by, 0);
          } break;

          case SDLK_UP: {
            move_window(0, -translate_by);
          } break;

          case SDLK_DOWN: {
            move_window(0, translate_by);
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
            n.Start([window_weak] -> oxygen::co::Co<> {
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
            n.Start([window_weak] -> oxygen::co::Co<> {
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
            PrintHelpToLog();

            // Display help overlay on window if renderer is available
            if (help_renderer) {
              help_renderer->ToggleHelp();
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

    n.Start([&platform, &n] -> oxygen::co::Co<> {
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

extern "C" auto MainImpl(std::span<const char*> args) -> int
{
  using namespace oxygen::clap; // NOLINT

  bool start_fullscreen = false;
  bool show_help_overlay = true;

  try {
    const auto window_options = std::make_shared<Options>("Window options");
    window_options->Add(Option::WithKey("fullscreen")
        .About("Start the demo in full-screen mode")
        .Short("F")
        .Long("fullscreen")
        .WithValue<bool>()
        .DefaultValue(false)
        .UserFriendlyName("fullscreen")
        .StoreTo(&start_fullscreen)
        .Build());
    window_options->Add(Option::WithKey("show-help")
        .About("Show the on-screen help overlay on startup; set false to "
               "start with it hidden")
        .Long("show-help")
        .WithValue<bool>()
        .DefaultValue(true)
        .UserFriendlyName("show-help")
        .StoreTo(&show_help_overlay)
        .Build());

    const Command::Ptr default_command
      = CommandBuilder(Command::DEFAULT).WithOptions(window_options);
    auto cli = oxygen::host::BuildCli(
      "platform", "Interactive platform window controls demo", default_command);

    auto context = cli->Parse(args);
    if (oxygen::host::HandleMetaCommand(context, default_command)) {
      return EXIT_SUCCESS;
    }

    auto platform
      = std::make_shared<Platform>(PlatformConfig { .headless = false });

    oxygen::co::Run(
      *platform, AsyncMain(platform, start_fullscreen, show_help_overlay));

    // Explicit destruction order due to dependencies.
    platform.reset();
    return EXIT_SUCCESS;
  } catch (const CmdLineArgumentsError& e) {
    LOG_F(ERROR, "Platform CLI parse failed: {}", e.what());
    return EXIT_FAILURE;
  } catch (const std::exception& e) {
    LOG_F(ERROR, "Platform example failed: {}", e.what());
    return EXIT_FAILURE;
  }
}
