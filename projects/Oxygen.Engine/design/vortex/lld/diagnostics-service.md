# DiagnosticsService LLD

**Phase:** 5A - Remaining Services
**Deliverable:** D.14
**Status:** `authoritative_for_M05A_implementation`

## Mandatory Vortex Rule

- For Vortex planning and implementation, `Oxygen.Renderer` is legacy dead
  code. It is not production, not a reference implementation, not a fallback,
  and not a simplification path for any Vortex task.
- Vortex diagnostics must be grounded in UE5.7 runtime and shader patterns from
  `F:\Epic Games\UE_5.7\Engine\Source\Runtime` and
  `F:\Epic Games\UE_5.7\Engine\Shaders`, adapted to Oxygen's renderer facade,
  bindless resource model, publication contracts, and external proof tooling.
- No diagnostics milestone may be marked complete until implementation,
  docs/status, and validation evidence are all recorded.

## 1. Executive Decision

`DiagnosticsService` is the Vortex diagnostics control plane. Its job is to
make a frame explainable when the CPU or GPU pipeline goes wrong.

The design is intentionally a smart, small toolkit rather than a clone of UE
Insights, UE editor show flags, RenderDoc, or a full VisualizeTexture system.
It must give developers enough facts to answer these questions without guesswork:

1. Which frame/view/pass/debug mode was active?
2. Which passes executed, in what order, and with which input/output products?
3. Which resources and bindings were expected, missing, stale, or invalid?
4. Which shader variant or debug mode path was selected?
5. Where did GPU time go, and which scopes overflowed or failed to resolve?
6. Which structured warnings should the operator or capture tools investigate?
7. Which capture/log/export artifacts describe this exact frame?

The service owns diagnostics state, cataloging, snapshots, timeline access, and
panel/tool handoff. It does not own lighting, shadows, environment, post-process,
scene texture allocation, frame scheduling, RenderDoc, CDB, or proof scripts.

## 2. Design Principles

### 2.1 Swiss-Army-Knife Scope

The M05A product is a compact set of reusable instruments:

- stable debug-mode registry
- frame diagnostics ledger
- GPU timeline facade
- pass/product/binding observability records
- minimal recoverable runtime issues
- minimal ImGui panel registry
- capture/export manifest for external tools

Anything larger is optional unless it directly answers a recurring debugging
question. GPU debug primitives, texture viewers, and richer panels are planned
as incremental blades, not prerequisites for the core tool.

### 2.2 Disabled-Path Discipline

Diagnostics disablement is not a single switch hidden behind `NDEBUG`. It is a
layered policy so the engine stays observable in release investigations without
shipping accidental GPU work.

| Layer | Mechanism | Owns | Policy |
| --- | --- | --- | --- |
| Compile-time availability | Normal Vortex build plus narrow optional macros | Build system | Core diagnostics types, catalog, frame ledger, issue recording, and manifest code are compiled with Vortex. Do not add a broad `OXYGEN_WITH_VORTEX_DIAGNOSTICS` macro for M05A. Optional third-party sinks stay behind their existing macros, such as `OXYGEN_WITH_TRACY`. |
| Build-profile defaults | `NDEBUG` / non-`NDEBUG` default initializer only | `DiagnosticsConfig` defaults | `NDEBUG` may change the default feature mask and assertion verbosity. It must not remove APIs, enum values, schema fields, or troubleshooting code needed for release captures. |
| Renderer capability | `RendererCapabilityFamily::kDiagnosticsAndProfiling` | Renderer configuration | If the capability is absent, diagnostics features are clamped off and requests produce `diag.feature-unavailable` issues where an issue channel exists. The service can still expose a disabled snapshot. |
| Runtime feature mask | `DiagnosticsConfig`, CVars/CLI, and `DiagnosticsService::SetEnabledFeatures` | DiagnosticsService | The effective feature mask controls CPU ledger recording, GPU timeline collection, panels, manifest export, and GPU debug primitives. This is the main on/off switch. |
| Per-feature trigger | Debug mode, export request, panel registration, GPU primitive request | DiagnosticsService and owning stage | Heavy work runs only when both the feature mask and the specific trigger require it. |
| Pass execution | Service-owned pass config for diagnostics passes only | DiagnosticsService | Domain pass configs must not grow diagnostics payloads. Diagnostics passes are scheduled only from diagnostics-owned features. |

Default policy:

- non-`NDEBUG` builds default to a small CPU-only developer profile:
  frame ledger, minimal recoverable runtime issues, and shader debug-mode
  registry available in memory.
- `NDEBUG` builds default to no continuously recording diagnostics features
  unless `RendererConfig`, startup CVars, CLI, or tools explicitly enable them.
- GPU timeline is off by default in all build profiles and enabled explicitly.
- ImGui panels are off until the panel feature is enabled and an ImGui runtime is
  active.
- capture manifest export is on-demand only; it does not continuously write.
- GPU debug primitives are off by default and unavailable until their runtime
  slice is implemented and proven.

When the effective diagnostics feature mask is zero and shader debug mode is
`kDisabled`:

- production rendering output must be unchanged
- no diagnostics pass may allocate per-frame GPU resources
- no shader variant may be selected only because diagnostics code exists
- CPU overhead must be limited to cheap state reads and null checks

Every enabled feature must have an explicit flag and an observable state in the
latest diagnostics snapshot.

Rationale:

- Do not compile out core diagnostics with `NDEBUG`; many hard GPU bugs are
  release-only.
- Do not add a broad `OXYGEN_WITH_*` umbrella unless a dependency or platform
  limitation requires it. Broad macros create untested product variants.
- Use narrow compile-time flags for optional integrations only, such as Tracy.
- Use runtime feature flags for behavior, because tools and examples need to
  enable diagnostics surgically without rebuilding.
- Use pass configs only for diagnostics-owned passes. Domain systems report
  facts through the ledger and keep their production configs clean.

### 2.3 Ownership Discipline

DiagnosticsService is a control plane and fact collector. Domain systems remain
the execution owners:

| Domain | Owner | Diagnostics role |
| --- | --- | --- |
| Scene texture allocation | `SceneTextures` / SceneRenderer | report product names, descriptors, availability |
| Lighting | `LightingService` | report selected lights, debug-mode support, output products |
| Shadows | `ShadowService` | report shadow maps, cascades, masks, and recoverable issues |
| Environment/fog | `EnvironmentLightingService` | report atmosphere/fog products and state |
| Post-process | `PostProcessService` | report inputs, outputs, exposure/tonemap state |
| ImGui rendering | `ImGuiRuntime` | draw service-registered panels |
| GPU timing | `GpuTimelineProfiler` | collect and export timeline frames |

DiagnosticsService can visualize or expose facts. It must not mutate another
domain's products to make a debug view pass.

## 3. UE5.7 Grounding And Oxygen Adaptation

The design follows UE5.7 principles rather than UE object structure.

| UE5.7 family | References checked | Principle to adopt | Oxygen adaptation |
| --- | --- | --- | --- |
| Debug view modes | `Renderer/Private/DebugViewModeRendering.cpp`; `Shaders/Private/DebugViewModePixelShader.usf` | Debug visualization is a renderer-owned vocabulary with explicit shader support, pass names, and product requirements. | Keep `ShaderDebugMode` as a typed Vortex enum, add runtime registry metadata, and let owning stages execute their debug views. |
| GPU profiler events | `RHI/Public/GPUProfiler.h`; `RHI/Public/GpuProfilerTrace.h`; `RHI/Public/RHIBreadcrumbs.h`; renderer `RDG_EVENT_SCOPE` and `RDG_GPU_STAT_SCOPE` use | GPU work must have named scopes, frame boundaries, queue context, sink/export paths, and overflow/failure diagnostics. | Reuse `graphics::IGpuProfileCollector`, `CommandRecorder::BeginProfileScope`, and `GpuTimelineProfiler`. Add DiagnosticsService facade and frame correlation. |
| ShaderPrint | `Renderer/Public/ShaderPrintParameters.h`; `Renderer/Private/ShaderPrint.cpp`; `Shaders/Private/ShaderPrint.ush` | GPU-side debug emission is optional, bounded, per-view, explicitly requested, and rendered later by a diagnostics path. | Treat existing `DebugFrameBindings` and `GpuDebug*.hlsl` as a future bounded GPU primitive slice. Do not block M05A on it. |
| VisualizeTexture | `Shaders/Private/Tools/VisualizeTexture.usf`; renderer visualize-texture call sites; `TexCreate_HideInVisualizeTexture` | Runtime products should have names, descriptors, and inspectability controls. | M05A records product descriptors and capture names first. A texture viewer can be added later using the same product catalog. |
| Debug names and breadcrumbs | `RHIDefinitions.h` `FDebugName`; renderer pass/resource names; RHI breadcrumbs | Debug names are part of the product, not comments. | Require stable pass, product, issue, and debug-mode names in snapshots and exports. |

