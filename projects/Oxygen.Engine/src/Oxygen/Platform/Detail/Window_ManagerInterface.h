//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include "Oxygen/Base/Macros.h"
#include "Oxygen/Platform/Window.h"

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

        virtual void DoRestore() const = 0;
        virtual void DoMaximize() const = 0;
        virtual void DoResize(const ExtentT& extent) const = 0;
        virtual void DoPosition(const PositionT& position) const = 0;

        virtual void DispatchEvent(Event event) const = 0;
        virtual void InitiateClose(co::Nursery& n) = 0;
    };

} // namespace platform::window
} // namespace oxygen
