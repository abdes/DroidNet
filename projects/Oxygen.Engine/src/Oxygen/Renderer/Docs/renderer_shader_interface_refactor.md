# Renderer-Shader Interface Refactor Plan

Status: `phase_1_completed` / `phase_2_in_progress`

Purpose: define the refactor required to make renderer-shader interfaces clean,
explicit, modular, and future-ready before any new shadow-system work lands.

Scope: renderer/shader interface architecture only. This document is a
prerequisite cleanup plan for future work such as shadows, virtual shadow maps,
ray tracing, post-processing growth, and additional lighting systems.

Backward compatibility is not required and is not desired. This refactor is
allowed to rename, remove, and replace current contracts aggressively.

Execution rule: no ocean boiling, but no shortcuts. The final architecture must
be defined clearly up front, then implemented in the smallest complete slices
that establish real ownership boundaries and remove real confusion. Avoid both
speculative framework work and temporary transitional architecture that is known
to be wrong.

Cross-references:

- [shader-system.md](shader-system.md)
- [bindless_conventions.md](bindless_conventions.md)
- [lighting_overview.md](lighting_overview.md)
- [shadows.md](shadows.md)
- [passes/data_flow.md](passes/data_flow.md)
- [passes/design-overview.md](passes/design-overview.md)

## 0. Phase Status

Legend: `[ ]` pending | `[~]` in progress | `[x]` completed

- [x] Phase 0: Freeze the authoritative interface inventory
- [x] Phase 1: Re-establish one authoritative root ABI
- [~] Phase 2: Replace `SceneConstants` with `ViewConstants`
- [ ] Phase 3: Introduce `ViewFrameBindings` and re-home system routing
- [ ] Phase 4: Extract lighting interfaces cleanly
- [ ] Phase 5: Extract environment interfaces cleanly
- [ ] Phase 6: Clean draw/material contracts
- [ ] Phase 7: Introduce shadow-ready and ray-tracing-ready system slots
- [ ] Phase 8: Documentation and validation hardening

### 0.1 Phase 0 Exit Record

Phase 0 is complete as a documentation/decision milestone.

Completed deliverables:

- The refactor plan exists and is approved for execution direction.
- Final top-level names are locked:
  - `ViewConstants`
  - `RootConstants`
  - `ViewFrameBindings`
  - `DrawFrameBindings`
  - `LightingFrameBindings`
  - `EnvironmentFrameBindings`
  - `ViewColorData`
  - `ShadowFrameBindings`
  - `PostProcessFrameBindings`
  - `DebugFrameBindings`
  - `HistoryFrameBindings`
  - `RayTracingFrameBindings`
  - `MaterialShadingConstants`
- The target root ABI is frozen for implementation:
  - bindless resource table
  - sampler table
  - `ViewConstants` at `b1`
  - `RootConstants` at `b2`
- The execution discipline is locked:
  - no ocean boiling
  - no shortcuts
  - smallest complete vertical slices
  - strangler migration where legacy removal is required

Authoritative status after Phase 0:

- This document is the authoritative design authority for the target
  renderer-shader interface architecture.
- Current code remains the authority for current-state implementation facts.
- Existing documents such as `shader-system.md`, `bindless_conventions.md`,
  and environment-related ABI notes remain useful as current-state references,
  but are non-authoritative for the target architecture until rewritten.

## 1. Executive Summary

The current renderer-shader interface layer has the right low-level pieces
(bindless access, generated root-signature metadata, root constants, pass
constants for compute, structured GPU payloads), but the system boundaries are
not clean:

- `SceneConstants` has drifted into a routing bucket for multiple unrelated
  systems.
- `EnvironmentDynamicData` is carrying lighting, light-culling, and sun state,
  not just environment state.
- `DirectionalLightShadows` is a technique-shaped payload, not a renderer
  shadow-system contract.
- Several documents and shader ABI expectations no longer match the actual
  code.
- Some renderer-shader contracts mix system concerns into the wrong ABI layer.

The refactor direction is:

1. Keep the root ABI minimal and system-neutral.
2. Remove the dedicated `EnvironmentDynamicData` root CBV entirely.
3. Rename and shrink `SceneConstants` into a true per-view invariant contract.
4. Route all evolving renderer systems through explicit bindless frame-binding
   payloads.
5. Give lighting, environment, shadows, draw submission, post-process, and
   debug their own published frame contracts, with shared `ViewColorData` for
   ubiquitous color/presentation state such as exposure.
6. Replace technique-shaped payloads such as `DirectionalLightShadows` with
   system-shaped contracts that can support multiple implementations.

This plan intentionally front-loads interface cleanup and documentation
authority before new feature implementation.

## 2. Current-State Findings

The findings below are grounded in the current codebase, not prior intent.

### 2.1 Root ABI and documentation drift

