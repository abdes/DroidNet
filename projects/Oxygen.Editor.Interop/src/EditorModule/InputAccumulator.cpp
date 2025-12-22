//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma unmanaged

#include "pch.h"

#include <EditorModule/InputAccumulator.h>

namespace oxygen::interop::module {

  static inline oxygen::time::PhysicalTime NowPhysicalTime() noexcept {
    using namespace std::chrono;
    return oxygen::time::PhysicalTime{ steady_clock::now() };
  }

  InputAccumulator::ViewportAccumulator&
    InputAccumulator::EnsureViewport(ViewId view) noexcept {
    std::scoped_lock lock(map_mutex_);
    auto it = views_.find(view);
    if (it == views_.end()) {
      auto ins_it =
        views_
        .emplace(std::piecewise_construct, std::forward_as_tuple(view),
          std::forward_as_tuple())
        .first;
      return ins_it->second;
    }
    return it->second;
  }

  void InputAccumulator::PushMouseMotion(ViewId view,
    EditorMouseMotionEvent ev) noexcept {
    ViewportAccumulator& v = EnsureViewport(view);
    std::scoped_lock lock(v.mutex);
    v.mouse_delta.dx += ev.motion.dx;
    v.mouse_delta.dy += ev.motion.dy;
    v.last_position = ev.position;
  }

  void InputAccumulator::PushMouseWheel(ViewId view,
    EditorMouseWheelEvent ev) noexcept {
    ViewportAccumulator& v = EnsureViewport(view);
    std::scoped_lock lock(v.mutex);
    v.scroll_delta.dx += ev.scroll.dx;
    v.scroll_delta.dy += ev.scroll.dy;
    v.last_position = ev.position;
  }

  void InputAccumulator::PushKeyEvent(ViewId view, EditorKeyEvent ev) noexcept {
    ViewportAccumulator& v = EnsureViewport(view);
    if (ev.timestamp == oxygen::time::PhysicalTime{}) {
      ev.timestamp = NowPhysicalTime();
    }
    std::scoped_lock lock(v.mutex);
    v.key_events.push_back(ev);
    v.last_position = ev.position;
  }

  void InputAccumulator::PushButtonEvent(ViewId view,
    EditorButtonEvent ev) noexcept {
    ViewportAccumulator& v = EnsureViewport(view);
    if (ev.timestamp == oxygen::time::PhysicalTime{}) {
      ev.timestamp = NowPhysicalTime();
    }
    std::scoped_lock lock(v.mutex);
    v.button_events.push_back(ev);
    v.last_position = ev.position;
  }

  AccumulatedInput InputAccumulator::Drain(ViewId view) noexcept {
    AccumulatedInput out{};

    // If viewport does not exist, return empty
    {
      std::scoped_lock map_lock(map_mutex_);
      auto it = views_.find(view);
      if (it == views_.end()) {
        return out;
      }
    }

    ViewportAccumulator& v = EnsureViewport(view);
    std::scoped_lock lock(v.mutex);
    out.mouse_delta = v.mouse_delta;
    out.scroll_delta = v.scroll_delta;
    out.last_position = v.last_position;
    out.key_events = std::move(v.key_events);
    out.button_events = std::move(v.button_events);

    // reset accumulators
    v.mouse_delta = SubPixelMotion{};
    v.scroll_delta = SubPixelMotion{};
    v.last_position = SubPixelPosition{};

    return out;
  }

  void InputAccumulator::OnFocusLost(ViewId view) noexcept {
    ViewportAccumulator& v = EnsureViewport(view);
    std::scoped_lock lock(v.mutex);
    v.mouse_delta = SubPixelMotion{};
    v.scroll_delta = SubPixelMotion{};
  }

} // namespace oxygen::interop::module
