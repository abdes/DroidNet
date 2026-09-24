# ShadowService Local-Light Conventional Expansion LLD

**Phase:** 5D — Conventional Shadow Parity And Expansion
**Deliverable:** `VTX-M05D`
**Status:** `m05d_spot_slice_validated`

## EX07 production contract

Projected-spot receiver offsets use the texel footprint at the
receiver's positive axial depth, not at the projection far plane. The published
`world_texel_size` is the far-plane footprint; multiply it by receiver depth
divided by `far_plane_m`, using unbiased clip W. Authored normal bias remains in
metres and separate from this projection-derived footprint. This correction
retains long-range light and off-screen caster contributions.

EX07E04 replaces the old cube linear-depth/3x3 path with unbiased projected
reversed-Z depth, cube-array hardware comparison and receiver-side depth bias.
Point lights and hemispherical spots share this cube contract. Native cube
sampling uses the receiver-to-light vector; physical face views therefore look
along the negative cube axes. The approved Low/Medium/High/Ultra comparison
counts are 1/5/29/29. Authored normal displacement remains separate, and the old
automatic cube texel offsets are removed. The
[implementation contract](../plan/EX07E-point-pcf-contract.md) owns the current
UE5.7 source comparison, units and qualification gates. The
[New Sponza analysis](../plan/EX07-NewSponza-regression-analysis.md) remains the
historical diagnosis preceding this migration. Final baseline visual acceptance
was received on 2026-09-25; [accepted results](../plan/EX07E-shadow-sharing-results.md)
retain the filter quality/cost difference.

