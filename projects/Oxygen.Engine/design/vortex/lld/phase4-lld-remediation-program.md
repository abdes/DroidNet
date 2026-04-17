# Phase 4 LLD Remediation Program

**Phase:** cross-phase design remediation beginning from Phase 4
**Status:** `completed`
**Scope:** tracked design-package patch series under `design/vortex/`

**Execution note:** this program has been executed on the current tracked
design package. The work packages below are retained as the execution record
and the reapplication guide for any future audit that finds new Phase 4 design
drift.

## 1. Purpose and Context

This document is the authoritative execution guide for the **complete**
remediation of the Phase 4 Vortex LLD package.

Its purpose is not to make the smallest possible wording edits. Its purpose is
to produce a **truthful, future-proof, architecture-aligned, UE5.7-informed**
Phase 4 design package that:

1. preserves the already-correct Vortex architectural decisions,
2. closes the parity and scope gaps found in the Phase 4 review,
3. does not hide unresolved authority or ownership problems behind vague
   "later" wording,
4. pushes work to later phases only when that deferral is already justified by
   the architecture and phase plan,
5. leaves a design package that can be implemented without inventing missing
   contracts during coding.

This remediation starts from the current authoritative design package:

- [ARCHITECTURE.md](../ARCHITECTURE.md)
- [PLAN.md](../PLAN.md)
- [DESIGN.md](../DESIGN.md)
- [IMPLEMENTATION-STATUS.md](../IMPLEMENTATION-STATUS.md)
- the Phase 4 LLD set in this directory

It is informed by UE5.7 source under:

- `F:\Epic Games\UE_5.7\Engine\Source\Runtime\Renderer`
- `F:\Epic Games\UE_5.7\Engine\Source\Runtime\Engine`

## 2. Non-Negotiable Design Truths

The remediation series must preserve these truths.

### 2.1 Architectural Truth

1. Stage 12 is the canonical deferred **direct**-lighting stage.
2. Stage 13 remains the future canonical **indirect**-lighting owner.
3. Stage 6 forward-light data is supporting shared data, not the deferred
   direct-lighting root.
4. Stage 8 shadow production remains distinct from later lighting
   consumption.
5. Stage 15 owns active sky/atmosphere/fog environment composition.
6. Stage 22 owns post processing.
7. Renderer Core and published per-view products remain the canonical routing
   seams. Services do not bypass them.

### 2.2 Scope Truth

1. Phase 4 is the first migration-capable runtime phase.
2. Phase 4E's first-success gate remains `Examples/Async` unless the product
   intent is explicitly changed in `PRD.md` and `PLAN.md`.
3. Phase 4C is a **directional-first** conventional-shadow baseline.
4. Spot-light and point-light conventional shadow expansion remain later work
   unless the design package is explicitly widened.
5. Volumetrics remain future Environment-family work.
6. Canonical indirect lighting remains future Stage-13 work.

### 2.3 Truthful Deferral Rules

Work may be deferred only when at least one of these is already true:

1. The later owner and phase are already named in `ARCHITECTURE.md` and
   `PLAN.md`.
2. Deferring the work does not break the current phase's truthfulness.
3. The current phase can still define a complete and honest contract without
   pretending the deferred work already exists.

### 2.4 Repository Constraint

Tracked remediation patches in this series must **not** modify `.planning/`.
If local `.planning` mirrors are maintained by the operator, they are updated
separately and remain untracked.

## 3. Documents In Scope

### 3.1 Primary Phase 4 LLD Targets

- [lighting-service.md](./lighting-service.md)
- [shadow-service.md](./shadow-service.md)
- [environment-service.md](./environment-service.md)
- [post-process-service.md](./post-process-service.md)
- [migration-playbook.md](./migration-playbook.md)

### 3.2 Immediate Same-Series Collateral Documents

These must be updated in the same tracked remediation series because leaving
them stale would preserve design-package contradictions.

- [README.md](./README.md)
- [PLAN.md](../PLAN.md)
- [DESIGN.md](../DESIGN.md)
- [IMPLEMENTATION-STATUS.md](../IMPLEMENTATION-STATUS.md)
- [shadow-local-lights.md](./shadow-local-lights.md)
- [indirect-lighting-service.md](./indirect-lighting-service.md)

### 3.3 Documents Not Changed Unless Scope Truly Changes