Oxygen divergences:

- Oxygen has no RDG. Diagnostics must attach to explicit Vortex stages,
  publication records, command-recorder scopes, and resource descriptors.
- Oxygen uses bindless handles and explicit ABI headers. Diagnostics must record
  logical slots and publication names, not raw descriptor heap internals unless
  a focused probe needs them.
- Oxygen uses external RenderDoc/CDB/Python/PowerShell tooling for proof. The
  runtime must emit facts those tools can consume, but the tools stay outside
  the engine dependency graph.

## 4. Current Codebase Assessment

### 4.1 Valuable Existing Surfaces

| Surface | Current location | Assessment | Required action |
| --- | --- | --- | --- |
| GPU timeline profiler | `src/Oxygen/Vortex/Internal/GpuTimelineProfiler.{h,cpp}` | Correct and valuable. It implements `graphics::IGpuProfileCollector`, records nested scopes, detects overflow/incomplete scopes, supports sinks, latest-frame retention, and JSON/CSV export. | Preserve. Expose through DiagnosticsService. Add frame-ledger correlation and service tests. |
| ImGui runtime | `src/Oxygen/Vortex/Internal/ImGuiRuntime.{h,cpp}` | Correct substrate. It initializes backend state, manages frame lifecycle, renders overlay texture, and returns composition data. | Preserve. Add service-owned panel registry on top. |
| Shader debug mode enum | `src/Oxygen/Vortex/ShaderDebugMode.h` | Valuable ABI used by Vortex, SceneRenderer, shaders, and DemoShell. Helper functions are real but metadata is incomplete. | Preserve enum values. Add authoritative catalog and tests. |
| Deferred debug visualization | `SceneRenderer::RenderDebugVisualization` | Real debug output for material/depth/shadow-mask views. Current mode filtering/name helpers are local to SceneRenderer. | Preserve execution in SceneRenderer. Move mode metadata and product requirement truth to the catalog. |
| GPU debug ABI and shaders | `Types/DebugFrameBindings.h`; `Shaders/Vortex/Services/Diagnostics/*` | Useful assets, not a complete runtime feature. | Mark as asset-only until CPU resources, pass scheduling, EngineShaderCatalog registration, ShaderBake, and runtime proof exist. |
| DemoShell controls | `Examples/DemoShell/Services/RenderingSettingsService.*`; `Examples/DemoShell/UI/*` | Useful operator controls, but mappings are duplicated. | Convert to debug-mode registry consumers after the registry lands. |
| External proof tools | `tools/vortex/*`; `tools/shadows/renderdoc_ui_analysis.py` | Essential for capture analysis, debug-layer audits, and validation. | Keep external. Define runtime export contracts they can consume. |

### 4.2 Problems This Design Fixes

- Debug truth is scattered across `Renderer`, SceneRenderer helpers, DemoShell,
  proof scripts, and shader defines.
- There is no per-frame ledger that records pass execution, product state, active
  debug mode, GPU timeline state, and warnings together.
- There is no stable runtime schema that external tools can parse before falling
  back to RenderDoc-specific heuristics.
- GPU debug primitive shaders exist without runtime product truth.
- Diagnostics terminology is inconsistent. A debug mode name, shader define,
  UI label, capture label, and analyzer key can drift.

## 5. Runtime Architecture

### 5.1 File Placement

```text
src/Oxygen/Vortex/
  Diagnostics/
    DiagnosticsService.h
    DiagnosticsService.cpp
    DiagnosticsTypes.h
    DiagnosticsFrameLedger.h
    DiagnosticsFrameLedger.cpp
    DiagnosticsCaptureManifest.h
    DiagnosticsCaptureManifest.cpp
    ShaderDebugModeRegistry.h
    ShaderDebugModeRegistry.cpp
    DiagnosticsPanel.h
    Internal/
      GpuTimelineDiagnosticsAdapter.h/.cpp
      ImGuiDiagnosticsPanelRegistry.h/.cpp
    Passes/
      GpuDebugPrimitivePass.h/.cpp      # deferred unless the slice lands
```

Shader assets remain under:

```text
src/Oxygen/Graphics/Direct3D12/Shaders/Vortex/Services/Diagnostics/
```

