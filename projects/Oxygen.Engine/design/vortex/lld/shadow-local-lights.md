# ShadowService Local-Light Conventional Expansion LLD

**Phase:** 5D — Conventional Shadow Parity And Expansion
**Deliverable:** `VTX-M05D`
**Status:** `m05d_spot_slice_validated`

## Mandatory Vortex Rule

- For Vortex planning and implementation, `Oxygen.Renderer` is legacy dead
  code. It is not production, not a reference implementation, not a fallback,
  and not a simplification path for any Vortex task.
- Every Vortex task must be designed and implemented as a new Vortex-native
  system that targets maximum parity with UE5.7, grounded in
  `F:\Epic Games\UE_5.7\Engine\Source\Runtime` and
  `F:\Epic Games\UE_5.7\Engine\Shaders`.
- No Vortex task may be marked complete until its parity gate is closed with
  explicit evidence against the relevant UE5.7 source and shader references.
- If maximum parity cannot yet be achieved, the task remains incomplete until
  explicit human approval records the accepted gap and the reason the parity
  gate cannot close.

## 1. Scope and Context

### 1.1 What This Covers

This document captures the ShadowService local-light expansion that follows the
VTX-M05D directional CSM parity/stability gate:

- spot-light conventional shadows
- explicit point-light conventional shadow strategy
- per-view publication of local-light shadow products without freezing a VSM ABI

### 1.2 Why This Exists

Phase 4C intentionally narrowed scope to avoid bluffing about point-light
storage. This future LLD is the handoff artifact that closes that gap. The
ShadowService roadmap now has a named later-phase owner for local-light
conventional shadows instead of leaving the issue as an open-ended note.

The corrected Phase 4C contract published **directional** conventional shadow
data only. VTX-M05D first audits and stabilizes that directional CSM baseline,
then extends `ShadowFrameBindings` without pretending that Phase 4 already
shipped spot-light or point-light conventional shadow payloads.

The local-light implementation is blocked until the M05D CSM audit/remediation
gate records why city-scale projected shadows were unstable under camera
movement and proves the corrected behavior.

### 1.3 Architectural Authority

- [ARCHITECTURE.md §8](../ARCHITECTURE.md) — `ShadowService` ownership
- [PLAN.md §7](../PLAN.md) — Phase 5 expansion scope
- [shadow-service.md](shadow-service.md) — Phase 4C directional-first baseline
- [../plan/VTX-M05D-conventional-shadow-parity.md](../plan/VTX-M05D-conventional-shadow-parity.md)
  — CSM-first M05D execution plan

## 2. Interface Contracts

### 2.1 Service Boundary

No new top-level service is introduced. This is an internal ShadowService
expansion.

### 2.2 Published Payload Contract

`ShadowFrameBindings` remains the canonical GPU-facing publication seam.
VTX-M05D may extend it with local-light-specific fields, but it must not bake
today's conventional storage choice into the long-lived binding ABI.

VTX-M05D therefore inherits these rules from the remediated directional
baseline:

1. directional conventional shadow publication exists but must pass the M05D
   parity/stability gate before local-light work starts
2. local-light conventional shadow publication is added here for the first time
3. the public binding seam stays consumer-oriented and does not freeze one
   internal storage layout

### 2.3 Explicit Non-Goals For 5G

The first local-light conventional expansion does **not** include:

- VSM activation or VSM projection logic
- translucent shadow-map targets
- cached preshadow families
- mobile-specific shadow paths

Those remain separate because mixing them into the first local-light expansion
would blur the line between conventional-shadow completion and later
ShadowService upgrades.

## 3. Data Flow and Dependencies

### 3.1 Inputs

| Source | Data | Purpose |
| ------ | ---- | ------- |
| Scene | Spot-light data | Spot-light shadow setup |
| Scene | Point-light data | Point-light shadow setup |
| Views | Per-view frusta / relevance | View-scoped publication |
| ShadowService baseline | Directional cascade path | Existing conventional-shadow foundation |

### 3.2 Outputs

| Product | Consumer | Delivery |
| ------- | -------- | -------- |
| Spot-light shadow publications | LightingService Stage 12. Stage 18 translucent local-light shadow consumption is deferred. | `ShadowFrameBindings` through `ViewFrameBindings` |
| Point-light shadow publications | LightingService / translucency | `ShadowFrameBindings` through `ViewFrameBindings` |

## 4. Resource Management

### 4.1 Point-Light Decision Matrix

The design must explicitly choose one point-light conventional storage strategy
before implementation. The baseline options are:

| Option | Pros | Cons | Vortex Verdict |
| ------ | ---- | ---- | -------------- |
| One-pass cubemap depth targets | Closest to UE conventional point-light handling, clean omnidirectional coverage, hardware comparison / filtering story is straightforward | Higher memory footprint than some niche alternatives | **Preferred baseline** |
| Atlased six-face conventional targets | Reuses atlas infrastructure, may simplify allocation bookkeeping | Harder to keep clean face ownership, still effectively a cubemap family with more packing complexity | Acceptable only if it clearly improves Oxygen resource management |
| Dual-paraboloid or other compressed representation | Lower target count in some cases | Projection / filtering complexity, less aligned with UE reference path, higher risk of bespoke artifact handling | Rejected unless a future profiling / platform constraint forces it |

