# VTX-M05A — Diagnostics product service

Status: `validated`

| Field     | Summary                                                                     |
| --------- | --------------------------------------------------------------------------- |
| Outcome   | Diagnostics service, shader-debug registry, timeline and capture manifest.  |
| Remaining | Extensions: [VX-DIAG-01](../../OPEN_ITEMS.md#p3--unscheduled-capabilities). |
| Evidence  | [Validation record](validation.md)                                          |

[Roadmap](../../PLAN.md) · [Design index](../../lld/README.md)

Dependencies: VTX-M04D.1, VTX-M04F

## Delivered scope

Concrete diagnostics product surface; not confused with proof tooling.

## Scope and acceptance

Scope:

- Concrete diagnostics service/files, debug-mode vocabulary, GPU timeline
  publishing, UI/panel/overlay hooks, and failure surfaces.
- Diagnostics may inspect environment, occlusion, lighting, and scene texture
  state through published contracts only.
- Detailed implementation plan:
  [`plan/VTX-M05A-diagnostics-product-service.md`](README.md).

Out of scope:

- Treating RenderDoc, logs, analyzer scripts, or tests as diagnostics delivery.
  Those remain verification tools unless DiagnosticsService-owned runtime
  behavior changes.

**Status:** `validated`
**Milestone:** `VTX-M05A - Diagnostics Product Service`
**Scope owner:** Vortex runtime diagnostics
**Primary LLD:** [../lld/diagnostics-service.md](../../lld/diagnostics-service.md)

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
- Update this milestone and its validation record.

Validation requirements:

- `rg` consistency scan for M05A, LLD status, and plan references.
- `git diff --check`.

### Slice B - DiagnosticsService Shell And Ownership Boundary

**Status:** `validated`

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

[Checks](validation.md#slice-b---diagnosticsservice-shell-and-ownership-boundary--checks).

### Slice C - ShaderDebugModeRegistry

**Status:** `validated`

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

[Checks](validation.md#slice-c---shaderdebugmoderegistry--checks).

### Slice D - Frame Ledger And Minimal Runtime Issues

**Status:** `validated`

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

[Checks](validation.md#slice-d---frame-ledger-and-minimal-runtime-issues--checks).

### Slice E - GPU Timeline Facade

**Status:** `validated`

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

[Checks](validation.md#slice-e---gpu-timeline-facade--checks).

### Slice F - Capture Manifest Export

**Status:** `validated`

Tasks:

- Add `DiagnosticsCaptureManifest.{h,cpp}`.
- Export `vortex.diagnostics.capture-manifest.v1` JSON from the latest snapshot.
- Include frame, active debug mode, enabled features, passes, products, issues,
  and optional GPU timeline export path.
- Avoid raw pointers and transient descriptor indices in the manifest.
- Keep paths relative or caller-provided.

[Checks](validation.md#slice-f---capture-manifest-export--checks).

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

[Checks](validation.md#slice-g---demoshell-diagnostics-panel-registry--checks).

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

[Checks](validation.md#slice-h---external-tool-handshake-and-automation-hardening--checks).

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

[Checks](validation.md#slice-i---optional-gpu-debug-primitive-runtime--checks).

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
- the VTX-M05A row in `milestone README` records files/areas changed,
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

## Supporting records

- [validation](validation.md)
