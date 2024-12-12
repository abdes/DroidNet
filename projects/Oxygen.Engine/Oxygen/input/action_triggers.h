//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <chrono>
#include <memory>
#include <vector>

#include "oxygen/api_export.h"
#include "oxygen/base/macros.h"
#include "oxygen/base/types.h"
#include "oxygen/input/types.h"

namespace oxygen::input {

  //-- ActionTrigger -------------------------------------------------------------

  enum class ActionTriggerType : uint8_t
  {
    kPressed,
    kReleased,
    kDown,
    kHold,
    kHoldAndRelease,
    kPulse,
    kTap,
    kActionChain,
    kCombo,
  };

  class ActionTrigger
  {
  public:
    enum class Behavior : uint8_t
    {
      kExplicit,  // Input may trigger if any explicit trigger is triggered.
      kImplicit,  // Input may trigger only if all implicit triggers are
      // triggered.
      kBlocker,   // Inverted trigger that will block all other triggers if it
      // is triggered.
    };

    explicit ActionTrigger() = default;
    virtual ~ActionTrigger() = default;

    OXYGEN_DEFAULT_COPYABLE(ActionTrigger);
    OXYGEN_DEFAULT_MOVABLE(ActionTrigger);

    [[nodiscard]] virtual auto GetType() const->ActionTriggerType = 0;

    [[nodiscard]] auto IsExplicit() const {
      return behavior_ == Behavior::kExplicit;
    }

    void MakeExplicit() { behavior_ = Behavior::kExplicit; }

    [[nodiscard]] auto IsImplicit() const {
      return behavior_ == Behavior::kImplicit;
    }

    void MakeImplicit() { behavior_ = Behavior::kImplicit; }

    [[nodiscard]] auto IsBlocker() const {
      return behavior_ == Behavior::kBlocker;
    }

    void MakeBlocker() { behavior_ = Behavior::kBlocker; }

    void SetActuationThreshold(float threshold) {
      actuation_threshold_ = threshold;
    }

    [[nodiscard]] auto IsIdle() const { return state_ == State::kIdle; }

    [[nodiscard]] auto IsOngoing() const { return state_ == State::kOngoing; }

    [[nodiscard]] virtual auto IsTriggered() const -> bool { return triggered_; }

    [[nodiscard]] virtual auto IsCanceled() const -> bool {
      return !triggered_ && (previous_state_ == State::kOngoing)
        && (state_ == State::kIdle);
    }

    [[nodiscard]] virtual auto IsCompleted() const -> bool {
      return triggered_ && (state_ == State::kIdle);
    }

    void UpdateState(const ActionValue& action_value, Duration delta_time);

  protected:
    enum class State : uint8_t
    {
      kIdle,
      kOngoing,
    };
    virtual auto DoUpdateState(const ActionValue& action_value,
                               Duration delta_time) -> bool = 0;

    void SetTriggerState(State state) {
      previous_state_ = state_;
      state_ = state;
    }

    [[nodiscard]] auto GetActuationThreshold() const {
      return actuation_threshold_;
    }

    [[nodiscard]] auto GetPreviousState() const { return previous_state_; }

  private:
    Behavior behavior_{ Behavior::kImplicit };
    float actuation_threshold_{ 0.5F };
    State state_{ State::kIdle };
    State previous_state_{ State::kIdle };
    bool triggered_{ false };
  };

  //-- ActionTriggerPressed ------------------------------------------------------

  // Trigger fires once only when input exceeds the actuation threshold. Holding
  // the input will not cause further triggers.
  class ActionTriggerPressed : public ActionTrigger
  {
  public:
    ActionTriggerPressed() = default;
    ~ActionTriggerPressed() override = default;

    OXYGEN_DEFAULT_COPYABLE(ActionTriggerPressed);
    OXYGEN_DEFAULT_MOVABLE(ActionTriggerPressed);

    [[nodiscard]] auto GetType() const -> ActionTriggerType override {
      return ActionTriggerType::kPressed;
    }

  protected:
    OXYGEN_API auto DoUpdateState(const ActionValue& action_value,
                                  Duration delta_time) -> bool override;

