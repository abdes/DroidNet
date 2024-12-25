//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <oxygen/base/macros.h>
#include <oxygen/base/types.h>

#include <oxygen/base/signal.hpp>

namespace oxygen::platform {

  class Window;

  class BaseWindow
  {
  public:
    BaseWindow() = default;
    ~BaseWindow();

    OXYGEN_MAKE_NON_COPYABLE(BaseWindow);
    OXYGEN_MAKE_NON_MOVEABLE(BaseWindow);

    auto RequestNotToClose() -> void {
      if (!forced_close_)
        should_close_ = false;
    }
    auto RequestClose(bool force) -> void;

    [[nodiscard]] auto ShouldClose() const -> bool { return should_close_; }

    auto OnCloseRequested() -> auto& { return on_close_requested_; }
    auto OnClosing() -> auto& { return on_closing_; }

    auto OnResized() -> auto& { return on_resized_; }

    auto OnMinimized() -> auto& { return on_minimized_; }

    auto OnMaximized() -> auto& { return on_maximized_; }

    auto OnRestored() -> auto& { return on_restored_; }

    void CancelCloseRequest() {
      should_close_ = false;
      forced_close_ = false;
    }

    void NotifyClosing() {
      OnClosing()();
    }

  private:
    bool should_close_{ false };
    bool forced_close_{ false };
    sigslot::signal<bool> on_close_requested_;
    sigslot::signal<> on_closing_;
    sigslot::signal<PixelExtent> on_resized_;
    sigslot::signal<> on_minimized_;
    sigslot::signal<> on_maximized_;
    sigslot::signal<> on_restored_;
  };

}  // namespace oxygen::platform
