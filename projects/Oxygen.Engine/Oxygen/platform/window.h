//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <string>

#include "oxygen/api_export.h"
#include "oxygen/base/macros.h"
#include "oxygen/base/signal.hpp"
#include "oxygen/platform/types.h"

namespace oxygen::platform {
  class BaseWindow;

  struct NativeWindowInfo
  {
    void* window_handle{ nullptr };

    // This will contain the HINSTANCE for MS Windows, the display for Wayland;
    // otherwise nullptr.
    void* extra_handle{ nullptr };
  };

  class Window
  {
  public:
    struct InitialFlags
    {
      // Visibility
      bool hidden : 1;
      bool always_on_top : 1;
      // Size
      bool full_screen : 1;
      bool maximized : 1;
      bool minimized : 1;
      bool resizable : 1;
      // Decorations
      bool borderless : 1;
    };

    OXYGEN_API Window();
    OXYGEN_API virtual ~Window();

    OXYGEN_MAKE_NON_COPYABLE(Window);
    OXYGEN_MAKE_NON_MOVEABLE(Window);

    [[nodiscard]] virtual auto Id() const->WindowIdType = 0;
    [[nodiscard]] virtual auto NativeWindow() const->NativeWindowInfo = 0;

    // Visibility
    virtual auto Show() -> void = 0;
    virtual auto Hide() -> void = 0;

    // Size
    virtual auto FullScreen(bool full_screen) -> void = 0;
    [[nodiscard]] virtual auto IsFullScreen() const -> bool = 0;
    [[nodiscard]] virtual auto IsMaximized() const -> bool = 0;
    virtual auto Minimize() -> void = 0;
    [[nodiscard]] virtual auto IsMinimized() const -> bool = 0;
    [[nodiscard]] virtual auto Size() const->PixelExtent = 0;
    virtual auto MinimumSize(PixelExtent const& extent) -> void = 0;
    virtual auto MaximumSize(PixelExtent const& extent) -> void = 0;
    virtual auto Resizable(bool resizable) -> void = 0;
    [[nodiscard]] virtual auto IsResizable() const -> bool = 0;
    [[nodiscard]] virtual auto IsBorderLess() const -> bool = 0;
    OXYGEN_API auto Maximize() -> void;
    OXYGEN_API auto Restore() -> void;
    OXYGEN_API auto Size(PixelExtent const& extent) -> void;
    OXYGEN_API [[nodiscard]] auto OnResized() const
      ->sigslot::signal<PixelExtent>&;
    OXYGEN_API [[nodiscard]] auto OnMinimized() const->sigslot::signal<>&;
    OXYGEN_API [[nodiscard]] auto OnRestored() const->sigslot::signal<>&;
    OXYGEN_API [[nodiscard]] auto OnMaximized() const->sigslot::signal<>&;

    // Position
    [[nodiscard]] virtual auto Position() const->PixelPosition = 0;
    OXYGEN_API auto Position(PixelPosition const& position) -> void;

    // Decorations
    virtual auto Title(std::string const& title) -> void = 0;
    [[nodiscard]] virtual auto Title() const->std::string = 0;

    // Input Focus
    virtual auto Activate() -> void = 0;
    virtual auto AlwaysOnTop(bool always_on_top) -> void = 0;

    // Application initiated close
    OXYGEN_API auto RequestNotToClose() const -> void;
    OXYGEN_API auto RequestClose(bool force = false) -> void;

    OXYGEN_API [[nodiscard]] auto ShouldClose() const -> bool;

    OXYGEN_API [[nodiscard]] auto OnCloseRequested() const
      ->sigslot::signal<bool>&;

    [[nodiscard]] virtual auto GetFrameBufferSize() const->PixelExtent = 0;

  protected:
    virtual auto DoRestore() -> void = 0;
    virtual auto DoMaximize() -> void = 0;
    virtual auto DoResize(PixelExtent const& extent) -> void = 0;
    virtual auto DoPosition(PixelPosition const& position) -> void = 0;

    virtual auto ProcessCloseRequest(bool force) -> void = 0;

  private:
    BaseWindow* pimpl_;
  };

}  // namespace oxygen::platform
