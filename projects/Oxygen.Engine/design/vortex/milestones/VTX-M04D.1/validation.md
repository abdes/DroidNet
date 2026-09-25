# VTX-M04D.1 — validation

This record preserves the conditions and results at each implementation checkpoint.
The milestone README owns the final outcome and remaining work.

## Validation and remaining work

**Qualification:** `validated`

EnvironmentLightingService sanitizes IBL probe bindings, distinguishes authored SkyLight from usable IBL resources, keeps invalid SkyLight/volumetric products explicit, exposes Stage 14 local-fog state through SceneRenderer, and focused EnvironmentLightingService/SceneRendererPublication tests passed on 2026-04-25.

**Remaining work:** Real SkyLight capture/filtering and fog parity are outside VTX-M04D.1 and tracked by later milestones.

## 4. Current State

Known current state from source and planning inspection:

| Surface                      | Current State                                                                                                             | Planning Consequence                                                  |
| ---------------------------- | ------------------------------------------------------------------------------------------------------------------------- | --------------------------------------------------------------------- |
| `EnvironmentLightingService` | Active owner for atmosphere, sky, fog, local fog, environment products, and Stage 14/15 work.                             | Preserve the service; do not create a replacement path.               |
| Sky/atmosphere               | Advanced implementation exists, including LUTs, sky view, aerial perspective, and stable below-horizon design invariants. | Treat as behavior to preserve while tightening publication truth.     |
| SkyLight/IBL                 | Implementation surface exists, but probe revision can advance without proving usable resource publication.                | Must become explicit valid/invalid/unavailable state.                 |
| Environment model slots      | Some slots can remain invalid even when authored state exists.                                                            | Each slot needs a defined truth rule.                                 |
| Local fog Stage 14           | Real local-fog tiled culling exists inside the environment service.                                                       | Expose state through SceneRenderer for validation and diagnostics.    |
| Volumetric fog               | Model/publication seams exist, but runtime parity is not present.                                                         | Report as unavailable/incomplete; do not imply runtime output.        |
| SceneRenderer state          | Environment state is Stage-15-biased.                                                                                     | Add Stage 14 and publication truth visibility.                        |
| Tests                        | Environment and SceneRenderer publication tests exist.                                                                    | Extend focused tests rather than creating an unrelated harness first. |

## 9. Test Plan

Focused tests to extend first:

| Test Area                       | Expected Coverage                                                                                            |
| ------------------------------- | ------------------------------------------------------------------------------------------------------------ |
| Environment service tests       | binding/static/view/product truth, SkyLight/IBL states, Stage 14 state reset, volumetric unavailable state   |
| SceneRenderer publication tests | Stage 14/15 state visible at renderer boundary, no private service dependency, per-view publication behavior |
| ABI tests/static asserts        | CPU/HLSL lockstep if any environment contract layout changes                                                 |

Recommended command:

```powershell
cmake --build --preset windows-debug --target Oxygen.Vortex.EnvironmentLightingService Oxygen.Vortex.SceneRendererPublication
ctest --preset test-debug -R "Oxygen.Vortex.(EnvironmentLightingService|SceneRendererPublication)" --output-on-failure
```

If CPU/HLSL contracts or shaders change, also run the shader bake/catalog
validation path used by the shader-contract LLD and record the exact command in
`milestone README`.

## 10. Runtime / Capture Proof

Runtime proof is not required to close every internal slice, but the work
package must leave the environment state inspectable enough for later runtime
proof.

Minimum capture-readiness requirements:

- stable pass/scope names for Stage 14 local-fog work
- stable pass/scope names for Stage 15 sky/atmosphere/fog work
- inspectable environment frame/static/view/product bindings
- inspectable SkyLight/IBL resource slots or explicit invalid state
- counters for requested/executed/skipped Stage 14 work

Do not claim runtime environment closure in this work package unless a runtime
command, capture/replay, analyzer, and assertion result are actually run and
recorded.
