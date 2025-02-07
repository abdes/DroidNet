//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include "Co.h"
#include "ParkingLot.h"

#include <string>

#include "Oxygen/Base/Macros.h"
#include "Oxygen/Base/Signals.h"
#include "Oxygen/Base/Types/Geometry.h"
#include "Oxygen/OxCo/Value.h"
#include "Oxygen/Platform/Types.h"
#include "Oxygen/Platform/api_export.h"

namespace oxygen::platform {

struct NativeWindowInfo {
    void* window_handle { nullptr };

    // This will contain the HINSTANCE for MS Windows, the display for Wayland;
    // otherwise nullptr.
    void* extra_handle { nullptr };
};

class Window {
public:
    friend class WindowManager;

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
        std::optional<Extent<int64_t>> extent {};
        std::optional<Point<int64_t>> position {};
        InitialFlags flags {};

        // Constructor that requires the title
        explicit Properties(std::string title)
            : title(std::move(title))
        {
        }
    };

    OXYGEN_PLATFORM_API explicit Window(const Properties& props);

    OXYGEN_PLATFORM_API ~Window();

    OXYGEN_MAKE_NON_COPYABLE(Window)
    OXYGEN_MAKE_NON_MOVEABLE(Window)

    [[nodiscard]] OXYGEN_PLATFORM_API auto Id() const -> WindowIdType;
    [[nodiscard]] OXYGEN_PLATFORM_API auto NativeWindow() const -> NativeWindowInfo;
    [[nodiscard]] OXYGEN_PLATFORM_API auto IsValid() const -> bool { return Id() != kInvalidWindowId; }

    // Visibility
    OXYGEN_PLATFORM_API auto Show() const -> void;
    OXYGEN_PLATFORM_API auto Hide() const -> void;

    // Size
    [[nodiscard]] OXYGEN_PLATFORM_API auto IsFullScreen() const -> bool;
    [[nodiscard]] OXYGEN_PLATFORM_API auto IsMaximized() const -> bool;
    [[nodiscard]] OXYGEN_PLATFORM_API auto IsMinimized() const -> bool;
    [[nodiscard]] OXYGEN_PLATFORM_API auto IsResizable() const -> bool;
    [[nodiscard]] OXYGEN_PLATFORM_API auto IsBorderLess() const -> bool;

    [[nodiscard]] OXYGEN_PLATFORM_API auto Size() const -> PixelExtent;
    [[nodiscard]] OXYGEN_PLATFORM_API auto GetFrameBufferSize() const -> PixelExtent;

    OXYGEN_PLATFORM_API void FullScreen(bool full_screen) const;
    OXYGEN_PLATFORM_API void Minimize() const;
    OXYGEN_PLATFORM_API void Maximize() const;
    OXYGEN_PLATFORM_API void Restore();
    OXYGEN_PLATFORM_API void MinimumSize(PixelExtent const& extent) const;
    OXYGEN_PLATFORM_API void MaximumSize(PixelExtent const& extent) const;
    OXYGEN_PLATFORM_API void Resizable(bool resizable) const;
    OXYGEN_PLATFORM_API void Size(PixelExtent const& extent);

    // Position
    [[nodiscard]] OXYGEN_PLATFORM_API auto Position() const -> PixelPosition;
    OXYGEN_PLATFORM_API auto Position(PixelPosition const& position) -> void;

    // Decorations
    OXYGEN_PLATFORM_API void Title(std::string const& title) const;
    [[nodiscard]] OXYGEN_PLATFORM_API auto Title() const -> std::string;

    // Input Focus
    OXYGEN_PLATFORM_API void Activate() const;
    OXYGEN_PLATFORM_API void AlwaysOnTop(bool always_on_top) const;

    // Application initiated close
    OXYGEN_PLATFORM_API void RequestClose(bool force = false) const;
    OXYGEN_PLATFORM_API void RequestNotToClose() const;

    auto Events() const -> co::Value<Event>&
    {
        return events_;
    }

    auto CloseRequested() -> co::Awaitable<> auto { return close_vote_aw_.Park(); }

private:
    auto DoRestore() const -> void;
    auto DoMaximize() const -> void;
    auto DoResize(PixelExtent const& extent) const -> void;
    auto DoPosition(PixelPosition const& position) const -> void;

    void InitiateClose(co::Nursery& n);
    auto DoClose() -> void;
    mutable co::Value<size_t> close_vote_count_ { 0 };
    co::ParkingLot close_vote_aw_;

    mutable co::Value<Event> events_ { Event::kUnknown };
    void DispatchEvent(const Event event) const { events_.Set(event); }

    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace oxygen::platform