- The generated root signature currently defines five root parameters in
  `src/Oxygen/Core/Bindless/Generated.RootSignature.h`:
  - bindless SRV table
  - sampler table
  - `SceneConstants` at `b1`
  - `RootConstants` at `b2`
  - `EnvironmentDynamicData` at `b3`
- `bindless_conventions.md` still documents the older four-parameter layout and
  still refers to `kDrawIndex` instead of `kRootConstants`.
- The shader system document correctly describes the intended small/stable
  `SceneConstants` boundary, but its concrete `SceneConstants` ABI section no
  longer matches the actual C++/HLSL layout.

Impact:

- The code has one ABI, several docs describe older ABIs, and future work is at
  risk of building on the wrong authority.

### 2.2 `SceneConstants` is no longer minimal

Current `SceneConstants` in `src/Oxygen/Renderer/Types/SceneConstants.h`
contains:

- view/projection/camera/frame identity/time
- exposure
- draw/material/transform routing slots
- environment slot
- directional light slot
- directional shadow slot
- positional light slot
- instance-data slot
- debug slots

This is materially broader than the boundary rules already documented in
[shader-system.md](shader-system.md), which explicitly say `SceneConstants`
must not become a feature bucket and explicitly call out exposure, shadow
cascades, and clustered-grid data as things that should live elsewhere.

Impact:

- New systems are incentivized to add “just one more slot” to `SceneConstants`.
- The root CBV becomes the place where unrelated systems accumulate bindings.

### 2.3 `EnvironmentDynamicData` is misnamed and misowned

Current `EnvironmentDynamicData` in
`src/Oxygen/Renderer/Types/EnvironmentDynamicData.h` contains:

- `LightCullingConfig`
- atmosphere view/context data
- synthetic sun state

It is updated by `EnvironmentDynamicDataManager`, but the manager is also used
by:

- `LightCullingPass`
- `ShaderPass`
- `TransparentPass`
- atmosphere compute passes
- sky passes

This is not environment-only state. It is a mixed “hot view data” contract with
an environment name.

Impact:

- Lighting and light-culling data are published through the environment system.
- The environment system appears larger and more authoritative than it should
  be.
- Future shadow and ray-tracing interfaces would likely repeat the same mistake.

### 2.4 `DirectionalLightShadows` is technique-shaped

Current `DirectionalLightShadows` in
`src/Oxygen/Renderer/Types/DirectionalLightShadows.h` hardcodes:

- cascade count
- cascade distances
- cascade view-projection matrices

`LightManager` directly publishes that payload in
`src/Oxygen/Renderer/LightManager.cpp`, even though it is only valid for one
specific shadow implementation family (conventional cascaded shadow maps).

Impact:

- The light interface is already coupled to one future shadow implementation.
- A virtual-shadow-map path would either need a parallel payload or a breaking
  replacement later.

### 2.5 Per-system helpers are crossing boundaries

Examples in current HLSL:

- `EnvironmentHelpers.hlsli` loads environment static data, exposes exposure,
  and computes cluster indices.
- `ForwardDirectLighting.hlsli` includes environment headers and reads the
  cluster grid and designated sun via `EnvironmentDynamicData`.
- `ForwardMesh_PS.hlsl` reads environment, lighting, culling, exposure, and
  atmosphere through a mixed interface surface.

Impact:

- Helper includes are no longer organized by contract ownership.
- Lighting code depends on environment naming for data that is not
  environment-owned.

### 2.6 `MaterialConstants` mixes pass/system-specific data into the material ABI

Current `MaterialConstants` in
`src/Oxygen/Renderer/Types/MaterialConstants.h` includes:

- core PBR material data
- UV transform data
- grid spacing, thickness, and grid debug colors

That means the material ABI currently mixes general material state with
ground-grid/debug-specific state.

Impact:

- The material ABI is larger and less stable than necessary.
- Pass-specific/debug-specific configuration is living in the wrong layer.

### 2.7 Multiple source-of-truth issues already exist

Examples:

- `SceneConstants` HLSL is 256 bytes and shader reflection validates 256 bytes,
  while `shader-system.md` still documents the older 176-byte ABI.
- `EnvironmentDynamicData.hlsli` comments still say 208 bytes while the C++
  contract is 160 bytes and reflection validation expects 160.
- Environment docs still discuss `EnvironmentDynamicData.exposure` even though
  exposure is currently in `SceneConstants`.

Impact:

- The codebase already has correctness debt in its interface documentation.
- Any new system added before cleanup will likely increase divergence.

## 3. Refactor Goals

The refactor must achieve the following:

### 3.1 Architectural goals

- One authoritative renderer-shader interface model.
- Explicit ownership boundaries between systems.
- Stable root ABI with minimal surface area.
- Bindless-first publication for all extensible resources.
- No “miscellaneous dynamic data” buckets.
- No technique-shaped payloads in system-facing contracts when a system-shaped
  contract is required.

