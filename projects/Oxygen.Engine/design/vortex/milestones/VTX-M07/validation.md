# VTX-M07 — validation

This record preserves the conditions and results at each implementation checkpoint.
The milestone README owns the final outcome and remaining work.

## Validation and remaining work

**Qualification:** `validated`

Detailed plan `design/vortex/milestones/VTX-M07/README.md` exists and is updated with closure evidence. Static source seam guard `tools/vortex/Assert-VortexLegacySeams.ps1` passed with tooling at `out\build-ninja\analysis\vortex\m07-closeout\legacy-seams-tooling.txt`, scanning 581 current source/build/tooling files with no forbidden legacy renderer seams. Stale uncompiled MultiView legacy source (`Examples/MultiView/ImGuiView.*`) was removed. Active DemoShell/TexturedCube/Physics compatibility seams under `oxygen::renderer` / `renderer::` were removed. Async README/tooling now executes current Vortex proof without `Capture-AsyncLegacyReference.ps1` or `ReferenceRoot`. Required demo refresh fixed DemoShell scene-authored environment behavior, fixed exposure seeding, TexturedCube/InputSystem/Physics sun/environment setup, no-texture-sampling solid materials, and the Physics +Z-up floor; Physics direct-light RenderDoc probe `out\build-ninja\analysis\physics\physics-after-scene-cleanup-direct-lighting-probe.txt` passed with nonzero floor direct lighting proof. Full registered example build matrix passed. Short D3D12/debug-layer smokes passed for Async, InputSystem, LightBench, TexturedCube, Physics, RenderScene, VortexBasic, and MultiView; Devices, Platform, and OxCo examples are classified with build/help/smoke evidence. Closure build passed `Oxygen.Vortex.LinkTest`, `oxygen-vortex`, and all required Vortex example targets. Focused CTest passed 14/14 after explicitly rebuilding a stale `Oxygen.Vortex.OcclusionModule.Tests` binary. Current-path doc/source audit `out\build-ninja\analysis\vortex\m07-closeout\current-source-legacy-doc-audit.txt` passed with zero matches under `src/Oxygen`, `Examples`, and `tools/vortex`. Binary dependency audit `out\build-ninja\analysis\vortex\m07-closeout\binary-dependencies.txt` passed with zero legacy renderer dependency matches. VortexBasic, Async, MultiView standard/auxiliary, Offscreen, and Feature-variant runtime proof wrappers passed under `out\build-ninja\analysis\vortex\m07-closeout` with CDB/debug-layer and RenderDoc/scripted-analysis reports. ShaderBake/catalog validation was not run because no shader source, shader ABI, root-binding, or catalog data changed. `oxygen::imgui` remains allowed UI infrastructure and not a legacy renderer seam.

**Remaining work:** No open VTX-M07 closure gap. VTX-M08 is validated as the first post-baseline family.

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
- M07 identified that older VTX-M01-M03 ledger rows needed fresh
  milestone-level proof before stronger status claims. Post-M08 closeout
  resolved that validation debt with focused build/test, shader-catalog, CDB,
  and RenderDoc evidence recorded in `milestone README`.

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

## A. Plan And Status Truth — checks

- `git diff --check`
- Status consistency scan for M07 and stale active-work references.

## A. Plan And Status Truth — results

- Planner subagent review completed before execution. The review flagged an
  incomplete demo matrix, missing active `oxygen::renderer` namespace seams,
  Async legacy/reference proof tooling, too-narrow current-doc routing scope,
  and a VTX-M03 prerequisite overclaim in `PLAN.md`.
- This plan was updated to include those findings before implementation work
  began.
- `git diff --check` passed for the planning/status patch.

## B. Legacy Seam Inventory And Static Guard — checks

- Static seam script on `src/Oxygen/Vortex` and required examples.
- `cmake --build out\build-ninja --config Debug --target Oxygen.Vortex.LinkTest --parallel 4`
- `ctest --preset test-debug -R "Oxygen\.Vortex\.LinkTest" --output-on-failure`

## B. Legacy Seam Inventory And Static Guard — results

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

## C. Stale Example Source And README Retirement — checks

- Static seam script from Slice B.
- Focused example builds for touched examples.
- `git diff --check`

## C. Stale Example Source And README Retirement — results

- Stale uncompiled `Examples/MultiView/ImGuiView.cpp` and
  `Examples/MultiView/ImGuiView.h` were removed. They were not part of
  `Examples/MultiView/CMakeLists.txt` and referenced old renderer APIs and
  missing local headers.
- Active DemoShell/TexturedCube/Physics compatibility seams using
  `oxygen::renderer` or `renderer::` were removed from current source.
- The Slice B seam guard and focused build/test gates above passed after this
  cleanup.
