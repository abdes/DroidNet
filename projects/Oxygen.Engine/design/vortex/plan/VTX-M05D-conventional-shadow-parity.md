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
| `SetupWholeSceneProjection` texel snapping with `MaxDownsampleFactor = 4` | Oxygen had no cascade-center snapping. | Remediate with light-space XY snapping at four shadow texels. |
| `SetupWholeSceneProjection` directional `DepthRangeClamp = 5000` | Oxygen used tight receiver-corner depth range. This can clip valid casters and explained the malformed/truncated right-frustum shadows. | Remediate with a 5000-unit directional depth extent minimum. |
| `UpdateShaderDepthBias` per-shadow depth-bias publication | Oxygen previously treated the authored scene `shadow.bias` as a raw clip-space bias. UE computes CSM bias as `r.Shadow.CSMDepthBias / depth_span`, scales it by cascade radius over resolution when `ShadowCascadeBiasDistribution == 1`, then multiplies by the user shadow-bias multiplier. | Remediate by computing the UE-style per-cascade clip-depth bias in setup, binding it to the shadow-depth pass, and not subtracting it again during opaque receiver comparison. |
| Directional shadow resolution quality | Oxygen's conventional allocator ignored the Environment panel's directional shadow resolution hint and always allocated 2048. A first wiring pass also exposed that the shadow-depth pass kept stale DSVs after surface reallocation. | Remediate by carrying the selected `ShadowResolutionHint` through frame-light selection, resolving it through the renderer shadow-quality budget at Low 1024, Medium 2048, High 3072, or Ultra 4096, and invalidating cached cascade DSVs when the surface changes. |
| `ShadowProjectionPixelShader.usf` / `ShadowFilteringCommon.ush` filtering | Oxygen has a compact reversed-Z 3x3 PCF path rather than UE's full projection shader permutations. | Accepted for M05D if proof shows stable conventional projected shadows; no PCSS/subsurface/transmission parity claimed. |
| `GetShadowSplitBounds` transition/fade extension | Oxygen keeps authored split distances as the published cascade boundaries and blends in shader over the configured terminal band, instead of extending non-last cascade bounds. | Accepted ABI simplification unless physics-domain proof exposes visible cascade seams. |
| UE caster/receiver frusta and per-cascade culling | Oxygen submits all prepared shadow casters to each cascade. | Accepted conservative correctness divergence for M05D; performance culling is not required to fix projected-shadow correctness. |
| UE atlas/tile storage | Oxygen uses a dedicated directional `Texture2DArray`. | Accepted Oxygen resource convention; public ABI stays `ShadowFrameBindings`. |
| UE CSM caching/scrolling | Oxygen has no CSM cache. | Accepted M05D divergence; stability is provided by no-AA frusta, stable bounds, and snapping. |

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

**Status:** `in_progress`

Tasks:

- Study the UE5.7 files listed in section 3.
- Map each directional CSM concern in section 4 to existing Oxygen code.
- Identify correct implementations, shortcuts, hacks, and missing pieces.
- Record accepted Oxygen divergences only when they are intentional and do not
  explain the city instability.

Validation:

- Source-to-target mapping recorded in section 4.1.
- No implementation status upgrade without this mapping.

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

**Status:** `planned`

Tasks:

- Design the spot-light shadow payload extension under `ShadowFrameBindings`.
- Render spot-light shadow depth under Stage 8.
- Consume the spot-light shadow product in Stage 12 deferred lighting and
  Stage 18 where applicable.
- Add a focused validation scene with a clear spot-light projected shadow.

Validation:

- Focused tests for publication and shader ABI.
- RenderDoc/CDB proof.
- User visual confirmation if requested by the validation scenario.

### Slice F - Point-Light Conventional Shadows

**Status:** `planned`

Tasks:

- Confirm or revise the one-pass cubemap depth-target strategy after UE5.7
  review.
- Implement point-light shadow depth, publication, and deferred consumption if
  retained.
- If deferred, document the approved reason and keep the milestone status
  honest.

Validation:

- Focused tests and RenderDoc/CDB proof if implemented.
- Explicit deferral record if not implemented.

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

If any item lacks evidence, M05D remains `in_progress`.
