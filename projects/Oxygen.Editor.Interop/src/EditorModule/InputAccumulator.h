//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//
#pragma once

#include <mutex>
#include <type_traits>
#include <unordered_map>
#include <vector>

#include <api_export.h>
#include <Oxygen/Base/Types/Geometry.h>
#include <Oxygen/Core/Time/Types.h>
#include <Oxygen/Core/Types/View.h>
#include <Oxygen/Platform/Input.h>
#include <Oxygen/Platform/Types.h>

namespace oxygen::interop::module {

  // Use the engine's canonical ViewId when available. Ensure it can be
  // constructed from the platform's WindowIdType to allow safe reuse.
  using ViewId = ::oxygen::ViewId;

  static_assert(std::is_constructible_v<ViewId, platform::WindowIdType>,
    "::oxygen::ViewId must be constructible from "
    "platform::WindowIdType to reuse WindowIdType for ViewId.");

  struct EditorKeyEvent {
    platform::Key key{ platform::Key::kNone };
    bool pressed{ false };
    oxygen::time::PhysicalTime timestamp{};
    SubPixelPosition position{};
    bool repeat{ false };
  };

  struct EditorButtonEvent {
    platform::MouseButton button{ platform::MouseButton::kNone };
    bool pressed{ false };
    oxygen::time::PhysicalTime timestamp{};
    SubPixelPosition position{};
  };

  struct EditorMouseMotionEvent {
    SubPixelMotion motion{};
    SubPixelPosition position{};
    oxygen::time::PhysicalTime timestamp{};
  };

  struct EditorMouseWheelEvent {
    SubPixelMotion scroll{};
    SubPixelPosition position{};
    oxygen::time::PhysicalTime timestamp{};
  };

  struct AccumulatedInput {
    SubPixelMotion mouse_delta{};  // dx, dy accumulated per-frame
    SubPixelMotion scroll_delta{}; // scroll as motion (x,y)
    SubPixelPosition last_position{}; // last known pointer position
    std::vector<EditorKeyEvent> key_events{};
    std::vector<EditorButtonEvent> button_events{};
  };

  class EditorModule; // forward-declare to allow friendship

  class InputAccumulator {
  public:
    InputAccumulator() = default;
    ~InputAccumulator() = default;

    // Non-copyable and non-movable: internal state holds mutexes and
    // other non-copyable members. Explicitly delete to make intent clear.
    InputAccumulator(InputAccumulator const&) = delete;
    InputAccumulator& operator=(InputAccumulator const&) = delete;
    InputAccumulator(InputAccumulator&&) = delete;
    InputAccumulator& operator=(InputAccumulator&&) = delete;

    // Pushers called by host/UI thread(s)
    OXGN_EI_API void PushMouseMotion(ViewId view, EditorMouseMotionEvent ev) noexcept;
    OXGN_EI_API void PushMouseWheel(ViewId view, EditorMouseWheelEvent ev) noexcept;
    OXGN_EI_API void PushKeyEvent(ViewId view, EditorKeyEvent ev) noexcept;
    OXGN_EI_API void PushButtonEvent(ViewId view, EditorButtonEvent ev) noexcept;

    // On focus lost: discard accumulated deltas (mouse + scroll) but keep ordered
    // key/button events.
    OXGN_EI_API void OnFocusLost(ViewId view) noexcept;

  protected:
    // Drain returns accumulated input for the view and resets accumulators for
    // that view. This method is protected so test shims can access it; only
    // production code (e.g. `EditorModule`) should call it in normal usage.
    OXGN_EI_API AccumulatedInput Drain(ViewId view) noexcept;

    private:
    struct ViewportAccumulator {
      std::mutex mutex{};
      SubPixelMotion mouse_delta{};
      SubPixelMotion scroll_delta{};
      SubPixelPosition last_position{};
      std::vector<EditorKeyEvent> key_events{};
      std::vector<EditorButtonEvent> button_events{};
    };

    // Guard for the map structure itself
    std::mutex map_mutex_{};
    std::unordered_map<ViewId, ViewportAccumulator> views_{};

    ViewportAccumulator& EnsureViewport(ViewId view) noexcept;

    friend class EditorModule;
  };

} // namespace oxygen::interop::module