- `Examples/Async/README.md` now describes the current Vortex runtime proof
  path and no longer instructs a legacy/reference capture flow.
- Async proof tooling now validates current Vortex capture/products directly:
  `tools/vortex/Capture-AsyncLegacyReference.ps1` was removed,
  `Run-AsyncRuntimeValidation.ps1` no longer accepts a reference root, and
  `Verify-AsyncRuntimeProof.ps1` / `Assert-AsyncRuntimeProof.ps1` now gate the
  current capture reports, exported color/depth products, and Stage 22
  input-bundle contract.
- `tools/vortex/README.md` now documents the current Vortex Async proof flow
  instead of reference-based parity.
- `powershell -NoProfile -Command
"$null = [scriptblock]::Create((Get-Content -Raw
'tools\vortex\Run-AsyncRuntimeValidation.ps1')); $null =
[scriptblock]::Create((Get-Content -Raw
'tools\vortex\Verify-AsyncRuntimeProof.ps1')); $null =
[scriptblock]::Create((Get-Content -Raw
'tools\vortex\Assert-AsyncRuntimeProof.ps1'))"` passed PowerShell parse for
  the edited Async proof scripts.
- `powershell -NoProfile -ExecutionPolicy Bypass -File
tools\vortex\Assert-VortexLegacySeams.ps1 -IncludeTooling -ReportPath
out\build-ninja\analysis\vortex\m07-legacy-seams-tooling.txt` passed,
  scanning 577 source/build/tooling files with no forbidden legacy renderer
  seams.
- `rg -n
"Oxygen\.Renderer|Oxygen/Renderer|legacy/reference|Capture-AsyncLegacyReference|ReferenceRoot|reference baseline|Reference-Based"
Examples tools\vortex -g README.md -g "*.ps1" -g
"!Assert-VortexLegacySeams.ps1" -S` found no stale current README/tooling
  proof references.
- `cmake --build out\build-ninja --config Debug --target
oxygen-examples-async --parallel 4` passed.
- `ctest --preset test-debug -R
"Oxygen\.Examples\.DemoShell\.AsyncVortexMigrationSurface\.Tests"
--output-on-failure` passed, but the executable currently contains 0 tests;
  this is not a closure blocker because the current Async runtime proof wrapper
  now gates the real capture/product/runtime behavior.

## D. Required Example Build And Runtime Smoke Matrix — checks

- Focused build matrix.
- Runtime smoke command for every required demo that lacks recent accepted
  milestone proof, or a blocker entry explaining the missing deterministic
  smoke hook that must be added.
- CDB/debug-layer audits for graphics examples that run frame loops.

## D. Required Example Build And Runtime Smoke Matrix — results

- Full registered example build matrix passed:
  `cmake --build out\build-ninja --config Debug --target
oxygen-platform-example oxygen-graphics-devicemanager-example
oxygen-examples-async oxygen-examples-inputsystem
oxygen-examples-lightbench oxygen-examples-texturedcube
oxygen-examples-demoshell oxygen-examples-renderscene
oxygen-examples-multiview oxygen-examples-physics
oxygen-examples-vortexbasic
oxygen-oxco-examples-batchexecution-yieldawaiter
oxygen-oxco-examples-batchexecution-broadcastchannel
oxygen-oxco-examples-batchexecution-repeatableshared --parallel 4`.
- Short D3D12/debug-layer smoke passed for Async, InputSystem, LightBench,
  TexturedCube, Physics, RenderScene, VortexBasic, and MultiView. Each run
  used `--frames 8 --fps 30 --vsync false --debug-layer --capture-provider
off`, exited `0`, and logged `d3d12_errors=0`, `dxgi_errors=0`, and
  `blocking=0` in
  `out\build-ninja\analysis\vortex\m07-demo-smoke\summary.txt`.
- OxCo batch examples initially exposed an invalid `void MainImpl` entry-point
  signature against the shared `int MainImpl(...)` launcher contract. The
  three examples now return `EXIT_SUCCESS`, rebuilt, and smoke-ran with
  `exit=0` and `blocking=0`.
- Devices example built and smoke-ran with `exit=0` and `blocking=0`; it is a
  non-Vortex D3D12 device-removal example, so D3D12 removal diagnostics are
  not interpreted as Vortex renderer proof.
- Platform example built and `--help` exited `0`. It remains an interactive
  platform/window sample with no finite-frame runtime CLI, so the M07 matrix
  records help/build proof only and a residual deterministic-smoke-hook gap if
  future baseline policy requires automated platform runtime closure.
- Demo refresh/fix closeout also covered scene-authored environment behavior
  and material/runtime correctness in required examples: DemoShell now preserves
  scene-authored environment controls and switches to custom mode when users
  edit sun/environment controls; fixed exposure seeds post-process exposure
  state; TexturedCube, InputSystem, and Physics scenes now author clear
  sun/environment defaults; generated solid-color demo materials are marked
  non-texture-sampling; the Physics floor uses correct Oxygen +Z-up placement
  and a non-zero above-atmosphere visual/collision plane.
