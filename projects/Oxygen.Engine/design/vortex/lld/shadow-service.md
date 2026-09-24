# ShadowService LLD

**Phase:** 4C - Migration-Critical Services
**Deliverable:** D.11
**Status:** Indexed directional/local shadows, hardware cube PCF and compatible local-map sharing implemented and qualified. [EX07F](../plan/EX07F-acceptance-report.md) awaits only user-owned editor interaction sign-off.

## V0.1 Production Extension

EX07 freezes the current [indexed family ABI](lighting-gpu-abi.md#shadow-association-and-deferred-draws)
and [capacity/failure contract](lighting-service.md#4-capacity-failure-and-recovery).
Use dynamic directional, projected-local and cube-local records, typed surface/
layer indices and source identity maps. Ordinary spots use one projected map
regardless of source radius; 90-degree soft spots use the existing cube technique. The 4-point/8-spot arrays are not product limits.
Requested maps for enabled, contributing lights cannot silently become absent.
Both forward/translucent and deferred consumers belong to EX07 qualification.

The [EX07 shadow-memory audit](../plan/EX07-shadow-memory-review.md) selects D32
for conventional CSM/local targets while preserving FP32 depth, reversed-Z,
bias and PCF. Migrate views, PSOs and depth-only clears together; scene/custom
stencil and independently owned VSM products are not changed. Compatible local
maps may share compatible complete contents and retain them across frames;
CSM/contact products remain view-specific. Re-evaluate caster membership,
including newly entering casters, and invalidate for relevant light, geometry,
material or projection changes. Cache publication requires successful submission
and the producer's queue/fence dependency; discarded work cannot become reusable.
Use Nexus `IndexReuse<ShadowSlotIndex>` and retirement tickets for retained
local-slot identity. Removing the final allocation owner invalidates its
generation; reuse waits for all recording/GPU uses to release. Content validity
remains separate from physical-slot lifetime. The D32
production cutover, focused tests and static-scene Tracy comparison are complete.
The user visually approved the conventional-shadow changes on 2026-09-24.
Per-light caster dependencies and projected-quality behavior are specified in
[the local-shadow contract](shadow-local-lights.md#ex07-production-contract).

[Editor V0.1 rendering](../plan/editor-v01-rendering-contract.md#4-conventional-shadows-and-receiver-control)
extends this baseline with explicitly indexed per-light directional CSMs, GPU
receiver eligibility and the conditional dedicated
[ContactShadowCasterDepth product](../plan/editor-v01-rendering-contract.md#5-contactshadowcasterdepth-and-contact-attenuation).
The exact contact algorithm is linked there. Projected-spot/CSM bias and 3x3 PCF contracts remain; cube-local filtering uses
the approved hardware-PCF contract below. Atmosphere disk diameter adds no
PCSS/finite-source effect.
Historical single-light interface examples and VTX evidence below do not close
the new two-source, receiver or contact requirements.

## Conditional contact-caster depth

`ContactShadowCasterDepthPass`, owned by ShadowService at Stage 8, renders one
camera-space D32 depth product per requesting view. It shares the depth-prepass
pipeline builder, opaque/masked mesh processing and raster-state rules. Its draw
selection uses shadow-caster eligibility, independently of main-view geometry
visibility. No eligible contact request means no allocation, recording or usable
binding. Resizing and view removal retire textures/descriptors through the
existing retained-texture pool; allocations count against the lighting budget.
The depth SRV belongs to the generated bindless texture domain, matching the
shader's domain guard. A retained texture reuses its registered SRV.

The shared contact shader implements the fixed 0.25 m/16-sample profile in the
[editor rendering contract](../plan/editor-v01-rendering-contract.md#5-contactshadowcasterdepth-and-contact-attenuation).
It uses geometric-normal bias, metric depth thickness, exact start-depth rejection
and edge/end fading. Forward and deferred consumers multiply its result with
conventional visibility once, after the receiver gate. Required allocation or
submission failure rejects the affected shadow publication. The screen-space
supplement retains conventional maps for off-screen and hidden-depth coverage.

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

`ShadowService` is the Stage-8 owner for conventional shadow-map production.
The Phase 4C directional baseline proved projected directional shadows exist,
but VTX-M05D reopens the directional CSM parity surface before local-light
expansion because city-scale shadows are unstable under camera movement.

Phase 4C covered:

- directional conventional shadow-map setup
- directional shadow depth rendering at Stage 8
- per-view publication of the directional conventional shadow product
- later consumption of that published directional shadow product by Stage 12

The Phase 4C closure does **not** claim full CSM parity or camera-stable
city-scale behavior. VTX-M05D must audit and remediate directional CSM first.

VTX-M05D covers:

- full UE5.7-informed directional CSM parity audit and remediation
- camera-movement stability proof in `CityEnvironmentValidation`
- spot-light conventional shadows
- explicit point-light conventional shadow strategy and proof

Phase 4C/VTX-M05D still do **not** claim:

- VSM activation

VSM remains future `ShadowService` work and is not part of the conventional
shadow-map baseline.

### 1.2 Stage Position

| Position    | Stage                                       | Notes                                                                                                   |
| ----------- | ------------------------------------------- | ------------------------------------------------------------------------------------------------------- |
| Predecessor | Stage 6 (`LightingService::BuildLightGrid`) | directional-light authority and forward-light publication already resolved                              |
| **This**    | **Stage 8 - Shadow Depths**                 | conventional directional shadow production                                                              |
| Successor   | Stage 9 (`BasePass`)                        | forward shading consumes the published directional shadow product; deferred shading writes GBuffer data |

The published directional shadow product is consumed later by:

- Stage 12 deferred direct lighting
- Stage 9 forward surface shading and Stage 18 translucency

### 1.3 Architectural Authority

- [ARCHITECTURE.md](../ARCHITECTURE.md) Section 8 - subsystem service ownership
- [ARCHITECTURE.md](../ARCHITECTURE.md) Section 6.2 - Stage 8 ownership
- [PLAN.md](../PLAN.md) Section 6 - Phase 4C directional-first scope
- [shadow-local-lights.md](./shadow-local-lights.md) - VTX-M05D local-light
  conventional shadow expansion after the CSM parity/stability gate
- [../plan/VTX-M05D-conventional-shadow-parity.md](../plan/VTX-M05D-conventional-shadow-parity.md)
  - detailed M05D execution plan

## 1.4 VTX-M05D Directional CSM Parity Gate

VTX-M05D begins with directional CSM, not local lights. No spot-light or
point-light implementation may start until the directional CSM audit has an
evidence-backed conclusion and any required remediation is documented.

The CSM audit must be grounded only in the local UE5.7 source tree:

- `Renderer/Private/ShadowSetup.cpp`
- `Renderer/Private/ShadowRendering.cpp`
- `Renderer/Private/ShadowDepthRendering.cpp`
- `Renderer/Private/SceneVisibility.cpp`
- `Renderer/Private/MeshDrawCommands.cpp`
- `Engine/Private/Components/DirectionalLightComponent.cpp`
- `Shaders/Private/ForwardShadowingCommon.ush`
- `Shaders/Private/ShadowFilteringCommon.ush`
- `Shaders/Private/ShadowProjectionPixelShader.usf`
- `Shaders/Private/DeferredLightPixelShaders.usf`

The audit must compare these UE concerns against Oxygen:

- cascade split generation, manual split handling, and max shadow distance
- cascade stabilization and snap-to-texel behavior under camera movement
- light-view basis construction and world-space convention conversion
- caster/receiver frustum bounds and cascade culling
- per-cascade depth range, reversed-Z projection, and receiver comparison
- cascade transition, last-cascade fade, and bias/normal-bias scaling
- shadow-map resource addressing and sampler/filtering contract
- shader-side cascade selection and blend behavior

The city-scale instability report is a first-class blocker: if the parity audit
does not identify the cause, the implementation work must continue with
focused capture/debugging until the unstable projected shadows are explained and
fixed. "Projected shadows exist in a static view" is not sufficient for M05D.

Deep validation captures are not required on the city scene. The city scene is
large enough that full RenderDoc inspection is expensive and noisy. Once the CSM
audit/remediation is complete, detailed capture proof should use the smaller
`physics_domain` scene, while city remains a runtime smoke/stability scenario
unless a city-only regression requires targeted inspection.

### 1.5 Directional CSM Audit Conclusions

The M05D UE5.7 audit maps the production directional CSM path to these required
behaviors:

| Concern                 | UE5.7 authority                                                                                                                                                                                                                                            | Oxygen decision                                                                                                                                                                                                                                                                                                                                                             |
| ----------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Split generation        | `FDirectionalLightSceneProxy::GetSplitDistance`                                                                                                                                                                                                            | Keep Oxygen manual/generated settings, already canonicalized from scene authoring.                                                                                                                                                                                                                                                                                          |
| No-AA frustum source    | `GetShadowSplitBoundsDepthRange` uses `GetProjectionNoAAMatrix`                                                                                                                                                                                            | Use `ResolvedView::StableProjectionMatrix()` for cascade corner extraction so TAA jitter cannot move cascades.                                                                                                                                                                                                                                                              |
| Stable bounds           | UE fits a cascade sphere around the split frustum                                                                                                                                                                                                          | Use a square sphere-derived light projection instead of a tight per-frame light-space AABB.                                                                                                                                                                                                                                                                                 |
| Light-space convention  | UE uses the directional light proxy direction and a renderer `FaceMatrix` axis remap                                                                                                                                                                       | Oxygen's contract is direction from shaded point toward source. Build a right-handed light basis from that vector, place the light eye along it, and look back at the cascade center.                                                                                                                                                                                       |
| Texel snapping          | `SetupWholeSceneProjection` snaps XY with `MaxDownsampleFactor = 4`                                                                                                                                                                                        | Snap cascade center in light-space XY at `world_texel_size * 4`.                                                                                                                                                                                                                                                                                                            |
| Directional depth range | UE clamps directional CSM subject range to at least `[-5000,+5000]`                                                                                                                                                                                        | Use a 5000-unit directional depth extent minimum to avoid clipping casters between the light and receivers.                                                                                                                                                                                                                                                                 |
| Depth encoding/compare  | UE writes normalized directional shadow depth and projects receiver depth through shadow filtering                                                                                                                                                         | Oxygen keeps its reversed-Z D3D depth convention: clear 0, depth test `GreaterOrEqual`, and receiver visible when receiver depth is greater than or equal to stored shadow depth.                                                                                                                                                                                           |
| Bias routing            | `UpdateShaderDepthBias` derives CSM clip-space depth bias from `r.Shadow.CSMDepthBias / (MaxSubjectZ - MinSubjectZ)`, scales it by `ShadowBounds.W / ResolutionX` when `ShadowCascadeBiasDistribution == 1`, then multiplies by the light user shadow bias | Mirror that contract for conventional directional shadows when `shadow.bias` is nonzero: `sampling_metadata1.z` stores the computed per-cascade clip-depth bias, not a raw scene-authored constant. Oxygen's default `shadow.bias` remains `0.0` because current meter-scale validation needs contact shadows at caster feet without peter-panning.                         |
| Slope bias routing      | `ShadowDepthVertexShader.usf` multiplies the computed depth bias by `r.Shadow.CSMSlopeScaleDepthBias` and the light `ShadowSlopeBias` default `0.5`                                                                                                        | Oxygen has no authored slope-bias knob yet. The depth pass keeps UE's internal default multiplier for the optional nonzero depth-bias path, but `shadow.bias == 0.0` still produces zero constant and zero slope depth bias.                                                                                                                                                |
| Receiver filtering      | `ShadowProjectionPixelShader.usf` + `ShadowFilteringCommon.ush` apply PCF and receiver-bias scaling                                                                                                                                                        | Keep Oxygen's compact 3x3 reversed-Z PCF for now. The computed CSM depth bias is applied in the shadow-depth pass, not subtracted again during opaque receiver comparison. Receiver-side normal/world-texel offset remains Oxygen's compact substitute for UE's broader receiver-bias/PCF machinery until the filter parity pass.                                           |
| Cascade transitions     | UE extends non-last cascade far bounds by the transition region and uses fade-plane data for split overlap/fade-out                                                                                                                                        | Oxygen builds non-last cascade projections to the extended far bound and publishes that extended bound as the cascade coverage limit. The next cascade still starts at the logical split, so the receiver's current/next blend samples two cascades that both cover the overlap region.                                                                                     |
| Depth bounds/scissor    | UE can use cascade split depth bounds during projection                                                                                                                                                                                                    | Oxygen uses shader cascade selection and does not rely on depth-bounds/scissor rejection for correctness. This is less aggressive but acceptable for M05D.                                                                                                                                                                                                                  |
| Caster culling          | UE builds accurate caster/receiver frusta per cascade                                                                                                                                                                                                      | Oxygen currently submits all prepared `PassMaskBit::kShadowCaster` draws to every cascade. This is conservative for correctness and accepted for M05D unless profiling makes it a blocker.                                                                                                                                                                                  |
| Masked casters          | UE shadow-depth passes run material clipping for masked materials                                                                                                                                                                                          | Oxygen selects `VortexShadowDepthMaskedPS` for masked shadow-caster draws.                                                                                                                                                                                                                                                                                                  |
| Storage layout          | UE packs conventional shadows in atlas/tile resources                                                                                                                                                                                                      | Oxygen keeps the dedicated directional `Texture2DArray` contract; this is an intentional engine convention, not a parity defect.                                                                                                                                                                                                                                            |
| Resolution selection    | UE resolves runtime shadow-map budgets from light settings and renderer scalability                                                                                                                                                                        | Oxygen resolves the directional `ShadowResolutionHint` selected in the Environment panel into concrete conventional shadow-map dimensions, clamped by the renderer shadow-quality tier: Low 1024, Medium 2048, High 3072, Ultra 4096. The allocator must reallocate when this hint changes, and the shadow-depth pass must invalidate cached DSVs when the surface changes. |
| Caching/scrolling       | UE supports cached/scrolling CSM work                                                                                                                                                                                                                      | Oxygen does not implement CSM caching in M05D; stability comes from no-AA frusta, sphere bounds, and texel snapping.                                                                                                                                                                                                                                                        |
| Debug/proof surface     | UE has built-in shadow debugging and GPU profiling hooks                                                                                                                                                                                                   | Oxygen uses Diagnostics, `directional-shadow-mask`, frame publication, RenderDoc proof scripts, and CDB/debug-layer capture. Full runtime proof remains a separate gate.                                                                                                                                                                                                    |

## 2. Interface Contracts

### 2.1 File Placement

```text
src/Oxygen/Vortex/
`-- Services/
    `-- Shadows/
        |-- ShadowService.h
        |-- ShadowService.cpp
        |-- Internal/
        |   |-- CascadeShadowSetup.h/.cpp
        |   |-- ConventionalShadowTargetAllocator.h/.cpp
        |   `-- ShadowCasterCulling.h/.cpp
        |-- Passes/
        |   |-- ShadowDepthPass.h/.cpp
        |   `-- CascadeShadowPass.h/.cpp
        |-- Types/
        |   |-- FrameShadowInputs.h
        |   |-- ShadowFrameBindings.h
        |   `-- ShadowCascadeBinding.h
        `-- Vsm/
            `-- Internal/   <- reserved and empty in Phase 4C
```

### 2.2 Public API

```cpp
namespace oxygen::vortex {

struct PreparedViewShadowInput {
  ViewId view_id;
  observer_ptr<const PreparedSceneFrame> prepared_scene;
  observer_ptr<const CompositionView> composition_view;
};

struct FrameShadowInputs {
  const FrameLightSelection& frame_light_set;
  std::span<const PreparedViewShadowInput> active_views;
};

class ShadowService : public ISubsystemService {
 public:
  explicit ShadowService(Renderer& renderer);
  ~ShadowService() override;

  void Initialize(graphics::IGraphics& gfx,
                  const RendererConfig& config) override;
  void OnFrameStart(const FrameContext& frame) override;
  void Shutdown() override;

  /// Stage 8: render directional conventional shadow depths and publish one
  /// per-view directional shadow payload for each active view.
  void RenderShadowDepths(const FrameShadowInputs& inputs);

  /// CPU-side inspection hook for tests/diagnostics of one view's directional
  /// shadow publication.
  [[nodiscard]] auto InspectShadowData(ViewId view_id) const
    -> const DirectionalShadowFrameData*;

  /// VSM support check. Returns false in Phase 4C.
  [[nodiscard]] auto HasVsm() const -> bool;

 private:
  Renderer& renderer_;
  std::unique_ptr<CascadeShadowSetup> cascade_setup_;
  std::unique_ptr<ConventionalShadowTargetAllocator> allocator_;
  std::unique_ptr<ShadowDepthPass> depth_pass_;
};

}  // namespace oxygen::vortex
```

### 2.3 Published Shadow Contract

The complete current target is the [EX07 wire contract](lighting-gpu-abi.md#shadow-association-and-deferred-draws).
The old singleton C++/HLSL sketch is removed; no parallel shadow interface is
normative. Preserve these sampling meanings while migrating their representation:

| Baseline meaning                        | EX07 destination                                                  |
| --------------------------------------- | ----------------------------------------------------------------- |
| Per-light directional authority         | Directional selection index and explicit shadow-reference map     |
| Matrix and depth interval               | Typed cascade/local record matrix and split/near/far fields       |
| Float-encoded layer/cube index          | uint array layer or first array layer; checked cube face addition |
| Inverse resolution and world texel size | Named float fields                                                |
| Cascade transition/far fade             | Named transition_width, fade_begin and fade_end                   |
| Authored depth bias                     | Applied once by the existing depth-pass owner                     |
| Authored receiver normal bias           | normal_bias_m, separate from existing texel offsets               |

These migrations preserve reversed depth and 3x3 PCF, and extend publication to
all eligible sources and required view products. Historical baseline evidence
below does not qualify the new ABI or required local/dual-source consumers.

The shadow-depth pass consumes the same inverse-transpose normal-matrix stream
as the base pass, using the resolved instance transform index. Its private
128-byte constants use byte offset 124 for `normal_matrices_slot`; the other
fields retain their offsets. Normal fetch/transform and slope evaluation are
skipped when the slope-bias coefficient is zero. Applying the world matrix to a
caster normal is incorrect under nonuniform scale/shear: transform-equivalent
geometry must produce the same biased shadow depth.

Projected spot maps retain Oxygen's linear reversed-depth profile: stored depth is
`1 - axial_distance / range - depth_bias`. The authored receiver normal bias is
in metres and remains separate. Receiver texel offsets use the actual axial
receiver distance, not the map's far-plane footprint, including the outer-cone
tangent.

The projected-spot constant-bias coefficient is
`clamp(3 * 512 / (depth_span_m * resolution) * 2 * user_bias, 0, 0.1)`, with the
existing safety clamps in the setup functions. It is applied once in the depth
pass, together with the bounded slope term. Before coefficient clamping, its
equivalent axial world displacement is `range * depth_bias`; at 1024 resolution
and range much larger than the near plane it is approximately `3 * user_bias`
metres before slope scaling. Default authored depth bias is zero. This is
Oxygen's retained authored response, not quantitative UE5.7 projected-cube bias
parity. Changing the projected-spot calibration or fixed 3x3 filter requires
explicit quality evidence and an authored-behavior decision; copying UE constants
into this different depth representation is not a valid conversion.

**EX07E04 cube migration (implemented and visually accepted):** the user approved
UE-aligned hardware PCF and Low/Medium/High/Ultra comparison counts of 1/5/29/29.
Cube-local maps (points and hemispherical spots) now use a cube-array SRV and
unbiased reversed-Z raster depth. The existing depth pass's `CUBE_SHADOW`
permutation omits caster bias and pixel depth override; masked coverage remains.
Receiver-to-light sampling and the producer's six RH face bases share the native
cube-addressing convention. Authored normal displacement is applied once; clip
depth bias is applied only during comparison, divided by clip W. The cube record
remains 448 bytes, with `pcf_sample_count` at offset 440 and reserved padding at 444. The [implementation contract](../plan/EX07E-point-pcf-contract.md) records
projection/bias units, UE source evidence and qualification requirements. This
filter/encoding change has an explicitly accepted quality/cost tradeoff; see the
[final measurements](../plan/EX07E-shadow-sharing-results.md).

### Compatible local-map ownership

`PrepareLocalRequests` receives the whole preparation family before individual
views render. Exact canonical caster/depth identities allow matching lights to
share immutable `ShadowMapVersion` contents even when view-local light lists or
indices differ. Per-view GPU publications keep their own association and physical
placement; directional/contact maps remain view-specific.

`SharedShadowBacking` owns the native texture, managed registration and immutable
views. Versions own physical slots; view owners, retained content and recorder
uses hold explicit capabilities. The cache and slot return targets use weak
references to avoid ownership cycles. Sharing a texture pointer alone cannot
retain the registration/descriptors or authorize a future read.

`AttachLocalReads` validates frame and preparation identity and attaches managed
uses to the actual command recorder. Prepared submission records completion
receipts and cross-queue ordering. Failed/discarded producers do not publish
reusable content; uncertain issue quarantines affected resources. Backing-wide
hazards apply even to distinct layers. Outstanding logical readers or unsubmitted
uses require copy-on-write when replacement cannot safely occur in place.

Frame publications expire at `CloseFramePublications`. Delayed readers use
`RetainLocalContent`/`ShadowContentLease` and fresh bindings, rather than retaining
frame-ring descriptors. Last-view removal cannot recycle a slot still used by
another view or GPU submission. Budget accounting retains native charges through
closing resources and diagnostic references. `InspectLocalSharing` reports
unique/spare/closing bytes, aliases, versions and cache decisions; these are not
whole-process heap totals. See the [qualified lifecycle and memory results](../plan/EX07E-shadow-sharing-results.md).

### 2.4 Directional-Light Authority

Phase 4C does not run an independent directional-light election.

ShadowService consumes the per-frame directional collection already resolved
earlier in frame execution. Publish per-light CSM records/indices for eligible
lights with `casts_shadows`; do not render maps for opted-out lights. If none
request directional shadows, the directional family is empty while independent
local-light families remain valid. Never select shadow authority solely from
atmosphere slot 0 or promote another assignment when Primary is absent.

Each directional record is also the source of its authored CSM settings:
cascade count, manual/generated split mode, maximum shadow distance, manual
cascade distances, distribution exponent, transition fraction, distance-fade
fraction, and receiver bias terms. The scene owner validates complete candidate
settings atomically before publishing them into Vortex frame-light selection so
`ShadowService` and `LightingService` consume the same directional authority.

### 2.5 Per-View Publication

The service publishes one `ShadowFrameBindings` payload per active view through
`ViewFrameBindings`.

This is distinct from any frame-shared depth-resource allocation:

- backing depth resources may be frame-shared or implementation-specific
- the published payload remains per-view and multi-view-safe
- CPU inspection is keyed by view identity and is not the GPU-facing contract

`PreparedSceneFrame` remains the Stage-2 prepared-scene packet. The shadow
publication seam begins at Stage 8 and is not silently smuggled into
`PreparedSceneFrame` as a second long-lived shadow ABI.

Phase 4 replaces the current Phase 3 interim shadow-binding shape with the
target contract in Section 2.3. The CPU struct, HLSL-side sampling contract,
and `ViewFrameBindings` routing must evolve together; the old slot-based
interim shape is not the long-lived Phase 4 contract.

## 3. Data Flow and Dependencies

### 3.1 Inputs

| Source                              | Data                                    | Purpose                                                                     |
| ----------------------------------- | --------------------------------------- | --------------------------------------------------------------------------- |
| Renderer Core frame-light selection | selected directional light              | authoritative light source for the directional baseline consumed by Stage 8 |
| Renderer Core frame-light selection | authored directional CSM settings       | cascade split, transition/fade, and receiver-bias publication               |
| Active prepared views               | per-view frusta and publication targets | cascade split computation and per-view publication                          |
| Scene / prepared-scene draw source  | shadow-casting geometry references      | directional shadow depth draws                                              |

### 3.2 Outputs

| Product                                             | Consumer                                                           | Delivery                                               |
| --------------------------------------------------- | ------------------------------------------------------------------ | ------------------------------------------------------ |
| `ShadowFrameBindings`                               | `LightingService` (Stage 12)                                       | Published through `ViewFrameBindings`                  |
| `ShadowFrameBindings`                               | Forward surface shading (Stage 9), `TranslucencyModule` (Stage 18) | Same canonical publication through `ViewFrameBindings` |
| CPU inspection view of `DirectionalShadowFrameData` | Tests / diagnostics                                                | `InspectShadowData(ViewId)` only                       |

Phase 4C publishes **directional** shadow data only. Local-light conventional
shadow bindings remain future work and are not exposed through the Phase 4C
payload.

### 3.3 Execution Flow

```text
ShadowService::RenderShadowDepths(frame_shadow_inputs)
  |
  |- Resolve selected directional light from the frame-light selection
  |- Skip directional CSM work unless the selected light casts shadows
  |
  |- For each active view:
  |    |- Compute manual or generated directional cascade splits
  |    |- Compute light-space matrices and consumer-facing addressing/bias metadata
  |    |- Allocate / reference conventional backing resources
  |    |- Cull shadow casters to the directional cascade frusta
  |    |- Render depth-only directional shadow draws
  |    `- Publish one ShadowFrameBindings payload for that view
  |
  `- Keep CPU inspection state keyed by ViewId only for diagnostics/tests
```

## 4. Resource Management

| Resource                                   | Lifetime                                             | Notes                                               |
| ------------------------------------------ | ---------------------------------------------------- | --------------------------------------------------- |
| Conventional shadow depth backing resource | Persistent or frame-persistent implementation detail | initial implementation may use atlas-backed storage |
| Shadow depth DSV views                     | Persistent / reused                                  | implementation detail                               |
| Per-view directional cascade metadata      | Per frame                                            | published through `ShadowFrameBindings`             |
| Shadow depth PSO                           | Persistent                                           | shared with masked shadow variants                  |

### 4.1 Storage Policy

Phase 4C may start with one simple conventional backing layout, but that choice
is **not** the long-lived public ABI.

The design must allow:

- an atlas-backed initial implementation for cascades
- dedicated cascade targets if that proves cleaner
- later local-light conventional targets without inheriting the directional
  storage ABI
- later VSM activation without inheriting the conventional storage ABI

The public contract therefore exposes:

- per-cascade transforms
- split distances
- consumer-facing sampling metadata
- technique/capability flags

It does **not** freeze atlas UV math or a single 2D storage shape as the
architectural contract.

## 5. Shader Contracts

### 5.1 Shadow Depth Shader

```hlsl
// Services/Shadows/ShadowDepth.hlsl

cbuffer ShadowViewConstants : register(b0) {
  float4x4 LightViewProjection;
  float DepthBias;
  float NormalBias;
};

struct ShadowVSOutput {
  float4 position : SV_Position;
};

ShadowVSOutput ShadowDepthVS(float3 pos : POSITION,
                             uint instance_id : SV_InstanceID) {
  float4x4 world = LoadInstanceTransform(instance_id);
  float4 world_pos = mul(world, float4(pos, 1.0));
  ShadowVSOutput output;
  output.position = mul(LightViewProjection, world_pos);
  output.position.z -= ConstantDepthBias + SlopeDepthBias * ClampedSlope;
  return output;
}
```

Oxygen's conventional directional CSM path uses reversed-Z depth. The shadow
surface clears to `0.0`, the depth compare is `GreaterOrEqual`, light-space
orthographic projection is built with the engine reversed-Z helper, and the
receiver comparison treats larger stored depth as closer to the light.

The shadow-depth pass constants are bound as a per-cascade CBV through
`g_PassConstantsIndex`. They carry the light view-projection matrix, UE-style
CSM constant/slope depth-bias terms derived from `sampling_metadata1.z`, the
light direction, and direct draw metadata/current-worlds/instance-data slots.
The depth shader must not reload draw bindings through mutable per-view frame
binding payloads.

### 5.2 Directional Shadow Sampling Contract

Lighting consumes the published directional conventional shadow product through
`ShadowFrameBindings`. The architectural contract does not require consumers to
know whether the underlying storage is atlas-backed, dedicated-per-cascade, or
otherwise.

`Contracts/Shadows/ShadowFrameBindings.hlsli` declares the GPU layout matching
native `Vortex/Types/ShadowFrameBindings.h`, including the directional cascades
and local-light arrays. Forward and deferred directional lighting both call
`ComputeDirectionalShadowVisibility` from
`Services/Shadows/DirectionalShadowCommon.hlsli`. Both paths therefore use the
same cascade selection, fixed 3x3 PCF, transitions and distance fade. Authored
depth bias is applied once by the shadow-depth pass; receivers add only the
shared normal and world-texel offsets. Forward normal-map shading retains its
separate geometric shadow normal from `ShadowSurfaceNormal.hlsli`.

```hlsl
// Contracts/ShadowData.hlsli

float SampleDirectionalShadow(
  float3 world_pos, uint cascade_index, ShadowFrameBindingData shadow) {
  float4 light_space_pos
    = mul(shadow.cascades[cascade_index].light_view_projection,
          float4(world_pos, 1.0));
  return SamplePublishedDirectionalShadowSurface(
    shadow.conventional_shadow_surface_handle,
    shadow.cascades[cascade_index].sampling_metadata0,
    shadow.cascades[cascade_index].sampling_metadata1,
    light_space_pos);
}
```

### 5.3 Catalog Registration

| Entrypoint                  | Profile | Notes                                           |
| --------------------------- | ------- | ----------------------------------------------- |
| `VortexShadowDepthVS`       | vs_6_0  | directional conventional shadow depth rendering |
| `VortexShadowDepthMaskedPS` | ps_6_0  | alpha-tested directional shadow path            |

## 6. Stage Integration

### 6.1 Dispatch Contract

`shadows_->RenderShadowDepths(frame_shadow_inputs)` runs at Stage 8.

The service may perform frame-shared allocation/cache work internally, but the
published payload remains per-view and multi-view-safe.

### 6.2 Null-Safe Behavior

Optional absence is valid only when shadow evaluation is explicitly disabled or
no map/contact product is required. If enabled lighting requires a shadow product,
a null service, allocation failure or missing matching publication fails the
view under EX07; it must not produce successful unshadowed output. Publish
explicit NoRequest/NoInfluence/OutsideAuthoredCoverage states for valid absence.

### 6.3 Capability Gate

Requires `kShadowing`.

## 7. Testability Approach

1. **Directional cascade validation:** single directional light, known scene ->
   verify cascade splits cover the camera frustum correctly.
2. **Directional shadow visual:** ground plane under the selected directional
   light -> visible occluder shadow. For M05D proof, use `physics_domain` for
   detailed capture evidence after the CSM audit/remediation is complete.
3. **Per-view publication:** inspect `ShadowFrameBindings` for multiple views ->
   verify view-specific matrices/addressing metadata remain isolated.
4. **RenderDoc:** inspect conventional shadow backing resources and verify
   Stage-8 ordering relative to Stage 12.

## 8. Open Questions

None for the Phase 4C baseline.

The intentionally deferred work already has named later owners:

- spot-light / point-light conventional shadows ->
  [shadow-local-lights.md](./shadow-local-lights.md)
- VSM activation -> later `ShadowService` expansion