### 3.2 Engineering goals

- C++ and HLSL contracts are code-accurate and validated.
- Docs are updated to match the implemented ABI, not historical intent.
- Pass-local data moves to pass constants instead of root CBVs or global
  buckets.
- Future systems can be added by adding system bindings, not by mutating core
  root contracts casually.

### 3.3 Future-facing goals

- Shadow interfaces must support conventional maps, virtual shadow maps, and
  future hybrid/ray-traced producers without replacing the top-level system
  contract.
- Ray tracing should be able to publish its own frame bindings without
  reworking the entire root ABI.
- View history, jitter, exposure, and reprojection data must have a clean home
  that is not environment-owned.

## 4. Hard Decisions

These are explicit plan decisions.

### 4.1 Backward compatibility

- Not required.
- Not preserved.
- Old contracts may be removed outright once replacement contracts exist.

### 4.2 No parallel legacy path

- There will not be a “temporary compatibility layer” that keeps both old and
  new interface models alive for long.
- Each phase replaces old authority rather than layering on top of it.

### 4.3 Root ABI simplification is allowed

- The dedicated `b3` `EnvironmentDynamicData` root CBV will be removed, but in
  a sequenced migration rather than a single-step cut.
- A system-neutral root ABI is preferred over convenience bindings for one
  subsystem.

### 4.4 Contract-first before feature work

- No new shadow implementation work should begin until this interface refactor
  is sufficiently complete.
- Shadow design may continue at the documentation level, but shadow runtime
  implementation should not proceed on the current contract model.

### 4.5 Refactor execution discipline

- Implement the smallest complete vertical slice that lands on the final model.
- Do not introduce speculative abstraction layers unless one is immediately
  required by a concrete migrated system.
- Do not keep temporary “bridge” contracts longer than necessary.
- When migration is required, use a strangler approach:
  - new work goes to the final contract
  - legacy paths are reduced subsystem by subsystem
  - the legacy interface is deleted once empty
- If a boundary can be made clean now, make it clean now; do not defer a known
  ownership fix just to move faster.

## 5. Target Interface Architecture

## 5.1 Root ABI

The target root ABI is intentionally small and system-neutral.

| Root param | Index | Binding | Purpose |
| --- | ---: | --- | --- |
| Bindless resource table | 0 | `t0, space0` | Required for bindless compatibility / direct heap indexing |
| Sampler table | 1 | `s0, space0` | Sampler heap |
| `ViewConstants` | 2 | `b1, space0` | Per-view invariants and top-level routing |
| `RootConstants` | 3 | `b2, space0` | Draw index + pass constants index |

There is no dedicated system-specific root CBV beyond `ViewConstants`.

## 5.2 Core contracts

### `ViewConstants` (replaces `SceneConstants`)

`ViewConstants` is the only per-view root CBV.

It contains:

- frame sequence / frame slot / time
- view/projection and camera invariants
- one bindless slot to `ViewFrameBindings`

It does not contain:

- exposure
- environment slots
- lighting slots
- shadow slots
- debug slots
- culling data
- post-process data

### `RootConstants`

`RootConstants` remains fixed:

- `g_DrawIndex`
- `g_PassConstantsIndex`

All pass-local configuration and transient resource bindings flow through the
pass constants object addressed by `g_PassConstantsIndex`.

### `ViewFrameBindings`

`ViewFrameBindings` becomes the top-level system routing object for a view.

It is fetched bindlessly using the slot published by `ViewConstants`.

It contains slots for system-owned frame payloads, for example:

- `draw_frame_slot`
- `lighting_frame_slot`
- `environment_frame_slot`
- `view_color_frame_slot`
- `shadow_frame_slot`
- `post_process_frame_slot`
- `debug_frame_slot`
- `history_frame_slot`
- `ray_tracing_frame_slot`

Not every system must be implemented immediately. Inactive systems publish
invalid slots.

This is the critical architectural change: system routing moves out of
`SceneConstants` and into an explicit renderer-system frame-binding layer.

Decision:

- Use one top-level `ViewFrameBindings` object.
- Do not place multiple coarse system slots directly into `ViewConstants`.

Rationale:

- `ViewConstants` must remain pristine and system-neutral.
- Adding a few “acceptable” system slots directly into the root CBV would
  recreate the same expansion pressure that caused `SceneConstants` drift.
- The top-level routing object should initially remain minimal: system slots
  only. Additional global routing layers should not be invented unless a
  concrete migrated system needs them.

## 5.3 System frame contracts

Each renderer subsystem publishes its own frame payload.

### `DrawFrameBindings`

Owns draw pipeline bindings:

- draw metadata buffer slot
- world transforms slot
- normal matrices slot
- instance data slot
- material table slot
- optional per-draw extras slot

### `LightingFrameBindings`

Owns lighting system bindings:

- directional light records slot
- local light records slot
- designated/canonical sun info
- light culling view-data slot
- optional future light-channel / light-volume / RT-lighting support slots

Decision:

- Canonical sun is owned directly by `LightingFrameBindings`.

Rationale:

- The sun is fundamentally a light source.
- Environment systems may consume it, but lighting remains authoritative.
- For the first refactor wave, prefer a single lighting-owned frame payload
  over introducing a micro-shared payload for sun data alone.

### `EnvironmentFrameBindings`

Owns environment system bindings:

- environment static data slot
- atmosphere view-data slot
- sky/IBL derived resources
- optional cloud/view-weather slots

It does not own:

- light culling data
- canonical sun selection
- generic exposure/public presentation state

### `ShadowFrameBindings`

Owns shadow system bindings:

- shadow product list slot
- directional shadow metadata slot
- local shadow metadata slot
- backend/family metadata slot
- optional conventional resource slots
- optional virtual-shadow-map resource slots
- shadow debug/telemetry slots

This contract exists before shadow implementation so the interface boundary is
ready.

### `ViewColorData`

Owns ubiquitous per-view color/presentation state that is read across multiple
systems.

- resolved exposure
- future view-wide presentation scalars that are not environment-owned and not
  pass-local

Decision:

- Exposure belongs in `ViewColorData`, not in `ViewConstants` and not buried
  under post-process-only bindings.

Rationale:

- Exposure is produced by post-processing workflows but consumed broadly by
  lighting, environment, forward shading, and presentation passes.
- A small neutral contract keeps dependency directions clean.
- The first implementation wave should keep `ViewColorData` minimal and start
  with exposure only unless another field already has clear multi-system read
  pressure.

### `PostProcessFrameBindings`

Owns view presentation/compositing state:

- post-process settings/view data
- optional auto-exposure / tone-map / bloom resources

### `DebugFrameBindings`

Owns GPU debug resources:

- line buffer slot
- debug counter slot
- optional debug overlay / marker / telemetry slots

Implementation rule:

- use a shared per-view structured publisher primitive for payload publication
- do not introduce one bespoke C++ manager class per tiny frame payload

### `HistoryFrameBindings`

Owns previous-frame and reprojection-facing data:

- previous view/projection matrices
- jitter
- motion/reprojection helpers
- future temporal system data

This is the correct home for view-history growth. It must not be folded into
environment or `ViewConstants`.

## 5.4 Per-pass contracts

Every pass owns a typed pass-constants payload.

Examples:

- `LightCullingPassConstants`
- `ForwardOpaquePassConstants`
- `TransparentPassConstants`
- `SkyPassConstants`
- `ToneMapPassConstants`

Pass constants are responsible for:

- pass-local resource slots
- pass-local toggles
- pass-local dimensions and dispatch parameters
- explicit references to system frame slots when the pass wants to pin or
  override a specific view payload

Pass constants are not responsible for replacing system frame payloads.

## 5.5 Material and draw contracts

### `DrawMetadata`

`DrawMetadata` remains a fixed small header and continues to be the per-draw
selection record.

It remains generic and future-proof by indirection, not by direct expansion.

### `MaterialConstants`

The current material ABI should be split.

Target direction:

- `MaterialShadingConstants`: material/shading data only
- pass-specific/debug material payloads move out

Concretely:

- grid spacing/color/thickness data should not live in the core material ABI
- pass/debug-specific material behavior belongs in pass constants or a
  dedicated debug/pass payload

Decision:

- Use a broader unified `MaterialShadingConstants` in the first refactor, not a
  strict `PbrMaterialConstants` split.

Rationale:

- The immediate goal is to remove non-material payloads from the material ABI.
- A stricter PBR-only contract can follow later without blocking this cleanup.
- The first refactor should remove clearly non-material/debug payloads without
  trying to redesign every material taxonomy at once.

## 5.6 Contract vs helper separation

HLSL files must separate ABI definitions from convenience helpers.

Target layering:

- `Contracts/.../*.hlsli`: pure ABI definitions only
- `Bindings/.../*.hlsli`: loading helpers for those contracts
- `Systems/.../*.hlsli`: system behavior helpers and algorithms
- `Passes/.../*.hlsl`: shader entry points

Examples:

- `Contracts/Core/ViewConstants.hlsli`
- `Contracts/Core/RootConstants.hlsli`
- `Contracts/Core/ViewFrameBindings.hlsli`
- `Contracts/Lighting/LightingFrameBindings.hlsli`
- `Contracts/Environment/EnvironmentFrameBindings.hlsli`
- `Contracts/Shadow/ShadowFrameBindings.hlsli`
- `Bindings/Environment/EnvironmentBindings.hlsli`
- `Bindings/Lighting/LightingBindings.hlsli`