The baseline evidence below remains historical. EX07 supersedes its bounded
arrays and Stage-18 deferral with the [indexed shadow-family ABI](lighting-gpu-abi.md#shadow-association-and-deferred-draws),
[analytic source and center-support model](../../renderer-core/physically-based-rendering.md#production-local-lighting-and-brdf-model-2)
and [resource/failure contract](lighting-service.md#4-capacity-failure-and-recovery).
Every required point/spot shadow is consumed by both surface families. Use
projected records for ordinary spots, including nonzero source radius. Only
90-degree soft cones use the existing conventional cube/multiple-face technique.
Preserve FP32 reversed depth, per-light quality and typed identity. Ordinary
projected spots retain 3x3 PCF; cube maps use the EX07E04 contract above.
The [audited EX07 memory work](../plan/EX07-shadow-memory-review.md) selects
depth-only D32 conventional targets after coordinated view/clear/PSO migration and
production qualification; conventional targets now use D32 with R32 SRVs. Scene/custom
stencil and VSM resources are unaffected. Retain unchanged conventional local
depth contents across frames in per-light resolution buckets. Re-evaluate current
caster membership so newly entering and leaving casters
invalidate the map. Recorded contents remain pending until successful submission;
retain the producer queue/fence dependency for every consumer. Failed/discarded
recordings never establish reusable contents. A camera move alone does not
invalidate a complete light-space local map. Light, projection, resolution,
geometry bindings/transforms and depth-affecting material changes invalidate
affected maps. Build per-source dependencies before instancing; hash only a
caster's own transform, geometry identity/content revision and raster state,
plus its alpha-test material
and base-color texture revision when masked. Re-select these dependencies from
all current shadow casters against each light volume. Unrelated transforms and
texture uploads must not invalidate that light. `GeometryUploader` advances its
content revision on accepted creation, explicit updates and hot reload, even
when topology, handle generation and descriptors stay unchanged. The revision
is published with complete resident bindings and copied into each unbatched
caster source. Revision zero means continuity is unknown and prevents reuse for
lights influenced by that caster. This also invalidates a retained map after the
light returns to camera coverage following an unseen geometry update.
The conventional depth shader
currently consumes undeformed geometry; adding deformation to that shader must
extend these dependencies with its per-caster inputs.
Directional cascades remain view-dependent. Implementation and focused tests
are complete. The user visually approved the conventional-shadow changes and
quality policy on 2026-09-24.
Remove the 4/8 cutoffs and float-encoded layers. Allocation pressure is explicit
failure/recovery, never automatic unshadowing or a resolution reduction.

### Conventional local quality and allocation

Treat the authored resolution hint and renderer tier as the maximum resolution.
For a camera outside the light volume, derive a conservative projected radius
from the resolved viewport/projection and the light's near-side view depth.
Use 1.27324 texels per pixel for cube projections and twice that for ordinary
spots. For perspective views, a camera inside the volume requests the authored/tier
ceiling. Orthographic views always use projected size, including when the camera
enters the volume; translation along the viewing axis does not change quality. Choose
power-of-two resolutions; retain the previous bucket until demand falls below
75% of it or reaches the next doubled bucket. An authored ceiling reduction
takes effect immediately. A resolution change redraws the affected point/spot
map at the new dimensions; the next unchanged frame reuses that result. Growing
back to a previously used resolution must not resurrect stale cached contents.

Fade map visibility smoothly from zero at 32 desired texels to full strength at
64 desired texels. A zero-strength request publishes `QualityOmitted` without a
map. Other local records publish `shadow_strength`; both shading families apply
it once as `lerp(1, map_visibility, shadow_strength)`. This is the normal selected
quality policy, not a response to memory exhaustion. User visual approval of
the implemented policy was received on 2026-09-24.

Retain view-local light associations independently of selection indices, while
compatible views share exact immutable per-light depth versions. Canonical
caster membership and content identity determine compatibility; resolution,
projection or depth-affecting changes acquire the appropriate new version.
Each physical slot identifies one resolution bucket, chunk and layer range.
Adding, removing or reordering unrelated lights cannot reinterpret its content.
Reconcile owners for every active view, including empty or directional-only
selections. Scene replacement or removal releases that view's aliases while
other owners and submitted users continue to retain their allocations.

`IndexReuse<ShadowSlotIndex>` and retirement tickets separate generation
invalidation from safe reuse. A version owns its slot and shared backing;
recording/GPU uses pin both. Removing the final allocation owner starts retirement,
but finalization waits for all uses. Closing a view cannot unregister descriptors
still owned by another view, retained content or submitted work. Stable weak
return targets avoid callbacks into a destroyed allocator.

Content becomes reusable only after successful producer submission. Consumers
attach through managed uses and order against actual completion receipts;
unsubmitted/retained readers can force copy-on-write. See the
[shared ownership contract](shadow-service.md#compatible-local-map-ownership)
for publication expiry, retained reads and backing-wide hazards.

Store each resolution bucket in appendable array chunks targeting 64 MiB, with
at most 64 lights per chunk and at least one complete projection. This limits
allocation replacement peaks when the visible light count grows. It does not
limit scene light count. Keep existing chunks when adding another, and retry
without unused spare layers if that spare capacity alone exceeds the budget.
Retire unused chunks through the existing deferred fence mechanism. Failure to
allocate the required layers remains an explicit preparation failure.

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

| Source                 | Data                        | Purpose                                 |
| ---------------------- | --------------------------- | --------------------------------------- |
| Scene                  | Spot-light data             | Spot-light shadow setup                 |
| Scene                  | Point-light data            | Point-light shadow setup                |
| Views                  | Per-view frusta / relevance | View-scoped publication                 |
| ShadowService baseline | Directional cascade path    | Existing conventional-shadow foundation |

### 3.2 Outputs

| Product                         | Consumer                                                                                   | Delivery                                          |
| ------------------------------- | ------------------------------------------------------------------------------------------ | ------------------------------------------------- |
| Spot-light shadow publications  | LightingService Stage 12. Stage 18 translucent local-light shadow consumption is deferred. | `ShadowFrameBindings` through `ViewFrameBindings` |
| Point-light shadow publications | LightingService Stage 12. Stage 18 translucent local-light shadow consumption is deferred. | `ShadowFrameBindings` through `ViewFrameBindings` |

## 4. Resource Management

### 4.1 Point-Light Decision Matrix

The design explicitly chose the first point-light conventional storage strategy
for M05D. The baseline options were:

| Option                                             | Pros                                                                                                                                      | Cons                                                                                                                           | Vortex Verdict                                                     |
| -------------------------------------------------- | ----------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------ | ------------------------------------------------------------------ |
| One-pass cubemap depth targets                     | Closest to UE conventional point-light handling, clean omnidirectional coverage, hardware comparison / filtering story is straightforward | Requires layered cubemap rendering support that Oxygen does not yet have wired through the shadow-depth pass                   | Future upgrade target                                              |
| Cube-array six-face conventional targets           | Uses the same cube-family storage shape while reusing the existing `ShadowDepthPass::RecordSlices` path                                   | Six face draws instead of UE's one-pass path; deterministic `Texture2DArray` sampling instead of texture-cube compare sampling | **M05D validated baseline**                                        |
| Atlased six-face conventional targets              | Reuses atlas infrastructure, may simplify allocation bookkeeping                                                                          | Harder to keep clean face ownership, still effectively a cubemap family with more packing complexity                           | Deferred unless it clearly improves Oxygen resource management     |
| Dual-paraboloid or other compressed representation | Lower target count in some cases                                                                                                          | Projection / filtering complexity, less aligned with UE reference path, higher risk of bespoke artifact handling               | Rejected unless a future profiling / platform constraint forces it |

Vortex therefore chooses **cube-array six-face conventional targets** as the
M05D validated baseline. This stays close to UE 5.7's cubemap model without
claiming one-pass layered rendering parity before Oxygen has that backend path.

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
2. Six-face cube-array point-light shadows
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

### 5.3 Slice F Point-Light Conventional Contract

Slice F adds the point-light conventional payload to the same
`ShadowFrameBindings` publication seam:

- one conventional point-shadow binding per shadow-casting point light;
- six 90-degree reversed-Z face matrices per point light;
- cube-array shadow storage allocated by `ConventionalShadowTargetAllocator`;
- Stage 8 renders six `ShadowDepthPass::DepthSlice` entries per point light;
- each face slice carries the point-light position, inverse range, face
  direction, and authored bias parameters into the shared shadow-depth shader;
- Stage 12 point deferred lighting selects the face from the receiver vector,
  samples the published point shadow surface, and compares the same reversed
  axial depth convention written by Stage 8.

UE5.7 uses a one-pass cubemap point-light path when the runtime supports layered
point-light shadow rendering. Oxygen's M05D baseline intentionally renders the
six faces through `RecordSlices` and samples them through a deterministic
`Texture2DArray` view of the cube-array surface. This is an accepted first
activation divergence, not a claim of one-pass parity. The binding/storage
choice keeps the upgrade path open for a later layered cubemap implementation.

Slice F implementation evidence:

- `PointShadowSetup` builds point bindings from shadow-casting point lights.
- `ShadowService` publishes point-shadow surface/count/bindings alongside the
  existing directional and spot conventional products.
- `LightingService` and `DeferredLightPass` consume the point-shadow product for
  Stage 12 point lights.
- `DirectionalShadowCommon.hlsli` implements point face selection, 3x3 PCF over
  the point shadow surface, and receiver-depth comparison in the same axial
  reversed-depth convention as the point depth pass.
- `DeferredLightProxyGeometry` owns service point/spot proxy geometry. The point
  sphere winding regression is covered so the outside-volume point-light path
  does not reintroduce the solid-shell failure.
- `PointShadowValidation` is the focused no-sun validation scene, and
  `ProbeRenderDocPointShadow.py` proves Stage 8/Stage 12 point-shadow bindings
  and SceneColor contribution in a capture.
- On 2026-04-27 the user visually confirmed the point-light shell bug and the
  point-shadow cube artifact were both fixed.

## 6. Design Decision

The hard requirement is not “implement point-light shadows somehow.” It is
“make the storage choice explicit, bind it through the existing publication
seam, and preserve a clean upgrade path to VSM later.”

The concrete M05D decision is to use cube-array storage with six explicit face
draws and deterministic array-face sampling. This is less efficient than UE's
one-pass cubemap path, but it gives Vortex a validated conventional baseline
before later work decides between layered cubemap rendering and VSM.

## 7. Testability Approach

1. Spot-light conventional shadows render correctly in deferred lighting.
2. Point-light conventional shadows render correctly using the six-face
   cube-array baseline.
3. `ShadowFrameBindings` remains valid for both directional and local-light
   consumers.
4. The same consuming shader path remains compatible with later VSM activation
   through capability / technique selection rather than ABI replacement.

## 8. Open Questions

1. Whether layered one-pass cubemap rendering should replace the six-face
   `RecordSlices` implementation before VSM work.
2. Whether point-light conventional shadows should remain behind a capability
   gate until content justifies their runtime cost.
