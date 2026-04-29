# VTX-M04D.1 — Environment Publication And Sky/Fog Contract Truth

Status: `validated`

This is the first work package inside `VTX-M04D — Environment / Fog Parity
Closure`. It makes the active environment service truthful and inspectable
before deeper UE5.7 height fog, local fog, and volumetric fog work proceeds.

## 1. Goal

Make environment publications tell the truth.

After this work package, downstream code, diagnostics, tests, and runtime proof
must be able to distinguish:

- disabled environment features
- authored-but-unavailable resources
- valid published resources
- stale or skipped publications
- Stage 14 work that ran versus Stage 14 work that was not requested

This work package does not claim fog parity. It prepares the environment
contract so later fog parity work can be implemented and verified reliably.

Validation evidence is recorded in
[../IMPLEMENTATION_STATUS.md](../IMPLEMENTATION_STATUS.md). Real SkyLight
capture/filtering resources, UE5.7 height fog parity, UE5.7 local fog parity,
and UE5.7 volumetric fog parity remain outside this validated package.

## 2. Scope

In scope:

- preserve current sky/atmosphere behavior, including below-horizon invariants
- audit environment frame/view/static/product publications
- make SkyLight/IBL publication truthful
- replace revision-only IBL semantics with resource-aware state
- expose Stage 14 environment execution state at the SceneRenderer boundary
- add focused positive and negative tests for publication states
- update `PLAN.md` and `IMPLEMENTATION_STATUS.md` if implementation findings
  change the milestone scope

## 3. Non-Scope

Out of scope:

- UE5.7-complete exponential height fog
- UE5.7-complete local fog volume parity
- UE5.7-complete volumetric fog
- volumetric clouds
- heterogeneous volumes
- water
- full Async migration proof
- diagnostics product service implementation

Do not use this work package to add a parallel environment path. It must harden
the existing `EnvironmentLightingService` ownership path.

## 4. Current State

Known current state from source and planning inspection:

| Surface | Current State | Planning Consequence |
| --- | --- | --- |
| `EnvironmentLightingService` | Active owner for atmosphere, sky, fog, local fog, environment products, and Stage 14/15 work. | Preserve the service; do not create a replacement path. |
| Sky/atmosphere | Advanced implementation exists, including LUTs, sky view, aerial perspective, and stable below-horizon design invariants. | Treat as behavior to preserve while tightening publication truth. |
| SkyLight/IBL | Implementation surface exists, but probe revision can advance without proving usable resource publication. | Must become explicit valid/invalid/unavailable state. |
| Environment model slots | Some slots can remain invalid even when authored state exists. | Each slot needs a defined truth rule. |
| Local fog Stage 14 | Real local-fog tiled culling exists inside the environment service. | Expose state through SceneRenderer for validation and diagnostics. |
| Volumetric fog | Model/publication seams exist, but runtime parity is not present. | Report as unavailable/incomplete; do not imply runtime output. |
| SceneRenderer state | Environment state is Stage-15-biased. | Add Stage 14 and publication truth visibility. |
| Tests | Environment and SceneRenderer publication tests exist. | Extend focused tests rather than creating an unrelated harness first. |

## 5. Existing Behavior To Preserve

Preserve these invariants:

- raw atmosphere light directions are not clamped for below-horizon cases
- atmosphere LUT domains continue to cover the full required ranges
- split-horizon and sky-view behavior remain stable
- far-depth sky pixels do not receive local fog contribution
- Stage 14 remains environment-owned
- Stage 15 remains the sky/atmosphere/fog composition stage
- disabled features publish explicit invalid states rather than stale values
- no consumer needs private service internals to determine whether a product is
  valid

## 6. UE5.7 Parity References

Use these UE5.7 families to ground the contract review:

| Area | UE5.7 Reference Family |
| --- | --- |
| Sky atmosphere and LUT state | `SkyAtmosphereRendering`, `SkyAtmosphereCommon` |
| Sky atmosphere authoring | `SkyAtmosphereComponent` |
| Height fog authoring and coupling | `ExponentialHeightFogComponent`, `FogRendering`, `HeightFogCommon` |
| Local fog volumes | `LocalFogVolumeRendering` |
| Volumetric fog contract surface | `VolumetricFog` |
| SkyLight / IBL | SkyLight capture, filtered cubemap, irradiance, prefilter, and BRDF LUT paths relevant to the chosen Oxygen implementation |

