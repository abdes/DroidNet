# Oxygen Input System

Frame-based, query-driven input for the Oxygen Engine. The Input module turns
platform input events into named, typed **Actions** through **mapping
contexts**, **mappings** and composable **triggers**. Gameplay code asks
precise questions — "did Jump trigger this frame?", "is Sprint ongoing?",
"how far did the mouse move this frame?" — instead of registering callbacks.

The module builds on every platform that builds the reusable engine modules
(Windows, Linux). It depends on `oxygen::platform-input` (the input
vocabulary: `InputSlot`, `InputEvent`), not on the SDL platform backend.

## Concepts

| Concept               | Role                                                                                                                                                           | Details                                        |
| --------------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------- | ---------------------------------------------- |
| `Action`              | Named logical input with a typed value (`bool`, `Axis1D`, `Axis2D`), a level state and per-frame edges. Combines every binding that drives it.                 | [Docs/Actions.md](Docs/Actions.md)             |
| `InputActionMapping`  | One binding: an `InputSlot` driving one action, with triggers and a scale/bias value transform.                                                                | [Docs/Mappings.md](Docs/Mappings.md)           |
| `InputMappingContext` | Ordered group of mappings, activated and prioritized as a unit (gameplay, UI, vehicle...).                                                                     | [Docs/Mappings.md](Docs/Mappings.md)           |
| Triggers              | Small state machines deciding when a binding triggers: Pressed, Released, Down, Hold, HoldAndRelease, Pulse, Tap, Chain, Combo; explicit, implicit or blocker. | [Docs/Triggers](Docs/Triggers)                 |
| `InputSystem`         | Engine module: routes events, evaluates contexts by priority, applies consumption, publishes the frame's `InputSnapshot`.                                      | below                                          |
| `InputSnapshot`       | Immutable value copy of all actions at the end of `kInput`, safe to read from any later phase, task or frame in flight.                                        | [Docs/InputSnapshot.md](Docs/InputSnapshot.md) |

## Quick start

```cpp
using oxygen::input::Action;
using oxygen::input::ActionTriggerPressed;
using oxygen::input::ActionValueType;
using oxygen::input::InputActionMapping;
using oxygen::input::InputMappingContext;
using oxygen::platform::InputSlots;

auto jump = std::make_shared<Action>("Jump", ActionValueType::kBool);
input_system->AddAction(jump);

auto gameplay = std::make_shared<InputMappingContext>("gameplay");
{
  auto mapping = std::make_shared<InputActionMapping>(jump, InputSlots::Space);
  auto pressed = std::make_shared<ActionTriggerPressed>();
  pressed->MakeExplicit();
  mapping->AddTrigger(pressed);
  gameplay->AddMapping(mapping);
}
input_system->AddMappingContext(gameplay, /*priority*/ 0);
input_system->ActivateMappingContext(gameplay);

// Any phase after kInput, in this frame:
if (jump->WasTriggeredThisFrame()) {
  player.Jump();
}
// Or, from a parallel task or a later frame still holding it:
if (snapshot->DidActionTrigger("Jump")) { /* ... */ }
```

WASD movement on one 2D action, using scale to map keys onto axes:

```cpp
auto move = std::make_shared<Action>("Move", ActionValueType::kAxis2D);
move->SetAccumulation(oxygen::input::ActionAccumulation::kCumulative);
auto bind = [&](const oxygen::platform::InputSlot& key, oxygen::Axis2D scale) {
  auto mapping = std::make_shared<InputActionMapping>(move, key);
  auto down = std::make_shared<ActionTriggerDown>();
  down->MakeExplicit();
  mapping->AddTrigger(down);
  mapping->SetScale(scale);
  gameplay->AddMapping(mapping);
};
bind(InputSlots::D, { .x = 1.0F, .y = 0.0F });
bind(InputSlots::A, { .x = -1.0F, .y = 0.0F });
bind(InputSlots::W, { .x = 0.0F, .y = 1.0F });
bind(InputSlots::S, { .x = 0.0F, .y = -1.0F });
// move->GetValue() is (1, 1) with W+D held, (0, 0) with W+S held.
```

## Engine integration

The InputSystem is an engine module (`kFrameStart`, `kInput`, `kSnapshot`,
`kFrameEnd`). One frame:

```mermaid
sequenceDiagram
  autonumber
  participant Engine
  participant IS as InputSystem
  participant Ctx as Active contexts<br/>(high -> low priority)
  participant Act as Actions

  Engine->>IS: kFrameStart
  IS->>Act: begin frame (clear edges; close non-held activations)
  Engine->>IS: kInput
  IS->>Ctx: apply deferred context deactivations (cancel edges)
  IS->>Ctx: time pass: Update(physical dt)
  loop each pending platform event
    IS->>Ctx: HandleInput(slot, event)
    IS->>Ctx: Update(0) with consumption by physical source
  end
  IS->>IS: freeze InputSnapshot (value copy)
  IS->>Act: end frame
  Engine->>IS: kSnapshot (snapshot published by the engine)
  Engine->>IS: kFrameEnd
```

