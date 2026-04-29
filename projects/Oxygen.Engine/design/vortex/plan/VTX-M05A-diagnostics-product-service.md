# VTX-M05A Diagnostics Product Service

**Status:** `validated`
**Milestone:** `VTX-M05A - Diagnostics Product Service`
**Scope owner:** Vortex runtime diagnostics
**Primary LLD:** [../lld/diagnostics-service.md](../lld/diagnostics-service.md)

## 1. Goal

Implement the compact diagnostics toolkit defined by the LLD: a Vortex
`DiagnosticsService`, shader debug-mode registry, frame ledger, GPU timeline facade,
minimal recoverable issue reporting, capture manifest export, and minimal
panel/tool handoff.

This milestone is not an editor tooling clone. It is the Swiss-army-knife layer
that lets us troubleshoot CPU/GPU pipeline problems without guessing.

## 2. Implementation Policy

- Preserve existing `GpuTimelineProfiler`, `ImGuiRuntime`, `ShaderDebugMode`,
  SceneRenderer debug visualization, and external proof tools.
- Add one runtime control plane instead of parallel debug stacks.
- Prefer stable facts and exports over large UI surfaces.
- Use layered diagnostics gating: compile core diagnostics with Vortex, keep
  optional third-party integrations behind narrow `OXYGEN_WITH_*` macros, let
  `NDEBUG` affect defaults/assertion verbosity only, and use runtime feature
  masks for behavior.
- Keep GPU debug primitives optional unless a required proof gap cannot be
  solved by the ledger, registry, timeline, and manifest.
- Every slice must update the single VTX-M05A row in
  `IMPLEMENTATION_STATUS.md` in place. Do not append per-commit evidence rows.
  Keep the row concise: files/areas changed, validation evidence, UE5.7
  references checked when relevant, and residual gaps.

## 3. Current State

| Area | Current state | M05A action |
| --- | --- | --- |
| GPU timeline | `Internal/GpuTimelineProfiler` exists with tests, sinks, latest-frame retention, and JSON/CSV export. | Wrap with service facade and correlate with pass ledger. |
| ImGui overlay | `Internal/ImGuiRuntime` exists. | Add service-owned panel registry; keep rendering backend unchanged. |
| Debug modes | `ShaderDebugMode.h` exists, but metadata is scattered. | Add authoritative `ShaderDebugModeRegistry`. |
| Deferred debug views | SceneRenderer has real debug view execution. | Keep execution there; move mode truth to the runtime registry. |
| GPU debug shaders | ABI and HLSL assets exist. | Keep asset-only unless optional slice is explicitly pulled in. |
| External tools | RenderDoc/CDB/analyzer wrappers exist. | Add capture manifest/runtime facts they can consume later, and harden M05A-owned or touched wrappers against false proof. |

## 4. UE5.7 References

Each implementation slice must re-check the relevant UE5.7 family:

- Debug view modes:
  `Renderer/Private/DebugViewModeRendering.cpp`,
  `Shaders/Private/DebugViewModePixelShader.usf`.
- GPU profiler events and breadcrumbs:
  `RHI/Public/GPUProfiler.h`, `RHI/Public/GpuProfilerTrace.h`,
  `RHI/Public/RHIBreadcrumbs.h`, plus renderer `RDG_EVENT_SCOPE` and
  `RDG_GPU_STAT_SCOPE` usage.
- ShaderPrint:
  `Renderer/Public/ShaderPrintParameters.h`,
  `Renderer/Private/ShaderPrint.cpp`, `Shaders/Private/ShaderPrint.ush`.
- VisualizeTexture and resource/debug naming:
  `Shaders/Private/Tools/VisualizeTexture.usf`,
  renderer visualize-texture call sites, and `RHIDefinitions.h` debug-name /
  hide-in-visualize-texture concepts.

## 5. Non-Goals

- No `Oxygen.Renderer` fallback.
- No UE Insights clone.
- No full editor showflag system.
- No generic VisualizeTexture clone in M05A.
- No RenderDoc or CDB orchestration inside runtime code.
- No new GPU debug primitive claim without runtime pass, ShaderBake/catalog,
  debug-layer, and visual/capture proof.

## 6. Implementation Slices

### Slice A - Architecture Hardening

**Status:** `validated`

Tasks:

- Update the LLD into the authoritative architecture for M05A.
- Update this detailed plan to match the hardened LLD.
- Update roadmap/status references and the single VTX-M05A status row.

