# Unified GPU Profiling Design for Oxygen.Engine

Date: April 10, 2026
Status: `proposed`
Owner: Renderer + Graphics
Scope: Engine-wide backend-agnostic GPU profiling with coarse engine telemetry and fine-grained Tracy tracing

Cross-references:

- `design/gpu_timestamp_system_d3d12.md`
- `src/Oxygen/Base/ObserverPtr.h`
- `src/Oxygen/Graphics/Common/ProfileScope.h`
- `src/Oxygen/Graphics/Common/GpuEventScope.h`
- `src/Oxygen/Graphics/Common/CommandRecorder.h`
- `src/Oxygen/Graphics/Common/CommandRecorder.cpp`
- `src/Oxygen/Renderer/Internal/GpuTimelineProfiler.h`
- `src/Oxygen/Renderer/Internal/GpuTimelineProfiler.cpp`
- `src/Oxygen/Graphics/Direct3D12/CommandRecorder.cpp`

## 1. Purpose

Define a single, backend-agnostic GPU profiling model for Oxygen that:

1. Keeps client instrumentation simple and uniform.
2. Preserves the built-in GPU timeline as a stable, low-cardinality telemetry system for engine debug tools.
3. Enables substantially finer-grained GPU tracing in Tracy for optimization work.
4. Avoids proliferating multiple caller-visible profiling APIs.
5. Keeps backend-specific Tracy integration hidden behind the graphics backend.

This document is a design specification only. No implementation is claimed here.

## 2. Problem Statement

Oxygen already has a backend-agnostic GPU timing path centered on `GpuEventScope`, `CommandRecorder`, and `GpuTimelineProfiler`.
That path is well-suited for curated per-frame telemetry, exports, and engine UI.

It is not, however, the right system for dense optimization instrumentation:

1. The built-in timeline has a deliberate per-frame scope budget.
2. Its output is intended for stable engine tooling, not arbitrary micro-zones.
3. Fine-grained optimization work benefits from Tracy's richer trace UI and much denser scope coverage.

The design challenge is to reconcile both needs without creating two independent instrumentation styles for client code.

## 3. Design Goals

### 3.1 Goals

1. One public GPU scope API for renderer and engine client code.
2. Two profiling consumers with distinct responsibilities:
   - built-in telemetry timeline,
   - Tracy GPU tracing.
3. Stable correlation between top-level engine scopes and fine-grained Tracy scopes.
4. Optional scope color and semantic grouping without making either mandatory.
5. Clean backend separation: renderer code does not reference D3D12, Vulkan, or Tracy-specific profiling APIs.

### 3.2 Non-Goals

1. Do not turn the built-in timeline profiler into a micro-profiler.
2. Do not expose Tracy backend types or Tracy macros to renderer/pass code.
3. Do not require every scope to provide category or color metadata.
4. Do not encode implementation details such as timestamp allocation in the public API contract.
5. Do not change or replace existing CPU-side Tracy instrumentation (`ZoneScopedN`, etc.); CPU zones are managed separately and are not in scope.

## 4. Existing Oxygen Anchors

The design deliberately builds on existing abstractions:

1. `GpuEventScope` is already the engine-wide RAII GPU instrumentation helper.
2. `CommandRecorder::BeginProfileScope(...)` / `EndProfileScope(...)` already form the public scope entry point.
3. `GpuTimelineProfiler` already owns hierarchy tracking, query-slot budgeting, resolve submission, and frame publication.
4. D3D12 debug event and marker emission already exists in `Graphics/Direct3D12/CommandRecorder.cpp`.
5. Tracy GPU integration is backend-specific by design and must remain hidden behind backend code.

These anchors are sufficient to support a unified design. The main gap is that the current public options type exposes `timestamp_enabled`, which describes one implementation path instead of describing caller intent.

## 5. Canonical Concepts

This design uses one term per concept:

1. `scope`: one logical GPU profiling region.
2. `telemetry scope`: a stable, budgeted scope visible in engine tools and Tracy.
3. `diagnostic scope`: a fine-grained scope intended for Tracy and excluded from the built-in timeline.
4. `category`: an optional semantic grouping used for default presentation and filtering.
5. `profile color`: an optional packed display color for tools that support colored ranges.
6. `collector`: an internal profiling consumer attached behind the public scope API.

## 6. High-Level Model

The system has one public instrumentation path and multiple hidden collectors.

Client code creates GPU scopes through a single engine API.
That single API routes the logical scope to an internal coordinator.
The coordinator dispatches to any enabled collectors.