  private:
    bool depleted_{ false };
  };

  //-- ActionTriggerReleased -----------------------------------------------------

  class ActionTriggerReleased : public ActionTrigger
  {
  public:
    ActionTriggerReleased() = default;
    ~ActionTriggerReleased() override = default;

    OXYGEN_DEFAULT_COPYABLE(ActionTriggerReleased);
    OXYGEN_DEFAULT_MOVABLE(ActionTriggerReleased);

    [[nodiscard]] auto GetType() const -> ActionTriggerType override {
      return ActionTriggerType::kReleased;
    }

  protected:
    auto DoUpdateState(const ActionValue& action_value,
                       Duration delta_time [[maybe_unused]] ) -> bool override;
  };

  //-- ActionTriggerDown ---------------------------------------------------------

  // Trigger fires when input exceeds the actuation threshold. Holding
  // the input will cause further triggers.
  class ActionTriggerDown : public ActionTrigger
  {
  public:
    OXYGEN_API ActionTriggerDown();
    ~ActionTriggerDown() override = default;

    OXYGEN_DEFAULT_COPYABLE(ActionTriggerDown);
    OXYGEN_DEFAULT_MOVABLE(ActionTriggerDown);

    [[nodiscard]] auto GetType() const -> ActionTriggerType override {
      return ActionTriggerType::kDown;
    }

    [[nodiscard]] auto IsCompleted() const -> bool override {
      return triggered_once_ && IsIdle();
    }

    [[nodiscard]] auto IsCanceled() const -> bool override {
      return ActionTrigger::IsCanceled() && !triggered_once_;
    }

  protected:
    OXYGEN_API auto DoUpdateState(const ActionValue& action_value,
                                  Duration delta_time) -> bool override;

  private:
    bool triggered_once_{ false };
  };

  //-- ActionTriggerTimed --------------------------------------------------------

  // Base class for building triggers that have firing conditions governed by
  // elapsed time. This class transitions state to Ongoing once input is actuated,
  // and will track Ongoing input time until input is released. Inheriting classes
  // should provide the logic for Triggered transitions.
  class ActionTriggerTimed : public ActionTrigger
  {
  public:
    explicit ActionTriggerTimed() = default;
    ~ActionTriggerTimed() override = default;

    OXYGEN_DEFAULT_COPYABLE(ActionTriggerTimed);
    OXYGEN_DEFAULT_MOVABLE(ActionTriggerTimed);

  protected:
    OXYGEN_API auto DoUpdateState(const ActionValue& action_value,
                                  Duration delta_time) -> bool override;

    [[nodiscard]] virtual auto GetHeldDuration() const -> Duration {
      return held_duration_;
    }

  private:
    Duration held_duration_{ 0 };
  };

  //-- ActionTriggerHold ---------------------------------------------------------

  // Trigger fires once input has remained actuated for HoldTimeThreshold seconds.
  // Trigger may optionally fire once, or repeatedly fire.
  class ActionTriggerHold : public ActionTriggerTimed
  {
  public:
    explicit ActionTriggerHold() = default;
    ~ActionTriggerHold() override = default;

    OXYGEN_DEFAULT_COPYABLE(ActionTriggerHold);
    OXYGEN_DEFAULT_MOVABLE(ActionTriggerHold);

    [[nodiscard]] auto GetType() const -> ActionTriggerType override {
      return ActionTriggerType::kHold;
    }

    void SetHoldDurationThreshold(const float threshold_seconds) {
      hold_duration_threshold_ = SecondsToDuration(threshold_seconds);
    }

    [[nodiscard]] auto GetHoldDurationThreshold() const {
      return hold_duration_threshold_;
    }

    [[nodiscard]] auto IsOneShot() const { return one_shot_; }
    void OneShot(bool enable = true) { one_shot_ = enable; }

    [[nodiscard]] auto IsCompleted() const -> bool override {
      return triggered_once_ && (IsIdle());
    }

  protected:
    OXYGEN_API auto DoUpdateState(const ActionValue& action_value,
                                  Duration delta_time) -> bool override;