- `PRD.md` stays unchanged unless the first-success gate or product goal itself
  changes.
- `.planning/` stays outside the tracked patch series.

## 4. Required End State

The remediation series is complete only when all of the following are true:

1. The Phase 4 LLDs no longer contradict `ARCHITECTURE.md`.
2. The Phase 4 LLDs no longer contradict `PLAN.md`.
3. The Phase 4 LLDs no longer silently narrow or widen Phase 4 scope.
4. Every per-view publication contract is explicit about:
   - persistent state owner,
   - per-view publication payload,
   - frame-scoped vs view-scoped work,
   - allowed transitional subsets.
5. No active Phase 4 LLD still forces implementers to invent:
   - stage ownership,
   - light-selection authority,
   - shadow publication shape,
   - environment publication shape,
   - exposure ownership shape,
   - migration proof boundary.
6. Every future carry-forward item has:
   - a named future owner,
   - a named future phase,
   - a bounded reason for deferral.

## 5. Review Protocol

Every remediation item is completed under a mandatory spawned-subagent review
gate. No item closes on leader judgment alone.

### 5.1 Item Completion Loop

For each item:

1. Edit only the files owned by that item.
2. Reread the touched sections in:
   - `ARCHITECTURE.md`
   - `PLAN.md`
   - the relevant future-owner LLD if the item touches a deferred boundary
3. Spawn the required review subagents for that item.
4. Integrate or rebut every material review finding explicitly.
5. Do not mark the item complete until the review gates pass.

### 5.2 Required Spawned Review Roles

| Item Type | Required Review Roles | Review Goal |
| --- | --- | --- |
| Lighting / Shadows / Environment / Post | `architect` + `critic` | UE5.7 parity, architecture truth, phase-scope truth |
| Migration playbook / phase-gate truth | `analyst` + `critic` | Runtime seam truth, migration proof truth, scope truth |
| Final coherence pass | `verifier` + `critic` | Cross-doc contradiction sweep |

### 5.3 Review Questions Every Item Must Answer

1. Does the updated doc now match `ARCHITECTURE.md`?
2. Does it now match the intended Phase 4 scope in `PLAN.md`?
3. Does it preserve truthful alignment to UE5.7 at the architectural-family
   level?
4. Does it avoid jeopardizing completed Phase 3 work?
5. If it defers anything, is that deferral now explicit, bounded, and assigned
   to a named later owner?

### 5.4 Commit Boundary Rule

Each remediation item lands in its own commit after the review gate passes.
Commits must follow the repo's Lore protocol.

## 6. Execution Order

The current Phase 4 package must **not** be treated as fully parallelizable.
The blanket `4A-4D are parallelizable` claim is itself part of the remediation
scope.

The tracked remediation series runs in this order:

1. **R0** - reopen the Phase 4 design gate and fix package truth
2. **R1** - remediate `LightingService`
3. **R2** - remediate `ShadowService`
4. **R3** - remediate `EnvironmentLightingService`
5. **R4** - remediate `PostProcessService`
6. **R5** - remediate `migration-playbook.md`
7. **R6** - update future-owner LLDs that define justified carry-forward
8. **R7** - run final design-package coherence review

Notes:

- `PostProcessService` can proceed after `R0`, but `R1-R3` define more of the
  cross-service authority boundary and should be stabilized first.
- `ShadowService` must follow `LightingService` because the directional-light
  authority and published lighting payload shape affect the truthful shadow
  contract.
- `EnvironmentLightingService` may proceed in parallel only after the Stage-12
  direct-vs-indirect split and the ambient-bridge policy are fixed in
  `LightingService`. This program keeps it after `R1` for truthfulness.
- `migration-playbook.md` must not be treated as independent of the earlier
  design fixes because it is the first-success gate and must name the real
  feature baseline.

## 7. Work Packages (Completed Execution Record)

The work packages below describe the remediation sequence that has now been
applied to the current tracked design package.

### R0 - Reopen The Phase 4 Design Gate

**Goal:** Correct the package truth before touching domain details.

**Files:**

- [PLAN.md](../PLAN.md)
- [DESIGN.md](../DESIGN.md)
- [IMPLEMENTATION-STATUS.md](../IMPLEMENTATION-STATUS.md)
- [migration-playbook.md](./migration-playbook.md)
- [README.md](./README.md)

