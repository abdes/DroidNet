# VTX-M07 — Production readiness and legacy retirement

Status: `validated`

| Field     | Summary                                                                  |
| --------- | ------------------------------------------------------------------------ |
| Outcome   | Legacy retirement, example migration and desktop baseline qualification. |
| Remaining | None in the recorded scope.                                              |
| Evidence  | [Validation record](validation.md)                                       |

[Roadmap](../../PLAN.md) · [Design index](../../lld/README.md)

Dependencies: VTX-M06C

## Delivered scope

Static legacy seam guard, stale demo/doc cleanup, required demo refresh/testing, production proof-suite consolidation, binary dependency audit, current-path doc routing, CDB/RenderDoc closure proof, and safe legacy retirement are validated.

## Desktop baseline acceptance

The production-complete desktop deferred baseline is reached when:

1. VTX-M04E proves the canonical Async runtime path through Vortex.
2. VTX-M04F proves single-view composition/presentation.
3. VTX-M05A through VTX-M05D close diagnostics, occlusion consumers,
   translucency, and local-light conventional shadows.
4. VTX-M06A through VTX-M06C close multi-view, offscreen, and feature-gated
   runtime variants.
5. VTX-M07 validates production readiness and safe legacy retirement.
6. All closure claims are recorded in `milestone README` with
   implementation files, docs/status updates, tests, runtime/capture evidence
   where applicable, and residual gaps.

Status: `validated`

## 1. Goal

VTX-M07 turns the validated Vortex runtime surfaces into the sole supported
renderer path for required examples/tests, and retires remaining legacy
`Oxygen.Renderer` seams without using legacy code as a reference,
simplification path, fallback, or compatibility bridge.

The milestone is not a new rendering-feature milestone. It is a
production-readiness and retirement gate: stale source, stale docs, build graph
edges, example routing, validation scripts, and status ledgers must all agree
that Vortex is the renderer path being built and proven.

## 2. Scope

In scope:

- Inventory and remove or quarantine source/build references to legacy
  `Oxygen.Renderer` in Vortex-owned code and required examples.
- Replace stale example/demo files that still reference legacy renderer APIs
  with Vortex-native code, or remove them when they are no longer part of a
  build target.
- Update stale README/proof instructions that still describe legacy renderer
  baseline capture as a current path.
- Refresh, upgrade, and fix required demos that have drifted from the current
  Vortex runtime contract. Demos that were extensively exercised by recent
  validated milestones may be accepted by citing that fresh proof unless M07
  touches them or a seam scan/runtime symptom contradicts the prior evidence.
- Harden seam validation so actual `Oxygen.Renderer` includes, namespaces,
  targets, runtime dependencies, bridges, and fallback paths are blockers.
- Keep allowed shared UI usage explicit: `oxygen::imgui` is permitted in
  Vortex and example UI code; it is not a legacy renderer seam by itself.
- Define and run a production-readiness suite for required Vortex examples,
  Vortex link/hermeticity tests, and the already validated runtime proof
  surfaces that guard multi-view, offscreen, feature-gated variants,
  diagnostics, occlusion, translucency, and conventional shadows.
- Reconcile `PLAN.md`, `milestone README`, and this plan with exact
  evidence as slices land.

Out of scope:

- New post-baseline rendering families such as GI/reflections, VSM,
  heterogeneous volumes, clouds, water, hair, distortion, light shafts, or
  material-composition systems.
- Reopening validated M04-M06 parity decisions unless a production-readiness
  validation exposes a regression.
- Using legacy `Oxygen.Renderer` code, docs, tests, or captures as a parity
  reference.
- Mechanical deletion of historical design/archive documents unless the slice
  explicitly owns documentation routing and preserves useful historical
  context.

## 3. Non-Scope

- No renderer rewrite or RDG migration.
- No compatibility layer that forwards Vortex calls into legacy renderer
  objects.
- No new legacy baseline captures.
- No broad source cleanup unrelated to legacy seams, production-readiness
  validation, or stale status truth.

## 5. Existing Behavior To Preserve

- Vortex-native architecture only: no legacy renderer fallback paths.
- Validated M04-M06 behavior: diagnostics, occlusion, translucency,
  conventional shadows, multi-view, offscreen, and feature-gated variants.
- DemoShell and example UI may keep using `oxygen::imgui`.
- CDB/debug-layer discipline remains the default for runtime crash/GPU error
  investigation.
- RenderDoc scripted proof remains required when GPU pass ordering, descriptor
  binding, stage omission, or visual product truth is part of a claim.
- Status truth stays evidence-based: no `validated` status without code,
  docs/status, and validation evidence.

## 6. UE5.7 Parity References