  private:
    Duration hold_duration_threshold_{ 0 };
    bool one_shot_{ true };
    bool triggered_once_{ false };
  };

  //-- ActionTriggerHoldAndRelease -----------------------------------------------

  // Trigger fires when input is released after having been actuated for at least
  // HoldTimeThreshold seconds.
  class ActionTriggerHoldAndRelease : public ActionTriggerTimed
  {
  public:
    explicit ActionTriggerHoldAndRelease() = default;
    ~ActionTriggerHoldAndRelease() override = default;

    OXYGEN_DEFAULT_COPYABLE(ActionTriggerHoldAndRelease);
    OXYGEN_DEFAULT_MOVABLE(ActionTriggerHoldAndRelease);

    [[nodiscard]] auto GetType() const -> ActionTriggerType override {
      return ActionTriggerType::kHoldAndRelease;
    }

    void SetHoldDurationThreshold(const float threshold_seconds) {
      hold_duration_threshold_ = SecondsToDuration(threshold_seconds);
    }

    [[nodiscard]] auto GetHoldDurationThreshold() const {
      return hold_duration_threshold_;
    }

  protected:
    OXYGEN_API auto DoUpdateState(const ActionValue& action_value,
                                  Duration delta_time) -> bool override;

  private:
    Duration hold_duration_threshold_{ 0 };
  };

  //-- ActionTriggerPulse --------------------------------------------------------

  // Trigger that fires at an Interval, in seconds, while input is actuated.
  // Completed only fires when the repeat limit is reached or when input is
  // released immediately after being triggered. Otherwise, Canceled is fired when
  // input is released.
  class ActionTriggerPulse : public ActionTriggerTimed
  {
  public:
    explicit ActionTriggerPulse() = default;
    ~ActionTriggerPulse() override = default;

    OXYGEN_DEFAULT_COPYABLE(ActionTriggerPulse);
    OXYGEN_DEFAULT_MOVABLE(ActionTriggerPulse);

    [[nodiscard]] auto GetType() const -> ActionTriggerType override {
      return ActionTriggerType::kPulse;
    }

    void SetInterval(const float interval_seconds) {
      interval_ = SecondsToDuration(interval_seconds);
    }

    [[nodiscard]] auto GetInterval() const { return interval_; }

    [[nodiscard]] auto TriggerOnStart() const { return trigger_on_start_; }
    void TriggerOnStart(bool enable = true) { trigger_on_start_ = enable; }

    [[nodiscard]] auto GetTriggerLimit() const { return trigger_limit_; }
    void SetTriggerLimit(uint32_t trigger_limit = 0) {
      trigger_limit_ = trigger_limit;
    }

    [[nodiscard]] auto IsCompleted() const -> bool override {
      return ((trigger_count_ == 1) || (trigger_count_ == trigger_limit_))
        && (IsIdle());
    }

    [[nodiscard]] auto IsCanceled() const -> bool override {
      return ((trigger_count_ != 1) && (trigger_count_ < trigger_limit_))
        && IsIdle() && (GetPreviousState() == State::kOngoing);
    }

  protected:
    OXYGEN_API auto DoUpdateState(const ActionValue& action_value,
                                  Duration delta_time) -> bool override;

  private:
    Duration interval_{ std::chrono::seconds{1} };
    bool trigger_on_start_{ true };
    uint32_t trigger_limit_{ 0 };
    uint32_t trigger_count_{ 0 };
  };

  //-- ActionTriggerTap ----------------------------------------------------------

  // Input must be actuated then released within tap release time threshold
  // seconds to trigger.
  class ActionTriggerTap : public ActionTriggerTimed
  {
  public:
    explicit ActionTriggerTap() = default;
    ~ActionTriggerTap() override = default;

    OXYGEN_DEFAULT_COPYABLE(ActionTriggerTap);
    OXYGEN_DEFAULT_MOVABLE(ActionTriggerTap);

    [[nodiscard]] auto GetType() const -> ActionTriggerType override {
      return ActionTriggerType::kHoldAndRelease;
    }

