# VTX-M05D Conventional Shadow Parity And Local-Light Expansion

**Status:** `in_progress`
**Milestone:** `VTX-M05D - Conventional Shadow Parity And Local-Light Expansion`
**Scope owner:** Vortex ShadowService / LightingService
**Primary LLDs:** [../lld/shadow-service.md](../lld/shadow-service.md),
[../lld/shadow-local-lights.md](../lld/shadow-local-lights.md)

## 1. Goal

VTX-M05D closes the conventional shadow-map baseline for the desktop deferred
path. The milestone starts by auditing and remediating directional CSM parity
against UE5.7 because `CityEnvironmentValidation` shows unstable projected
shadows under camera movement. Only after that gate is closed may the work move
to spot-light and point-light conventional shadows.

This milestone is not a VSM milestone. It is the conventional shadow-map
baseline: stable directional CSM, then spot-light shadows, then the chosen
point-light conventional strategy if retained in the production baseline.

## 2. Mandatory Ordering

1. Update this plan and the ShadowService LLDs before implementation.
2. Perform the UE5.7 directional CSM parity audit.
3. Reproduce and explain the city-scale camera-movement instability.
4. Remediate directional CSM until the instability is fixed.
5. Prove the corrected directional CSM path.
6. Implement and prove spot-light conventional shadows.
7. Implement and prove point-light conventional shadows or explicitly document
   an approved deferral.

No local-light coding starts before the directional CSM gate has evidence.

## 3. UE5.7 Reference Set

Parity is against the local UE5.7 codebase only:

- `F:\Epic Games\UE_5.7\Engine\Source\Runtime\Renderer\Private\ShadowSetup.cpp`
- `F:\Epic Games\UE_5.7\Engine\Source\Runtime\Renderer\Private\ShadowRendering.cpp`
- `F:\Epic Games\UE_5.7\Engine\Source\Runtime\Renderer\Private\ShadowDepthRendering.cpp`
- `F:\Epic Games\UE_5.7\Engine\Source\Runtime\Renderer\Private\SceneVisibility.cpp`
- `F:\Epic Games\UE_5.7\Engine\Source\Runtime\Renderer\Private\MeshDrawCommands.cpp`
- `F:\Epic Games\UE_5.7\Engine\Source\Runtime\Engine\Private\Components\DirectionalLightComponent.cpp`
- `F:\Epic Games\UE_5.7\Engine\Shaders\Private\ForwardShadowingCommon.ush`
- `F:\Epic Games\UE_5.7\Engine\Shaders\Private\ShadowFilteringCommon.ush`
- `F:\Epic Games\UE_5.7\Engine\Shaders\Private\ShadowProjectionPixelShader.usf`
- `F:\Epic Games\UE_5.7\Engine\Shaders\Private\DeferredLightPixelShaders.usf`

Local-light audit additions:

- `Renderer\Private\ProjectedShadowInfo.cpp` or equivalent UE5.7 projected
  shadow implementation files if present in this tree.
- UE5.7 shadow setup/rendering paths for spot and point whole-scene shadows.
- UE5.7 shader files used for local-light shadow projection and filtering.

## 4. Directional CSM Audit Checklist

Compare UE5.7 and Oxygen for:

- cascade split generation, manual splits, max distance, transition, and fade;
- cascade stabilization and snap-to-texel behavior under camera movement;
- light-view basis construction and Oxygen world-space conversion;
- near/far depth range, reversed-Z projection, and receiver comparison signs;
- caster and receiver bounds used for cascade frustum construction;
- shadow caster culling, draw filtering, and shadow-relevant bounds;
- depth bias, normal bias, slope scaling, and world-texel scaling;
- shader cascade selection, cascade blending, and last-cascade fade;
- PCF/filtering contract and out-of-shadow-map behavior;
- debug/capture observability for cascade selection and shadow mask.

The audit result must be recorded in this plan before remediation is called
complete.

### 4.1 Directional CSM Audit Record