### 5.2 Core Concepts

| Concept | Purpose |
| --- | --- |
| `DiagnosticsService` | Runtime control plane, settings, latest snapshot, panel registry, export requests. |
| `ShaderDebugModeRegistry` | Runtime source of truth for `ShaderDebugMode` names, families, pass owner, product requirements, shader define, and UI label. This is not the D3D12 shader bake catalog. |
| `DiagnosticsFrameLedger` | Per-frame facts: views, passes, products, bindings, issues, timeline correlation, and capture labels. |
| `DiagnosticsFrameSnapshot` | Immutable public copy of the latest ledger plus settings and timeline state. |
| `DiagnosticsIssue` | Structured warning/error with stable code, severity, frame/view/pass/product context, and operator message. |
| `DiagnosticsCaptureManifest` | JSON export describing how to inspect a frame with external tools. |
| `DiagnosticsPanel` | Optional ImGui consumer of snapshots and catalog state. |

## 6. Public API Contract

The API is intentionally small. It exposes state, not ownership of render work.

```cpp
#include <Oxygen/Base/Macros.h>

namespace oxygen::vortex {

// NOLINTNEXTLINE(*-enum-size)
enum class DiagnosticsFeature : std::uint32_t {
  kNone = 0,
  kFrameLedger = OXYGEN_FLAG(0),
  kGpuTimeline = OXYGEN_FLAG(1),
  kShaderDebugModes = OXYGEN_FLAG(2),
  kImGuiPanels = OXYGEN_FLAG(3),
  kCaptureManifest = OXYGEN_FLAG(4),
  kGpuDebugPrimitives = OXYGEN_FLAG(5),
};

OXYGEN_DEFINE_FLAGS_OPERATORS(DiagnosticsFeature)

enum class DiagnosticsSeverity : std::uint8_t {
  kInfo,
  kWarning,
  kError,
};

enum class DiagnosticsPassKind : std::uint8_t {
  kCpuOnly,
  kGraphics,
  kCompute,
  kCopy,
  kComposite,
};

enum class DiagnosticsDebugPath : std::uint8_t {
  kNone,
  kForwardMeshVariant,
  kDeferredFullscreen,
  kServicePass,
  kExternalToolOnly,
};

using DiagnosticsFeatureSet = DiagnosticsFeature;

[[nodiscard]] auto to_string(DiagnosticsFeatureSet features) -> std::string;
[[nodiscard]] constexpr auto to_string(DiagnosticsSeverity severity) noexcept
    -> std::string_view;
[[nodiscard]] constexpr auto to_string(DiagnosticsPassKind kind) noexcept
    -> std::string_view;
[[nodiscard]] constexpr auto to_string(DiagnosticsDebugPath path) noexcept
    -> std::string_view;

struct ShaderDebugModeInfo {
  ShaderDebugMode mode { ShaderDebugMode::kDisabled };
  std::string_view canonical_name;
  std::string_view display_name;
  std::string_view family;
  std::string_view shader_define;
  DiagnosticsDebugPath path { DiagnosticsDebugPath::kNone };
  bool requires_scene_color { false };
  bool requires_scene_depth { false };
  bool requires_gbuffer { false };
  bool requires_lighting_products { false };
  bool requires_shadow_products { false };
};

struct DiagnosticsIssue {
  DiagnosticsSeverity severity { DiagnosticsSeverity::kInfo };
  std::string code;
  std::string message;
  std::string view_name;
  std::string pass_name;
  std::string product_name;
};

struct DiagnosticsPassRecord {
  std::string name;
  DiagnosticsPassKind kind { DiagnosticsPassKind::kCpuOnly };
  bool executed { false };
  std::vector<std::string> inputs;
  std::vector<std::string> outputs;
  std::vector<std::string> missing_inputs;
  std::optional<float> gpu_duration_ms;
};

struct DiagnosticsProductRecord {
  std::string name;
  std::string producer_pass;
  std::string resource_name;
  std::string descriptor;
  bool published { false };
  bool valid { false };
  bool stale { false };
};

struct DiagnosticsFrameSnapshot {
  std::uint64_t frame_index { 0 };
  ShaderDebugMode active_shader_debug_mode { ShaderDebugMode::kDisabled };
  DiagnosticsFeatureSet enabled_features { DiagnosticsFeature::kNone };
  bool imgui_overlay_active { false };
  bool gpu_timeline_enabled { false };
  bool gpu_timeline_frame_available { false };
  bool capture_manifest_available { false };
  std::vector<DiagnosticsPassRecord> passes;
  std::vector<DiagnosticsProductRecord> products;
  std::vector<DiagnosticsIssue> issues;
};

class DiagnosticsPanel {
 public:
  virtual ~DiagnosticsPanel() = default;
  [[nodiscard]] virtual auto Name() const -> std::string_view = 0;
  virtual void Draw(DiagnosticsFrameSnapshot const& snapshot) = 0;
};

class DiagnosticsService {
 public:
  void SetEnabledFeatures(DiagnosticsFeatureSet features);
  [[nodiscard]] auto GetEnabledFeatures() const -> DiagnosticsFeatureSet;

  void SetShaderDebugMode(ShaderDebugMode mode);
  [[nodiscard]] auto GetShaderDebugMode() const -> ShaderDebugMode;
  [[nodiscard]] auto EnumerateShaderDebugModes() const
      -> std::span<const ShaderDebugModeInfo>;
  [[nodiscard]] auto FindShaderDebugMode(std::string_view canonical_name) const
      -> std::optional<ShaderDebugMode>;

  void SetGpuTimelineEnabled(bool enabled);
  void RequestGpuTimelineExport(std::filesystem::path path);
  [[nodiscard]] auto GetLatestGpuTimelineFrame() const
      -> std::optional<internal::GpuTimelineFrame>;

  void RegisterPanel(std::unique_ptr<DiagnosticsPanel> panel);

  void BeginFrame(frame::SequenceNumber frame);
  void RecordPass(DiagnosticsPassRecord record);
  void RecordProduct(DiagnosticsProductRecord record);
  void ReportIssue(DiagnosticsIssue issue);
  void EndFrame();

  void RequestCaptureManifestExport(std::filesystem::path path);
  [[nodiscard]] auto GetLatestSnapshot() const -> DiagnosticsFrameSnapshot;
};

}  // namespace oxygen::vortex
```