**Required outcomes:**

1. Remove the false implication that `4A-4D` are already design-complete and
   safely parallelizable in their current form.
2. Reconcile the first-success runtime proof boundary:
   - keep `Examples/Async` as the Phase 4E gate unless product intent changes,
   - remove stale historical wording that implies `Async + DemoShell` is the
     required first migration gate.
3. Mark D.9-D.13 readiness truthfully if the current LLD set is still under
   remediation.
4. Make the remediation program discoverable from the LLD package index.

**Review gate:** `analyst` + `critic`

**Completion signal:** the tracked design package no longer claims that Phase 4
is design-ready while the cross-service authority gaps are still open.

### R1 - Remediate `LightingService`

**Goal:** Make `LightingService` the truthful owner of Stage 6 shared
forward-light preparation and Stage 12 deferred direct-lighting ownership
transfer without collapsing the direct/indirect split.

**Files:**

- [lighting-service.md](./lighting-service.md)
- [indirect-lighting-service.md](./indirect-lighting-service.md)
- [PLAN.md](../PLAN.md) if the dependency text remains stale
- [DESIGN.md](../DESIGN.md) if it still states Stage 12 consumes generic IBL

**Items:**

1. **R1.1 - Frame-scope vs per-view seam**
   - Replace `BuildLightGrid(RenderContext&)` with a frame-scope input contract.
   - Define the frame-shared build path separately from per-view publication.

2. **R1.2 - Authoritative directional-light selection and payload**
   - Promote directional-light authority from a loose index to a real per-view
     published directional-light data contract.
   - State explicitly who selects the directional light and when.

3. **R1.3 - Expand `ForwardLightFrameBindings`**
   - Align the published package with Vortex's architecture-minimum contract and
     truthful UE5.7-family parity.
   - Include grid metadata, directional-light data, and the minimum consumer
     fields required by future translucency and shadow-adjacent families.

4. **R1.4 - Restore Stage 12 authority**
   - Remove any implication that Stage 6 owns the Stage 12 deferred-light draw
     contract.
   - State that Stage 12 derives per-light draw packets from the same
     renderer-owned frame light set used by Stage 6.

5. **R1.5 - Geometry ownership split**
   - State explicitly that `LightingService` owns persistent sphere/cone proxy
     geometry and related GPU resources.
   - State explicitly that authoritative scene light state remains outside the
     service and is imported per frame.

6. **R1.6 - Ambient bridge truth**
   - Keep the bridge transitional, opt-in, and explicitly bounded to an ambient
     subset.
   - Update [indirect-lighting-service.md](./indirect-lighting-service.md) so
     the bridge retirement path is named now, not invented later.

**Review gate:** `architect` + `critic`

**Completion signal:** `LightingService` has a complete frame/shared and
per-view contract, and Stage 12 remains a truthful deferred direct-lighting
owner.

### R2 - Remediate `ShadowService`

**Goal:** Make Phase 4C a truthful directional-first shadow baseline without
freezing future local-light or VSM ABI by accident.

**Files:**

- [shadow-service.md](./shadow-service.md)
- [shadow-local-lights.md](./shadow-local-lights.md)
- [PLAN.md](../PLAN.md) if any Phase 4C dependency wording remains stale

**Items:**

1. **R2.1 - Phase 4C directional-only scope**
   - Remove Phase 4C local-light shadow payload commitments from the current
     contract.
   - State that local-light conventional shadow bindings remain future work.

2. **R2.2 - Consumer-neutral directional shadow publication**
   - Replace the atlas-shaped public contract with a directional conventional
     shadow product contract.
   - Keep implementation storage strategy internal to the service.

3. **R2.3 - Directional-light authority source**
   - State that Phase 4C consumes the per-view selected directional-light
     result produced earlier in the frame rather than inventing a second
     election path.

4. **R2.4 - Per-view state and publication split**
   - Replace singleton-style service state with an explicit per-view published
     payload model and a separate CPU/debug inspection model.

5. **R2.5 - Future-owner sync**
   - Update [shadow-local-lights.md](./shadow-local-lights.md) so the future
     local-light conventional path is now a clear, bounded carry-forward owner.

**Review gate:** `architect` + `critic`

