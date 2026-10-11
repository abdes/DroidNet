//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>

#include <Oxygen/Base/Types/Geometry.h>
#include <Oxygen/Core/FrameContext.h>
#include <Oxygen/Input/Action.h>
#include <Oxygen/Input/ActionTriggers.h>
#include <Oxygen/Input/InputActionMapping.h>
#include <Oxygen/Input/InputMappingContext.h>
#include <Oxygen/Input/Test/InputSystemTest.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/Platform/Input.h>

namespace oxygen::input::testing {

//! Fixture for gameplay scenarios driven through the real InputSystem.
/*!
 Each frame does what the engine does: inject platform events, write
 ModuleTimingData, then run kFrameStart and kInput. Assertions state what the
 player or the gameplay programmer expects.
*/
class ScenarioTest : public InputSystemTest {
protected:
  using Duration = std::chrono::microseconds;

  static constexpr Duration kFrame60 { 16'667 };

  //! Priority of a context layered above the default (0) gameplay context.
  static constexpr int32_t kOverlayPriority = 10;

  //! Frame time deltas fed to ModuleTimingData for one engine frame.
  struct FrameTime {
    //! ModuleTimingData::game_delta_time (scaled, zero while paused).
    Duration game;
    //! ModuleTimingData::physical_delta_time (the real frame time).
    Duration physical;
  };

  //! One engine frame with independent game and physical time deltas.
  auto Frame(FrameTime dt) -> co::Co<>;

  //! One frame where game and physical time advance equally.
  auto Frame(Duration dt = kFrame60) -> co::Co<>
  {
    return Frame(FrameTime { .game = dt, .physical = dt });
  }

  auto MakeAction(const std::string& name, ActionValueType type)
    -> std::shared_ptr<Action>;

  auto MakeContext(const std::string& name, int32_t priority)
    -> std::shared_ptr<InputMappingContext>;

  //! Add a mapping with one trigger; returns it for further configuration.
  static auto Map(InputMappingContext& context,
    const std::shared_ptr<Action>& action, const platform::InputSlot& slot,
    std::shared_ptr<ActionTrigger> trigger)
    -> std::shared_ptr<InputActionMapping>;

  //! An explicit trigger of type T.
  template <typename T> static auto Explicit() -> std::shared_ptr<T>
  {
    auto trigger = std::make_shared<T>();
    trigger->MakeExplicit();
    return trigger;
  }

  static auto Vec(const Action& action) -> Axis2D
  {
    return action.GetValue().GetAs<Axis2D>();
  }
};

} // namespace oxygen::input::testing