- Physics direct-light proof passed with RenderDoc analysis:
  `tools/vortex/ProbeRenderDocPhysicsDirectLighting.py
out\build-ninja\analysis\physics\physics-after-scene-cleanup_capture.rdc`
  wrote
  `out\build-ninja\analysis\physics\physics-after-scene-cleanup-direct-lighting-probe.txt`
  with `analysis_result=success`, `floor_candidate_count=4`,
  `nonzero_direct_floor_count=3`, lit floor `NoL=0.690735274`, and direct
  output values around `8680..9160`. The one zero-direct floor sample was
  shadowed, not missing lighting.

| Example target                          | Scope                                         | M07 proof status                                                                                                     |
| --------------------------------------- | --------------------------------------------- | -------------------------------------------------------------------------------------------------------------------- |
| `oxygen-examples-async`                 | Vortex graphics runtime                       | Built; fresh 8-frame D3D12/debug-layer smoke passed; current RenderDoc Async proof wrapper retained for closure.     |
| `oxygen-examples-inputsystem`           | Vortex graphics runtime                       | Built; fresh 8-frame D3D12/debug-layer smoke passed.                                                                 |
| `oxygen-examples-lightbench`            | Vortex graphics runtime                       | Built; fresh 8-frame D3D12/debug-layer smoke passed.                                                                 |
| `oxygen-examples-texturedcube`          | Vortex graphics runtime                       | Built; fresh 8-frame D3D12/debug-layer smoke passed.                                                                 |
| `oxygen-examples-demoshell`             | Shared example UI/library                     | Built as dependency and target; runtime covered by dependent demos and focused DemoShell tests.                      |
| `oxygen-examples-renderscene`           | Vortex graphics runtime                       | Built; fresh 8-frame D3D12/debug-layer smoke passed.                                                                 |
| `oxygen-examples-multiview`             | Vortex graphics runtime                       | Built; fresh 8-frame D3D12/debug-layer smoke passed; deeper M06A/M06B/M06C proof remains the closure reference.      |
| `oxygen-examples-physics`               | Vortex graphics runtime plus physics          | Built; fresh 8-frame D3D12/debug-layer smoke passed.                                                                 |
| `oxygen-examples-vortexbasic`           | Vortex graphics runtime                       | Built; fresh 8-frame D3D12/debug-layer smoke passed; deeper M04-M06 proof remains the closure reference.             |
| `oxygen-platform-example`               | Non-Vortex interactive platform/window sample | Built; `--help` exited `0`; no finite-frame runtime CLI exists.                                                      |
| `oxygen-graphics-devicemanager-example` | Non-Vortex D3D12 device-management sample     | Built; smoke exited `0`; classified outside Vortex renderer proof because it intentionally exercises device removal. |
| `oxygen-oxco-examples-batchexecution-*` | Non-Vortex coroutine samples                  | Built; entry-point return contract fixed; all three smoke runs exited `0`.                                           |

## E. Production Proof Suite Consolidation — checks

- Wrapper dry run or full run depending on scope.
- Parser/static checks for any new tooling.

## E. Production Proof Suite Consolidation — results

- Closure reused existing proof wrappers rather than introducing duplicate
  runtime analyzers.
- VortexBasic runtime proof passed:
  `powershell -NoProfile -ExecutionPolicy Bypass -File
tools\vortex\Run-VortexBasicRuntimeValidation.ps1 -Output
out\build-ninja\analysis\vortex\m07-closeout\vortexbasic-runtime -Frame 3
-RunFrames 6 -Fps 30 -BuildJobs 4`.
- Async runtime proof passed:
  `powershell -NoProfile -ExecutionPolicy Bypass -File
tools\vortex\Run-AsyncRuntimeValidation.ps1 -Output
out\build-ninja\analysis\vortex\m07-closeout\async-runtime -Frame 90
-RunFrames 94 -Fps 30 -BuildJobs 4`.
- MultiView standard and auxiliary proof passed:
  `tools\vortex\Run-VortexMultiViewValidation.ps1 -Output
out\build-ninja\analysis\vortex\m07-closeout\multiview-proof -Frame 5
-RunFrames 65 -Fps 30 -BuildJobs 4` and the same command with
  `-AuxProofLayout -Output
out\build-ninja\analysis\vortex\m07-closeout\multiview-aux-proof`.
- Offscreen proof passed:
  `tools\vortex\Run-VortexOffscreenValidation.ps1 -Output
out\build-ninja\analysis\vortex\m07-closeout\offscreen-proof -Frame 5
-RunFrames 65 -Fps 30 -BuildJobs 4`.
- Feature-variant proof passed:
  `tools\vortex\Run-VortexFeatureVariantValidation.ps1 -Output
out\build-ninja\analysis\vortex\m07-closeout\feature-variant-proof -Frame 5
-RunFrames 65 -Fps 30 -BuildJobs 4`.
- The generated reports under `out\build-ninja\analysis\vortex\m07-closeout`
  include CDB/debug-layer, RenderDoc/scripted analysis, assertion, and
  allocation-churn evidence for the relevant wrappers.