Validation requirements:

- `rg` consistency scan for M05A, LLD status, and plan references.
- `git diff --check`.

### Slice B - DiagnosticsService Shell And Ownership Boundary

**Status:** `validated`

Current evidence:

- `DiagnosticsTypes` and `DiagnosticsService` exist with feature flags,
  enum/string helpers, capability clamping, shader-debug state, frame
  begin/end, pass/product/issue recording, snapshot publication, and disabled
  ledger no-op behavior.
- `Renderer` owns a `DiagnosticsService` instance and forwards
  `SetShaderDebugMode`/`GetShaderDebugMode` through it.
- Focused build and tests passed on 2026-04-26:
  `cmake --build out\build-ninja --config Debug --target Oxygen.Vortex.DiagnosticsService --parallel 4`;
  `ctest --preset test-debug -R "Oxygen\.Vortex\.(DiagnosticsService|GpuTimelineProfiler)" --output-on-failure`.

Remaining gap:

- No open Slice B gap.

Tasks:

- Add `src/Oxygen/Vortex/Diagnostics/DiagnosticsService.{h,cpp}`.
- Add `DiagnosticsTypes.h` with feature flags, severity, pass/product records,
  issues, and snapshots.
- Use `OXYGEN_FLAG` and `OXYGEN_DEFINE_FLAGS_OPERATORS` from
  `src/Oxygen/Base/Macros.h` for diagnostics feature flags. Keep the
  `std::uint32_t` underlying type only with the local clang-tidy enum-size
  suppression used by existing Vortex flag enums.
- Add namespace-local `to_string` overloads for every diagnostics enum. Logging
  call sites should pass enum values directly and rely on ADL instead of
  wrapping every value in `to_string(...)`.
- Add `DiagnosticsConfig` defaults:
  non-`NDEBUG` gets CPU-only ledger/issues/catalog by default; `NDEBUG` gets no
  continuously recording features unless config/CVar/CLI explicitly enables
  them.
- Clamp enabled features through `RendererCapabilityFamily::kDiagnosticsAndProfiling`.
  If the capability is absent, expose a disabled snapshot and reject feature
  enablement without scheduling diagnostics work.
- Wire service lifetime into `Renderer`.
- Preserve current `Renderer::SetShaderDebugMode` and
  `Renderer::GetShaderDebugMode` as forwarding APIs during migration.
- Add CMake and test target wiring.

Validation:

- Focused Vortex build.
- Unit tests for default feature state, enable/disable behavior, frame begin/end,
  snapshot immutability, and no-op disabled path.
- Unit tests for diagnostics flag operators and `to_string` coverage.
- Unit tests for non-`NDEBUG`/`NDEBUG` default policy where practical, or
  compile-time-specific tests documented separately if the test matrix cannot
  exercise both in one build.
- Unit tests proving capability gating clamps feature enablement.
- Release build/test proof passed on 2026-04-26:
  `cmake --build out\build-ninja --config Release --target Oxygen.Vortex.DiagnosticsService --parallel 4`;
  `ctest --preset test-release -R "Oxygen\.Vortex\.DiagnosticsService" --output-on-failure`
  with `DiagnosticsService` 1/1.

### Slice C - ShaderDebugModeRegistry

**Status:** `validated`

Current evidence:

- `ShaderDebugModeRegistry` exists with one entry per `ShaderDebugMode`,
  canonical tool names, display names, UI families, shader define linkage,
  debug path, capability/product requirements, and explicit support state.
- `DiagnosticsService` exposes registry enumeration and canonical-name
  resolution.
- VortexBasic CLI debug-mode parsing now consumes the registry while preserving
  the old `normal` alias for disabled mode.
- The registry documents `ibl-no-brdf-lut` as currently unsupported because the
  `SKIP_BRDF_LUT` shader variant exists but runtime mode selection is not wired.
- Post-validation cleanup removed the misleading UV0 and Opacity debug modes
  from `ShaderDebugMode`, `ShaderDebugModeRegistry`, `EngineShaderCatalog.h`,
  and `ForwardDebug_PS.hlsl` because the active deferred RenderScene path cannot
  render those values as real fullscreen debug products.
- Follow-up debug-mode correctness work marks IBL and light-culling modes
  unsupported with explicit reasons until real deferred products exist, moves
  direct-lighting debug modes to the deferred directional-light service pass,
  and makes Masked Alpha Coverage a deferred fullscreen view backed by GBuffer
  custom-data metadata written by the base pass.