This removes the current pattern where “environment helpers” also provide
cluster indexing and exposure.

Decision:

- Perform semantic/interface cleanup first.
- Perform directory/file reorganization second.

Rationale:

- Contract changes and file moves in the same wave make history and rollback
  unnecessarily hard to reason about.

## 6. Explicit Replacements and Deletions

The table below captures the intended structural replacements.

| Current | Target | Action |
| --- | --- | --- |
| `SceneConstants` | `ViewConstants` | Rename and shrink |
| `EnvironmentDynamicData` | No direct replacement; split into system/view payloads | Remove |
| `EnvironmentDynamicDataManager` | shared per-view structured publisher plus system-specific payload contracts | Replace |
| `DirectionalLightShadows` | shadow-system metadata (`DirectionalShadowMetadata`, `ShadowProductHeader`, etc.) | Remove |
| `bindless_env_static_slot` in `SceneConstants` | `EnvironmentFrameBindings.environment_static_slot` | Move |
| `bindless_directional_lights_slot` in `SceneConstants` | `LightingFrameBindings.directional_lights_slot` | Move |
| `bindless_directional_shadows_slot` in `SceneConstants` | `ShadowFrameBindings.*` | Move |
| cluster-grid slots in `EnvironmentDynamicData` | `LightingFrameBindings` / `LightCullingViewData` | Move |
| synthetic sun in `EnvironmentDynamicData` | `LightingFrameBindings` / `LightingViewData` | Move |
| exposure in `SceneConstants` | `ViewColorData` | Move |
| grid debug fields in `MaterialConstants` | pass/debug payloads | Remove from material ABI |

## 7. Target Data Flow

The target shader-side data flow is:

1. Root CBV fetch `ViewConstants`.
2. Bindless fetch `ViewFrameBindings` using `ViewConstants.view_frame_slot`.
3. Bindless fetch system-owned frame bindings from `ViewFrameBindings`.
4. Bindless fetch system payloads/resources from those system frame bindings.
5. Bindless fetch pass constants using `g_PassConstantsIndex`.
6. Bindless fetch draw metadata using `g_DrawIndex`.

Illustrative HLSL shape:

```hlsl
#include "Contracts/Core/ViewConstants.hlsli"
#include "Contracts/Core/RootConstants.hlsli"
#include "Bindings/Core/ViewFrameBindings.hlsli"
#include "Bindings/Lighting/LightingBindings.hlsli"
#include "Bindings/Environment/EnvironmentBindings.hlsli"

ViewFrameBindings view_bindings = LoadViewFrameBindings(view_frame_slot, frame_slot);
LightingFrameBindings lighting = LoadLightingFrameBindings(view_bindings.lighting_frame_slot, frame_slot);
EnvironmentFrameBindings env = LoadEnvironmentFrameBindings(view_bindings.environment_frame_slot, frame_slot);
ViewColorData view_color = LoadViewColorData(view_bindings.view_color_frame_slot, frame_slot);
PostProcessFrameBindings pp = LoadPostProcessFrameBindings(view_bindings.post_process_frame_slot, frame_slot);
```

This makes system ownership explicit and avoids using environment naming as a
carrier for lighting or shadow state.

## 8. Refactor Phases

The phases below are execution phases for the refactor itself.

## 8.1 Phase 0: Freeze the authoritative interface inventory

Deliverables:

- Create and approve this plan.
- Decide final names for:
  - `ViewConstants`
  - `ViewFrameBindings`
  - system frame-binding structs
- Freeze the target root ABI before code changes start.

Rules:

- No new feature interfaces are added during this phase.
- Any doc that still describes older ABI becomes non-authoritative until updated.

Status:

- Completed on March 8, 2026 as a design/documentation milestone.

## 8.2 Phase 1: Re-establish one authoritative root ABI

Status:

- In progress.
- First implementation slice completed on March 8, 2026:
  - legacy `b3` binding is quarantined in the graphics/compute pass base
    classes
  - direct pass-level `EnvironmentDynamicData` root-CBV binding was removed
    from `ShaderPass`, `TransparentPass`, `SkyPass`, `SkyCapturePass`, and
    `SkyAtmosphereLutComputePass`
- Second implementation slice completed on March 8, 2026:
  - current root-ABI docs were corrected to match the live generated/code ABI
  - `Bindless.yaml` now marks `b3` as transitional legacy state
  - stale root-signature and contract docs were updated to describe the
    current 5-root-parameter ABI and the current `SceneConstants` /
    `EnvironmentDynamicData` sizes correctly
