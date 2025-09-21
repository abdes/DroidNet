//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Platform/Window.h>

namespace oxygen {
namespace co {
  class Nursery;
} // namespace co

namespace platform::window {

  class ManagerInterface {
  public:
    ManagerInterface() = default;
    virtual ~ManagerInterface() = default;
    OXYGEN_MAKE_NON_COPYABLE(ManagerInterface)
    OXYGEN_MAKE_NON_MOVABLE(ManagerInterface)

    virtual auto DoRestore() const -> void = 0;
    virtual auto DoMaximize() const -> void = 0;
    virtual auto DoResize(const ExtentT& extent) const -> void = 0;
    virtual auto DoPosition(const PositionT& position) const -> void = 0;

    virtual auto DispatchEvent(Event event) const -> void = 0;
    virtual auto InitiateClose(co::Nursery& n) -> void = 0;

    //! Check if this window is pending close (for WindowManager use only)
    virtual auto IsPendingClose() const -> bool = 0;

    //! Actually destroy the native window (for WindowManager use only)
    virtual auto DestroyNativeWindow() -> void = 0;
  };

} // namespace platform::window
} // namespace oxygen
