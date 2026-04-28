# VTX-M07 - Production Readiness And Legacy Retirement

Status: `in_progress`

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
- Reconcile `PLAN.md`, `IMPLEMENTATION_STATUS.md`, and this plan with exact
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

## 4. Current State

Validated prerequisites:

- VTX-M04D environment/fog parity, VTX-M05A diagnostics, VTX-M05B occlusion,
  VTX-M05C translucency, VTX-M05D conventional directional/spot/point shadows,
  VTX-M06A multi-view, VTX-M06B offscreen, and VTX-M06C feature-gated runtime
  variants are recorded as `validated`.
- Required example targets currently registered under `Examples/CMakeLists.txt`
  include Async, InputSystem, LightBench, TexturedCube, DemoShell,
  RenderScene, MultiView, Physics, and VortexBasic. The examples tree also
  registers Platform, Devices, and OxCo batch examples; M07 must classify them
  as non-Vortex/non-graphics smoke scope or include them in the broader example
  health matrix explicitly.
- Live example entry points already include `<Oxygen/Vortex/Renderer.h>` for
  the inspected examples; no source directory named `src/Oxygen/Renderer`
  exists in the current source tree.
- `src/Oxygen/Vortex/Test/Link_test.cpp` constructs `oxygen::vortex::Renderer`
  with an empty capability set and exercises frame lifecycle hooks.
- `src/Oxygen/Vortex/CMakeLists.txt` links `oxygen::imgui` privately. This is
  allowed UI infrastructure, not a forbidden legacy renderer seam.

Known retirement seams discovered during the planning inventory:

- `Examples/MultiView/ImGuiView.cpp` includes
  `<Oxygen/Renderer/Renderer.h>` and refers to old `engine::Renderer` /
  `engine::RenderContext` registration APIs. The file is stale: it is not in
  `Examples/MultiView/CMakeLists.txt`, and it references missing local
  headers such as `MultiView/DemoView.h` and `MultiView/OffscreenCompositor.h`.
- Demo/example code still carries compatibility namespace seams that look like
  legacy renderer API even when they alias Vortex types:
  `Examples/DemoShell/Runtime/RendererUiTypes.h` defines
  `oxygen::renderer::RenderMode`,
  `Examples/DemoShell/Services/PostProcessSettingsService.h` references
  `renderer::CompositionView` and `renderer::RenderingPipeline`,
  `Examples/TexturedCube/MainModule.h` forward-declares
  `oxygen::renderer::CompositionView`, and
  `Examples/Physics/MainModule.h` forward-declares the same legacy namespace.
  M07 must remove these or carry a temporary allowlist with an explicit
  removal gate.
- `Examples/Async/README.md` still describes legacy/reference baseline
  capture and legacy renderer target linkage as current Phase 4 language. The
  code path is Vortex-migrated, so the README needs current production
  wording.
- Async proof tooling still exposes reference/legacy baseline language through
  `tools/vortex/Capture-AsyncLegacyReference.ps1`,
  `tools/vortex/Run-AsyncRuntimeValidation.ps1`, and `tools/vortex/README.md`.
  M07 must retire, rename, quarantine, or reword that flow so closure cannot
  depend on legacy/reference capture as a current proof path.
- The required demo set may have accumulated runtime drift even when it still
  compiles. Demos without recent milestone proof must be inspected, refreshed,
  fixed where needed, and tested through an appropriate runtime path before
  M07 closure. Recent proof can be reused for heavily exercised demos when the
  code path is unchanged and the evidence is cited explicitly.
- Historical design packages outside `design/vortex`, such as
  `design/renderer-*`, still mention `Oxygen.Renderer`. Current docs outside
  that family also route readers to removed `src/Oxygen/Renderer` paths, for
  example `src/Oxygen/ARCHITECTURE.md` and
  `src/Oxygen/Content/Docs/overview.md`. M07 must classify historical docs as
  archive/history and route current docs/readers to Vortex plans so stale paths
  are not mistaken for implementation instructions.
- Older VTX-M01-M03 ledger rows still use `landed_needs_validation` even
  though later milestones validated many concrete surfaces. M07 should
  reconcile those rows only where current production-readiness evidence
  supports a stronger claim.

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