- Focused build and tests passed on 2026-04-26:
  `cmake --build out\build-ninja --config Debug --target oxygen-examples-vortexbasic Oxygen.Vortex.DiagnosticsService Oxygen.Vortex.ShaderDebugModeRegistry --parallel 4`;
  `ctest --preset test-debug -R "Oxygen\.Vortex\.(ShaderDebugModeRegistry|DiagnosticsService)" --output-on-failure`.
- Direct catalog-introspection proof passed on 2026-04-26:
  `cmake --build out\build-ninja --config Debug --target Oxygen.Vortex.ShaderDebugModeRegistry --parallel 4`;
  `ctest --preset test-debug -R "Oxygen\.Vortex\.ShaderDebugModeRegistry" --output-on-failure`
  with `ShaderDebugModeRegistry` 9/9, including `EngineShaderCatalog.h`
  define/path-family checks.

Remaining gap:

- DemoShell Diagnostics and light-culling debug-mode UI now consume the
  registry. Runtime UI smoke remains tracked in Slice G.
- No open Slice C registry/catalog consistency gap.

Tasks:

- Add `ShaderDebugModeRegistry.{h,cpp}`.
- Create one registry entry for every `ShaderDebugMode` value.
- Record canonical name, display label, family, shader define, debug path,
  product requirements, and capability requirements.
- Keep existing helper functions mechanically consistent with the registry.
- Convert DemoShell mode lists and support checks to registry consumption where
  practical in this milestone.
- Keep this runtime registry separate from
  `src/Oxygen/Graphics/Direct3D12/Shaders/EngineShaderCatalog.h`, which remains
  the graphics-layer shader bake/permutation catalog.

Validation:

- Registry coverage test for every enum value.
- Duplicate canonical-name test.
- Helper consistency tests for string conversion, IBL family, light-culling
  family, forward-variant classification, and shader define mapping.
- Consistency tests for modes whose `shader_define` must exist in
  `EngineShaderCatalog.h`, without making the registry generate shader bake
  entries.
- DemoShell focused tests if UI/service mappings change.
- ShaderBake and RenderScene target validation when a registry change adds,
  removes, or retargets shader variants.

### Slice D - Frame Ledger And Minimal Runtime Issues

**Status:** `validated`

Current evidence:

- `DiagnosticsFrameLedger` now owns frame reset/snapshot publication,
  pass/product append rules, bounded recoverable issue context, per-frame issue
  deduplication, occurrence counts, and fail-fast validation for invalid
  pass/product/issue records.
- `DiagnosticsIssueCode` defines the minimal recoverable runtime issue
  vocabulary currently required by M05A: feature unavailable, manifest write
  failed, GPU timeline overflow/incomplete scope, unsupported debug mode,
  missing debug-mode product, and stale product.
- `DiagnosticsService` now delegates frame recording to the ledger while
  keeping feature gating and renderer capability clamping at the service
  boundary.
- `SceneRenderer::OnRender` records major pass/product facts at existing
  stage/publication boundaries: init views, lighting bindings, depth prepass,
  screen HZB, shadow bindings, environment bindings, base pass scene textures,
  deferred/debug lighting, volumetric/local fog, sky/atmosphere/fog, resolved
  scene color, post-process bindings, and ground grid.
- Focused build and tests passed on 2026-04-26:
  `cmake --build out\build-ninja --config Debug --target Oxygen.Vortex.DiagnosticsFrameLedger Oxygen.Vortex.DiagnosticsService --parallel 4`;
  `ctest --preset test-debug -R "Oxygen\.Vortex\.(DiagnosticsFrameLedger|DiagnosticsService)" --output-on-failure`
  with `DiagnosticsFrameLedger` 5/5 and `DiagnosticsService` 7/7.
- Additional focused build and tests passed after the writer hooks:
  `cmake --build out\build-ninja --config Debug --target Oxygen.Vortex.DiagnosticsService Oxygen.Vortex.SceneRendererPublication --parallel 4`;
  `ctest --preset test-debug -R "Oxygen\.Vortex\.(SceneRendererPublication|DiagnosticsService|DiagnosticsFrameLedger)" --output-on-failure`
  with `SceneRendererPublication` 17/17, `DiagnosticsService` 7/7, and
  `DiagnosticsFrameLedger` 5/5.