Collector responsibilities are intentionally split:

1. Built-in timeline collector:
   - coarse engine telemetry,
   - stable scope set,
   - per-frame budgets,
   - engine UI and export.
2. Tracy collector:
   - fine-grained optimization tracing,
   - dense nested scopes,
   - backend-specific GPU zone integration.
3. Native debug-marker collector:
   - PIX / RenderDoc / backend marker events where supported.

## 7. Public API Design

### 7.1 Public Scope Description

The public API should be intent-based rather than implementation-based.

Proposed public types:

```cpp
enum class GpuProfileGranularity : uint8_t {
  kTelemetry,
  kDiagnostic,
};

enum class GpuProfileCategory : uint8_t {
  kGeneral,
  kPass,
  kCompute,
  kRaster,
  kUpload,
  kSynchronization,
};

struct ProfileColor {
  uint32_t argb { 0 }; // 0 means unspecified

  static constexpr auto Argb(
    uint8_t a, uint8_t r, uint8_t g, uint8_t b) -> ProfileColor;

  static constexpr auto Rgb(
    uint8_t r, uint8_t g, uint8_t b) -> ProfileColor;

  [[nodiscard]] constexpr auto IsSpecified() const -> bool;

  // Returns packed RGB24 (0x00RRGGBB) for tools that use 24-bit color (e.g., Tracy GPU zones).
  // Alpha is stripped. Returns 0 if unspecified.
  [[nodiscard]] constexpr auto Rgb24() const -> uint32_t;
};

struct GpuProfileScopeDesc {
  observer_ptr<const char> literal_name {};  // non-owning; must be a string literal
  GpuProfileGranularity granularity { GpuProfileGranularity::kTelemetry };
  GpuProfileCategory category { GpuProfileCategory::kGeneral };
  ProfileColor color {};
};
```

> **Static lifetime contract:** The field is named `literal_name` to encode the constraint at the call site.
> `observer_ptr<const char>` further expresses non-ownership in the type.
> Always use `make_observer("...")` with a string literal. Never pass a `std::string` temporary,
> a `string_view` of a local buffer, or a stack-allocated `char[]` — the Tracy GPU backend
> dereferences the raw pointer asynchronously on a future frame, well after
> `BeginProfileScope` returns.

### 7.2 Scope Token

The token remains opaque to callers:

```cpp
struct GpuProfileScopeToken {
  uint32_t scope_id { 0 };
  uint16_t stream_id { 0 };
  uint8_t flags { 0 };
};
```

The token identifies a logical scope record owned by the coordinator, not by the caller.
The coordinator maintains a fixed-capacity scope-stack per command recorder and uses `scope_id` to index into that stack.
Each record in the stack stores one sub-token per active collector (for example, a query-slot index for the built-in timeline collector and a zone-handle index for the Tracy collector).
This per-collector state never appears in `GpuProfileScopeToken` itself; the token is solely an opaque handle that `EndProfileScope` uses to locate the correct coordinator record.

The token must not expose backend-specific Tracy handles or D3D12/Vulkan implementation details.

> **Scope-stack depth:** The coordinator's scope-stack has a bounded capacity. Exceeding it at runtime is a programming error. The capacity must be at least as large as the maximum permitted `kTelemetry` nesting depth enforced by the built-in timeline budget.

### 7.3 Single Client-Facing Helper

`GpuEventScope` remains the single engine-wide RAII helper.

Client code should look like this:

```cpp
GpuEventScope pass_scope(recorder, {
  .literal_name = make_observer("VSM.StaticDynamicMerge"),
  .granularity = GpuProfileGranularity::kTelemetry,
  .category = GpuProfileCategory::kPass,
});

GpuEventScope detail_scope(recorder, {
  .literal_name = make_observer("VSM.MergePage"),
  .granularity = GpuProfileGranularity::kDiagnostic,
  .category = GpuProfileCategory::kCompute,
});
```

The key point is that client code describes intent, not mechanisms such as timestamp allocation.

`GpuEventScope` class skeleton:

```cpp
class GpuEventScope {
public:
  explicit GpuEventScope(CommandRecorder& recorder,
    const GpuProfileScopeDesc& desc);
  ~GpuEventScope();

  OXYGEN_MAKE_NON_COPYABLE(GpuEventScope)
  OXYGEN_MAKE_NON_MOVABLE(GpuEventScope)

private:
  CommandRecorder* recorder_;
  GpuProfileScopeToken token_ {};
};
```

