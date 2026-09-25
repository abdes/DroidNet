# EX08.2 — Widget regression automation

Status: `validated`

| Field     | Summary                                                        |
| --------- | -------------------------------------------------------------- |
| Outcome   | Real-widget regression workflows and ordinary-build isolation. |
| Remaining | None in the recorded scope.                                    |
| Evidence  | [Validation record](validation.md)                             |

[Roadmap](../../../PLAN.md) · [Design index](../../../lld/README.md)

## Delivery scope

**Disposition:** validated and closed (2026-09-25), following reactivation after
EX09. The [execution report](validation.md) and [EX10 closeout](../EX10/validation.md)
supersede the prior deferral. EX082-01–04/GATE cover Test Engine
dependency/configuration/context integration; numeric/focus/drag/mode/curve/mask/
reset/save/load workflows; TexturedCube assignment and panel-return workflows;
and the native UI runner with failure evidence and build isolation. Reuse the
existing application controls and native owners. Do not restore the removed
experiment controller, measurement system or universal recipe/report engine.

The UI test configuration must be explicitly enabled in an existing Ninja tree
and excluded from ordinary Release builds. Native rendering tests remain the
numerical authority; widget tests verify actual input, focus and visible state.

## Recorded qualification

| Slice | Status | Boundary | Evidence |
| ----- | ------ | -------- | -------- |

| 8.2 — ImGui UI automation | validated | Real-app Debug/Release workflows, test migration and ordinary-build isolation. | [UI qualification](validation.md) |

## Tasks and outcome

**Validated after EX09.** Six LightBench workflows and one TexturedCube workflow
pass in both Debug and Release. Both ordinary Ninja trees exclude UI instrumentation.
See [qualification](validation.md) and [migration review](README.md).

| ID         | Required work                                                                          | Status    |
| ---------- | -------------------------------------------------------------------------------------- | --------- |
| EX082-01   | Test Engine dependency/configuration/context integration.                              | validated |
| EX082-02   | Automated numeric/focus/drag/mode/curve/mask/reset/load/save workflows.                | validated |
| EX082-03   | Automated TexturedCube assignment/panel-return workflows.                              | validated |
| EX082-04   | Native UI runner, reports/failure artifacts and build isolation.                       | validated |
| EX082-GATE | Automated widget suite qualification; required before EX10 under the latest user goal. | validated |

## LightBench test migration review

Reviewed and qualified 2026-09-25. Developer instructions live in
[the test README](../../../../../Examples/LightBench/Test/README.md); this record owns
the migration history and evidence.

| Previous check                               | Current ownership                                                                                                                                                                                                                                                                                                                   |
| -------------------------------------------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `ModifiedSettingsRoundTripAndResetAllInputs` | Renamed to `ModifiedSettingsRoundTripThroughScene`; retains codec/scene fidelity. Its simulated reset moves to `save_load_and_panel_return`, which loads modifications to geometry, lights, camera, exposure/curve and output, then uses the actual Save/Reset/Load buttons.                                                        |
| `CompletePresetsValidateRoundTripAndReset`   | Renamed to `CompletePresetsValidateAndRoundTrip`: the fast data test validates all seven preset records and dirty detection. The application Reset operation belongs to Test Engine.                                                                                                                                                |
| `QueuesOwnerActionsAndRetiresCommands`       | Renamed to `ValidatesArgumentsDispatchesAndRetiresCommands`; removes manually advanced fake active/pending application state. Grammar, dispatch, Release access and lifetime remain native. `console_preset_and_settings` types commands into the actual console and observes application, invalid-input and stale-target outcomes. |
| Other three settings checks                  | Stay native: malformed/coupled-invalid documents, the shipped Indoor file and preset lighting/display contracts.                                                                                                                                                                                                                    |
| Three file checks                            | Stay native: missing/malformed/oversized files, atomic replacement, denied replacement preserving bytes and successful retry.                                                                                                                                                                                                       |
| Registration-conflict rollback               | Stays native: intentionally conflicting ownership and partial-construction failure.                                                                                                                                                                                                                                                 |
| Nine reference/rendering checks              | Stay native: independent material response, HDR/output/depth, directional publication and both rendering paths, saved Auto convergence, point photometry, spot footprint/oracle, clipping and history-preserving adaptation.                                                                                                        |
| Coordinate-driven panel/preset checks        | Named widget operations now cover panel return, unchanged camera extents, dirty/reset state, popup scrolling and complete save/load. Earlier screenshots retain their separate visual-review role.                                                                                                                                  |

Changed CPU checks pass **7/7 Debug and 7/7 Release**. LightBench's six real-app
widget cases pass in both configurations; TexturedCube's assignment/panel-return
case also passes in both. The nine numerical cases were neither weakened nor
rerun for this migration. See [results and raw evidence](validation.md#results).

## Supporting records

- [validation](validation.md)
- [evidence](evidence/README.md)
- [Captured evidence](evidence/README.md)