Implementation may split writer and read-only interfaces, but these semantics
are mandatory: settings are service-owned, records are append-only per frame,
snapshots are immutable, and external tools consume exported state rather than
calling into renderer internals.

Enum and logging rules are mandatory:

- Flag enums must use `OXYGEN_FLAG` and `OXYGEN_DEFINE_FLAGS_OPERATORS` from
  `src/Oxygen/Base/Macros.h`.
- `DiagnosticsFeature` intentionally uses `std::uint32_t` so it can remain a
  stable feature-mask ABI for snapshots, manifests, and tooling. The enum must
  carry the local clang-tidy suppression already used by Vortex flag enums for
  the "underlying type is wider than needed" warning.
- Every diagnostics enum must provide a namespace-local `to_string` overload.
  The logging framework performs ADL lookup for these overloads, so
  `LOG_F`/`DLOG_F` call sites should pass enum values directly instead of
  wrapping each value in `to_string(...)`.

## 7. Frame Ledger Contract

The frame ledger is the central troubleshooting primitive.

### 7.1 Required Per-Frame Records

Every Vortex frame with diagnostics enabled must be able to emit:

- frame sequence and slot
- active view id/name where available
- active `ShaderDebugMode`
- diagnostics feature flags
- pass records for major Vortex stages and service passes
- product records for published scene, lighting, shadow, environment, and
  post-process outputs
- minimal recoverable issues emitted by services
- GPU timeline availability and correlated pass durations where names match
- export paths for generated timeline or capture manifests

### 7.2 Pass Record Requirements

Pass records must use stable names. Stage numbers can appear in names, but the
semantic name is required:

- `Stage5.ScreenHzb`
- `Stage8.ShadowDepth.Directional`
- `Stage12.DeferredLighting`
- `Stage14.Environment.LocalFogTiledCulling`
- `Stage14.Environment.VolumetricFog`
- `Stage15.Environment.Compose`
- `Stage22.PostProcess.Tonemap`
- `Composition.SceneCopy`
- `Composition.ImGuiOverlay`

This naming rule is a design requirement because RenderDoc event matching and
human troubleshooting both depend on stable names.

### 7.3 Product Record Requirements

Product records must distinguish:

- `not_authored`
- `authored_unavailable`
- `published_valid`
- `published_invalid`
- `stale`