`GpuProfileScopeDesc` may be constructed as a temporary aggregate at the call site and does not need to outlive the `BeginProfileScope` call. The field name `literal_name` and the type `observer_ptr<const char>` together encode the full contract without requiring the reader to consult any documentation: non-owning, static-duration. The sole remaining constraint not expressible in C++ types is that the pointer must point to static-duration data — fulfilled automatically when using string literals with `make_observer("...")`.

## 8. Granularity Semantics

### 8.1 Telemetry

`kTelemetry` is for:

1. pass boundaries,
2. major renderer phases,
3. stable production-relevant budgets,
4. scopes that must appear in engine debug tools,
5. scopes that should also appear in Tracy for correlation.

Rules:

1. Telemetry scopes are eligible for built-in timestamp collection.
2. Telemetry scopes are eligible for Tracy collection.
3. Telemetry scopes should remain curated and relatively stable over time.

### 8.2 Diagnostic

`kDiagnostic` is for:

1. hot loops,
2. page-level or tile-level work,
3. narrow optimization probes,
4. temporary or evolving instrumentation.

Rules:

1. Diagnostic scopes are excluded from the built-in timeline collector.
2. Diagnostic scopes are eligible for Tracy collection.
3. Diagnostic scopes may be far denser than telemetry scopes.

This split reconciles the engine's need for stable telemetry with Tracy's role as the fine-grained optimization microscope.

## 9. Semantic Grouping

`GpuProfileCategory` is optional semantic metadata.
It is not a routing switch and does not affect whether a scope is telemetry or diagnostic.

Its responsibilities are limited to:

1. default presentation,
2. visual consistency,
3. filtering or grouping in future tooling,
4. making recurring captures easier to scan.

Initial categories:

1. `kGeneral`
2. `kPass`
3. `kCompute`
4. `kRaster`
5. `kUpload`
6. `kSynchronization`

This enum should remain intentionally small.
It is not meant to become a taxonomy of every renderer subsystem.

## 10. Profile Color

### 10.1 Type Choice

Use a dedicated profiling color type:

1. `ProfileColor` is a packed `uint32_t ARGB` value.
2. `0` means unspecified.
3. It is distinct from `graphics::Color`, which is a render-data float RGBA type and not appropriate for profiling tool metadata.

This choice aligns with profiling/debug tooling better than a float color structure:

1. Tracy-style color metadata is naturally integer-oriented.
2. PIX-style marker APIs use packed integer color values.
3. A packed color is cheap to store and backend-neutral.

### 10.2 Color Policy

Color is optional and presentation-only.

Resolution policy:

1. If a scope specifies an explicit `ProfileColor`, use it.
2. Otherwise, derive a default color from `GpuProfileCategory` if one exists.
3. If neither applies, leave color unspecified.
4. If a backend/tool does not support scope color, ignore it.

Recommended defaults:

1. `kPass`: blue
2. `kCompute`: green
3. `kRaster`: orange
4. `kUpload`: violet
5. `kSynchronization`: red
6. `kGeneral`: unspecified

Example mapping:

```cpp
[[nodiscard]] constexpr auto DefaultProfileColor(
  GpuProfileCategory category) -> ProfileColor
{
  switch (category) {
    case GpuProfileCategory::kPass:
      return ProfileColor::Rgb(0x4D, 0x96, 0xFF);
    case GpuProfileCategory::kCompute:
      return ProfileColor::Rgb(0x00, 0xB8, 0x5C);
    case GpuProfileCategory::kRaster:
      return ProfileColor::Rgb(0xFF, 0x8C, 0x42);
    case GpuProfileCategory::kUpload:
      return ProfileColor::Rgb(0x8A, 0x63, 0xD2);
    case GpuProfileCategory::kSynchronization:
      return ProfileColor::Rgb(0xE0, 0x3E, 0x36);
    default:
      return {};
  }
}
```

Color is deliberately not represented as a public enum. A color enum would either be too generic to be useful or too specific to remain stable.

## 11. Internal Collector Architecture

### 11.1 Coordinator

`CommandRecorder` remains the sole public entry point.
Internally, it forwards scopes to a profiling coordinator.

The coordinator owns collector fan-out and keeps client code unaware of how many collectors are active.

Internal collector contract:

