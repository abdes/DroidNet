//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <memory>

namespace oxygen {

class Platform;
using PlatformPtr = std::shared_ptr<Platform>;

namespace platform {

    class Display;
    class Window;
    class InputEvent;
    class KeyEvent;
    class MouseButtonEvent;
    class MouseWheelEvent;
    class MouseMotionEvent;
    class InputSlot;
    class InputSlots;

    using WindowPtr = std::weak_ptr<Window>;

    using WindowIdType = uint32_t;
    constexpr WindowIdType kInvalidWindowId = 0;

} // namespace platform

} // namespace oxygen::platform