**Completion signal:** the Phase 4C LLD is directional-first, per-view-safe,
and no longer freezes future local-light shadow ABI.

### R3 - Remediate `EnvironmentLightingService`

**Goal:** Restore truthful Stage-15 environment ownership and make the
environment publication model compatible with the direct/indirect split and the
live reverse-Z renderer baseline.

**Files:**

- [environment-service.md](./environment-service.md)
- [indirect-lighting-service.md](./indirect-lighting-service.md)
- [DESIGN.md](../DESIGN.md) if stale Stage-12 IBL text remains

**Items:**

1. **R3.1 - Restore active atmosphere ownership**
   - Rewrite Stage 15 so it owns sky/atmosphere/fog composition, not merely a
     far-plane cubemap background plus fog.
   - Keep higher-fidelity atmosphere refinement as future internal work within
     the same owner, not as a Phase-7 deferral of atmosphere itself.

2. **R3.2 - Reverse-Z-safe depth semantics**
   - Remove any hardcoded `depth == 1.0` / non-reverse-Z assumptions from the
     doc examples and contract language.
   - State the logic in terms of renderer depth conventions and far-background
     eligibility, not raw fixed depth values.

3. **R3.3 - Persistent probe state vs per-view published evaluation**
   - Separate long-lived environment probe state from the per-view published
     `EnvironmentFrameBindings`.
   - Remove singleton-style service-global publication language.

4. **R3.4 - Narrow the Phase 4 ambient bridge**
   - Define the exact subset that Stage 12 may temporarily consume.
   - Explicitly keep reflections, AO, skylight shadowing, and canonical
     indirect environment evaluation in the future Stage-13 owner.

5. **R3.5 - Future-owner sync**
   - Update [indirect-lighting-service.md](./indirect-lighting-service.md) so
     the future Stage-13 contract explicitly absorbs the temporary Phase 4
     bridge and becomes the canonical owner.

**Review gate:** `architect` + `critic`

**Completion signal:** Stage 15 is truthful again, the environment publication
model is per-view-safe, and the direct/indirect split is explicit.

### R4 - Remediate `PostProcessService`

**Goal:** Close the remaining exposure-ownership gap without distorting the
already-correct Stage-22 boundary.

**Files:**

- [post-process-service.md](./post-process-service.md)
- [DESIGN.md](../DESIGN.md) only if it still places post-owned exposure work
  elsewhere

**Items:**

1. **R4.1 - Exposure ownership split**
   - State explicitly that view-to-view source resolution belongs to the view
     lifecycle contract, while post owns eye adaptation, local exposure, and
     post histories once Stage 22 executes.

2. **R4.2 - Reserve local exposure**
   - Add local exposure as a named post-owned family now rather than leaving it
     as an unowned future concern.

3. **R4.3 - Preserve the Stage 21/22/23 boundary**
   - Reconfirm that post owns post-family work only, with resolve and
     extraction/handoff remaining outside the service.

**Review gate:** `architect` + `critic`

**Completion signal:** the post-family ownership model is complete and no
longer invites future drift around exposure.

### R5 - Remediate `migration-playbook.md`

**Goal:** Make the migration guide match the real legacy seams, the truthful
Phase 4 feature baseline, and the actual proof boundary.

**Files:**

- [migration-playbook.md](./migration-playbook.md)
- [PLAN.md](../PLAN.md) if the migration dependency text must be tightened
- [IMPLEMENTATION-STATUS.md](../IMPLEMENTATION-STATUS.md) if stale first-gate
  wording remains

**Items:**

1. **R5.1 - Real legacy seam inventory**
   - Document the actual `Examples/Async` renderer dependencies:
     legacy pipeline selection, composition views, DemoShell integration,
     ImGui coupling, and renderer-owned view-registration seams.

2. **R5.2 - Truthful feature baseline**
   - Define which features are part of the Phase 4E parity target.
   - The baseline must include atmosphere where the live example requires it.
   - Spotlight presence is part of the scene baseline.
   - Spotlight shadows stay outside the baseline unless the design package is
     explicitly widened, because the current example default is non-shadowing.

3. **R5.3 - No-shim rule with truthful migration work**
   - Keep the "no long-lived compatibility clutter" rule.
   - Remove any implication that the migration is mostly a namespace/include
     swap.
   - State the real seam replacements that must occur.