```cpp
class IGpuProfileCollector {
public:
  virtual ~IGpuProfileCollector() = default;

  [[nodiscard]] virtual auto BeginScope(
    CommandRecorder& recorder,
    const GpuProfileScopeDesc& desc) -> GpuProfileScopeToken = 0;

  virtual auto EndScope(
    CommandRecorder& recorder,
    const GpuProfileScopeToken& token) -> void = 0;
};
```

The coordinator's `BeginScope` dispatch applies granularity semantics via an explicit conditional check:

```cpp
// Coordinator dispatch — illustrative pseudocode only
auto Coordinator::BeginScope(CommandRecorder& rec, const GpuProfileScopeDesc& desc)
  -> GpuProfileScopeToken
{
  const auto slot = scope_stack_.Allocate();

  // Native debug markers: always emitted (subject to backend support)
  native_markers_.BeginScope(rec, desc);

  // Built-in timeline: telemetry scopes only — this is the exclusion gate
  if (desc.granularity == GpuProfileGranularity::kTelemetry && timeline_) {
    scope_stack_[slot].timeline_sub = timeline_->BeginScope(rec, desc);
  }

  // Tracy: receives all scopes
  if (tracy_) {
    scope_stack_[slot].tracy_sub = tracy_->BeginScope(rec, desc);
  }

  return GpuProfileScopeToken { .scope_id = slot };
}
```

The single `if (desc.granularity == kTelemetry)` check is the complete mechanism by which diagnostic scopes are excluded from the built-in timeline.

### 11.2 Routing Rules

The coordinator applies these routing rules:

1. Native debug events/markers:
   - eligible for both `kTelemetry` and `kDiagnostic`, subject to backend support and runtime policy.
2. Built-in timeline collector:
   - eligible only for `kTelemetry`.
3. Tracy collector:
   - eligible for both `kTelemetry` and `kDiagnostic`.

This yields the intended behavior:

1. Engine tools remain coarse and stable.
2. Tracy can include both top-level and micro-level scopes.
3. Top-level telemetry scopes appear in both systems for correlation.

### 11.3 Coordinator Lifecycle and Collector Registration

The coordinator is an internal component of `CommandRecorder`, not a separate singleton or global subsystem.

Lifecycle rules:

1. The coordinator is constructed as part of `CommandRecorder` at initialization time.
2. Collectors are injected at `CommandRecorder` construction by the backend or device abstraction that owns the Tracy context and the timeline profiler instance.
3. Collectors that hold GPU resources (e.g., the Tracy D3D12 context) are owned by the backend and may outlive individual `CommandRecorder` instances if reused across frames.
4. The set of active collectors is fixed at `CommandRecorder` construction time and does not change during its lifetime.

Suggested injection interface:

```cpp
// Called by the backend when constructing a CommandRecorder
void CommandRecorder::SetProfileCollectors(
  GpuTimelineProfiler* timeline,   // nullptr if telemetry is disabled
  IGpuProfileCollector* tracy);    // nullptr if Tracy is not enabled
```

### 11.4 Thread Safety

`CommandRecorder` is a per-command-list, single-threaded object by design.
Scope operations on a single recorder require no synchronization.

Shared collector instances accessed concurrently from multiple recorders (e.g., a Tracy context serving both a direct queue recorder and a compute queue recorder) must implement their own internal synchronization.

The built-in `GpuTimelineProfiler` is per-recorder by design and does not require external synchronization.

## 12. Built-In Timeline Collector Responsibilities

`GpuTimelineProfiler` should become a telemetry collector rather than the public profiling abstraction.

Its responsibilities remain:

1. per-frame query-slot budgeting,
2. scope nesting and hierarchy bookkeeping,
3. resolve submission,
4. resolved frame publication,
5. diagnostics and export.

Its responsibilities do not expand to include arbitrary fine-grained diagnostic scope density.

Telemetry collector rules:

1. It records only `kTelemetry` scopes.
2. It enforces the existing per-frame budget only on telemetry scopes.
3. It remains suitable for engine UI, regression tracking, and structured exports.

## 13. Tracy Collector Responsibilities

The Tracy collector is responsible for fine-grained trace integration.

Rules:

1. It may record both `kTelemetry` and `kDiagnostic` scopes.
2. It must remain backend-specific internally.
3. Renderer and pass code must not call Tracy backend macros directly.

Backend integration examples:

1. D3D12 uses `TracyD3D12...`
2. Vulkan uses `TracyVk...`
3. Other backends use their own Tracy integration family if supported.

The Tracy collector may use category and color metadata when supported by the backend API or Tracy UI conventions.

### 13.1 Tracy D3D12 Context Lifecycle

