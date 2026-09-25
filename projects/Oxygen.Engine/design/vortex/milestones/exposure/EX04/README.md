# EX04 — GPU lifecycle and sharing

Status: `validated`

| Field     | Summary                                                       |
| --------- | ------------------------------------------------------------- |
| Outcome   | GPU exposure lifecycle, transitions and source-owned sharing. |
| Remaining | None in the recorded scope.                                   |
| Evidence  | [Record below](#ex04--gpu-lifecycle-and-sharing)              |

[Roadmap](../../../PLAN.md) · [Design index](../../../lld/README.md)

## Delivery scope

- Update the same GPU state in Manual, ManualCamera, Auto and disabled modes.
- Add public per-view transitions and request generation handling, including
  recording/submission failure, invalid metering and idempotent retries.
- Implement the policies in section 4.1, including exact seed event-frame
  behavior and manual-to-auto continuity by retaining gain.
  Exercise controlled-input discontinuity notification, source destruction,
  default detach, lifetime-safe selection, inactive-owner validation and
  acknowledgement backpressure. Include captured controls, DemoShell reset,
  and both offscreen execution paths.
- Implement source-owned updates, pinned prior generations, root-source
  resolution, cycle rejection, inactive-source retention and bootstrap fallback.
  Cover registered composition views and offscreen facade routing.
- Implement stateless transient state, recovery events, frame-safe uploads,
  synchronization and fence retirement using controlled float-input fixtures.
- Exercise native game-facing producers without DemoShell. Wire DemoShell's
  reset to the same public API.

**Gate:** state-machine, request, ownership and publication tests pass against
controlled float inputs, including both shared-view execution orders. GPU state
identifies the applied generation and settings revision. Slice 5 supplies and
validates the scene-integrated high-range route for startup, cuts, stateless
views and recovery.

## Recorded qualification

| Slice | Status | Boundary | Evidence |
| ----- | ------ | -------- | -------- |

| 4 — GPU lifecycle and sharing | validated | Controlled-input/public-event gate, including offscreen routing. Real-scene resource lifetime is tracked in slice 5. | [Lifecycle](../../../../../out/build-ninja/analysis/vortex/exposure-lightbench/lifecycle/discontinuity-manifest.json), [offscreen sharing](../../../../../out/build-ninja/analysis/vortex/exposure-lightbench/lifecycle/offscreen-sharing-manifest.json) |