For this work package, UE5.7 grounding is used to define truthful contracts and
state transitions. Full fog algorithm parity closes in `VTX-M04D.2` through
`VTX-M04D.4`.

## 7. Contract Truth Table

The implementation plan must audit these publications.

| Publication | Valid State | Invalid / Disabled State | Stale State Rule | Required Inspection Surface |
| --- | --- | --- | --- | --- |
| Environment frame bindings | All slots and flags match products produced this frame or stable persistent products. | Invalid shader-visible indices and disabled flags. | Old valid slots must not survive when source becomes unavailable. | Service and SceneRenderer publication state. |
| Environment static data | SkyLight, atmosphere, fog, and persistent resource metadata match the stable state. | Explicit zero/invalid fields with disabled flags. | Revision changes without resource changes must not imply valid resources. | Tests over CPU payload and GPU publication where possible. |
| Environment view data | Per-view atmosphere/fog parameters match the selected view and feature flags. | Per-view disabled state is explicit. | View resize/cut/feature toggles must reset dependent state. | Service test plus renderer publication test. |
| Environment view products | LUTs, aerial perspective, distant sky light, local/volumetric products are valid only when produced. | Invalid SRV/UAV indices and zero counters. | Previous frame products must not masquerade as current products unless explicitly persistent and valid. | Product state counters and capture/resource proof. |
| SkyLight / IBL resources | Environment map, irradiance, prefiltered map, BRDF LUT, and generation metadata are usable or intentionally unavailable. | Explicit unavailable reason and invalid slots. | Probe revision alone is not a valid publication. | Focused tests for enabled, disabled, unavailable, and source-changed cases. |
| Stage 14 local fog | Requested/executed/skipped state, HZB availability, tile counts, instance counts, draw/dispatch data are visible. | Not requested or skipped states are explicit. | Stage 14 counters reset every frame/view. | SceneRenderer environment state and service state. |
| Volumetric fog placeholder | Authored model may exist, but runtime output is invalid until VTX-M04D.4. | Explicit incomplete/unavailable state. | Model revision must not imply integrated scattering exists. | Tests that enabled model does not fake a product before implementation. |

## 8. Implementation Slices

### Slice 1 — Publication Audit And State Vocabulary

Purpose:

- define explicit state for valid, invalid, disabled, unavailable, and stale
  environment publications
- identify every binding/data/product field that needs a truth rule

Primary code areas:

- environment service
- environment data types
- environment frame/static/view/product contracts
- HLSL environment contract mirrors if layouts change

Likely touch points:

| Area | Paths |
| --- | --- |
| service orchestration | `src/Oxygen/Vortex/Environment/EnvironmentLightingService.*` |
| environment product types | `src/Oxygen/Vortex/Environment/Types/*` |
| renderer-facing bindings | `src/Oxygen/Vortex/Types/EnvironmentFrameBindings.h`, `src/Oxygen/Vortex/Types/EnvironmentStaticData.h` |
| HLSL mirrors | `src/Oxygen/Graphics/Direct3D12/Shaders/Vortex/Contracts/Environment/*` |
| focused tests | `src/Oxygen/Vortex/Test/EnvironmentLightingService_test.cpp` |

Tests:

- focused service tests for default, disabled, and authored states
- ABI/static-assert checks if layouts change

Per-slice validation:

```powershell
cmake --build --preset windows-debug --target Oxygen.Vortex.EnvironmentLightingService
ctest --preset test-debug -R "Oxygen.Vortex.EnvironmentLightingService" --output-on-failure
```

Status update:

- record every field whose truth rule changed in `IMPLEMENTATION_STATUS.md`
- record whether CPU/HLSL ABI changed and which validation covered it

Replan trigger:

- a required publication needs a new ABI field that conflicts with existing
  shader contract layout

### Slice 2 — SkyLight / IBL Truth

Purpose:

- stop treating probe revision as resource publication proof
- publish usable SkyLight/IBL resources when available
- publish explicit invalid/unavailable states when resources are missing

Primary code areas:

- SkyLight state translation
- IBL processor and probe pass
- environment static data construction
- environment frame bindings

Likely touch points:

| Area | Paths |
| --- | --- |
| state translation | `src/Oxygen/Vortex/Environment/Internal/AtmosphereState.*` |
| IBL processing | `src/Oxygen/Vortex/Environment/Internal/IblProcessor.*`, `src/Oxygen/Vortex/Environment/Passes/IblProbePass.*` |
| SkyLight model/data | `src/Oxygen/Vortex/Environment/Types/SkyLightEnvironmentModel.h`, `src/Oxygen/Vortex/Types/EnvironmentStaticData.h` |
| frame bindings | `src/Oxygen/Vortex/Environment/EnvironmentLightingService.*`, `src/Oxygen/Vortex/Types/EnvironmentFrameBindings.h` |
| HLSL consumers/contracts | `src/Oxygen/Graphics/Direct3D12/Shaders/Vortex/Contracts/Environment/*`, `src/Oxygen/Graphics/Direct3D12/Shaders/Vortex/Services/Environment/*` |
| focused tests | `src/Oxygen/Vortex/Test/EnvironmentLightingService_test.cpp` |

Tests:

- enabled SkyLight with usable inputs
- enabled SkyLight with unavailable inputs
- disabled SkyLight
- source-changed revision with and without resource changes

Per-slice validation:

```powershell
cmake --build --preset windows-debug --target Oxygen.Vortex.EnvironmentLightingService
ctest --preset test-debug -R "Oxygen.Vortex.EnvironmentLightingService" --output-on-failure
```

Status update:

- record the final SkyLight/IBL valid, disabled, unavailable, and stale-state
  semantics
- record whether real IBL resource generation landed or was explicitly
  deferred as unavailable-state publication

Replan trigger:

- real IBL resource generation requires broader capture/filtering work than can
  fit this work package; if so, publish explicit unavailable state and move
  real filtering to a follow-up milestone with human-visible status.

### Slice 3 — Stage 14 SceneRenderer Observability

Purpose:

- expose Stage 14 environment execution state at the renderer boundary
- make local-fog and future volumetric state visible to diagnostics and tests

Primary code areas:

- SceneRenderer environment state
- EnvironmentLightingService stage state
- SceneRenderer publication tests

Likely touch points:

| Area | Paths |
| --- | --- |
| renderer state contract | `src/Oxygen/Vortex/SceneRenderer/SceneRenderer.h` |
| renderer stage dispatch/publication | `src/Oxygen/Vortex/SceneRenderer/SceneRenderer.cpp` |
| environment service state | `src/Oxygen/Vortex/Environment/EnvironmentLightingService.*` |
| local fog state sources | `src/Oxygen/Vortex/Environment/Internal/LocalFogVolumeState.*`, `src/Oxygen/Vortex/Environment/Passes/LocalFogVolumeTiledCullingPass.*`, `src/Oxygen/Vortex/Environment/Passes/LocalFogVolumeComposePass.*` |
| focused tests | `src/Oxygen/Vortex/Test/SceneRendererPublication_test.cpp`, `src/Oxygen/Vortex/Test/EnvironmentLightingService_test.cpp` |

Tests:

- no local fog requested
- local fog requested with HZB available
- local fog requested without usable HZB
- per-view/sub-viewport reset of counters

Per-slice validation:

```powershell
cmake --build --preset windows-debug --target Oxygen.Vortex.SceneRendererPublication Oxygen.Vortex.EnvironmentLightingService
ctest --preset test-debug -R "Oxygen.Vortex.(SceneRendererPublication|EnvironmentLightingService)" --output-on-failure
```

Status update:

- record the public Stage 14 state fields added or changed
- record any diagnostics-facing assumptions introduced for VTX-M05A

Replan trigger:

- exposing Stage 14 state would leak environment-private structures instead of
  a stable summary contract

### Slice 4 — Product Truth Tests And Negative Cases

Purpose:

- prevent future false closure by testing invalid and unavailable states

Tests:

- authored volumetric fog does not publish integrated light scattering before
  VTX-M04D.4 runtime implementation
- enabled height fog/local fog states do not imply full parity closure
- model slots and product slots reset when features are disabled
- stale resources do not persist after source invalidation

Likely touch points:

| Area | Paths |
| --- | --- |
| service publication logic | `src/Oxygen/Vortex/Environment/EnvironmentLightingService.*` |
| view products | `src/Oxygen/Vortex/Environment/Types/EnvironmentViewProducts.h` |
| volumetric model seam | `src/Oxygen/Vortex/Environment/Types/VolumetricFogModel.h` |
| fog/local fog tests | `src/Oxygen/Vortex/Test/EnvironmentLightingService_test.cpp` |
| renderer publication tests | `src/Oxygen/Vortex/Test/SceneRendererPublication_test.cpp` |

Per-slice validation:

```powershell
cmake --build --preset windows-debug --target Oxygen.Vortex.EnvironmentLightingService Oxygen.Vortex.SceneRendererPublication
ctest --preset test-debug -R "Oxygen.Vortex.(EnvironmentLightingService|SceneRendererPublication)" --output-on-failure
```

Status update:

- record every negative case added and the false-completion behavior it blocks

Replan trigger:

- tests reveal that the current design cannot distinguish disabled from
  unavailable or stale; update the LLD before proceeding.

### Slice 5 — Status And Proof Wiring

Purpose:

- update planning/status docs with actual evidence and residual gaps
- define proof commands for later environment sub-milestones

Required updates:

- `IMPLEMENTATION_STATUS.md`
- this plan, if the implementation reveals new constraints
- environment LLD, if contract truth requires design changes

Likely touch points:

| Area | Paths |
| --- | --- |
| status ledger | `design/vortex/IMPLEMENTATION_STATUS.md` |
| milestone plan | `design/vortex/PLAN.md` |
| detailed plan | `design/vortex/plan/VTX-M04D.1-environment-publication-truth.md` |
| environment LLD | `design/vortex/lld/environment-service.md` |

Per-slice validation:

```powershell
rg -n --glob "*.md" "Environment Publication|SkyLight/IBL|Stage 14" design/vortex
git diff --check
```

Status update:

- append the final evidence row for VTX-M04D.1 after implementation
- leave VTX-M04D.2 through VTX-M04D.4 unvalidated until their own proof lands;
  those downstream proof packages are now validated in the status ledger

## 9. Test Plan

Focused tests to extend first:

| Test Area | Expected Coverage |
| --- | --- |
| Environment service tests | binding/static/view/product truth, SkyLight/IBL states, Stage 14 state reset, volumetric unavailable state |
| SceneRenderer publication tests | Stage 14/15 state visible at renderer boundary, no private service dependency, per-view publication behavior |
| ABI tests/static asserts | CPU/HLSL lockstep if any environment contract layout changes |

Recommended command:

```powershell
cmake --build --preset windows-debug --target Oxygen.Vortex.EnvironmentLightingService Oxygen.Vortex.SceneRendererPublication
ctest --preset test-debug -R "Oxygen.Vortex.(EnvironmentLightingService|SceneRendererPublication)" --output-on-failure
```

If CPU/HLSL contracts or shaders change, also run the shader bake/catalog
validation path used by the shader-contract LLD and record the exact command in
`IMPLEMENTATION_STATUS.md`.

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

## 11. Exit Gate

`VTX-M04D.1` can close only when:

1. every environment publication affected by this work has a documented truth
   rule
2. SkyLight/IBL states distinguish valid, disabled, unavailable, and stale
   cases
3. Stage 14 state is visible through SceneRenderer
4. focused environment service tests pass
5. focused SceneRenderer publication tests pass
6. shader/ABI validation passes if contracts changed
7. `IMPLEMENTATION_STATUS.md` records files changed, commands run, results,
   UE5.7 references checked, and remaining gaps

## 12. Replan Triggers

Stop and update design/plan before continuing if:

- SkyLight/IBL requires a broader resource-generation architecture than this
  work package can safely contain
- an environment binding needs an ABI change that affects downstream consumers
  beyond environment/shaders
- Stage 14 state cannot be exposed without leaking private service structures
- tests show disabled, unavailable, and stale states cannot be distinguished
- preserving below-horizon sky behavior conflicts with a publication change
- implementation work starts drifting into full height/local/volumetric fog
  parity before contract truth is solved

## 13. Status Update Requirements

When implementation work starts, update
[../IMPLEMENTATION_STATUS.md](../IMPLEMENTATION_STATUS.md):

- change slice-specific notes from planning to active implementation evidence
- record files changed
- record focused tests and results
- record shader/ABI validation if relevant
- record UE5.7 references checked
- record residual gaps

Do not mark `VTX-M04D.1` validated until all exit-gate evidence is present.