The D3D12 Tracy collector requires one `TracyD3D12Context` per GPU command queue.

Lifecycle rules:

1. **Creation:** call `TracyD3D12Context(device, queue)` immediately after the `ID3D12CommandQueue` is created. Store the context in the backend device or queue wrapper, not in `CommandRecorder`.
2. **Context naming:** call `TracyD3D12ContextName(ctx, name, len)` after creation to assign a human-readable label (e.g., `"Direct"`, `"Compute"`) visible in the Tracy GPU timeline view.
3. **Per-frame collection:** call `TracyD3D12Collect(ctx)` once per frame, after the frame's GPU work has completed (after the frame fence has been signaled). Never call it from a command-list recording thread.
4. **Destruction:** call `TracyD3D12Destroy(ctx)` when the queue is destroyed or the device shuts down, before releasing D3D12 device resources.

The Tracy collector receives the context pointer at construction. All `TracyD3D12Zone*` calls inside `BeginScope`/`EndScope` use that context pointer together with the `ID3D12GraphicsCommandList*` carried by the `CommandRecorder`.

### 13.2 Profile Color for Tracy

Tracy GPU zone macros (`TracyD3D12ZoneC`) accept a 24-bit `uint32_t` RGB value (`0x00RRGGBB`), not the 32-bit ARGB stored in `ProfileColor`.

The Tracy collector must extract the RGB component using `Rgb24()` before passing it to Tracy:

```cpp
const ProfileColor effective = desc.color.IsSpecified()
  ? desc.color
  : DefaultProfileColor(desc.category);

if (effective.IsSpecified()) {
  // Use TracyD3D12ZoneC(ctx, cmdList, desc.literal_name.get(), effective.Rgb24())
}
```

### 13.3 CPU Tracy Zones Are Out of Scope

CPU-side Tracy instrumentation (`ZoneScopedN`, `ZoneValue`, `ZoneText`) is independent of this GPU profiling design.

CPU zones instrument CPU thread activity. GPU zones (via this design) instrument GPU command execution. Both coexist unchanged; this design does not migrate or replace CPU Tracy zones.

## 14. Command Recorder Contract

The public recorder API remains singular.

Desired shape:

```cpp
auto CommandRecorder::BeginProfileScope(const GpuProfileScopeDesc& desc)
  -> GpuProfileScopeToken;

auto CommandRecorder::EndProfileScope(const GpuProfileScopeToken& token)
  -> void;
```

Behavior:

1. `BeginProfileScope` opens one logical GPU scope.
2. The recorder emits a native debug event if enabled.
3. The recorder forwards the scope to the internal coordinator.
4. `EndProfileScope` closes the logical scope and lets the coordinator end any active collector-specific state.

This keeps the engine-wide contract small and stable.

## 15. Migration From Current API

The current `GpuEventScopeOptions { .timestamp_enabled = ... }` model should be treated as transitional.

Problems with the current model:

1. It exposes one implementation detail in the public API.
2. It makes callers think in terms of timestamp writes rather than profiling intent.
3. It does not express the critical distinction between stable telemetry and dense diagnostics.

Migration rule:

1. Replace timestamp-oriented public options with `GpuProfileScopeDesc`.
2. Move timestamp eligibility behind the telemetry collector.
3. Keep `GpuEventScope` as the single call-site primitive.

### 15.1 Affected Files

| File | Change |
|------|--------|
| `src/Oxygen/Graphics/Common/ProfileScope.h` | Replace `GpuEventScopeOptions`, `IGpuProfileScopeHandler` with `GpuProfileScopeDesc`, `GpuProfileGranularity`, `GpuProfileCategory`, `ProfileColor`, `IGpuProfileCollector` |
| `src/Oxygen/Graphics/Common/GpuEventScope.h` | Update constructor from `(recorder, name, options)` to `(recorder, desc)` |
| `src/Oxygen/Graphics/Common/CommandRecorder.h` | Update `BeginProfileScope` signature to accept `const GpuProfileScopeDesc&` |
| `src/Oxygen/Graphics/Common/CommandRecorder.cpp` | Implement coordinator dispatch with granularity routing |
| `src/Oxygen/Renderer/Internal/GpuTimelineProfiler.h/.cpp` | Implement `IGpuProfileCollector`; reject `kDiagnostic` scopes |
| `src/Oxygen/Graphics/Direct3D12/CommandRecorder.cpp` | Implement D3D12 Tracy collector; manage `TracyD3D12Context` usage |
| All renderer/pass call sites using `GpuEventScope` | Migrate from options-based to desc-based constructor |