Tasks:

- Add `DiagnosticsFrameLedger.{h,cpp}`.
- Record frame sequence, active debug mode, enabled features, pass records,
  product records, and issues.
- Add only the minimal recoverable issue codes required by implemented
  behavior: feature unavailable, manifest write failed, timeline overflow or
  incomplete scope, unsupported debug mode, missing debug-mode product, and
  stale product.
- Treat contract breaches and invalid invariants as asserts/aborts, not as
  recoverable diagnostics issues.
- Add lightweight writer hooks where Vortex already has stage/product truth.
  Start with the major stages and products used by current proof tools.

Validation:

- Unit tests for append/reset/snapshot behavior.
- Unit tests for bounded issue context and deduplication.
- Tests proving recoverable missing debug-mode products produce issue records
  without crashing.
- Tests proving invalid contract examples still fail fast where such tests are
  practical in the existing assertion-test style.

### Slice E - GPU Timeline Facade

**Status:** `validated`

Current evidence:

- `DiagnosticsService` exposes the existing `Internal::GpuTimelineProfiler`
  through a service-owned facade for profiler attachment, requested/effective
  enablement, max scopes, latest-frame retention, sinks, one-shot export, and
  latest published frame access.
- `Renderer` wires its profiler instance into `DiagnosticsService` and syncs
  timeline diagnostics at frame start after the profiler publishes the previous
  resolved frame.
- Profiler overflow and incomplete-scope diagnostics are converted into the
  frame ledger's minimal recoverable issue vocabulary while retaining the
  profiler as the implementation owner.
- Focused build and tests passed on 2026-04-26:
  `cmake --build out\build-ninja --config Debug --target Oxygen.Vortex.DiagnosticsService Oxygen.Vortex.GpuTimelineProfiler.Tests Oxygen.Vortex.DiagnosticsFrameLedger --parallel 4`;
  `ctest --preset test-debug -R "Oxygen\.Vortex\.(DiagnosticsService|GpuTimelineProfiler|DiagnosticsFrameLedger)" --output-on-failure`
  with `DiagnosticsService` 8/8, `GpuTimelineProfiler` 8/8, and
  `DiagnosticsFrameLedger` 5/5.

Tasks:

- Keep `GpuTimelineProfiler` as implementation.
- Expose enablement, latest-frame access, sinks, and one-shot export through
  DiagnosticsService.
- Convert profiler diagnostics into frame-ledger issues.
- Correlate top-level pass records with GPU timeline scopes when names match.
- Surface Tracy build/availability state in diagnostics snapshots or panel data
  when it can be queried without coupling Vortex services to Tracy APIs.
- Keep pass code on Oxygen profiling scopes. Tracy integration remains owned by
  the profiling/backend layer behind `OXYGEN_WITH_TRACY`.

Validation:

- Existing `GpuTimelineProfiler` tests pass.
- Service facade tests for enablement, latest-frame access, export forwarding,
  overflow/incomplete-scope issue conversion, and disabled no-op behavior.
- Build/config evidence that DiagnosticsService compiles without Tracy and does
  not introduce direct Tracy dependencies into Vortex pass/domain services.

### Slice F - Capture Manifest Export

**Status:** `validated`

Current evidence:

- `DiagnosticsCaptureManifest` builds
  `vortex.diagnostics.capture-manifest.v1` JSON from a
  `DiagnosticsFrameSnapshot` and writes it to a caller-provided path.
- The runtime manifest records frame facts, active debug mode, requested and
  enabled feature masks, GPU timeline state, passes, products, issues, and an
  optional GPU timeline export path.
- Transient descriptor indices are deliberately omitted from product records in
  the manifest; products keep stable names, producer pass, resource name, and
  validity/published/stale state.
- `DiagnosticsService::ExportCaptureManifest` writes the latest snapshot and
  converts writer failures into the minimal `capture-manifest.write-failed`
  recoverable issue when a ledger frame is open.
- Focused build and tests passed on 2026-04-26:
  `cmake --build out\build-ninja --config Debug --target Oxygen.Vortex.DiagnosticsCaptureManifest Oxygen.Vortex.DiagnosticsService --parallel 4`;
  `ctest --preset test-debug -R "Oxygen\.Vortex\.(DiagnosticsCaptureManifest|DiagnosticsService)" --output-on-failure`
  with `DiagnosticsCaptureManifest` 2/2 and `DiagnosticsService` 8/8.