M05A can encode these as fields instead of an enum if that fits existing
publication types, but the meaning must be visible in snapshots and exports.

## 8. Shader Debug Mode Registry

`ShaderDebugMode` remains the stable typed ABI. The runtime registry is the
authority for:

- canonical CLI/tool name, for example `directional-shadow-mask`
- display label, for example `Directional Shadow Mask`
- family, for example `shadow`
- shader define, if any
- debug path: forward mesh variant, deferred fullscreen, service pass, or
  external-tool-only
- product requirements
- capability requirements

Rules:

- Every enum value must have exactly one registry entry.
- Canonical names must be stable, lowercase, hyphenated, and tool-safe.
- UI code must consume the registry rather than duplicate mode lists.
- SceneRenderer can keep executing debug views, but mode classification must
  move to the registry or remain tested against it.
- Adding a debug mode requires a registry entry, test update, EngineShaderCatalog
  and ShaderBake proof when shader variants change, and status evidence.

Positioning relative to `EngineShaderCatalog.h`:

| Artifact | Layer | Purpose | Changes when |
| --- | --- | --- | --- |
| `src/Oxygen/Graphics/Direct3D12/Shaders/EngineShaderCatalog.h` | Graphics/D3D12 shader build layer | Declares shader files, entry points, required defines, and permutations to bake into `shaders.bin`. | A shader file, entry point, or permutation variant changes. |
| `src/Oxygen/Vortex/Diagnostics/ShaderDebugModeRegistry.*` | Vortex runtime diagnostics layer | Describes `ShaderDebugMode` values for UI, CLI/tool names, feature requirements, owning pass path, and optional shader define linkage. | A runtime debug mode is added, renamed, exposed to UI/tools, or changes product/capability requirements. |

The registry may reference a shader define string that also appears in
`EngineShaderCatalog.h`, but it must not register shaders, generate shader bake
requests, or own permutation expansion. Tests must verify consistency only for
debug modes that use shader variants.

## 9. GPU Timeline And Breadcrumbs

The existing `GpuTimelineProfiler` remains the built-in implementation for
curated frame timings. DiagnosticsService provides the product facade.

Tracy is also part of Oxygen's diagnostics toolkit. M05A must not pretend that
basic frame timings replace Tracy, and it must not force every runtime proof to
depend on Tracy being compiled or connected. The boundary is:

- built-in timeline: always uses Oxygen-owned data structures, is exportable in
  test/proof runs, and gives concise pass-level timing facts to the diagnostics
  panel and capture manifest
- Tracy: provides dense CPU/GPU tracing and interactive optimization views when
  `OXYGEN_WITH_TRACY` is enabled and the Tracy collector/client is available

Render passes and Vortex services must not call Tracy APIs directly. They should
use the existing profiling scope path, such as command-recorder profile scopes,
so the D3D12/backend profiling layer can feed Oxygen timeline data and Tracy as
configured.

Required M05A behavior:

- service-controlled enablement
- latest-frame access
- one-shot JSON/CSV export forwarding
- overflow and incomplete-scope issues converted into `DiagnosticsIssue`
- pass records correlated with scope names when possible
- diagnostics snapshot/panel visibility for whether Tracy support was compiled,
  whether GPU collection is active when this can be queried, and where the
  developer should look for the dense trace
- no runtime or test proof that requires Tracy unless the proof explicitly tests
  Tracy integration

Best-practice requirements:

- scope names must match pass record names where practical
- timeline diagnostics must include frame sequence and query slot usage
- missing timestamp provider or queue must produce a structured issue, not a
  silent empty result
- exports must include a schema version
- timeline scopes should follow the engine profiling contract in
  `design/profiling/unified-profiling-architecture.md` and the profiling
  developer guide, keeping broad pass scopes available to the built-in timeline
  and detailed nested scopes available to Tracy/capture analysis

## 10. Capture Manifest And External Tools

The capture manifest is the bridge between runtime diagnostics and external
analysis tools. It is a small JSON document, not a trace database.

Minimum manifest fields:

```json
{
  "schema": "vortex.diagnostics.capture-manifest.v1",
  "frame": 90,
  "debug_mode": "directional-shadow-mask",
  "features": ["frame-ledger", "gpu-timeline"],
  "passes": [],
  "products": [],
  "issues": [],
  "timeline_export": "gpu-timeline.json",
  "recommended_tools": ["RenderDoc", "cdb", "tools/vortex/AnalyzeRenderDoc*.py"]
}
```