### 15.2 Call Site Migration Pattern

Before (current API):

```cpp
GpuEventScope scope(recorder, "VSM.StaticDynamicMerge",
  { .timestamp_enabled = true });
```

After (new API):

```cpp
GpuEventScope scope(recorder, {
  .literal_name = make_observer("VSM.StaticDynamicMerge"),
  .granularity = GpuProfileGranularity::kTelemetry,
  .category = GpuProfileCategory::kPass,
});
```

Granularity mapping:

- `timestamp_enabled = true` → `kTelemetry`
- `timestamp_enabled = false` (debug-marker-only scope) → `kDiagnostic` if dense; `kTelemetry` if pass-level — choose based on scope intent, not on whether a timestamp was previously requested
- New scopes without a prior equivalent: pass boundaries and major phases → `kTelemetry`; dense inner loops → `kDiagnostic`

`category` is a new field without a prior equivalent. Assign the most applicable category or default to `kGeneral`. Category assignment must be reviewed at migration time, not deferred indefinitely.

## 16. Usage Guidance

### 16.1 When to Use `kTelemetry`

Use `kTelemetry` for:

1. pass entry/exit,
2. major subpass boundaries,
3. frame phase transitions,
4. stable long-lived budgets used by engine tooling.

### 16.2 When to Use `kDiagnostic`

Use `kDiagnostic` for:

1. dense inner loops,
2. page-level work,
3. narrow optimization experiments,
4. temporary performance probes.

### 16.3 Category and Color Guidance

1. Prefer category-only annotations first.
2. Use explicit color only when it adds durable value to captures.
3. Do not rely on color for correctness or routing.
4. Avoid making every scope manually choose a color.

## 17. Example

```cpp
GpuEventScope pass_scope(recorder, {
  .literal_name = make_observer("VSM.StaticDynamicMerge"),
  .granularity = GpuProfileGranularity::kTelemetry,
  .category = GpuProfileCategory::kPass,
});

for (uint32_t logical_page = 0; logical_page < logical_page_count; ++logical_page) {
  GpuEventScope page_scope(recorder, {
    .literal_name = make_observer("VSM.MergePage"),
    .granularity = GpuProfileGranularity::kDiagnostic,
    .category = GpuProfileCategory::kCompute,
  });

  MergeOnePage(recorder, logical_page);
}
```

Expected result:

1. Engine timeline shows `VSM.StaticDynamicMerge`.
2. Tracy shows `VSM.StaticDynamicMerge` and nested `VSM.MergePage` scopes.
3. The built-in telemetry collector remains protected from per-page scope explosion.

## 18. Risks and Mitigations

### Risk 1: Category Proliferation

If categories become too detailed, the API will grow noisy and unstable.

Mitigation:

1. Keep the enum intentionally small.
2. Add categories only for repeated, cross-system value.

### Risk 2: Color Becoming Mandatory Noise

If every caller feels required to specify color, the API becomes cluttered.

Mitigation:

1. Default to category-derived colors.
2. Leave explicit color optional.

### Risk 3: Tracy Becomes a Parallel Caller API

If pass code starts using Tracy macros directly, the unified model collapses.

Mitigation:

1. Keep Tracy behind backend collectors.
2. Preserve one public scope API.

### Risk 4: Telemetry Density Creeps Up

If too many scopes are marked `kTelemetry`, the built-in timeline loses clarity and budget headroom.

Mitigation:

1. Treat `kTelemetry` as curated.
2. Use `kDiagnostic` for optimization density.

## 19. Validation Plan

Design validation for this feature should include:

1. API review confirming that client code uses one scope API only.
2. Backend review confirming that Tracy integration remains backend-specific.
3. Runtime validation showing that telemetry scopes appear in both engine tools and Tracy.
4. Runtime validation showing that diagnostic scopes appear in Tracy but not in the built-in timeline.
5. Verification that existing telemetry exports remain stable under added diagnostic tracing.

No implementation or runtime validation is claimed by this document.

## 20. Final Design Summary

The final design is:

1. one public GPU scope API,
2. one internal coordinator,
3. one coarse telemetry collector,
4. one fine-grained Tracy collector,
5. two granularity levels,
6. one small semantic category enum,
7. one optional packed `ProfileColor` type.

This preserves simple client code, keeps the built-in timeline intentionally coarse, allows dense optimization tracing in Tracy, and avoids proliferating alternate instrumentation styles.