| UE5.7 concern | Oxygen result | Remediation / accepted divergence |
| --- | --- | --- |
| `DirectionalLightComponent.cpp::GetSplitDistance` split distribution | Oxygen already supports manual distances and generated distribution from canonicalized scene CSM settings. | Keep. Oxygen does not mirror UE far-shadow or distance-field cascade extensions in M05D. |
| `GetShadowSplitBoundsDepthRange` no-AA projection | Oxygen previously extracted cascade corners from the current inverse view-projection, which can include temporal jitter. | Remediate by extracting corners from `StableProjectionMatrix() * ViewMatrix()`. |
| UE sphere-fitted cascade bounds | Oxygen previously used tight light-space AABB bounds around split corners. | Remediate with a sphere-derived square projection for stable extents. |
| UE directional light basis / `FaceMatrix` convention | UE builds light-space matrices from the directional light proxy and remaps axes through `FaceMatrix`. Oxygen stores the directional-light vector as direction from shaded point toward source. | Accepted Oxygen convention: `CascadeShadowSetup` builds a right-handed light basis from direction-to-source, places the light eye along that direction, looks back toward the cascade center, and uses the same direction for receiver and slope-bias math. No extra axis-remap layer is required. |
| `SetupWholeSceneProjection` texel snapping with `MaxDownsampleFactor = 4` | Oxygen had no cascade-center snapping. | Remediate with light-space XY snapping at four shadow texels. |
| `SetupWholeSceneProjection` directional `DepthRangeClamp = 5000` | Oxygen used tight receiver-corner depth range. This can clip valid casters and explained the malformed/truncated right-frustum shadows. | Remediate with a 5000-unit directional depth extent minimum. |
| Directional depth encoding and receiver compare | UE normalizes directional shadow depth for projection and compares receiver depth against the stored shadow depth in projection/filtering shaders. Oxygen uses a reversed-Z depth surface, clears to 0, writes with `GreaterOrEqual`, and treats larger stored depth as closer to the light. | Accepted Oxygen convention: the sign is internally consistent for the dedicated `Texture2DArray` path. Receiver-side subtraction of CSM depth bias was removed because UE's opaque projection path does not subtract the CSM depth bias a second time. |
| `UpdateShaderDepthBias` per-shadow depth-bias publication | Oxygen previously treated the authored scene `shadow.bias` as a raw clip-space bias. UE computes CSM bias as `r.Shadow.CSMDepthBias / depth_span`, scales it by cascade radius over resolution when `ShadowCascadeBiasDistribution == 1`, then multiplies by the user shadow-bias multiplier. | Remediate by computing the UE-style per-cascade clip-depth bias in setup, binding it to the shadow-depth pass, and not subtracting it again during opaque receiver comparison. |
| `ShadowDepthVertexShader.usf` constant/slope bias application | Oxygen's depth shader previously had only a constant depth-bias field. UE applies constant plus slope bias in the shadow-depth pass. | Remediate by passing constant/slope/max-slope terms to the depth shader. Oxygen keeps UE's default slope-bias multiplier internally for nonzero `shadow.bias`; with the current `0.0` authored bias both terms evaluate to zero. |
| Directional shadow resolution quality | Oxygen's conventional allocator ignored the Environment panel's directional shadow resolution hint and always allocated 2048. A first wiring pass also exposed that the shadow-depth pass kept stale DSVs after surface reallocation. | Remediate by carrying the selected `ShadowResolutionHint` through frame-light selection, resolving it through the renderer shadow-quality budget at Low 1024, Medium 2048, High 3072, or Ultra 4096, and invalidating cached cascade DSVs when the surface changes. |
| `ShadowProjectionPixelShader.usf` / `ShadowFilteringCommon.ush` filtering | Oxygen has a compact reversed-Z 3x3 PCF path rather than UE's full projection shader permutations. | Accepted for M05D if proof shows stable conventional projected shadows; no PCSS/subsurface/transmission parity claimed. |
| `GetShadowSplitBounds` transition/fade extension | Oxygen previously kept authored split distances as the published cascade boundaries while blending against the next cascade inside the pre-split band, which could sample a next cascade outside the region it was fitted to cover. | Remediate by extending every non-last cascade projection and published coverage far bound by the transition width, while keeping the next cascade's logical near split unchanged. This mirrors UE's overlap/fade-plane coverage shape within Oxygen's single-pass receiver shader. |
| `ShadowRendering.cpp::SetDepthBoundsTest` and split-depth projection scissoring | UE uses split near/far depth bounds while projecting each cascade. Oxygen performs cascade selection in shader using view depth and does not use depth-bounds/scissor rejection. | Accepted correctness-preserving divergence for M05D. It may be less efficient, but it does not explain missing or unstable projected shadows. |
| UE caster/receiver frusta and per-cascade culling | UE builds accurate culling volumes and subject primitive lists per projected shadow. Oxygen filters shadow casters by `PassMaskBit::kShadowCaster`, then submits the prepared shadow-caster draw list to every cascade. | Accepted conservative correctness divergence for M05D; performance culling is not required to fix projected-shadow correctness. |
| Shadow-depth raster state and masked casters | UE shadow-depth mesh passes support masked materials through the shadow-depth pixel shader and material clipping. Oxygen has `VortexShadowDepthMaskedPS` selected per draw via `PassMaskBit::kMasked`, and the opaque path uses the same depth target contract. | Keep. This covers the correctness requirement for masked alpha casters in the conventional directional path. |
| UE atlas/tile storage | Oxygen uses a dedicated directional `Texture2DArray`. | Accepted Oxygen resource convention; public ABI stays `ShadowFrameBindings`. |
| UE CSM caching/scrolling | Oxygen has no CSM cache. | Accepted M05D divergence; stability is provided by no-AA frusta, stable bounds, and snapping. |
| Debug and proof observability | UE has extensive shadow debug/profiling infrastructure around shadow setup, rendering, and projection. Oxygen has the Diagnostics panel, the `directional-shadow-mask` debug mode, shadow frame publication, and RenderDoc/CDB capture scripts. | Accepted for the audit gate. Runtime proof still has to use small-scene captures after the user allows validation; no completion claim depends on city deep captures. |