Tasks:

- Add `DiagnosticsCaptureManifest.{h,cpp}`.
- Export `vortex.diagnostics.capture-manifest.v1` JSON from the latest snapshot.
- Include frame, active debug mode, enabled features, passes, products, issues,
  and optional GPU timeline export path.
- Avoid raw pointers and transient descriptor indices in the manifest.
- Keep paths relative or caller-provided.

Validation:

- Unit tests for schema name/version.
- Unit tests for stable field names and omission of transient data.
- Round-trip parse test if the repo has a JSON parser available in Vortex tests.

### Slice G - DemoShell Diagnostics Panel Registry

**Status:** `validated`

Tasks:

- Replace the Vortex runtime-facing `RenderingPanel`/`RenderingVm` surface with
  `DiagnosticsPanel`/`DiagnosticsVm`; no migration alias is required.
- Reuse the existing `PanelRegistry` and `ImGuiRuntime` draw path instead of
  adding a second panel registry.
- Structure the compact built-in panel around runtime status, renderer
  capabilities, render-mode controls when available, and shader-debug mode
  selection. Directional-light shadow quality remains owned by the environment
  light settings, not by Diagnostics.
- Display selected versus active shader debug state so developers can
  distinguish the panel choice from renderer capability clamping without
  exposing service internals in the UI.
- Use `ShaderDebugModeRegistry` for visible mode names, grouping, support state,
  and disabled reasons in Diagnostics and light-culling debug UI.
- Keep UI state transient; persisted settings store requested values only.

Implementation evidence:

- `Examples/DemoShell/UI/DiagnosticsPanel.{h,cpp}` and
  `DiagnosticsVm.{h,cpp}` replace the old rendering panel/VM files.
- `DemoShellPanelConfig::diagnostics` replaces the former rendering panel
  enablement field in touched examples.
- `RenderingSettingsService` now persists requested shader-debug mode,
  computes an effective Vortex mode from `ShaderDebugModeRegistry` plus renderer
  capabilities, and applies the effective mode to the renderer.
- The Diagnostics panel reports Vortex binding, selected/active debug view, a
  read-only renderer capability checklist, and registry-grouped debug controls
  with disabled reasons.
- The Diagnostics panel exposes render-mode controls for Vortex runtime:
  solid, wireframe, wireframe overlay, and wireframe color. The settings
  service applies requested render mode and color to `vortex::Renderer`.
- `BasePassModule` implements wireframe-only rendering and exposes a late
  wireframe-overlay draw path. Wireframe-only clears SceneColor/SceneDepth,
  skips environment and ground-grid overlays, and uses neutral
  exposure/tonemapping policy. Overlay wireframe is executed after
  lighting/environment work so the solid path remains solid. Wire color is
  passed as an HDR `float4` constant buffer value; overlay mode applies explicit
  exposure compensation because it is recorded before post-process.
- The stale Diagnostics-panel directional shadow quality control and its
  `RenderingSettingsService` persistence key were removed; RenderScene no
  longer reads `rendering.shadow_quality_tier` at startup.
- `LightCullingDebugPanel` consumes the same registry for its visualization mode
  list.

Validation:

- Build passed:
  `cmake --build out\build-ninja --config Debug --target oxygen-examples-demoshell Oxygen.Examples.DemoShell.RenderingSettingsService.Tests --parallel 4`.
- Focused tests passed:
  `ctest --preset test-debug -R "Oxygen\.Examples\.DemoShell\.RenderingSettingsService" --output-on-failure`
  with RenderingSettingsService 6/6.
- Touched example binaries build and link:
  `cmake --build out\build-ninja --config Debug --target oxygen-examples-async oxygen-examples-lightbench oxygen-examples-physics oxygen-examples-renderscene oxygen-examples-texturedcube --parallel 4`.
- Focused panel draw smoke passed:
  `cmake --build out\build-ninja --config Debug --target Oxygen.Examples.DemoShell.DiagnosticsPanel.Tests Oxygen.Examples.DemoShell.RenderingSettingsService.Tests --parallel 4`;
  `ctest --preset test-debug -R "Oxygen\.Examples\.DemoShell\.(DiagnosticsPanel|RenderingSettingsService)" --output-on-failure`
  with `DiagnosticsPanel` 1/1 and `RenderingSettingsService` 6/6.