    void SetTapReleaseThreshold(const float threshold_seconds) {
      threshold_ = SecondsToDuration(threshold_seconds);
    }

    [[nodiscard]] auto GetTapReleaseThreshold() const { return threshold_; }

    // Canceled does not make sense for this trigger
    // TODO(abdes) restrict events supported for action events
    [[nodiscard]] auto IsCanceled() const -> bool override { return false; }

  protected:
    OXYGEN_API auto DoUpdateState(const ActionValue& action_value,
                                  Duration delta_time) -> bool override;

  private:
    Duration threshold_{ 0 };
  };

  //-- ActionTriggerChain --------------------------------------------------------

  // Links this trigger to an action that must trigger for this one to trigger.
  // Note that when this trigger is associated with an action, no other action
  // with the same input slot should trigger when the former does. It is therefore
  // important to consume input from an action that has a trigger chain.
  class ActionTriggerChain : public ActionTrigger
  {
  public:
    ActionTriggerChain() = default;
    ~ActionTriggerChain() override = default;

    OXYGEN_DEFAULT_COPYABLE(ActionTriggerChain);
    OXYGEN_DEFAULT_MOVABLE(ActionTriggerChain);

    [[nodiscard]] auto GetType() const -> ActionTriggerType override {
      return ActionTriggerType::kActionChain;
    }

    OXYGEN_API void SetLinkedAction(std::shared_ptr<Action> action);
    [[nodiscard]] auto GetLinkedAction() const -> std::weak_ptr<const Action> {
      return linked_action_;
    }

  protected:
    OXYGEN_API auto DoUpdateState(const ActionValue& action_value,
                                  Duration delta_time) -> bool override;

  private:
    std::shared_ptr<Action> linked_action_;
  };

  //-- ActionTriggerCombo --------------------------------------------------------

  struct InputComboStep
  {
    std::shared_ptr<Action> action;
    ActionStates completion_states;
    Duration time_to_complete;
  };

  struct InputComboBreaker
  {
    std::shared_ptr<Action> action;
    ActionStates completion_states;
  };

  // A sequence of actions that must enter a certain state (triggered, completed,
  // etc) in the order they are specified in the combo array for this trigger to
  // fire.
  class ActionTriggerCombo : public ActionTrigger
  {
  public:
    ActionTriggerCombo() = default;
    ~ActionTriggerCombo() override = default;

    OXYGEN_DEFAULT_COPYABLE(ActionTriggerCombo);
    OXYGEN_DEFAULT_MOVABLE(ActionTriggerCombo);

    [[nodiscard]] auto GetType() const -> ActionTriggerType override {
      return ActionTriggerType::kCombo;
    }

    OXYGEN_API void AddComboStep(
      std::shared_ptr<Action> action,
      ActionStates completion_states = ActionStates::kTriggered,
      float time_to_complete_seconds = 0.5F);
    OXYGEN_API void RemoveComboStep(uint32_t index);
    OXYGEN_API void ClearComboSteps();
    [[nodiscard]] auto GetComboSteps() const { return combo_steps_; }

    OXYGEN_API void AddComboBreaker(
      std::shared_ptr<Action> action,
      ActionStates completion_states = ActionStates::kTriggered);
    OXYGEN_API void RemoveComboBreaker(uint32_t index);
    OXYGEN_API void ClearComboBreakers();
    [[nodiscard]] auto GetComboBreakers() const { return combo_breakers_; }

  protected:
    OXYGEN_API auto DoUpdateState(const ActionValue& action_value,
                                  Duration delta_time) -> bool override;

  private:
    std::vector<InputComboStep> combo_steps_;
    std::vector<InputComboBreaker> combo_breakers_;

    Duration waited_time_{ 0 };
    size_t current_step_index_{ 0 };
  };

}  // namespace oxygen::input

// TODO(abdes) trigger has supported event types:
// None               = (0x0),
// Instant            = (1 << 0),
// Uninterruptible    = (1 << 1),
// Ongoing            = (1 << 2),
// All                = (Instant | Uninterruptible | Ongoing),