The manifest must not contain raw pointers, transient descriptor heap indices
without logical names, or machine-specific absolute paths unless explicitly
requested by the caller.

External tools remain outside runtime:

- `tools/vortex/Run-*RuntimeValidation.ps1`
- `tools/vortex/Assert-*` and `Verify-*` wrappers
- `tools/vortex/AnalyzeRenderDoc*.py`
- `tools/vortex/ProbeRenderDoc*.py`
- `tools/shadows/renderdoc_ui_analysis.py`
- CDB/D3D12 debug-layer audits

M05A should not rewrite analyzer semantics owned by another feature milestone.
It must, however, put the automation contract in order for new or modified
Vortex diagnostics/proof scripts. Existing scripts that M05A touches, invokes as
proof, or documents as the canonical M05A path must follow the same rules.

Automation hardening requirements:

- PowerShell wrappers run in strict mode, set `$ErrorActionPreference = 'Stop'`,
  and treat native non-zero exit codes as failures immediately.
- Dependent proof stages run sequentially: build, run, capture, analyze, assert.
  Independent analyzers may run in parallel only if each branch propagates its
  own failure and the final wrapper fails when any branch fails.
- A wrapper must not assert proof after a failed build, failed runtime launch,
  failed capture, failed analyzer, missing report, or malformed report.
- Native command helpers capture `$LASTEXITCODE` before running another command
  and return a typed success/failure result or throw; they do not rely on noisy
  transcript parsing.
- RenderDoc UI automation is serialized through the existing UI-analysis lock
  pattern because the RenderDoc UI is a shared process resource.
- Output folders, report names, and manifest paths are deterministic and printed
  in the final success/failure summary.
- Analyzer JSON must include a schema/result field and an explicit verdict or
  failure reason. Wrappers must verify those fields instead of treating "file
  exists" as proof.
- Shared PowerShell helpers should live in a Vortex/tooling common module or an
  existing common helper that Vortex can depend on. M05A is responsible for
  documenting the helper contract and using it for M05A-owned scripts.
- Where practical, wrappers get a small negative test or synthetic failing
  report check so future scripts cannot accidentally convert failed baselines
  into successful proof.

## 11. ImGui Panels

The panel system is a consumer of snapshots. DemoShell's Vortex-facing runtime
debugging surface is the Diagnostics panel. The old Rendering panel name is not
kept as a migration alias for Vortex samples; clean UX and clear ownership are
more valuable than preserving an ambiguous panel label.

Developer workflow target:

1. A DemoShell sample or validation window shows a bad frame.
2. The developer opens Diagnostics.
3. The panel answers what the engine is doing, which debug modes are available,
   what products/passes exist for the current frame, whether timings/capture
   exports are available, and whether the requested UI state was actually
   accepted by the engine.

Panel structure:

- Status strip: renderer implementation, frame index, active view, enabled
  diagnostics features, capture/export state, and Tracy compiled/available
  state when known.
- Debug Modes: modes grouped by `ShaderDebugModeRegistry` family. Unsupported
  modes stay visible but disabled with the missing capability/product reason.
  Selection writes a requested debug mode; the UI displays both requested and
  effective state so capability clamping is explicit.
- Frame Facts: compact pass/product table showing executed/skipped state,
  major inputs/outputs, stale or missing products, and the issue count. This is
  for scanning, not for displaying raw logs.
- Timeline: built-in GPU timeline enable/export controls, latest top-level
  scope timings, overflow/incomplete-scope state, and a Tracy status hint for
  dense trace inspection.
- Tools: one-shot capture manifest export, last artifact paths, and proof-tool
  handoff information.
- Settings: requested runtime diagnostics features, effective enabled features,
  persistence state, and reset-to-engine-defaults.

State ownership rules:

- UI owns transient widget layout only.
- Persisted DemoShell settings store requested state only.
- `DiagnosticsService` owns effective state, clamps it through build/runtime
  capabilities, and reports the reason for any rejected or downgraded request.
- The panel must display requested versus effective state when they differ. This
  prevents ambiguity between a UI bug, a persistence bug, and an engine
  capability/configuration decision.
- Domain services must not depend on DemoShell or ImGui. They report facts to
  the service/ledger; the panel reads snapshots.

The panel is a compact operational tool, not an editor and not a trace viewer.

## 12. GPU Debug Primitives

