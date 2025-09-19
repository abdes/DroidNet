//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Input/Test/InputSystemTest.h>

#include <Oxygen/Input/Action.h>
#include <Oxygen/Input/ActionTriggers.h>
#include <Oxygen/Input/InputActionMapping.h>
#include <Oxygen/Input/InputMappingContext.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/OxCo/Run.h>
#include <Oxygen/Platform/Input.h>

using oxygen::input::Action;
using oxygen::input::ActionTriggerChain;
using oxygen::input::ActionTriggerDown;
using oxygen::input::ActionTriggerHoldAndRelease;
using oxygen::input::ActionTriggerPressed;
using oxygen::input::ActionTriggerTap;
using oxygen::input::InputActionMapping;
using oxygen::input::InputMappingContext;
using oxygen::input::testing::InputSystemTest;
using oxygen::platform::ButtonState;
using oxygen::platform::InputSlots;
using oxygen::platform::Key;

namespace {

class InputSystemIntegrationTest : public InputSystemTest { };

//! Integration: realistic combat input with history log and validation
/*!
 Simulates a simple combat scheme:
 - Attack (Tap J)
 - ChargedAttack (Hold-and-release J, >= 0.5s)
 - Jump (Space)
 - Move (W/A/S/D Down)
 - Roll (K)
 - DodgeRoll (K while Move is ongoing; implemented as Chain(Move) + Pressed)

 For each simulation frame, we log: "[F#] <events> -> <triggered actions>"
 and compare the resulting history with an expected sequence.
*/
NOLINT_TEST_F(
  InputSystemIntegrationTest, CombatScenario_Integration_HistoryMatches)
{
  oxygen::co::Run(loop_, [&]() -> oxygen::co::Co<> {
    // Arrange ---------------------------------------------------------------
    auto attack = std::make_shared<Action>(
      "Attack", oxygen::input::ActionValueType::kBool);
    auto charged = std::make_shared<Action>(
      "ChargedAttack", oxygen::input::ActionValueType::kBool);
    auto jump
      = std::make_shared<Action>("Jump", oxygen::input::ActionValueType::kBool);
    auto move
      = std::make_shared<Action>("Move", oxygen::input::ActionValueType::kBool);
    auto roll
      = std::make_shared<Action>("Roll", oxygen::input::ActionValueType::kBool);
    auto dodge = std::make_shared<Action>(
      "DodgeRoll", oxygen::input::ActionValueType::kBool);

    // Consumption rules so mutually-exclusive actions suppress others
    attack->SetConsumesInput(true);
    charged->SetConsumesInput(true);
    roll->SetConsumesInput(true);
    dodge->SetConsumesInput(true);

    input_system_->AddAction(attack);
    input_system_->AddAction(charged);
    input_system_->AddAction(jump);
    input_system_->AddAction(move);
    input_system_->AddAction(roll);
    input_system_->AddAction(dodge);

    auto ctx = std::make_shared<InputMappingContext>("combat");

    // Jump: Space Pressed
    {
      auto m = std::make_shared<InputActionMapping>(jump, InputSlots::Space);
      auto t = std::make_shared<ActionTriggerPressed>();
      t->MakeExplicit();
      m->AddTrigger(t);
      ctx->AddMapping(m);
    }

    // Attack: Tap(J) < 0.25s (placed before Charged so it wins on tap)
    {
      auto m = std::make_shared<InputActionMapping>(attack, InputSlots::J);
      auto tap = std::make_shared<ActionTriggerTap>();
      tap->SetTapTimeThreshold(0.25F);
      tap->MakeExplicit();
      m->AddTrigger(tap);
      ctx->AddMapping(m);
    }

    // Charged: Hold-and-Release(J) >= 0.5s
    {
      auto m = std::make_shared<InputActionMapping>(charged, InputSlots::J);
      auto hold = std::make_shared<ActionTriggerHoldAndRelease>();
      hold->SetHoldDurationThreshold(0.5F);
      hold->MakeExplicit();
      m->AddTrigger(hold);
      ctx->AddMapping(m);
    }

    // Move: W/A/S/D Down (treat as boolean movement on). We'll use W in the
    // scenario, but wire all 4 for completeness.
    auto add_move_map = [&](const auto& slot) {
      auto m = std::make_shared<InputActionMapping>(move, slot);
      auto d = std::make_shared<ActionTriggerDown>();
      d->MakeExplicit();
      m->AddTrigger(d);
      ctx->AddMapping(m);
    };
    add_move_map(InputSlots::W);
    add_move_map(InputSlots::A);
    add_move_map(InputSlots::S);
    add_move_map(InputSlots::D);

    // DodgeRoll: Chain(Move) implicit + Pressed(K) explicit (placed before
    // Roll)
    {
      auto m = std::make_shared<InputActionMapping>(dodge, InputSlots::K);
      // Explicit local press
      {
        auto p = std::make_shared<ActionTriggerPressed>();
        p->MakeExplicit();
        m->AddTrigger(p);
      }
      // Implicit prerequisite: Move must be ongoing at press
      {
        auto c = std::make_shared<ActionTriggerChain>();
        c->SetLinkedAction(move);
        c->RequirePrerequisiteHeld(true);
        c->MakeImplicit();
        m->AddTrigger(c);
      }
      ctx->AddMapping(m);
    }

    // Roll: Pressed(K) (fallback when not moving)
    {
      auto m = std::make_shared<InputActionMapping>(roll, InputSlots::K);
      auto p = std::make_shared<ActionTriggerPressed>();
      p->MakeExplicit();
      m->AddTrigger(p);
      ctx->AddMapping(m);
    }

    // Register context
    input_system_->AddMappingContext(ctx, 0);
    input_system_->ActivateMappingContext(ctx);

    // Helpers ---------------------------------------------------------------
    auto key_to_str = [](Key k) -> const char* {
      switch (k) {
      case Key::kSpace:
        return "Space";
      case Key::kJ:
        return "J";
      case Key::kK:
        return "K";
      case Key::kW:
        return "W";
      case Key::kA:
        return "A";
      case Key::kS:
        return "S";
      case Key::kD:
        return "D";
      default:
        return "?";
      }
    };
    auto state_to_str = [](ButtonState s) -> const char* {
      return s == ButtonState::kPressed ? "Pressed" : "Released";
    };

    struct FrameStep {
      std::vector<std::pair<Key, ButtonState>> events;
      int dt_ms { 0 }; // Game delta time in milliseconds for this frame
    };

    // Build scenario frames -------------------------------------------------
    std::vector<FrameStep> steps = {
      // F1: Jump
      FrameStep { { { Key::kSpace, ButtonState::kPressed } }, 0 },
      // F2: Attack (tap J in same frame)
      FrameStep {
        {
          { Key::kJ, ButtonState::kPressed },
          { Key::kJ, ButtonState::kReleased },
        },
        0,
      },
      // F3..F6: Charged (hold J across time, release)
      FrameStep { { { Key::kJ, ButtonState::kPressed } }, 0 }, // F3
      FrameStep { {}, 300 }, // F4 +300ms
      FrameStep { {}, 250 }, // F5 +250ms (total 550ms)
      FrameStep { { { Key::kJ, ButtonState::kReleased } }, 0 }, // F6
      // F7-F9: Dodge roll while moving (W held, then K press, then stop move)
      FrameStep { { { Key::kW, ButtonState::kPressed } }, 0 }, // F7
      FrameStep { { { Key::kK, ButtonState::kPressed } }, 0 }, // F8
      FrameStep { { { Key::kW, ButtonState::kReleased } }, 0 }, // F9
      // F10: Roll without movement
      FrameStep { { { Key::kK, ButtonState::kPressed } }, 0 }, // F10
    };

    // Expected log ----------------------------------------------------------
    std::vector<std::string> expected = {
      "[F1] Space Pressed -> Jump",
      "[F2] J Pressed, J Released -> Attack",
      "[F3] J Pressed -> None",
      "[F4] No Input -> None",
      "[F5] No Input -> None",
      "[F6] J Released -> ChargedAttack",
      "[F7] W Pressed -> Move",
      // Move remains ongoing; 'Down' may also trigger on the K press update.
      // We include both to reflect per-frame triggers.
      "[F8] K Pressed -> Move, DodgeRoll",
      "[F9] W Released -> None",
      "[F10] K Pressed -> Roll",
    };

    // Act + Log -------------------------------------------------------------
    std::vector<std::string> history;
    history.reserve(steps.size());

    auto engine_tag = oxygen::engine::internal::EngineTagFactory::Get();

    for (size_t i = 0; i < steps.size(); ++i) {
      const auto frame_index = static_cast<int>(i + 1);
      const auto& step = steps[i];

      // Inject input events for this frame
      std::string ev_desc;
      if (step.events.empty()) {
        ev_desc = "No Input";
      } else {
        bool first = true;
        for (const auto& [k, s] : step.events) {
          SendKeyEvent(k, s);
          if (!first) {
            ev_desc += ", ";
          }
          ev_desc += key_to_str(k);
          ev_desc += " ";
          ev_desc += state_to_str(s);
          first = false;
        }
      }

      // Set timing
      {
        oxygen::engine::ModuleTimingData timing;
        timing.game_delta_time = std::chrono::milliseconds(step.dt_ms);
        timing.fixed_delta_time = std::chrono::milliseconds(step.dt_ms);
        frame_context_->SetModuleTimingData(timing, engine_tag);
      }

      // Process a frame through the InputSystem
      input_system_->OnFrameStart(*frame_context_);
      co_await input_system_->OnInput(*frame_context_);

      // Gather triggered actions for this frame
      std::vector<std::string_view> fired;
      auto capture = [&](const std::shared_ptr<Action>& a) {
        if (a->WasTriggeredThisFrame()) {
          fired.emplace_back(a->GetName());
        }
      };
      capture(jump);
      capture(attack);
      capture(charged);
      capture(move);
      capture(dodge);
      capture(roll);

      std::string act_desc;
      if (fired.empty()) {
        act_desc = "None";
      } else {
        for (size_t j = 0; j < fired.size(); ++j) {
          if (j > 0)
            act_desc += ", ";
          act_desc += std::string(fired[j]);
        }
      }

      history.emplace_back("[F" + std::to_string(frame_index) + "] " + ev_desc
        + " -> " + act_desc);

      // Finalize frame phases
      input_system_->OnSnapshot(*frame_context_);
      input_system_->OnFrameEnd(*frame_context_);
    }

    // Assert ---------------------------------------------------------------
    EXPECT_EQ(history, expected);

    co_return;
  });
}

} // namespace