- Third implementation slice completed on March 8, 2026:
  - `ViewFrameBindings` now exists as a real C++/HLSL contract
  - renderer publishes a per-view `ViewFrameBindings` SRV and routes its slot
    through `SceneConstants.bindless_view_frame_bindings_slot`
  - the published payload initially exposed placeholder invalid system slots
- Fourth implementation slice completed on March 8, 2026:
  - GPU debug became the first real migrated consumer through
    `ViewFrameBindings.debug_frame_slot`
  - shaders now fetch debug resources through `DebugFrameBindings` instead of
    the legacy `SceneConstants` debug slots
- Fifth implementation slice completed on March 8, 2026:
  - `ViewColorData` now exists as a real C++/HLSL contract
  - renderer publishes per-view exposure through
    `ViewFrameBindings.view_color_frame_slot`
  - shared shader exposure reads now prefer `ViewColorData`, with
    `SceneConstants.exposure` retained only as a transitional fallback
- Sixth implementation slice completed on March 8, 2026:
  - `EnvironmentFrameBindings` now exists as a real C++/HLSL contract
  - renderer publishes the environment-static SRV through
    `ViewFrameBindings.environment_frame_slot`
  - shared shader environment reads now prefer `EnvironmentFrameBindings`,
    with `SceneConstants.bindless_env_static_slot` retained only as a
    transitional fallback
- Seventh implementation slice completed on March 8, 2026:
  - `LightingFrameBindings` now exists as a real C++/HLSL contract
  - renderer publishes per-view lighting state through
    `ViewFrameBindings.lighting_frame_slot`
  - shared shader light-array, sun, and clustered-light reads now prefer
    `LightingFrameBindings`, with `SceneConstants` light slots and
    `EnvironmentDynamicData.{light_culling,sun}` retained only as transitional
    fallbacks
  - `SyntheticSunData` now lives in its own lighting-owned contract and
    `GetExposure()` moved to `ViewColorHelpers.hlsli`, so environment helpers
    no longer surface view-color or lighting ownership
- Eighth implementation slice completed on March 8, 2026:
  - `EnvironmentViewData` now exists as a real C++/HLSL contract
  - renderer publishes per-view atmosphere frame state through
    `EnvironmentFrameBindings.environment_view_slot`
  - shared shader atmosphere reads now prefer `EnvironmentViewData`, with
    `EnvironmentDynamicData.atmosphere` retained only as a transitional
    fallback
- Ninth implementation slice completed on March 8, 2026:
  - the dedicated `b3 EnvironmentDynamicData` root CBV was removed from the
    live ABI
  - the generated root signature is now back to the intended 4-root-parameter
    contract
  - `SceneConstants` was stripped back to view invariants, draw-routing slots,
    instance-data slot, and `bindless_view_frame_bindings_slot`
  - helper fallbacks through legacy `SceneConstants` lighting/environment/debug
    fields were removed
- Tenth implementation slice completed on March 8, 2026:
  - `EnvironmentDynamicData` and `EnvironmentDynamicDataManager` were deleted
    from the runtime codebase
  - renderer-owned per-view runtime state now owns clustered-lighting config,
    canonical sun data, and per-view atmosphere data directly
  - `LightCullingPass` now republishes the current view's shader-facing
    lighting bindings through the renderer instead of writing into a legacy
    staging payload
  - renderer shutdown now drains uploads first, releases renderer-owned
    per-view/system resources in a deliberate order, and then flushes deferred
    releases
  - `EnvironmentStaticDataManager` teardown now unregisters its SRV views as
    well as the backing buffers, so teardown does not strand environment-frame
    views in the resource registry
- Remaining Phase 1 work:
  - run explicit DX12 debug-layer shutdown validation and prove a clean live-
    object report
  - harden any remaining teardown-order issues found by that validation

Deliverables:

- Update generated-root-signature usage/docs to the chosen final ABI.
- Remove the dedicated `b3 EnvironmentDynamicData` root param once no live
  readers remain.
- Update pass base classes and all pass binding code to the new root ABI.
- Make `RootConstants` the single mechanism for pass-local payload routing.

Primary files:

- `src/Oxygen/Core/Bindless/Generated.RootSignature.h`
- pass base classes
- shader reflection validation in `ShaderManager.cpp`
- root-binding docs

Exit criteria:

- All engine passes compile against the transitional root ABI and route new
  work through bindless/system frame bindings.
- New or refactored system data no longer depends on `b3`.
- Renderer shutdown is validated against the DX12 debug layer with no live
  renderer-owned resources reported.

Execution note:

- This migration was executed as a strangler sequence: new/refactored consumers
  moved first, then `b3` was removed once no live readers remained.

## 8.3 Phase 2: Replace `SceneConstants` with `ViewConstants`

Status:

- In progress.
- First implementation slice completed on March 8, 2026:
  - `SceneConstants` was renamed to `ViewConstants` in the live C++ and HLSL
    contracts
  - the generated root-signature source of truth now publishes
    `binding::RootParam::kViewConstants`
  - `RenderContext.view_constants`, pass binding helpers, and renderer wiring
    were renamed to `view_constants`, `BindViewConstantsBuffer()`, and
    `PrepareAndWireViewConstantsForView()`
  - shader reflection validation now requires `ViewConstants` at `b1, space0`
  - the current authoritative ABI docs now describe `ViewConstants`, not
    `SceneConstants`
- Second implementation slice completed on March 8, 2026:
  - `DrawFrameBindings` now exists as a real C++/HLSL contract
  - renderer publishes per-view draw routing through
    `ViewFrameBindings.draw_frame_slot`
  - draw/material/transform/instance shader reads now resolve through
    `DrawFrameBindings` instead of direct slots in `ViewConstants`
  - `ViewConstants` was shrunk again and now carries only view invariants plus
    `bindless_view_frame_bindings_slot`
  - current authoritative ABI docs now describe draw routing through
    `ViewFrameBindings` and `DrawFrameBindings`

Deliverables:

- Rename `SceneConstants` to `ViewConstants`.
- Shrink it to:
  - frame identity/time
  - view/projection/camera invariants
  - `view_frame_slot`
- Remove all system-specific slots from it.

Exit criteria:

- `ViewConstants` is small, stable, and system-neutral.
- No system publishes resource-specific slots directly into the root CBV.

Remaining Phase 2 work:

- tighten the remaining non-authoritative docs and notes that still use
  `SceneConstants` terminology where they describe the live contract
- remove or rename any remaining stale comments/notes that still imply
  draw-routing ownership in `ViewConstants`
- confirm there are no remaining live shader/pass consumers that bypass
  `ViewFrameBindings` for draw routing before closing Phase 2

## 8.4 Phase 3: Introduce `ViewFrameBindings` and re-home system routing

Deliverables:

- Create `ViewFrameBindings` C++ and HLSL contracts.
- Create a manager/uploader for it.
- Publish system frame-binding slots through `ViewFrameBindings`.
- Add placeholder slots immediately for:
  - shadow
  - history
  - ray tracing
  even if they initially publish invalid bindless indices only.

Exit criteria:

- Shaders can route to draw/lighting/environment/debug/post-process systems
  through `ViewFrameBindings`.
- The renderer has one top-level view-routing object instead of many slots in
  `SceneConstants`.

Execution note:

- Placeholder future-system slots should initially publish invalid indices.
- Do not build empty placeholder payload hierarchies unless shader or C++ code
  immediately needs typed access to them.

## 8.5 Phase 4: Extract lighting interfaces cleanly

Deliverables:

- Introduce `LightingFrameBindings`.
- Move cluster-grid/list routing and designated sun data out of
  `EnvironmentDynamicData`.
- Replace mixed environment-lighting helper dependencies in HLSL.
- Keep `LightManager` focused on lighting records, not shadow-technique
  payloads.

Primary changes:

- new lighting bindings contract
- `LightCullingPass` publishes lighting-owned view data
- `ForwardDirectLighting.hlsli` consumes lighting bindings, not environment
  dynamic state

Exit criteria:

- Lighting no longer depends on environment-owned root/state names for its
  dynamic data.
- Clustered light culling outputs are published as lighting-owned system data,
  not pass-local state.

Execution note:

- If a resource is produced by one pass but consumed across multiple passes, it
  is system-owned and must be published through system frame bindings.
- Pass constants remain strictly pass-local.

## 8.6 Phase 5: Extract environment interfaces cleanly

Deliverables:

- Introduce `EnvironmentFrameBindings`.
- Narrow environment-owned runtime data to actual environment concerns:
  atmosphere, sky, fog, IBL, clouds, and related derived resources.
- Remove exposure ownership from environment.

Notes:

- Environment may consume the designated sun selected by lighting.
- Environment does not own the canonical sun selection contract.

Exit criteria:

- Environment contracts no longer carry lighting/culling responsibilities.

## 8.7 Phase 6: Clean draw/material contracts

Deliverables:

- Introduce `DrawFrameBindings`.
- Keep `DrawMetadata` fixed and generic.
- Split `MaterialConstants` into `MaterialShadingConstants` and
  pass/debug-specific payloads.

Exit criteria:

- The material ABI describes material state only.
- Ground-grid/debug-specific settings are removed from the core material
  contract.

Execution note:

- The first material cleanup wave should stop after non-material/debug payloads
  are removed from the core ABI.
- Do not expand this phase into a full shading-model redesign.

## 8.8 Phase 7: Introduce shadow-ready and ray-tracing-ready system slots

Deliverables:

- Add `ShadowFrameBindings` to `ViewFrameBindings`.
- Add `HistoryFrameBindings` and `RayTracingFrameBindings` slots, even if the
  initial payloads are placeholders publishing invalid slots.