## 5. Implementation Slices

### Slice A - Design Scope And Truth Surface

**Status:** `planned`

Tasks:

- Update ShadowService and local-light shadow LLDs.
- Create this dedicated M05D plan.
- Update `PLAN.md`, `IMPLEMENTATION_STATUS.md`, and `plan/README.md`.

Validation:

- `git diff --check`.

### Slice B - Directional CSM UE5.7 Parity Audit

**Status:** `completed_source_audit`

Tasks:

- Study the UE5.7 files listed in section 3.
- Map each directional CSM concern in section 4 to existing Oxygen code.
- Identify correct implementations, shortcuts, hacks, and missing pieces.
- Record accepted Oxygen divergences only when they are intentional and do not
  explain the city instability.

Validation:

- Source-to-target mapping recorded in section 4.1.
- No runtime/build validation is implied by this source audit.

### Slice C - City-Scale Instability Reproduction

**Status:** `superseded_by_targeted_audit_and_small_scene_proof`

Tasks:

- Use the city scene as the motivating runtime smoke scenario, not the deep
  capture scene.
- Do detailed post-remediation proof on `physics_domain` after the full CSM
  parity audit is complete.
- Add or improve debug modes only if the smaller proof scene cannot expose
  cascade index, shadow UV/depth, or shadow-mask visibility.

Validation:

- Runtime city observation that the reported malformed right-frustum shadows are
  explained by the depth-range audit finding.
- Smaller-scene RenderDoc/debug-layer proof after remediation.

### Slice D - Directional CSM Remediation

**Status:** `in_progress`

Tasks:

- Fix the directional CSM issues found in slices B and C.
- Keep `ShadowFrameBindings` and shader structs in lockstep.
- Preserve Oxygen world-space and reversed-Z conventions explicitly.
- Add focused unit tests for any CPU-side split/stabilization/bounds math.

Validation:

- Focused build/tests.
- ShaderBake/catalog validation if shadow shaders or ABI change.
- CDB/D3D12 debug-layer audit.
- RenderDoc proof showing Stage 8 cascade writes, Stage 12 consumption, stable
  projected shadows under camera movement, and expected debug-mask behavior.
- User visual confirmation for the city-scale scenario.

### Slice E - Spot-Light Conventional Shadows

**Status:** `validated`

UE5.7 local-source audit for this slice:

| UE5.7 concern | Oxygen target for Slice E |
| --- | --- |
| `SpotLightComponent.cpp::GetWholeSceneProjectedShadowInitializer` creates one whole-scene projected shadow with light-relative pre-translation, `WorldToLight`, `InvTanOuterCone` scale, cone subject bounds, `MinLightW = 0.1`, and `MaxDistanceToCastInLightW = Radius`. | Build one view-independent spot-light projection per shadow-casting spot light using the authored light transform, `outer_cone_cos`, range, and reversed-Z perspective depth. Publish the resulting matrix as a consumer-facing spot binding, not as a directional cascade. |
| `ShadowSetup.cpp::CreateWholeSceneProjectedShadow` computes local-light shadow resolution from effective screen radius, clamps to renderer/scalability limits, uses a border for non-cubemap shadows, and adds shadow-casting primitives affected by the light. | Slice E uses the existing conventional quality tiers and authored `resolution_hint` for deterministic first activation. It conservatively submits prepared `kShadowCaster` draws, matching the current directional correctness-first culling divergence. Screen-radius resolution fading, borders, caching, and per-light interaction lists remain documented gaps until a later optimization pass. |
| `FProjectedShadowInfo::SetupWholeSceneProjection` uses non-directional subject depth from the light-space subject bounds, records caster/receiver frusta, and updates shader depth-bias terms. | Use near `0.1`, far `range`, `MakeReversedZPerspectiveProjectionRH_ZO`, and UE-style whole-scene spot depth-bias scaling: `3.0 * 512 / ((far-near) * resolution) * 2 * user_bias`, clamped to `0.1`, with slope multiplier `3.0` and max slope `1.0`. |
| `ShadowDepthVertexShader.usf` / `ShadowDepthPixelShader.usf` keep perspective spot rasterization projection separate from biased shadow depth. UE's perspective-correct path carries bias to pixel depth output instead of clipping casters by moving `SV_Position` before rasterization. | Spot slices render with the projected cone for clipping/rasterization but write biased linear reversed depth along the spot axis. Stage 12 samples that same spot-axis depth convention before the 3x3 PCF compare. This fixes the `SpotShadowValidation` capture where UE-style slope bias pushed all caster `SV_Position.z` behind the far plane and left `Vortex.SpotShadowSurface` at clear depth. |
| `DeferredLightPixelShaders.usf` obtains local-light attenuation and multiplies it by `GetLightAttenuationFromShadow`; `ShadowProjectionCommon.ush`/`ShadowFilteringCommon.ush` provide projection and PCF filtering. | Extend Stage 12 spot lighting to sample the published spot shadow array before BRDF evaluation. Use the same compact 3x3 reversed-Z PCF convention already accepted for directional conventional shadows. |

Tasks:

- [x] Design the spot-light shadow payload extension under `ShadowFrameBindings`.
- [x] Render spot-light shadow depth under Stage 8.
- [x] Consume the spot-light shadow product in Stage 12 deferred lighting.
- [x] Document Stage 18 as a deferred local-light shadow consumer. The current
  forward/translucency path accumulates positional lights without conventional
  spot shadow lookup, so Slice E does not claim translucent spot-shadow parity.
- [x] Add a focused validation scene with a clear spot-light projected shadow.

Validation:

- Focused tests for publication and shader ABI.
- RenderDoc/CDB proof. Diagnostic evidence before the final bias/depth
  remediation:
  `spot-shadow-validation.bias01.spot-shadow-probe.txt` showed Stage 8 issuing
  two caster draws while `Vortex.SpotShadowSurface` remained all clear depth
  (`min=max=0`), and Stage 12 bound the spot shadow SRV correctly. Post-fix
  proof `spot-shadow-validation.bias0.final.spot-shadow-probe.txt` shows
  Stage 8 spot draws `168,171`, non-clear `Vortex.SpotShadowSurface` depth
  (`max=0.463512063`, center `0.447184265`), Stage 12 spot draw `248`, and
  Stage 12 binding `Vortex.SpotShadowSurface`.
- User visual confirmation: on 2026-04-27 the validation scene showed visible
  spot shadows after the spot-axis depth fix, and the user confirmed the shadows
  were perfect after setting the authored spot shadow bias to `0.0` and
  recooking `SpotShadowValidation`.
- Focused validation: `cmake --build out\build-ninja --target
  Oxygen.Vortex.ShadowService.Tests Oxygen.Vortex.SceneRendererDeferredCore.Tests
  Oxygen.Vortex.LightingService.Tests
  Oxygen.Graphics.Direct3D12.ShaderBakeCatalog.Tests
  oxygen-graphics-direct3d12_shaders --parallel 4` succeeded; test executables
  passed ShadowService `8/8`, SceneRendererDeferredCore `41/41`,
  LightingService `4/4`, and ShaderBakeCatalog `4/4`.
- CDB/D3D12 audit:
  `spot-shadow-validation.bias0.final.debug-layer.report.txt` passed with
  runtime exit `0`, no debugger break, `0` D3D12 errors, `0` DXGI errors,
  `0` blocking warnings, and one accepted DXGI live-factory shutdown warning.