- Cleanup validation passed:
  `cmake --build out\build-ninja --config Debug --target Oxygen.Vortex.ShaderDebugModeRegistry Oxygen.Examples.DemoShell.DiagnosticsPanel.Tests Oxygen.Examples.DemoShell.RenderingSettingsService.Tests oxygen-examples-renderscene --parallel 4`;
  `ctest --preset test-debug -R "Oxygen\.(Vortex\.ShaderDebugModeRegistry|Examples\.DemoShell\.(DiagnosticsPanel|RenderingSettingsService))" --output-on-failure`
  with `ShaderDebugModeRegistry` 9/9, `DiagnosticsPanel` 1/1, and
  `RenderingSettingsService` 3/3. ShaderBake repacked `shaders.bin` and removed
  the eight stale UV0/Opacity debug artifacts.
- Debug-mode/render-mode correction validation passed:
  `cmake --build out\build-ninja --config Debug --target Oxygen.Vortex.ShaderDebugModeRegistry.Tests Oxygen.Examples.DemoShell.RenderingSettingsService.Tests Oxygen.Examples.DemoShell.DiagnosticsPanel.Tests --parallel 4`;
  `ctest --test-dir out\build-ninja -C Debug -R "(Oxygen\.Vortex\.ShaderDebugModeRegistry|Oxygen\.Examples\.DemoShell\.(RenderingSettingsService|DiagnosticsPanel))" --output-on-failure`
  with `ShaderDebugModeRegistry` 9/9, `RenderingSettingsService` 4/4 in the
  focused executable, and `DiagnosticsPanel` 1/1; `cmake --build out\build-ninja
  --config Debug --target oxygen-examples-renderscene --parallel 4` passed and
  ShaderBake repacked `shaders.bin` with 185 shader modules.
- Runtime DemoShell registration smoke passed:
  `cmake --build out\build-ninja --config Debug --target oxygen-examples-texturedcube --parallel 4`;
  `Oxygen.Examples.TexturedCube.exe --frames 4 --fps 30 --vsync false --capture-provider off`
  exited 0 and `texturedcube-diagnostics-panel.stderr.log` contains
  `Registered Diagnostics panel for the Vortex runtime`.

### Slice H - External Tool Handshake And Automation Hardening

**Status:** `validated`

Tasks:

- Document how existing RenderDoc/CDB/Python/PowerShell tools consume the
  diagnostics manifest and timeline export.
- Add or update tool README entries only if runtime export paths are landed.
- Do not rewrite analyzer semantics owned by another feature milestone unless a
  minimal parser/handoff is required for M05A value.
- Define the common PowerShell automation contract for Vortex proof scripts:
  strict mode, fail-fast native command handling, sequential dependent proof
  stages, deterministic output paths, report schema/verdict checks, and no proof
  assertion after failed prerequisites.
- Add or reuse a shared helper module for M05A-owned/touched wrappers so native
  command return codes and generated reports are handled consistently.
- Serialize RenderDoc UI automation through the existing UI-analysis lock
  pattern.
- Add a small negative/synthetic failing-report check for touched wrappers where
  practical.

Validation requirements:

- Syntax checks for changed Python/PowerShell files only if tool code changes.
- Wrapper dry-run or focused helper tests for failure propagation when tool code
  changes.
- Runtime/capture proof only if runtime logging/export behavior changes.

Implementation evidence:

- `tools/vortex/VortexProofCommon.ps1` defines the Vortex proof helper surface:
  native command failure propagation, sequential PowerShell proof-step
  invocation, RenderDoc UI analysis invocation through the existing
  `tools/shadows/Invoke-RenderDocUiAnalysis.ps1` lock/report gate, and compact
  key/value report helpers.
- `Verify-VortexBasicRuntimeProof.ps1`,
  `Verify-VortexBasicDebugViewProof.ps1`, and `Verify-AsyncRuntimeProof.ps1`
  now use the helper instead of open-coded `powershell` calls and
  `$LASTEXITCODE` checks.
- `tools/vortex/README.md` documents the proof automation contract and the
  diagnostics capture manifest handoff role.
- `tools/vortex/tests/test_vortex_proof_common.py` covers explicit report
  success/pass acceptance, failed-verdict rejection, and native command exit
  propagation.

Validation:

- PowerShell parser checks passed for `VortexProofCommon.ps1` and the three
  migrated verify wrappers.
- `pytest tools/vortex/tests/test_vortex_proof_common.py tools/vortex/tests/test_async_renderdoc_analysis.py`
  passed with 7/7 tests.

