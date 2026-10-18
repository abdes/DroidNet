# InputSnapshot

A consolidated, read-only view of input for a single frame. Built and FROZEN at
the end of the kInput phase so it is immediately available to subsequent phases
(FixedSim, Gameplay, etc.) within the same frame and to the final FrameContext
snapshot at kSnapshot.

This snapshot exposes two complementary views:

- Level: Final state at the end of the frame (what is currently true).
- Edge: Transitions that occurred during the frame window (what happened).

It does not store raw input events (those are part of the platform layer and not
needed for action queries here). Timing data (frame start time) is provided by
FrameContext and should be queried from there when needed.

## Frame window and lifecycle

- Each frame, actions call BeginFrameTracking() before input processing. During
  kInput, input events and mapping contexts update actions; transitions (edges)
  are recorded as they occur.
- At the END of kInput, the InputSnapshot is built as a **value copy** of every
  action: key, state flags, value, value-updated flag and the frame's
  transitions. It holds no pointer to an action, so it is immutable: a reader
  that keeps it into the next frame still sees this frame, and it stays valid
  after the action is removed (level unload) or destroyed.
- The frozen snapshot is available to FixedSim, Gameplay, and other phases in
  the same frame, and is also included in the unified snapshots built at
  kSnapshot (without rebuilding). Parallel tasks and frames in flight may hold
  it as long as they need (`std::shared_ptr<const InputSnapshot>`).
- Copy cost: one small entry per action plus its transitions for the frame
  (typically zero to three).

## Edge vs Level — precise meanings

- Level (final snapshot at frame end)
  - Answers: “Is the action ongoing?”, “Is it triggered at frame end?”, etc.
  - Persistent: ongoing/value persist across frames.
  - Use for sustained/hold behaviors (aiming, sprint hold, UI focus).

- Edge (per-frame transitions)
  - Answers: “Did it trigger this frame?”, “Did it complete/cancel?”, “Did it
    start or release?”, “Was the value updated this frame?”.
  - Non-sticky: edges exist only for the current frame window.
  - Use for event-like behaviors (single-shot fire, open menu, confirm).

## API overview

Level queries (final snapshot):

- GetActionStateFlags(key) → ActionState
  - Aggregated bitfield of the final state. Mirrors Action’s snapshot flags.
- IsActionTriggered(key) → bool
- IsActionOngoing(key) → bool
- IsActionCompleted(key) → bool
- IsActionCanceled(key) → bool
- IsActionIdle(key) → bool
- GetActionValue(key) → ActionValue

Edge queries (per-frame transitions):

- DidActionStart(key) → bool
  - Start = either None→Triggered this frame, or an Ongoing edge occurred and a
    Triggered transition happened later within the same frame.
- DidActionTrigger(key) → bool
  - Any transition this frame whose to_state contains kTriggered.
- DidActionComplete(key) → bool
  - Matches Ongoing→Completed or Triggered→Completed in this frame.
- DidActionCancel(key) → bool
  - Matches Ongoing→Canceled or Triggered→Canceled in this frame.
- DidActionRelease(key) → bool
  - Any transition Ongoing→not Ongoing in this frame (release edge).
- DidActionValueUpdate(key) → bool
  - True if the action’s value was updated at least once during the frame.
- DidActionTransition(key, from, to) → bool
  - Precise predicate for a specific state transition.
- GetActionTransitions(key) → `span<const FrameTransition>`
  - Full list of transitions for the action during the frame; includes the
    states and the value at each transition.

Timing:

- Use FrameContext::GetFrameStartTime() if you need frame timing.

## Usage patterns

### One-shot actions (fire, confirm, open menu)

Use DidActionTrigger() to react exactly once per press within the frame that
caused it.

```cpp
if (snapshot.DidActionTrigger(fire_key)) {
  FireWeaponOnce();
}
```

### Hold actions (aim, sprint, drag)

Use IsActionOngoing() to gate continuous behavior while held.

```cpp
if (snapshot.IsActionOngoing(aim_key)) {
  camera.EnterAimMode();
} else {
  camera.ExitAimMode();
}
```

### Start/Release edges (begin/end of a hold)

Use DidActionStart() for the first frame the hold begins, and DidActionRelease()
for the first frame it ends.

```cpp
if (snapshot.DidActionStart(sprint_key)) {
  fx.PlaySprintStart();
}
if (snapshot.DidActionRelease(sprint_key)) {
  fx.PlaySprintEnd();
}
```

### Value-driven actions (axis, pointer)

Use DidActionValueUpdate() to process updates only when the value changed this
frame; read the current value via GetActionValue().

```cpp
if (snapshot.DidActionValueUpdate(move_key)) {
  const auto v = snapshot.GetActionValue(move_key);
  movement.ApplyInput(v);
}
```

### Complex timing: combos/animations

Use GetActionTransitions() to read the exact sequence of edges with their values
to drive animations or combo logic.

```cpp
for (const auto& t : snapshot.GetActionTransitions(attack_key)) {
  anim.ProcessEdge(t.from_state, t.to_state, t.value_at_transition);
}
```

### Relative axes (look, wheel zoom)

GetActionValue() of a mouse or wheel action is the whole-frame delta its
triggers accepted; a look gated by a chord is zero while the chord is not held.
Do not sum transition values.

```cpp
const auto look = snapshot.GetActionValue(look_key).GetAs<oxygen::Axis2D>();
camera.AddYawPitch(look.x, look.y);
```

## Guidelines

- Prefer edge queries for event-driven logic; prefer level queries for sustained
  behavior.
- Start/Release are convenience edges derived from Ongoing transitions and are
  non-sticky.
- For exact control and timing, prefer
  DidActionTransition()/GetActionTransitions().
- Avoid polling IsActionTriggered for “click once” behavior; use
  DidActionTrigger instead to eliminate bounce/double-trigger issues.

## Glossary

- Level: Final state at the end of the frame.
- Edge: A state transition that occurred within the frame window.
- Ongoing: The persistent “held/active” condition for an action.