### Slice F - Point-Light Conventional Shadows

**Status:** `validated`

UE5.7 source mapping:

- `ShadowSetup.cpp` uses a whole-scene point-light shadow cubemap with six
  cube-face views, a 90-degree reversed-Z projection, and a one-pass path when
  the runtime supports layered point-light shadow rendering.
- `ShadowProjectionCommon.ush` samples point-light shadows through cube-face
  selection and compares the receiver against the face depth convention.

Oxygen implementation:

- `ShadowFrameBindings` now carries a conventional point-shadow surface handle,
  point count, and up to four `PointShadowBinding` records with six
  face-projection matrices per point light.
- `PointShadowSetup` builds one binding per shadow-casting point light using a
  90-degree reversed-Z projection and authored range/bias values.
- `ConventionalShadowTargetAllocator` allocates a cube-array point shadow
  surface. The current renderer records the six faces with
  `ShadowDepthPass::RecordSlices` rather than UE's one-pass layered cubemap
  draw. This is the accepted M05D divergence; the storage leaves a direct
  upgrade path to layered cube rendering.
- Stage 8 passes each point face direction and inverse range into the shared
  shadow-depth shader so the depth pass writes the same reversed axial depth
  that Stage 12 compares.
- Stage 12 point deferred lighting samples the published point shadow surface
  before BRDF evaluation and multiplies local-light attenuation by point-shadow
  visibility.
- The point-light proxy sphere had mixed winding in its middle bands, causing
  the outside-volume deferred pass to behave like a solid light shell. The proxy
  geometry is now factored into `DeferredLightProxyGeometry`, all point-proxy
  triangles wind outward, and a regression test verifies that invariant.

Validation evidence:

- Focused build/shader validation passed:
  `cmake --build out\build-ninja --target Oxygen.Vortex.SceneRendererDeferredCore.Tests Oxygen.Vortex.ShadowService.Tests oxygen-graphics-direct3d12_shaders --parallel 4`.
- Focused tests passed: `Oxygen.Vortex.SceneRendererDeferredCore.Tests`
  `43/43`; `Oxygen.Vortex.ShadowService.Tests` `9/9`.
- ShaderBake repacked `186` shader modules after the point-shadow HLSL change.
- CDB/D3D12 audit
  `point-shadow-validation.final.debug-layer.report.txt` passed with runtime
  exit `0`, no debugger break, `0` D3D12 errors, `0` DXGI errors, and `0`
  blocking warnings.
- RenderDoc probe `point-shadow-validation.final.point-shadow-probe.txt` passed:
  Stage 8 point shadow draws
  `168,171,187,190,206,209,225,228,244,247,263,266`; non-clear point-shadow
  cube-array slices `0`, `2`, and `5` with max depth about `0.69985/0.70000`;
  Stage 12 point draw `343`; Stage 12 bound
  `Vortex.PointShadowCubeSurface`; Stage 12 `SceneColor` changed with max
  `[839.5, 772.5, 688.5, 1.0]`.
- User visual validation on 2026-04-27 confirmed both the point-light shell bug
  and the point-shadow cube artifact were fixed.

## 6. Exit Gate

M05D can move to `validated` only when:

- the UE5.7 CSM parity audit is recorded;
- the city-scale CSM instability is reproduced, explained, remediated, and
  proven stable under camera movement;
- spot-light conventional shadows are implemented and proven, unless the scope
  is explicitly narrowed with approval;
- point-light conventional shadows are implemented and proven or explicitly
  deferred with approval;
- docs/status are updated in one concise ledger row;
- focused builds/tests pass;
- ShaderBake/catalog validation runs when shader requests or shader code
  changes;
- CDB/D3D12 debug-layer audit reports no blocking errors or warnings;
- RenderDoc proof validates shadow products and projected receiver shadows;
- user visual validation is requested and received for visual proof scenarios.

As of 2026-04-27, directional conventional shadow parity/remediation, Slice E
spot-light conventional shadows, and Slice F point-light conventional shadows
are implemented, documented, and validated. Stage 18 translucent local-light
shadow consumption remains explicitly deferred outside M05D. The conventional
local-light baseline is validated for deferred Stage 12 lighting.

If any future regression invalidates one of the evidence items above, M05D must
return to `in_progress` until the failed proof is replaced.