| Contract | Valid state | Invalid/stale state | Proof surface |
| --- | --- | --- | --- |
| Source include seam | Vortex/example code includes Vortex, Graphics, Engine, ImGui, or other approved modules. | Any current Vortex/example source includes `<Oxygen/Renderer/...>` or uses legacy renderer namespaces/types. | Static seam scan and focused builds. |
| Build graph seam | Required examples and `oxygen::vortex` link without `oxygen-renderer`. | A Vortex or required-example target links legacy renderer directly or transitively. | CMake target scan plus binary dependency inspection where available. |
| Runtime path | Required examples instantiate/run Vortex renderer paths. | Runtime bootstraps legacy renderer, fallback renderer, or bridge/adaptor. | Runtime logs, CDB audits, proof scripts. |
| Documentation routing | Current docs point implementation work to Vortex plans/status. Historical docs are marked archive or scoped. | Stale docs present legacy renderer as current production/reference path. | README/status/doc grep and source-to-target coverage check for edited docs. |
| UI dependency | `oxygen::imgui` usage remains explicitly allowed for panels/overlays. | Validation treats ImGui as legacy renderer or blocks Vortex UI without an actual renderer seam. | Link test/validation rule review. |
| Status ledger | `PLAN.md` and `IMPLEMENTATION_STATUS.md` name real next work and evidence. | Rows claim closure without proof or leave stale active milestone names. | `git diff --check`, status consistency scan. |

## 8. Implementation Slices

### A. Plan And Status Truth

Required work:

- Create this detailed M07 plan.
- Add the plan to `design/vortex/plan/README.md`.
- Update `PLAN.md` and `IMPLEMENTATION_STATUS.md` so M07 is `in_progress`
  and clearly not yet implemented.

Validation:

- `git diff --check`
- Status consistency scan for M07 and stale active-work references.

Evidence:

- Planner subagent review completed before execution. The review flagged an
  incomplete demo matrix, missing active `oxygen::renderer` namespace seams,
  Async legacy/reference proof tooling, too-narrow current-doc routing scope,
  and a VTX-M03 prerequisite overclaim in `PLAN.md`.
- This plan was updated to include those findings before implementation work
  began.
- `git diff --check` passed for the planning/status patch.

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

Validation:

- Static seam script on `src/Oxygen/Vortex` and required examples.
- `cmake --build out\build-ninja --config Debug --target Oxygen.Vortex.LinkTest --parallel 4`
- `ctest --preset test-debug -R "Oxygen\.Vortex\.LinkTest" --output-on-failure`

Evidence:

- `tools/vortex/Assert-VortexLegacySeams.ps1` now scans current Vortex and
  required-example source/build files for legacy renderer includes,
  namespaces, qualified symbols, and target seams while leaving
  `oxygen::imgui` allowed.
- `powershell -NoProfile -ExecutionPolicy Bypass -File
  tools\vortex\Assert-VortexLegacySeams.ps1 -ReportPath
  out\build-ninja\analysis\vortex\m07-legacy-seams.txt` passed, scanning 547
  current source/build files with no forbidden seams.
- `cmake --build out\build-ninja --config Debug --target
  Oxygen.Vortex.LinkTest oxygen-examples-demoshell
  oxygen-examples-multiview oxygen-examples-texturedcube
  oxygen-examples-physics --parallel 4` passed after the source cleanup.
- `ctest --preset test-debug -R
  "Oxygen\.Vortex\.LinkTest|Oxygen\.Examples\.DemoShell\.RenderingSettingsService\.Tests|Oxygen\.Examples\.DemoShell\.DiagnosticsPanel\.Tests"
  --output-on-failure` passed with 3/3 test executables.

### C. Stale Example Source And README Retirement

Required work:

- Remove or replace stale, uncompiled MultiView legacy files such as
  `Examples/MultiView/ImGuiView.cpp` / `.h` if they are no longer used.
- Correct `Examples/Async/README.md` so it describes the current Vortex proof
  path and no longer presents legacy renderer baseline capture as a current
  production workflow.
- Scan required example READMEs for stale `Oxygen.Renderer` or legacy capture
  wording.

Validation:

- Static seam script from Slice B.
- Focused example builds for touched examples.
- `git diff --check`

Evidence so far:

- Stale uncompiled `Examples/MultiView/ImGuiView.cpp` and
  `Examples/MultiView/ImGuiView.h` were removed. They were not part of
  `Examples/MultiView/CMakeLists.txt` and referenced old renderer APIs and
  missing local headers.
- Active DemoShell/TexturedCube/Physics compatibility seams using
  `oxygen::renderer` or `renderer::` were removed from current source.
- The Slice B seam guard and focused build/test gates above passed after this
  cleanup.
- This slice is not closed yet: `Examples/Async/README.md` and Async
  proof-tool wording still need current Vortex production-proof language.

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

Validation:

- Focused build matrix.
- Runtime smoke command for every required demo that lacks recent accepted
  milestone proof, or a blocker entry explaining the missing deterministic
  smoke hook that must be added.
- CDB/debug-layer audits for graphics examples that run frame loops.

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

Validation:

- Wrapper dry run or full run depending on scope.
- Parser/static checks for any new tooling.

### F. Build Graph And Binary Dependency Audit

Required work:

- Prove `oxygen::vortex` and required example binaries have no direct or
  transitive legacy renderer target dependency.
- If a binary dependency inspection tool is used, record the exact command and
  output.
- Add a repeatable local script if the check is not already covered by CMake
  metadata tests.

Validation:

- CMake target graph inspection.
- Binary dependency inspection for relevant built DLLs/executables.

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

Validation:

- Documentation grep for current-path `Oxygen.Renderer`,
  `src/Oxygen/Renderer`, and legacy/reference proof claims.
- `git diff --check`.

### H. Closure

Required work:

- Run the full M07 build/test/static/runtime proof suite.
- Update `PLAN.md`, `IMPLEMENTATION_STATUS.md`, and this plan with exact
  evidence.
- Record residual gaps as blockers or accepted/deferred items.

Validation:

- Full focused build matrix.
- Full focused CTest set.
- Static seam script.
- Required runtime/CDB/RenderDoc proof wrappers.
- `git diff --check`.

## 9. Test Plan

Initial planning/status slice:

```powershell
git diff --check
```

Expected focused test/build gates as slices land:

```powershell
cmake --build out\build-ninja --config Debug --target Oxygen.Vortex.LinkTest oxygen-vortex oxygen-examples-async oxygen-examples-inputsystem oxygen-examples-renderscene oxygen-examples-multiview oxygen-examples-vortexbasic oxygen-examples-texturedcube oxygen-examples-physics oxygen-examples-lightbench oxygen-examples-demoshell --parallel 4
ctest --preset test-debug -R "Oxygen\.Vortex\.(LinkTest|RendererCapability|RendererCompositionQueue|OffscreenSceneFacade|SceneRendererDeferredCore|SceneRendererPublication|EnvironmentLightingService|ShadowService|DiagnosticsService|OcclusionModule)" --output-on-failure
```

Run ShaderBake/catalog validation only if shader source, shader ABI,
root-binding, or catalog data changes.

## 10. Runtime / Capture Proof

M07 closure must reuse existing proof wrappers for validated runtime surfaces
unless a slice deliberately replaces or consolidates them:

- VortexBasic environment/fog runtime proof.
- Async runtime proof.
- Diagnostics runtime proof.
- Occlusion runtime proof.
- Translucency runtime proof.
- Conventional shadow proof for affected shadow families.
- MultiView proof.
- Offscreen proof.
- Feature-variant proof.

Runtime proof must include CDB/debug-layer coverage for graphics paths touched
by M07, and RenderDoc scripted analysis when GPU pass/product behavior is part
of the claim.

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

Replan before continuing if:

- A required example still depends on a legacy renderer type or target and
  cannot be trivially migrated.
- Legacy source is found to be absent but docs/tooling still depend on it as a
  reference baseline.
- A static seam check would incorrectly reject allowed `oxygen::imgui` usage.
- A production-readiness smoke exposes a rendering regression in a validated
  M04-M06 surface.
- Removing a stale file would delete user-owned local validation state or an
  untracked workflow artifact.

## 13. Status Update Requirements

- Update this plan slice evidence only after its validation command passes.
- Update `IMPLEMENTATION_STATUS.md` when a slice has implementation plus
  validation evidence.
- Keep VTX-M07 `in_progress` until the full exit gate is satisfied.
- Do not mark M07 `validated` based on planning docs, inventory, or static
  scans alone.