4. **R5.4 - Runtime proof boundary**
   - Keep `Examples/Async` as the first-success gate unless the product goal is
     intentionally widened.
   - Remove stale historical language that incorrectly makes `DemoShell`
     co-required for the first truthful Phase 4 runtime proof.

**Review gate:** `analyst` + `critic`

**Completion signal:** the migration playbook describes the real workload,
real feature baseline, and real proof boundary.

### R6 - Future-Owner LLD Sync

**Goal:** Future-proof the repaired Phase 4 boundaries by updating the named
later owners that absorb the justified deferrals.

**Files:**

- [shadow-local-lights.md](./shadow-local-lights.md)
- [indirect-lighting-service.md](./indirect-lighting-service.md)

**Items:**

1. **R6.1 - Local-light shadow carry-forward**
   - Make the Phase 5G shadow expansion doc explicitly inherit the corrected
     Phase 4C boundary.

2. **R6.2 - Indirect-lighting carry-forward**
   - Make the Stage-13 future owner explicitly retire the temporary Phase 4
     ambient bridge.

**Review gate:** `architect` + `critic`

**Completion signal:** later-phase owners are named clearly enough that the
Phase 4 LLDs do not need ambiguous placeholder prose.

### R7 - Final Coherence Pass

**Goal:** Confirm that the tracked design package tells one consistent story.

**Files:**

- all files touched by `R0-R6`

**Required checks:**

1. No active doc still says `4A-4D` are safely parallelizable without
   qualification.
2. No active doc still treats Stage 12 as indirect-light/IBL owner.
3. No active doc still treats Phase 4C as if it already includes local-light
   conventional shadows.
4. No active doc still narrows Stage 15 to background-only sky.
5. No active doc still leaves per-view publication to singleton-style service
   sketches.
6. No active doc still understates the actual `Examples/Async` migration seam.

**Review gate:** `verifier` + `critic`

**Completion signal:** the tracked design package is internally coherent and
ready for implementation planning without hidden contract invention.

## 8. Explicitly Justified Deferrals

The remediation must preserve only these deferrals unless product intent is
explicitly widened.

1. **Local-light conventional shadows** remain deferred to Phase 5G.
   - Justification: Phase 4C is intentionally directional-first.
   - Future owner: [shadow-local-lights.md](./shadow-local-lights.md)

2. **Canonical indirect lighting / reflections / AO / skylight shadowing**
   remain deferred to Stage 13 / future indirect-lighting work.
   - Justification: Stage 12 remains direct-lighting only.
   - Future owner: [indirect-lighting-service.md](./indirect-lighting-service.md)

3. **VSM** remains future work.
   - Justification: Phase 4C is the conventional-shadow baseline.
   - Future owner: `ShadowService` future VSM expansion

4. **Volumetrics / heterogeneous volumes / clouds** remain future work.
   - Justification: Stage 14 is reserved future Environment-family work.
   - Future owner: `EnvironmentLightingService` future Stage-14 family

5. **Higher-fidelity atmosphere refinement** may remain future internal work.
   - Justification: Stage 15 must still own active atmosphere composition in
     Phase 4; only fidelity refinement is deferred.

## 9. Stop Conditions

Stop the current item and widen the same-series tracked patch if any of the
following becomes true:

1. A remediation item proves that `ARCHITECTURE.md` itself is stale.
2. A remediation item proves that the intended Phase 4 scope in `PLAN.md` is
   insufficient for the truthful first-success gate.
3. The `Examples/Async` baseline is found to require spotlight shadows or any
   other feature that the current Phase 4 plan does not actually include.
4. A proposed deferral has no named future owner and phase.

When a stop condition triggers, update the authoritative design package in the
same tracked series before continuing.

## 10. Implementation Notes

1. This is a **document remediation program**, not the service implementation
   itself.
2. The tracked series may update `PLAN.md`, `DESIGN.md`, and
   `IMPLEMENTATION-STATUS.md` when required for truthfulness.
3. The tracked series does **not** update `.planning/`.
4. If local untracked mirrors are maintained, sync them only after the tracked
   series is complete.

## 11. Open Questions

None are accepted at program start. If a new open question appears during the
series, it must either:

1. be resolved in the same remediation item, or
2. be deferred only through the explicit deferral rules in Section 2.3 and
   recorded with a named future owner.