- **Per-event evaluation.** Each event is evaluated as it is drained, so a
  press and release within one frame are both seen, and consumption applies to
  the event that caused it.
- **Trigger clock.** Hold, Tap, Pulse, Chain and Combo windows advance with
  `ModuleTimingData::physical_delta_time` (real elapsed time), falling back to
  game time when the engine provides none. Hold-to-confirm works in a paused
  menu, and a 0.5 s hold takes 0.5 s in slow motion.
- **Relative axes.** Mouse motion and wheel bindings sum every event of the
  frame that their triggers accept: `GetValue()` is the whole-frame delta, at
  any frame rate. Rejected input never reaches the value: a look gated by a
  right-button chord reads zero while the button is up.
- **Context deactivation.** `DeactivateMappingContext` stops routing at once
  and cancels the context's active bindings at the start of the next `kInput`,
  so the Canceled edge is visible to gameplay. `RemoveMappingContext` cancels
  immediately. Turning input off (`nput.enabled 0`) cancels held actions and
  drops events until it is turned back on.
- **Fixed step.** A fixed-step simulation calls
  `Action::ConsumeFixedStepTrigger()` once per substep: one press gives exactly
  one `true`, whether the frame runs zero, one or several substeps.
- **Snapshots.** The snapshot is a value copy taken at the end of `kInput`. A
  reader that holds it into the next frame, or after an action was removed,
  still sees the frame it was built for.

## Combining bindings

Several bindings may drive one action (W and Up Arrow; LMB and Ctrl; a stick
and keys). Each binding contributes its own evaluation, and the action
combines them:

- **Ongoing** while any binding is ongoing. Releasing one of several held keys
  does not stop the action.
- **Triggered** if any binding triggered in this frame.
- **Canceled** only when a binding canceled and no other binding keeps the
  action active.
- **Value** per component, by the action's `ActionAccumulation`:
  `kHighestAbsolute` (default; two keys for the same direction do not stack)
  or `kCumulative` (sum; opposite directions cancel out).

## Activation outcome

An activation ends with exactly one of two edges:

- **Completed** — the activation triggered at least once and ended without a
  cancel: a hold that fired and was released, auto-fire released between
  pulses, a press or tap (completed in the following frame).
- **Canceled** — the activation ended without triggering (a hold released
  early), or it was interrupted (context deactivated, input consumed by a
  higher-priority binding, input disabled).

This follows Unreal Enhanced Input's invariant (every triggered activation
completes) but decides by outcome, not by trigger state at release; see
[Docs/Actions.md](Docs/Actions.md).

## Priority and consumption

Contexts are evaluated from highest to lowest priority; mappings inside a
context in insertion order. When an action that consumes input triggers on a
fresh event, the **physical sources** its binding reads are claimed for the
rest of that evaluation:

| Consuming binding        | Claims                         |
| ------------------------ | ------------------------------ |
| A key or button          | That key or button             |
| `MouseXY`                | Mouse X and Mouse Y            |
| `MouseX` / `MouseY`      | That axis                      |
| `MouseWheelXY`           | Both wheel axes                |
| `MouseWheelUp/Down/Y`    | The vertical wheel axis        |
| `MouseWheelLeft/Right/X` | The horizontal wheel axis      |
| `AnyKey`                 | The key or button of the event |

A later binding that reads a claimed source is skipped: if it holds an active
activation that activation is canceled; an idle binding just drops the staged
input, with no edge. Bindings on other sources are unaffected — pressing E to
interact does not stop walking on W.

## Capabilities and known limits

Delivered:

- Keyboard and mouse slots (buttons, motion, wheel, `AnyKey`); per-binding
  scale and bias; per-action accumulation.
- Nine trigger types with explicit/implicit/blocker composition, including
  cross-action Chain and Combo.
- Context layering with priority, physical-source consumption and visible
  cancellation on context switches.
- Engine-integrated timing (physical clock), fixed-step latch, immutable
  snapshots.
- Authoring: actions and contexts cooked from JSON (`oxygen.input.schema.json`,
  `oxygen.input-action.schema.json`) and hydrated at load, including scale,
  bias, consumption and accumulation.

Not yet provided (known gaps against current engines):

- **Gamepads and other devices.** No gamepad slots, dead zones or
  response curves; values come from keyboard and mouse only.
- **Per-player input.** One input stream; no local multiplayer or device
  assignment.
- **Runtime rebinding.** Mappings are authored or built in code; there is no
  rebinding service or persistence of player bindings.
- **Modifier stack.** Scale and bias cover key-to-axis and sensitivity; there
  is no general modifier chain (negate, swizzle, dead zone, smoothing).
- **Chord and modifier keys** are expressed with Chain triggers, not a
  dedicated chord trigger.
- **Text input** is outside this module (platform text events).

## Tests

- `Oxygen.Input.InputSystem.Tests` — unit tests per class and trigger.
- `Oxygen.Input.Scenarios.Tests` — functional suite (`Test/Scenarios/`):
  gameplay scenarios driven through the real `InputSystem` frame by frame
  (bindings, mouse axes, contexts, consumption, activation lifecycle,
  timing). Each test states what the player or gameplay programmer expects.