M07 has no new UE5.7 rendering algorithm target. It is a retirement and
production-readiness milestone.

If any M07 slice modifies a renderer subsystem that owns a UE-shaped behavior,
that slice must cite the relevant UE5.7 source/shader files already used by
the owning milestone, or re-check local UE5.7 source under:

- `F:\Epic Games\UE_5.7\Engine\Source`
- `F:\Epic Games\UE_5.7\Engine\Shaders`

Legacy `Oxygen.Renderer` remains forbidden as parity evidence.

## 7. Contract Truth Table

| Contract              | Valid state                                                                                                  | Invalid/stale state                                                                                          | Proof surface                                                               |
| --------------------- | ------------------------------------------------------------------------------------------------------------ | ------------------------------------------------------------------------------------------------------------ | --------------------------------------------------------------------------- |
| Source include seam   | Vortex/example code includes Vortex, Graphics, Engine, ImGui, or other approved modules.                     | Any current Vortex/example source includes `<Oxygen/Renderer/...>` or uses legacy renderer namespaces/types. | Static seam scan and focused builds.                                        |
| Build graph seam      | Required examples and `oxygen::vortex` link without `oxygen-renderer`.                                       | A Vortex or required-example target links legacy renderer directly or transitively.                          | CMake target scan plus binary dependency inspection where available.        |
| Runtime path          | Required examples instantiate/run Vortex renderer paths.                                                     | Runtime bootstraps legacy renderer, fallback renderer, or bridge/adaptor.                                    | Runtime logs, CDB audits, proof scripts.                                    |
| Documentation routing | Current docs point implementation work to Vortex plans/status. Historical docs are marked archive or scoped. | Stale docs present legacy renderer as current production/reference path.                                     | README/status/doc grep and source-to-target coverage check for edited docs. |
| UI dependency         | `oxygen::imgui` usage remains explicitly allowed for panels/overlays.                                        | Validation treats ImGui as legacy renderer or blocks Vortex UI without an actual renderer seam.              | Link test/validation rule review.                                           |
| Status ledger         | `PLAN.md` and `milestone README` name real next work and evidence.                                           | Rows claim closure without proof or leave stale active milestone names.                                      | `git diff --check`, status consistency scan.                                |

## 8. Implementation Slices

### A. Plan And Status Truth

Required work:

- Create this detailed M07 plan.
- Add the plan to `design/vortex/PLAN.md`.
- Update `PLAN.md` and `milestone README` so M07 is `in_progress`
  and clearly not yet implemented.