- Replace `DirectionalLightShadows` usage with neutral extension points in
  lighting and shadow contracts.

Important:

- This phase does not implement shadows or ray tracing.
- It prepares the interface layer so those systems do not need to break it
  later.

Exit criteria:

- The renderer interface is ready for future shadow and RT systems without
  another root/ownership redesign.

## 8.9 Phase 8: Documentation and validation hardening

Deliverables:

- Rewrite:
  - `shader-system.md`
  - `bindless_conventions.md`
  - relevant pass docs
  - environment/lighting/shadow docs as needed
- Remove outdated ABI descriptions.
- Add reflection/contract validation for all final core/system contracts.
- Add targeted tests for:
  - contract sizes and offsets
  - required root bindings
  - frame-binding publication validity
  - domain validation for bindless slots

Exit criteria:

- Docs match code.
- Reflection validation matches code.
- Static and runtime validation catch future drift early.

Execution note:

- Semantic and contract cleanup comes before shader file/directory
  reorganization.
- Once the interfaces are stable, a separate cleanup pass may reorganize file
  layout without mixing semantic changes and file moves.

## 9. Validation Gates

This refactor is not complete unless the following are true.

### 9.1 Code-validation gates

- C++/HLSL size and offset parity checks pass.
- Shader reflection validates the final root ABI and contract sizes.
- All engine passes bind the final root signature correctly.
- No shader reads removed legacy contracts.

### 9.2 Documentation gates

- One document is designated authoritative for the renderer-shader contract
  model.
- Non-authoritative older descriptions are updated or removed.
- All system docs use the new names and ownership model.

### 9.3 Architecture gates

- No system-specific dynamic payload is published through another system’s
  interface surface.
- No technique-specific payload is treated as a stable top-level system
  contract when a system-shaped contract is required.
- `ViewConstants` remains small.
- Any resource consumed across multiple passes is published through a system
  frame-binding contract, not through pass constants.

## 10. Immediate Concrete Follow-Up Work

If this plan is approved, the first implementation sequence should be:

1. Rewrite the authoritative shader-system contract doc to match the target
   model.
2. Introduce `ViewConstants`, `ViewFrameBindings`, and placeholder system slots.
3. Introduce `ViewConstants` and `ViewFrameBindings`.
4. Re-home existing draw, lighting, environment, and debug slots under system
   frame bindings.
5. Migrate all remaining `b3` consumers off `EnvironmentDynamicData`.
6. Delete `EnvironmentDynamicData` and remove `b3` from the root signature.

This should happen before any shadow runtime implementation work resumes.

Implementation posture for uncertain details:

- Choose the smallest correct shape that supports the migrated slice.
- Add no secondary routing or payload layers unless a concrete consumer needs
  them immediately.
- Prefer invalid placeholder slots over speculative empty payload types.
- Prefer removing obviously wrong ownership now over preserving it for
  convenience.

## 11. Locked Decisions

The following decisions are now considered resolved for implementation unless
explicitly revisited:

1. Use one top-level `ViewFrameBindings` object.
2. Keep `ViewConstants` pristine and system-neutral with exactly one routing
   slot.
3. Put exposure in a shared `ViewColorData` contract.
4. Keep canonical sun ownership in `LightingFrameBindings`.
5. Use `MaterialShadingConstants` for the first material ABI cleanup.
6. Treat cross-pass resources as system-owned frame bindings, not pass
   constants.
7. Perform semantic cleanup before shader file/directory reorganization.
8. Sequence `b3` removal instead of deleting it in one step.
9. Add placeholder shadow/history/ray-tracing slots to `ViewFrameBindings`
   early.

## 12. Current Status

This document now tracks an active refactor, not just a proposed design.

Evidence:

- Files changed: this document plus the live renderer/shader contracts and
  supporting docs referenced in the Phase 1 and Phase 2 status records
- Tests run:
  - `msbuild out/build-vs/src/Oxygen/Renderer/oxygen-renderer.vcxproj /m:6 /p:Configuration=Debug /nologo`
  - `msbuild out/build-vs/src/Oxygen/Engine/oxygen-engine.vcxproj /m:6 /p:Configuration=Debug /nologo`
  - `msbuild out/build-vs/src/Oxygen/Graphics/Direct3D12/Shaders/oxygen-graphics-direct3d12_shaders.vcxproj /m:6 /p:Configuration=Debug /nologo`
- Validation performed:
  - renderer build succeeded
  - engine build succeeded
  - shader bake succeeded for 148 modules
  - DX12 debug-layer shutdown validation for Phase 1 was confirmed clean
- Remaining gap:
  - Phase 2 is still in progress until the final `ViewConstants` shrink is
    complete and the remaining live-contract doc stragglers are drained
