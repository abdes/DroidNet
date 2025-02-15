//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <string>

#include "Oxygen/Base/Macros.h"
#include "Oxygen/Base/Types/Geometry.h"
#include "Oxygen/Composition/Composition.h"
#include "Oxygen/OxCo/ParkingLot.h"
#include "Oxygen/OxCo/Value.h"
#include "Oxygen/Platform/Types.h"
#include "Oxygen/Platform/api_export.h"

namespace oxygen::platform {

namespace window {

    struct NativeHandles {
        //! Opaque window handle, platform dependent.
        void* window_handle { nullptr };

        //! This will contain the `HINSTANCE` for MS Windows, the display for
        //! Wayland; otherwise `nullptr`.
        void* extra_handle { nullptr };
    };

    using ExtentT = Extent<uint32_t>;
    using PositionT = Point<uint32_t>;

    enum class Event : uint32_t { // NOLINT(performance-enum-size) - consistent with implementation
        kUnknown = 0,
        kShown, //!< Window has been shown
        kHidden, //!< Window has been hidden
        kExposed, //!< Window has been exposed and should be redrawn
        kMoved, //!< Window position has changed
        kResized, //!< Window size has changed
        kPixelSizeChanged, //!< The pixel size of the window has changed
        kMetalViewResized, //!< The pixel size of a Metal view associated with the window has changed
        kMinimized, //!< Window has been minimized
        kMaximized, //!< Window has been maximized
        kRestored, //!< Window has been restored to normal size and position
        kMouseEnter, //!< Window has gained mouse focus
        kMouseLeave, //!< Window has lost mouse focus
        kFocusGained, //!< Window has gained keyboard focus
        kFocusLost, //!< Window has lost keyboard focus
        kCloseRequested, //!< The window manager requests that the window be closed
        kIccProfChanged, //!< The ICC profile of the window's display has changed
        kDisplayChanged, //!< Window has been moved to a different display
        kDisplayScaleChanged, //!< Window display scale has been changed
        kSafeAreaChanged, //!< The window safe area has been changed
        kOccluded, //!< The window has been occluded
        kEnterFullscreen, //!< The window has entered fullscreen mode
        kLeaveFullscreen, //!< The window has left fullscreen mode
        kDestroyed, //!< The window is about to be destroyed and should not be used after this event
        kHdrStateChanged //!< Window HDR properties have changed
    };

    struct InitialFlags {
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

    struct Properties {
        std::string title;
        std::optional<ExtentT> extent;
        std::optional<ExtentT> min_extent;
        std::optional<ExtentT> max_extent;
        std::optional<PositionT> position;
        InitialFlags flags {};

        // Constructor that requires the title
        explicit Properties(std::string title)
            : title(std::move(title))
        {
        }
    };
    class ManagerInterface;

} // namespace window

class Window : public Composition {
    class Data; //!< Component, holds the window data.
    class ManagerInterfaceImpl; //!< Component, window management functionality.

public:
    OXYGEN_PLATFORM_API explicit Window(const window::Properties& props);

    OXYGEN_PLATFORM_API ~Window() override;

    OXYGEN_MAKE_NON_COPYABLE(Window)
    OXYGEN_MAKE_NON_MOVEABLE(Window)

    [[nodiscard]] OXYGEN_PLATFORM_API auto Id() const -> WindowIdType;
    [[nodiscard]] OXYGEN_PLATFORM_API auto Native() const -> window::NativeHandles;
    [[nodiscard]] OXYGEN_PLATFORM_API auto Valid() const -> bool;
    [[nodiscard]] OXYGEN_PLATFORM_API auto Size() const -> window::ExtentT;
    [[nodiscard]] OXYGEN_PLATFORM_API auto FrameBufferSize() const -> window::ExtentT;
    [[nodiscard]] OXYGEN_PLATFORM_API auto FullScreen() const -> bool;
    [[nodiscard]] OXYGEN_PLATFORM_API auto Maximized() const -> bool;
    [[nodiscard]] OXYGEN_PLATFORM_API auto Minimized() const -> bool;
    [[nodiscard]] OXYGEN_PLATFORM_API auto Resizable() const -> bool;
    [[nodiscard]] OXYGEN_PLATFORM_API auto BorderLess() const -> bool;
    [[nodiscard]] OXYGEN_PLATFORM_API auto Position() const -> window::PositionT;
    [[nodiscard]] OXYGEN_PLATFORM_API auto Title() const -> std::string;

    OXYGEN_PLATFORM_API auto Show() const -> void;
    OXYGEN_PLATFORM_API auto Hide() const -> void;
    OXYGEN_PLATFORM_API void EnterFullScreen() const;
    OXYGEN_PLATFORM_API void ExitFullScreen() const;
    OXYGEN_PLATFORM_API void Minimize() const;
    OXYGEN_PLATFORM_API void Maximize() const;
    OXYGEN_PLATFORM_API void Restore() const;
    OXYGEN_PLATFORM_API void SetMinimumSize(const window::ExtentT& extent) const;
    OXYGEN_PLATFORM_API void SetMaximumSize(const window::ExtentT& extent) const;
    OXYGEN_PLATFORM_API void EnableResizing() const;
    OXYGEN_PLATFORM_API void DisableResizing() const;
    OXYGEN_PLATFORM_API void Resize(const window::ExtentT& extent) const;
    OXYGEN_PLATFORM_API auto MoveTo(const window::PositionT& position) const -> void;
    OXYGEN_PLATFORM_API void SetTitle(const std::string& title) const;
    OXYGEN_PLATFORM_API void Activate() const;
    OXYGEN_PLATFORM_API void KeepAlwaysOnTop(bool always_on_top = true) const;

    // Application initiated close
    OXYGEN_PLATFORM_API void RequestClose(bool force = false) const;
    OXYGEN_PLATFORM_API void VoteNotToClose() const;
    OXYGEN_PLATFORM_API void VoteToClose() const;

    //! Returns an awaitable that when `co_await`ed, suspends the caller until a
    //! close request for the window is made.
    /*!
     This event when the window is requested to be closed, either by the user or
     programmatically. In both cases, the request is not immediately granted. A
     vote is taken from all awaken tasks, and the window is closed only if all
     parties agree.

     \see RequestClose(), VoteNotToClose(), VoteToClose()
    */
    OXYGEN_PLATFORM_API auto CloseRequested() const -> co::ParkingLot::Awaiter;

    //! Returns an awaitable `Value` that contains the last window event to
    //! occur. When `co_await`ed, it suspends the caller until the next event
    //! occurs and its value meets the awaited condition.
    /*!
     Window events do not convey any data other than the event type. Use the
     Window API to query the window properties and state when the event occurs.

     \see `oxygen::co::Value` for all the possible condition.
    */
    [[nodiscard]]
    OXYGEN_PLATFORM_API auto Events() const -> co::Value<window::Event>&;

    //! Get a reference to this window management interface. For internal use
    //! only, hence, its symbol should not be exported.
    auto GetManagerInterface() const -> window::ManagerInterface&;
};

} // namespace oxygen::platform