GPU debug primitives are a narrow spatial-debug tool, not a logging system.
They exist for cases where the important fact is geometric: cascade bounds,
cluster/frustum bounds, local-fog volume extents, froxel slice planes, selected
pixel rays, culling cells, or similar spatial relationships that are hard to
understand from a ledger, a timeline, or a capture manifest alone.

They are not core M05A unless a required M05A proof cannot be understood without
shader-emitted visual geometry.

If a later slice enables them, the scope is deliberately small:

- fixed-capacity per-frame primitive buffer with overflow count
- explicit runtime feature gate plus explicit producer/debug-mode opt-in
- shader include with a small set of helpers such as debug line/cross/box emit
- clear and draw integration in a named overlay pass
- optional depth-tested versus always-on-top display mode
- EngineShaderCatalog and ShaderBake/catalog validation for any shader changes
- disabled-path proof that producers compile out or no-op cleanly
- enabled-path runtime or RenderDoc proof that primitives appear and overflow is
  bounded

Until then the status remains deferred. M05A can reference the existing debug
shader asset inventory, but it must not claim a debug-primitive runtime.

## 13. Contract Failures And Minimal Runtime Issues

Diagnostics issues are for bounded, developer-facing state that helps explain a
frame. They are not a substitute for assertions, aborts, or readable code.

Policy:

- If the engine detects a contract breach or invariant violation that makes
  further execution invalid, use the existing assert/abort/fail-fast mechanism.
  Do not hide it behind an issue code.
- If a requested diagnostics/debug feature is unavailable, downgraded, or
  missing an expected product in a recoverable way, emit a small structured
  issue so the panel and manifest can explain the frame.
- Progress and data logging belong in pass/product/timeline records or normal
  focused logs, not in a broad issue taxonomy.
- Issues must be bounded and deduplicated per frame or per state transition.
  They should never become a high-volume stream.

Initial issue vocabulary should be minimal and tied to implemented behavior:

| Code | Meaning |
| --- | --- |
| `diag.feature-unavailable` | A requested diagnostics feature was clamped off by build/runtime capability. |
| `diag.manifest-write-failed` | A requested capture manifest export failed. |
| `timeline.query-overflow` | GPU timestamp capacity was exceeded. |
| `timeline.incomplete-scope` | A GPU timing scope could not produce a complete duration. |
| `debug-mode.unsupported` | A requested debug mode is not supported by current renderer/capabilities. |
| `debug-mode.missing-product` | A recoverable debug mode request lacks an input product. |
| `product.stale` | A published product exists but is not valid for the current frame. |

Adding a new code requires a concrete consumer in the panel, manifest, or proof
tooling. Otherwise, use assertions for bugs and normal logs for local progress.

## 14. Validation Requirements

M05A validation must include:

- focused build for diagnostics code and affected Vortex targets
- unit tests for registry coverage, canonical names, helper consistency, and
  duplicate detection
- unit tests for frame ledger append/snapshot/reset behavior
- unit tests for issue recording and manifest schema output
- existing `GpuTimelineProfiler` tests plus service facade tests
- panel registry tests if panels are wired
- `git diff --check`

Runtime or capture proof is required for any runtime-visible behavior changed by
M05A. ShaderBake/catalog validation is required for shader or
`EngineShaderCatalog.h` changes. D3D12 debug-layer audit is required for new GPU
resources or passes.

## 15. M05A Minimal Closure

M05A can close with GPU debug primitives deferred. It cannot close unless:

1. `DiagnosticsService` code exists and is wired into Vortex.
2. `ShaderDebugModeRegistry` is authoritative and tested.
3. `DiagnosticsFrameLedger` records passes, products, issues, feature state, and
   active debug mode.
4. Existing GPU timeline functionality is preserved and service-facing.
5. Capture manifest export exists and has tests.
6. DemoShell diagnostics panel registry is implemented, or panel rendering is
   explicitly deferred with an issue/status record and no false claim.
7. Docs/status contain exact validation evidence.

## 16. Replan Triggers

Update this LLD and the M05A plan before continuing implementation if:

- a current profiler, ImGui, or debug-mode path cannot be reused safely
- DiagnosticsService would need to own frame scheduling or scene texture
  allocation
- external RenderDoc/CDB orchestration would need to enter runtime code
- GPU debug primitives become required for the M05A exit gate
- UE5.7 grounding reveals a diagnostics pattern that changes the minimal
  product boundary
