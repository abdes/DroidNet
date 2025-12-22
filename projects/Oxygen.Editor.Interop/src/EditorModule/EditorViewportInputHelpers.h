//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once
#pragma managed(push, off)

#include <cmath>
#include <string_view>

#include <Oxygen/Base/Types/Geometry.h>
#include <Oxygen/Input/InputSnapshot.h>

namespace oxygen::interop::module::viewport {

  [[nodiscard]] inline auto HasAction(const input::InputSnapshot& snapshot,
    std::string_view name) noexcept -> bool {
    return snapshot.GetActionStateFlags(name) != input::ActionState::kNone;
  }

  [[nodiscard]] inline auto GetAxis2DOrZero(const input::InputSnapshot& snapshot,
    std::string_view name) noexcept -> Axis2D {
    if (!HasAction(snapshot, name)) {
      return Axis2D{ .x = 0.0f, .y = 0.0f };
    }
    return snapshot.GetActionValue(name).GetAs<Axis2D>();
  }

  [[nodiscard]] inline auto AccumulateAxis2DFromTransitionsOrZero(
    const input::InputSnapshot& snapshot,
    std::string_view name) noexcept -> Axis2D {
    if (!HasAction(snapshot, name)) {
      return Axis2D{ .x = 0.0f, .y = 0.0f };
    }

    Axis2D delta{ .x = 0.0f, .y = 0.0f };
    bool saw_non_zero = false;
    for (const auto& tr : snapshot.GetActionTransitions(name)) {
      const auto& v = tr.value_at_transition.GetAs<Axis2D>();
      if ((std::abs(v.x) > 0.0f) || (std::abs(v.y) > 0.0f)) {
        delta.x += v.x;
        delta.y += v.y;
        saw_non_zero = true;
      }
    }

    if (saw_non_zero) {
      return delta;
    }

    return GetAxis2DOrZero(snapshot, name);
  }

  [[nodiscard]] inline auto GetAxis1DOrZero(const input::InputSnapshot& snapshot,
    std::string_view name) noexcept -> float {
    if (!HasAction(snapshot, name)) {
      return 0.0f;
    }
    return snapshot.GetActionValue(name).GetAs<Axis1D>().x;
  }

  [[nodiscard]] inline auto AccumulateAxis1DFromTransitionsOrZero(
    const input::InputSnapshot& snapshot,
    std::string_view name) noexcept -> float {
    if (!HasAction(snapshot, name)) {
      return 0.0f;
    }

    float delta = 0.0f;
    bool saw_non_zero = false;
    for (const auto& tr : snapshot.GetActionTransitions(name)) {
      const auto& v = tr.value_at_transition.GetAs<Axis1D>();
      if (std::abs(v.x) > 0.0f) {
        delta += v.x;
        saw_non_zero = true;
      }
    }

    if (saw_non_zero) {
      return delta;
    }

    return GetAxis1DOrZero(snapshot, name);
  }

} // namespace oxygen::interop::module::viewport

#pragma managed(pop)