Vortex therefore chooses **one-pass cubemap depth targets** as the default
conventional point-light direction unless later evidence overturns it. This is
the closest useful match to UE 5.7 and avoids inventing a niche representation
with little architectural value.

That choice remains an internal storage decision, not the definition of the
published shadow-binding ABI.

### 4.2 VSM Coexistence Rules

The local-light conventional design must stay compatible with the later VSM
upgrade:

- `ShadowFrameBindings` may expose a technique selector or capability flags,
  but must not hardwire consumers to one conventional storage ABI
- conventional point-light storage and future VSM payloads must coexist behind
  the same published shadow-binding seam
- the 5G choice must not force 7C to keep a cubemap-shaped public contract if
  VSM needs a different internal representation

## 5. Stage Integration

- Work remains under stage 8 and under `ShadowService`.
- Stage 12 and stage 18 continue to consume only published shadow bindings.
- VSM remains a separate later ShadowService upgrade and must not be conflated
  with the conventional local-light expansion.

### 5.1 Chosen First Activation Order

1. Spot-light conventional shadows
2. One-pass cubemap point-light shadows
3. Validation that both flow through the same published binding seam

This order is intentional. Spot lights are cheaper to land and validate first,
but the document now also gives point lights a concrete target rather than
leaving them as an abstract future choice.

### 5.2 Slice E Spot-Light Conventional Contract

Slice E adds the first local-light conventional payload to
`ShadowFrameBindings`. It follows the local UE5.7 spot whole-scene shadow path:

- one projected shadow per shadow-casting spot light;
- light-space projection derived from light position, light forward direction,
  outer cone angle, `MinLightW = 0.1`, and authored range;
- UE-style whole-scene spot depth-bias scaling before the shadow-depth draw;
- perspective spot depth uses a UE-style separation between raster projection
  and biased shadow depth: Stage 8 clips/rasterizes with the projected cone but
  writes biased linear reversed depth along the spot axis, and Stage 12 compares
  receivers against the same spot-axis depth convention;
- Stage-8 depth rendering through the existing Vortex shadow-caster draw path;
- Stage-12 spot deferred lighting multiplies local-light attenuation by the
  sampled spot shadow visibility.
- Stage-18 translucent spot-shadow consumption is not part of Slice E. The
  current forward/translucency path accumulates positional lights without a
  conventional spot shadow lookup, so this remains deferred until the forward
  local-light shadow contract is designed and validated.

Intentional Slice E divergences, not closure claims:

- no local-light shadow caching;
- no per-light CPU interaction list or screen-radius resolution fade yet;
- no UE shadow border emulation for the dedicated `Texture2DArray` storage;
- no point-light cubemap payload until Slice F.

Slice E implementation evidence:

- `ShadowFrameBindings` now carries a conventional spot shadow surface handle,
  spot count, and per-spot bindings.
- `SpotShadowSetup` builds one projected shadow binding per shadow-casting spot
  light from authored light position, direction, cone angle, range, and
  UE-shaped whole-scene spot bias scaling.
- Stage 8 renders spot depth slices through `ShadowDepthPass::RecordSlices`.
- Stage 12 spot deferred lighting samples the conventional spot shadow array
  and multiplies local-light attenuation by shadow visibility.
- `SpotShadowValidation` is the focused no-sun/no-atmosphere validation scene.
  On 2026-04-27 the user confirmed visible spot shadows and then confirmed the
  shadows were perfect after the authored spot shadow bias was set to `0.0` and
  the scene was recooked.
- Fresh post-review RenderDoc proof
  `spot-shadow-validation.bias0.final.spot-shadow-probe.txt` shows Stage 8
  spot draws `168,171`, non-clear `Vortex.SpotShadowSurface` depth
  (`max=0.463512063`, center `0.447184265`), Stage 12 spot draw `248`, and
  Stage 12 spot-light binding of `Vortex.SpotShadowSurface`.
- Focused tests/shader validation passed: ShadowService `8/8`,
  SceneRendererDeferredCore `41/41`, LightingService `4/4`, and
  ShaderBakeCatalog `4/4`.
- CDB/D3D12 audit
  `spot-shadow-validation.bias0.final.debug-layer.report.txt` passed with
  runtime exit `0`, no debugger break, `0` D3D12/DXGI errors, and no blocking
  warnings.

## 6. Design Decision

The hard requirement for this future phase is not “implement point-light
shadows somehow.” It is “make the storage choice explicit, bind it through the
existing publication seam, and preserve a clean upgrade path to VSM later.”

The concrete decision taken here is to prefer one-pass cubemap depth targets
for conventional point-light shadows. That aligns with UE's mature path and
gives Vortex a clear conventional baseline before 7C upgrades ShadowService to
VSM.

## 7. Testability Approach

1. Spot-light conventional shadows render correctly in deferred lighting.
2. Point-light conventional shadows render correctly using the one-pass
   cubemap baseline.
3. `ShadowFrameBindings` remains valid for both directional and local-light
   consumers.
4. The same consuming shader path remains compatible with later VSM activation
   through capability / technique selection rather than ABI replacement.

## 8. Open Questions

1. Whether atlased six-face allocation offers any concrete Oxygen-specific win
   over the preferred one-pass cubemap baseline.
2. Whether point-light conventional shadows should remain behind a capability
   gate until content justifies their runtime cost.
