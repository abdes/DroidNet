//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "MainModule.h"

#include <random>

#include "Oxygen/Core/Engine.h"
#include "Oxygen/Graphics/Common/Renderer.h"
#include "Oxygen/Input/Action.h"
#include "Oxygen/Input/ActionTriggers.h"
#include "Oxygen/Input/InputActionMapping.h"
#include "Oxygen/Input/InputMappingContext.h"
#include "Oxygen/Input/InputSystem.h"
#include "Oxygen/Input/Types.h"
#include "Oxygen/Platform/Common/Platform.h"
#include "Oxygen/Platform/Common/input.h"
// #include "Oxygen/Graphics/Common/Renderer.h"

using oxygen::Engine;
using oxygen::graphics::Renderer;
using oxygen::input::Action;
using oxygen::input::ActionTriggerPressed;
using oxygen::input::ActionTriggerTap;
using oxygen::input::ActionValueType;
using oxygen::input::InputActionMapping;
using oxygen::input::InputMappingContext;
using oxygen::input::InputSystem;
using oxygen::platform::InputSlots;

const char* const MainModule::LOGGER_NAME = "MainModule";

void MainModule::OnInitialize(const Renderer* /*renderer*/)
{
  // DCHECK_NOTNULL_F(renderer);

  // Initialize the input system.
  player_input_ = std::make_shared<InputSystem>(GetEngine().GetPlatform());

  // Modifier Key actions

  const auto shift = std::make_shared<Action>("shift", ActionValueType::kBool);
  player_input_->AddAction(shift);
  const auto modifier_keys = std::make_shared<InputMappingContext>("modifier keys");
  {
    const auto trigger = std::make_shared<oxygen::input::ActionTriggerDown>();
    trigger->MakeExplicit();
    const auto left_shift_mapping = std::make_shared<InputActionMapping>(shift, InputSlots::LeftShift);
    left_shift_mapping->AddTrigger(trigger);
    modifier_keys->AddMapping(left_shift_mapping);
  }
  player_input_->AddMappingContext(modifier_keys, 1000);

  // Example actions setup

  const auto jump_action = std::make_shared<Action>("jump", ActionValueType::kBool);
  player_input_->AddAction(jump_action);

  const auto jump_higher_action = std::make_shared<Action>("jump higher", ActionValueType::kBool);
  jump_higher_action->SetConsumesInput(true);
  player_input_->AddAction(jump_higher_action);

  const auto swim_up_action = std::make_shared<Action>("swim up", ActionValueType::kBool);
  player_input_->AddAction(swim_up_action);

  // Setup mapping context when moving on the ground
  const auto ground_movement = std::make_shared<InputMappingContext>("ground movement");
  {
    {
      const auto trigger = std::make_shared<ActionTriggerTap>();
      trigger->SetTapReleaseThreshold(0.25F);
      trigger->MakeExplicit();
      const auto mapping = std::make_shared<InputActionMapping>(
        jump_higher_action, InputSlots::Space);
      mapping->AddTrigger(trigger);

      const auto shift_trigger = std::make_shared<oxygen::input::ActionTriggerChain>();
      shift_trigger->SetLinkedAction(player_input_->GetActionByName("shift"));
      shift_trigger->MakeImplicit();
      mapping->AddTrigger(shift_trigger);

      ground_movement->AddMapping(mapping);
    }
    {
      const auto trigger = std::make_shared<ActionTriggerTap>();
      trigger->SetTapReleaseThreshold(0.25F);
      trigger->MakeExplicit();
      const auto mapping = std::make_shared<InputActionMapping>(jump_action, InputSlots::Space);
      mapping->AddTrigger(trigger);

      ground_movement->AddMapping(mapping);
    }

    player_input_->AddMappingContext(ground_movement, 0);
  }

  // Setup mapping context when swimming
  const auto swimming = std::make_shared<InputMappingContext>("swimming");
  {
    auto trigger = std::make_shared<ActionTriggerPressed>();
    auto mapping = std::make_shared<InputActionMapping>(swim_up_action, InputSlots::Space);
    mapping->AddTrigger(trigger);
    swimming->AddMapping(mapping);
    player_input_->AddMappingContext(swimming, 0);
  }

  // Now the player is moving on the ground
  player_input_->ActivateMappingContext(modifier_keys);
  player_input_->ActivateMappingContext(ground_movement);
}

void MainModule::ProcessInput(const oxygen::platform::InputEvent& event)
{
  // TODO: make a module for the input system
  player_input_->ProcessInput(event);
}

void MainModule::Update(const oxygen::Duration delta_time)
{
  const oxygen::engine::SystemUpdateContext update_context {
    .time_since_start = oxygen::Duration::zero(), .delta_time = delta_time
  };
  player_input_->Update(update_context);
}

namespace {

auto CheckLimits(float& direction, float& new_distance) -> void
{
  if (new_distance >= 320.0F) {
    new_distance = 320.0F;
    direction = -1.0F;
  }
  if (new_distance <= 10.0F) {
    new_distance = 10.0F;
    direction = 1.0F;
  }
}

} // namespace

void MainModule::FixedUpdate()
{
  auto new_distance = state_.distance + state_.direction * 2.0F;
  CheckLimits(state_.direction, new_distance);
  state_.distance = new_distance;
}

void MainModule::Render(const Renderer* /*renderer*/)
{
  // DCHECK_NOTNULL_F(renderer);

  // Create a random number core.
  static std::random_device rd;
  static std::mt19937 gen(rd());
  static std::uniform_int_distribution<> distribution(60, 90);

  std::this_thread::sleep_for(std::chrono::milliseconds(distribution(gen)));
}

void MainModule::OnShutdown() noexcept { }
