//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once
#pragma managed(push, off)

#include <Oxygen/Base/Types/Geometry.h>
#include <Oxygen/Core/Types/AssetKey.h>
#include <Oxygen/Input/InputSnapshot.h>

namespace oxygen::interop::module::viewport {

  [[nodiscard]] inline auto HasAction(const input::InputSnapshot& snapshot,
    const data::AssetKey& action) noexcept -> bool {
    return snapshot.GetActionStateFlags(action) != input::ActionState::kNone;
  }

  [[nodiscard]] inline auto GetAxis2DOrZero(const input::InputSnapshot& snapshot,
    const data::AssetKey& action) noexcept -> Axis2D {
    if (!HasAction(snapshot, action)) {
      return Axis2D{ .x = 0.0f, .y = 0.0f };
    }
    return snapshot.GetActionValue(action).GetAs<Axis2D>();
  }

  [[nodiscard]] inline auto GetAxis1DOrZero(const input::InputSnapshot& snapshot,
    const data::AssetKey& action) noexcept -> float {
    if (!HasAction(snapshot, action)) {
      return 0.0f;
    }
    return snapshot.GetActionValue(action).GetAs<Axis1D>().x;
  }

} // namespace oxygen::interop::module::viewport

#pragma managed(pop)