[Checks](validation.md#a-plan-and-status-truth--checks).

[Results](validation.md#a-plan-and-status-truth--results).

### B. Legacy Seam Inventory And Static Guard

Required work:

- Add or extend a static validation script that scans current Vortex and
  required-example code for actual legacy renderer includes, namespaces,
  target links, and bridge/adaptor seams.
- Keep `oxygen::imgui` explicitly allowed.
- Make stale uncompiled source visible in the report instead of letting it
  hide outside CMake target membership.
- Record the initial inventory and fail on remaining source/build seams.

Likely touch points:

- `tools/vortex/*`
- `src/Oxygen/Vortex/Test/Link_test.cpp`
- `src/Oxygen/Vortex/Test/CMakeLists.txt`

[Checks](validation.md#b-legacy-seam-inventory-and-static-guard--checks).

[Results](validation.md#b-legacy-seam-inventory-and-static-guard--results).

### C. Stale Example Source And README Retirement

Required work:

- Remove or replace stale, uncompiled MultiView legacy files such as
  `Examples/MultiView/ImGuiView.cpp` / `.h` if they are no longer used.
- Correct `Examples/Async/README.md` so it describes the current Vortex proof
  path and no longer presents legacy renderer baseline capture as a current
  production workflow.
- Scan required example READMEs for stale `Oxygen.Renderer` or legacy capture
  wording.

[Checks](validation.md#c-stale-example-source-and-readme-retirement--checks).

[Results](validation.md#c-stale-example-source-and-readme-retirement--results).

### D. Required Example Build And Runtime Smoke Matrix

Required work:

- Define the required example set and minimum runtime command for each using
  an explicit matrix. The matrix must include target/module, graphics vs
  non-graphics scope, recent proof citation or fresh smoke requirement,
  runtime command, and skip rationale.
- Refresh/upgrade/fix required demos that no longer match the current Vortex
  runtime contracts, UI expectations, or validation controls.
- Build the example matrix through Vortex.
- Run every required demo that lacks recent accepted milestone proof. Use short
  deterministic frame-count runs when the demo supports it, and document any
  demo that still needs a deterministic smoke CLI added before it can close.
- For demos with recent proof, record the cited proof and rerun only if the
  demo is touched, seam validation flags it, or the proof no longer covers the
  production-readiness claim.
- Run CDB/debug-layer smoke for every graphics demo path that creates a D3D12
  device or exercises Vortex rendering.
- Keep runtime visual proof requirements tied to existing M04-M06 proof tools
  instead of inventing duplicate tooling.

Candidate build targets:

- `oxygen-examples-async`
- `oxygen-examples-inputsystem`
- `oxygen-examples-renderscene`
- `oxygen-examples-multiview`
- `oxygen-examples-vortexbasic`
- `oxygen-examples-texturedcube`
- `oxygen-examples-physics`
- `oxygen-examples-lightbench`
- `oxygen-examples-demoshell`
- Platform, Devices, and OxCo batch examples are registered under
  `Examples/CMakeLists.txt`, but they are not Vortex renderer demos. M07 must
  still classify them in the matrix, either as non-graphics smoke scope or as
  outside Vortex production-readiness scope with a concrete reason.

[Checks](validation.md#d-required-example-build-and-runtime-smoke-matrix--checks).

[Results](validation.md#d-required-example-build-and-runtime-smoke-matrix--results).

### E. Production Proof Suite Consolidation

Required work:

- Create a single M07 wrapper or documented command list that reuses validated
  proof wrappers instead of duplicating them.
- Include the closure-critical proof surfaces:
  VortexBasic environment/fog, Async, diagnostics, occlusion, translucency,
  conventional shadows, MultiView, offscreen, and feature variants.
- Record which proofs are mandatory for every M07 closure run and which are
  conditional on touched subsystems.
- Retire or quarantine legacy/reference proof flows. In particular, the Async
  proof must no longer require `Capture-AsyncLegacyReference.ps1` as a current
  production proof dependency.

[Checks](validation.md#e-production-proof-suite-consolidation--checks).

[Results](validation.md#e-production-proof-suite-consolidation--results).

### F. Build Graph And Binary Dependency Audit

Required work:

- Prove `oxygen::vortex` and required example binaries have no direct or
  transitive legacy renderer target dependency.
- If a binary dependency inspection tool is used, record the exact command and
  output.
- Add a repeatable local script if the check is not already covered by CMake
  metadata tests.

[Checks](validation.md#f-build-graph-and-binary-dependency-audit--checks).

[Results](validation.md#f-build-graph-and-binary-dependency-audit--results).

### G. Status Reconciliation And Legacy Archive Routing

Required work:

- Reconcile VTX-M01-M03 ledger wording where later validated milestones now
  provide production-readiness evidence for concrete surfaces.
- Mark historical non-Vortex renderer design docs as archival or out of the
  Vortex implementation path where needed.
- Scan and update current-path docs under `src/Oxygen`, `Examples`, and
  `tools` when they route readers to removed `src/Oxygen/Renderer` paths or
  present legacy renderer proof as current workflow.
- Keep post-baseline families in `future` unless there is a concrete approved
  implementation plan.

[Checks](validation.md#g-status-reconciliation-and-legacy-archive-routing--checks).

[Results](validation.md#g-status-reconciliation-and-legacy-archive-routing--results).

### H. Closure

Required work:

- Run the full M07 build/test/static/runtime proof suite.
- Update `PLAN.md`, `milestone README`, and this plan with exact
  evidence.
- Record residual gaps as blockers or accepted/deferred items.

[Checks](validation.md#h-closure--checks).

[Results](validation.md#h-closure--results).

## 11. Exit Gate

VTX-M07 cannot be marked `validated` until:

1. Production-readiness implementation exists.
2. Required docs/status files are current.
3. Static legacy seam validation passes.
4. Required example build matrix passes.
5. Focused Vortex tests pass.
6. Required runtime CDB/debug-layer audits pass.
7. Required RenderDoc/scripted proof wrappers pass.
8. ShaderBake/catalog validation is recorded if shader/ABI changed.
9. Residual legacy/archive gaps are recorded and accepted, or there are no
   residual gaps.

## 12. Replan Triggers

Revisit the design if:

- A required example still depends on a legacy renderer type or target and
  cannot be trivially migrated.
- Legacy source is found to be absent but docs/tooling still depend on it as a
  reference baseline.
- A static seam check would incorrectly reject allowed `oxygen::imgui` usage.
- A production-readiness smoke exposes a rendering regression in a validated
  M04-M06 surface.
- Removing a stale file would delete user-owned local validation state or an
  untracked workflow artifact.

## Supporting records

- [migration](migration.md)

## Validation

See the [validation record](validation.md) for commands, conditions, results and remaining work.