### Slice I - Optional GPU Debug Primitive Runtime

**Status:** `deferred_unless_required`

Tasks:

- Keep this slice deferred unless M05A proof shows that ledger, debug-mode
  registry, timeline, and capture manifest cannot explain a required spatial GPU
  issue.
- If required, wire fixed-capacity CPU/GPU resources and per-view
  `DebugFrameBindings`.
- Integrate clear and draw passes.
- Register EngineShaderCatalog entries.
- Add explicit producer/debug-mode opt-in, bounded capacity, and overflow
  reporting.
- Prove disabled and enabled paths.

Validation:

- Focused Vortex build.
- ShaderBake/catalog tests.
- Runtime or RenderDoc proof of visible primitives.
- D3D12 debug-layer audit.

## 7. Expected Test Commands

Adjust target names to final CMake wiring and summarize the exact commands in
the single VTX-M05A status row.

```powershell
cmake --build out\build-ninja --config Debug --target Oxygen.Vortex.DiagnosticsService.Tests Oxygen.Vortex.GpuTimelineProfiler.Tests --parallel 4
ctest --preset test-debug -R "Oxygen\.Vortex\.(DiagnosticsService|GpuTimelineProfiler)" --output-on-failure
git diff --check
```

If EngineShaderCatalog or GPU debug primitive work changes shader requests:

```powershell
cmake --build --preset windows-debug --target oxygen-graphics-direct3d12_shaders Oxygen.Graphics.Direct3D12.ShaderBakeCatalog.Tests
ctest --preset test-debug -R "Oxygen\.Graphics\.Direct3D12\.ShaderBakeCatalog" --output-on-failure
```

If runtime-visible panels, manifests, or debug modes change:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\vortex\Run-VortexBasicRuntimeValidation.ps1 -Output out\build-ninja\analysis\vortex\m05a-diagnostics\vortexbasic-runtime -Frame 5 -RunFrames 9 -Fps 30 -BuildJobs 4
```

If new GPU resources or passes are added:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\vortex\Assert-VortexBasicDebugLayerAudit.ps1 -Output out\build-ninja\analysis\vortex\m05a-debug-layer -Frame 5 -RunFrames 9 -Fps 30
```

## 8. Exit Gate

M05A can move to `validated` only when all implemented scope has evidence:

- DiagnosticsService code exists and is wired into Vortex.
- ShaderDebugModeRegistry is authoritative and tested.
- Frame ledger records pass, product, issue, feature, and active-mode state.
- GPU timeline behavior is preserved and service-facing.
- Tracy remains available through the existing profiling/backend integration
  when compiled, and Vortex diagnostics do not take a direct Tracy dependency.
- Capture manifest export exists and is tested.
- DemoShell diagnostics panel registry is implemented or explicitly deferred
  with a recorded residual gap.
- M05A-owned or touched proof scripts follow the automation hardening contract,
  including fail-fast native command handling and no proof assertion after
  failed prerequisites.
- Any shader/runtime/capture-visible changes have matching ShaderBake, runtime,
  capture, or debug-layer evidence.
- the VTX-M05A row in `IMPLEMENTATION_STATUS.md` records files/areas changed,
  commands/results, UE5.7 references, and remaining gaps without adding
  per-commit rows.

Current closure evidence:

- Full VortexBasic runtime proof passed on 2026-04-26:
  `powershell -NoProfile -ExecutionPolicy Bypass -File tools\vortex\Run-VortexBasicRuntimeValidation.ps1 -Output out\build-ninja\analysis\vortex\m05a-diagnostics\vortexbasic-runtime -Frame 5 -RunFrames 9 -Fps 30 -BuildJobs 4`.
  The validation report records `analysis_result=success`; the CDB/D3D12
  debug-layer report records `overall_verdict=pass`; the RenderDoc capture
  report records `analysis_result=success`.
- No M05A closeout change altered shader requests or shader bytecode, so no
  additional ShaderBake proof is required for this closeout.

If any item lacks validation, M05A remains `in_progress`.

## 9. Review Checklist

Before each commit in this milestone:

- Does the slice add a reusable troubleshooting fact?
- Does it avoid a parallel legacy path?
- Is the disabled path cheap and observable?
- Is every new name stable enough for RenderDoc and tools?
- Can an external script consume the result without engine internals?
- Are strong claims backed by tests, build output, or runtime/capture evidence?
