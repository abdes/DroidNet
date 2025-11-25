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
using oxygen::input::ActionValueType;
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
    auto attack = std::make_shared<Action>("Attack", ActionValueType::kBool);
    auto charged
      = std::make_shared<Action>("ChargedAttack", ActionValueType::kBool);
    auto jump = std::make_shared<Action>("Jump", ActionValueType::kBool);
    auto move = std::make_shared<Action>("Move", ActionValueType::kBool);
    auto roll = std::make_shared<Action>("Roll", ActionValueType::kBool);
    auto dodge = std::make_shared<Action>("DodgeRoll", ActionValueType::kBool);

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
        timing.game_delta_time = oxygen::time::CanonicalDuration {
          std::chrono::milliseconds(step.dt_ms)
        };
        timing.fixed_delta_time = oxygen::time::CanonicalDuration {
          std::chrono::milliseconds(step.dt_ms)
        };
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

NOLINT_TEST_F(InputSystemIntegrationTest, CrossContext_ConsumerCancelsEarlierContext)
{
  oxygen::co::Run(loop_, [&]() -> oxygen::co::Co<> {
    // Arrange
    auto shift = std::make_shared<Action>("Shift", ActionValueType::kBool);
    auto jump = std::make_shared<Action>("Jump", ActionValueType::kBool);
    auto jump_higher = std::make_shared<Action>(
      "JumpHigher", ActionValueType::kBool);

    // JumpHigher consumes input
    jump_higher->SetConsumesInput(true);

    input_system_->AddAction(shift);
    input_system_->AddAction(jump);
    input_system_->AddAction(jump_higher);

    // High-priority modifier context (Shift)
    auto mod_ctx = std::make_shared<InputMappingContext>("mods");
    {
      auto map_shift = std::make_shared<InputActionMapping>(
        shift, InputSlots::LeftShift);
      auto t = std::make_shared<ActionTriggerDown>();
      t->MakeExplicit();
      map_shift->AddTrigger(t);
      mod_ctx->AddMapping(map_shift);
    }
    input_system_->AddMappingContext(mod_ctx, 1000);
    input_system_->ActivateMappingContext(mod_ctx);

    // Ground movement context with JumpHigher (chain + consume), then Jump
    auto ground_ctx = std::make_shared<InputMappingContext>("ground");
    {
      // JumpHigher (Tap + chain to Shift)
      auto m_higher = std::make_shared<InputActionMapping>(
        jump_higher, InputSlots::Space);
      auto tap = std::make_shared<ActionTriggerTap>();
      tap->SetTapTimeThreshold(0.25F);
      tap->MakeExplicit();
      m_higher->AddTrigger(tap);
      auto chain = std::make_shared<ActionTriggerChain>();
      chain->SetLinkedAction(shift);
      chain->MakeImplicit();
      m_higher->AddTrigger(chain);
      ground_ctx->AddMapping(m_higher);

      // Jump (Tap)
      auto m_jump = std::make_shared<InputActionMapping>(
        jump, InputSlots::Space);
      auto t_jump = std::make_shared<ActionTriggerTap>();
      t_jump->SetTapTimeThreshold(0.25F);
      t_jump->MakeExplicit();
      m_jump->AddTrigger(t_jump);
      ground_ctx->AddMapping(m_jump);
    }
    input_system_->AddMappingContext(ground_ctx, 0);
    input_system_->ActivateMappingContext(ground_ctx);

    // Step 1: press Shift to arm the chain
    SendKeyEvent(Key::kLeftShift, ButtonState::kPressed);
    input_system_->OnFrameStart(*frame_context_);
    co_await input_system_->OnInput(*frame_context_);

    // Step 2: Press and release Space (same frame) -> JumpHigher should trigger
    SendKeyEvent(Key::kSpace, ButtonState::kPressed);
    SendKeyEvent(Key::kSpace, ButtonState::kReleased);
    input_system_->OnFrameStart(*frame_context_);
    co_await input_system_->OnInput(*frame_context_);

    // After the release the consuming JumpHigher must trigger and the Jump
    // mapping in the same context (or other contexts) must be cancelled.
    EXPECT_TRUE(jump_higher->WasTriggeredThisFrame());
    EXPECT_TRUE(jump->WasCanceledThisFrame());
    EXPECT_FALSE(jump->IsOngoing());

    co_return;
  });
}

  NOLINT_TEST_F(InputSystemIntegrationTest, MappingOrderAcrossContexts)
  {
    oxygen::co::Run(loop_, [&]() -> oxygen::co::Co<> {
      // Arrange: High-priority context with 'High' mapping; low-priority context
      // with two mappings whose order relative to a consumer will be validated.
      auto high = std::make_shared<Action>("High", ActionValueType::kBool);
      auto lowA = std::make_shared<Action>("LowA", ActionValueType::kBool);
      auto lowB = std::make_shared<Action>("LowB", ActionValueType::kBool);

      // B will be the consumer in our scenarios
      lowB->SetConsumesInput(true);

      input_system_->AddAction(high);
      input_system_->AddAction(lowA);
      input_system_->AddAction(lowB);

      // High context (priority 100)
      auto ctx_high = std::make_shared<InputMappingContext>("high_ctx");
      {
        auto m = std::make_shared<InputActionMapping>(high, InputSlots::Space);
        auto t = std::make_shared<ActionTriggerPressed>();
        t->MakeExplicit();
        m->AddTrigger(t);
        ctx_high->AddMapping(m);
      }

      // Case 1: Low context with non-consuming A then consuming B (B after A)
      auto ctx_low1 = std::make_shared<InputMappingContext>("low_ctx1");
      {
        auto mA = std::make_shared<InputActionMapping>(lowA, InputSlots::Space);
        auto ta = std::make_shared<ActionTriggerTap>();
        ta->SetTapTimeThreshold(0.25F);
        ta->MakeExplicit();
        mA->AddTrigger(ta);
        ctx_low1->AddMapping(mA);

        auto mB = std::make_shared<InputActionMapping>(lowB, InputSlots::Space);
        auto tb = std::make_shared<ActionTriggerTap>();
        tb->SetTapTimeThreshold(0.25F);
        tb->MakeExplicit();
        mB->AddTrigger(tb);
        ctx_low1->AddMapping(mB);
      }

      input_system_->AddMappingContext(ctx_low1, 0);
      input_system_->AddMappingContext(ctx_high, 100);
      input_system_->ActivateMappingContext(ctx_low1);
      input_system_->ActivateMappingContext(ctx_high);

      // Press+release: high triggers; both lowA and lowB should also trigger when
      // lowB is placed after lowA (consumer after non-consumer in same context)
      SendKeyEvent(Key::kSpace, ButtonState::kPressed);
      input_system_->OnFrameStart(*frame_context_);
      co_await input_system_->OnInput(*frame_context_);

      SendKeyEvent(Key::kSpace, ButtonState::kReleased);
      input_system_->OnFrameStart(*frame_context_);
      co_await input_system_->OnInput(*frame_context_);

      EXPECT_TRUE(high->WasTriggeredThisFrame());
      EXPECT_TRUE(lowA->WasTriggeredThisFrame());
      EXPECT_TRUE(lowB->WasTriggeredThisFrame());

      // Case 2: Low context with consuming B first, then non-consuming A
      auto ctx_low2 = std::make_shared<InputMappingContext>("low_ctx2");
      {
        auto mB_first = std::make_shared<InputActionMapping>(lowB, InputSlots::Space);
        auto tb1 = std::make_shared<ActionTriggerTap>();
        tb1->SetTapTimeThreshold(0.25F);
        tb1->MakeExplicit();
        mB_first->AddTrigger(tb1);
        ctx_low2->AddMapping(mB_first);

        auto mA_late = std::make_shared<InputActionMapping>(lowA, InputSlots::Space);
        auto ta2 = std::make_shared<ActionTriggerTap>();
        ta2->SetTapTimeThreshold(0.25F);
        ta2->MakeExplicit();
        mA_late->AddTrigger(ta2);
        ctx_low2->AddMapping(mA_late);
      }

      // Replace the lower-priority context with this new ordering
      input_system_->DeactivateMappingContext(ctx_low1);
      input_system_->AddMappingContext(ctx_low2, 0);
      input_system_->ActivateMappingContext(ctx_low2);

      // Press+release: high triggers; lowB triggers and should cancel the later
      // lowA mapping (since the consumer is earlier in the low context)
      SendKeyEvent(Key::kSpace, ButtonState::kPressed);
      input_system_->OnFrameStart(*frame_context_);
      co_await input_system_->OnInput(*frame_context_);

      SendKeyEvent(Key::kSpace, ButtonState::kReleased);
      input_system_->OnFrameStart(*frame_context_);
      co_await input_system_->OnInput(*frame_context_);

      EXPECT_TRUE(high->WasTriggeredThisFrame());
      EXPECT_TRUE(lowB->WasTriggeredThisFrame());
      EXPECT_TRUE(lowA->WasCanceledThisFrame());

      co_return;
    });
  }

  // TDD integration spec: after a consuming mapping fires (JumpHigher) and
  // cancels earlier mappings (Jump), a subsequent single tap of the same slot
  // should allow Jump to trigger immediately (no extra press needed).
  NOLINT_TEST_F(InputSystemIntegrationTest, TDD_JumpHigherCancelsThenJumpTriggersOnNextTap)
  {
    oxygen::co::Run(loop_, [&]() -> oxygen::co::Co<> {
      // Arrange (same wiring as CrossContext_ConsumerCancelsEarlierContext)
      auto shift = std::make_shared<Action>("Shift", ActionValueType::kBool);
      auto jump = std::make_shared<Action>("Jump", ActionValueType::kBool);
      auto jump_higher = std::make_shared<Action>(
        "JumpHigher", ActionValueType::kBool);
      jump_higher->SetConsumesInput(true);

      input_system_->AddAction(shift);
      input_system_->AddAction(jump);
      input_system_->AddAction(jump_higher);

      auto mod_ctx = std::make_shared<InputMappingContext>("mods");
      {
        auto map_shift = std::make_shared<InputActionMapping>(
          shift, InputSlots::LeftShift);
        auto t = std::make_shared<ActionTriggerDown>(); t->MakeExplicit();
        map_shift->AddTrigger(t);
        mod_ctx->AddMapping(map_shift);
      }
      input_system_->AddMappingContext(mod_ctx, 1000);
      input_system_->ActivateMappingContext(mod_ctx);

      auto ground_ctx = std::make_shared<InputMappingContext>("ground");
      {
        auto m_higher = std::make_shared<InputActionMapping>(
          jump_higher, InputSlots::Space);
        auto tap = std::make_shared<ActionTriggerTap>(); tap->MakeExplicit();
        m_higher->AddTrigger(tap);
        auto chain = std::make_shared<ActionTriggerChain>();
        chain->SetLinkedAction(shift); chain->MakeImplicit(); m_higher->AddTrigger(chain);
        ground_ctx->AddMapping(m_higher);

        auto m_jump = std::make_shared<InputActionMapping>(jump, InputSlots::Space);
        auto t_jump = std::make_shared<ActionTriggerTap>(); t_jump->MakeExplicit();
        m_jump->AddTrigger(t_jump);
        ground_ctx->AddMapping(m_jump);
      }
      input_system_->AddMappingContext(ground_ctx, 0);
      input_system_->ActivateMappingContext(ground_ctx);

      // Press Shift
      SendKeyEvent(Key::kLeftShift, ButtonState::kPressed);
      input_system_->OnFrameStart(*frame_context_);
      co_await input_system_->OnInput(*frame_context_);

      // Press+release Space -> JumpHigher should trigger and Jump canceled
      SendKeyEvent(Key::kSpace, ButtonState::kPressed);
      SendKeyEvent(Key::kSpace, ButtonState::kReleased);
      input_system_->OnFrameStart(*frame_context_);
      co_await input_system_->OnInput(*frame_context_);

      EXPECT_TRUE(jump_higher->WasTriggeredThisFrame());
      EXPECT_TRUE(jump->WasCanceledThisFrame());

      // Release Shift so the chain is no longer armed, then single tap Space
      // should allow Jump to trigger on its own.
      SendKeyEvent(Key::kLeftShift, ButtonState::kReleased);
      input_system_->OnFrameStart(*frame_context_);
      co_await input_system_->OnInput(*frame_context_);

      SendKeyEvent(Key::kSpace, ButtonState::kPressed);
      SendKeyEvent(Key::kSpace, ButtonState::kReleased);
      input_system_->OnFrameStart(*frame_context_);
      co_await input_system_->OnInput(*frame_context_);

      EXPECT_TRUE(jump->WasTriggeredThisFrame());

      co_return;
    });
  }

} // namespace