## F. Build Graph And Binary Dependency Audit — checks

- CMake target graph inspection.
- Binary dependency inspection for relevant built DLLs/executables.

## F. Build Graph And Binary Dependency Audit — results

- Current source/build/tooling seam guard passed:
  `powershell -NoProfile -ExecutionPolicy Bypass -File
tools\vortex\Assert-VortexLegacySeams.ps1 -IncludeTooling -ReportPath
out\build-ninja\analysis\vortex\m07-closeout\legacy-seams-tooling.txt`,
  scanning 581 files with no forbidden current-source legacy renderer seams.
- Binary dependency audit passed:
  `out\build-ninja\analysis\vortex\m07-closeout\binary-dependencies.txt`
  records `result=PASS` and `blocking_legacy_dependency_matches=0` for
  `Oxygen.Vortex-d.dll`, required Vortex example executables, and matching
  Vortex/DemoShell test binaries inspected with `dumpbin /dependents`.

## G. Status Reconciliation And Legacy Archive Routing — checks

- Documentation grep for current-path `Oxygen.Renderer`,
  `src/Oxygen/Renderer`, and legacy/reference proof claims.
- `git diff --check`.

## G. Status Reconciliation And Legacy Archive Routing — results

- Current-path docs were routed away from removed legacy renderer material:
  `src/Oxygen/ARCHITECTURE.md`, `src/Oxygen/ImGui/README.md`,
  `src/Oxygen/Content/Docs/overview.md`,
  `src/Oxygen/Content/Docs/implementation_plan.md`,
  `src/Oxygen/Content/Docs/deps_and_cache.md`,
  `src/Oxygen/Content/Docs/chunking.md`,
  `src/Oxygen/Graphics/Direct3D12/Tools/ShaderBake/README.md`,
  `src/Oxygen/Scene/Light/light-deisgn.md`, and
  `src/Oxygen/Nexus/Docs/design.md`.
- Current-path legacy doc/source audit passed:
  `out\build-ninja\analysis\vortex\m07-closeout\current-source-legacy-doc-audit.txt`
  records `rg_exit=1`, `result=PASS`, and `matches=0` for
  `src/Oxygen`, `Examples`, and `tools/vortex`.
- Historical and Vortex planning docs may still discuss `Oxygen.Renderer` as
  legacy migration context or guardrail language; they are not current
  implementation routing.

## H. Closure — checks

- Full focused build matrix.
- Full focused CTest set.
- Static seam script.
- Required runtime/CDB/RenderDoc proof wrappers.
- `git diff --check`.

## H. Closure — results

- Full focused Vortex/example build passed:
  `cmake --build out\build-ninja --config Debug --target
Oxygen.Vortex.LinkTest oxygen-vortex oxygen-examples-async
oxygen-examples-inputsystem oxygen-examples-renderscene
oxygen-examples-multiview oxygen-examples-vortexbasic
oxygen-examples-texturedcube oxygen-examples-physics
oxygen-examples-lightbench oxygen-examples-demoshell --parallel 4`.
- Focused CTest passed after explicitly rebuilding the stale
  `Oxygen.Vortex.OcclusionModule.Tests` binary:
  `ctest --preset test-debug -R
"Oxygen\.Vortex\.(LinkTest|RendererCapability|RendererCompositionQueue|OffscreenSceneFacade|SceneRendererDeferredCore|SceneRendererPublication|EnvironmentLightingService|ShadowService|DiagnosticsService|OcclusionModule)|Oxygen\.Examples\.DemoShell\.(DemoShellPanelConfig|EnvironmentSettingsService|RenderingSettingsService|DiagnosticsPanel)\.Tests"
--output-on-failure` passed 14/14.
- Static seam guard, current-path doc/source audit, binary dependency audit,
  VortexBasic, Async, MultiView standard/auxiliary, Offscreen, and
  Feature-variant runtime proof wrappers passed with reports under
  `out\build-ninja\analysis\vortex\m07-closeout`.
- ShaderBake/catalog validation was not run because the M07 closure patch did
  not change shader source, shader ABI, root-binding, or shader catalog data.
- No open VTX-M07 closure gap remains. Accepted/deferred post-baseline gaps are
  listed in `VTX-FUTURE` and in `milestone README`.
