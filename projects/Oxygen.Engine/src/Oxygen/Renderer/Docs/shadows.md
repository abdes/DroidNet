# Renderer Shadow System Design

Purpose: define the final renderer shadow architecture for Oxygen so phased
implementation work lands on stable engine contracts from the first iteration.

Cross-references: [lighting_overview.md](lighting_overview.md) |
[passes/data_flow.md](passes/data_flow.md) |
[passes/design-overview.md](passes/design-overview.md) |
[implementation_plan.md](implementation_plan.md)

Scene-authored shadow properties and light invariants remain owned by:

- `src/Oxygen/Scene/Light/light-deisgn.md`

This document defines how the renderer consumes those authored properties and
turns them into runtime shadowing.

If a shadow capability is ever narrowed, removed, or otherwise changed in
scope, record that decision in the section that owns the capability. Do not add
blanket "non-goals" or catch-all exclusion sections.

## Implementation Status

Current execution status for this design lives in
[implementation_plan.md](implementation_plan.md). The first Phase 1 slice is
now present in code:

- `LightManager` collects authored directional shadow intent as
  `DirectionalShadowCandidate` records instead of owning live shadow metadata
  uploads.
- `ShadowManager` is now the top-level shadow scheduler/coordinator and
  delegates conventional raster publication/planning to
  `ConventionalShadowBackend`.
- Shared shadow publication for the conventional directional path now
  publishes:
  - `ShadowInstanceMetadata`
  - `DirectionalShadowMetadata`
- The shading-facing and raster-planning contracts are now explicit:
  - `ShadowFramePublication`
  - `RasterShadowRenderPlan`
- `ShadowFrameBindings` now routes those published buffers through
  `ViewFrameBindings.shadow_frame_slot`.
- ScenePrep/draw finalization now publishes explicit shadow-caster pass routing
  through `PassMask::kShadowCaster` for currently supported caster domains.
- Main-view visibility is now separate from shadow-caster routing through
  `PassMask::kMainViewVisible`, so offscreen shadow casters survive ScenePrep
  without leaking into main-view shading/depth paths.
- `ConventionalShadowRasterPass` now consumes backend-neutral raster jobs
  instead of reaching into a directional-only published view blob.
- Forward shading now dispatches from shared `ShadowInstanceMetadata` and then
  resolves the conventional directional payload inside shadow helpers.
- Conventional shadow publication invalidates from shadow-relevant view,
  candidate, and caster inputs rather than `ViewId` alone.
- Focused automated coverage now exists for:
  - shading publication plus raster-plan publication
  - synthetic-sun publication
  - invalidation when shadow inputs change within a frame
  - shadow-only casters staying out of main-view partitions

This does not mean the conventional directional path is complete. The current
implementation status is:

- Shared shadow-product publication: done for the conventional directional path
- Shadow-caster draw classification: done for the conventional directional path
- Conventional directional shadow rendering: done for conventional CSM
- Multi-technique architecture remediation: done for conventional raster
- Family-selection policy: not started
- Virtual shadow-map backend: not started

## 1. Design Rules

- One authored shadow model, multiple runtime implementations. Delivery phases
  may activate subsets of producers and consumers, but they must not introduce
  temporary shadow-only APIs, duplicate metadata layouts, or parallel shading
  paths.
- Implementation choice is renderer policy. Scene data authors whether a light
  casts shadows; the renderer chooses which runtime family produces those
  shadows for the current platform, quality tier, and budget.
- Bindless-first. Shadow metadata, pooled maps, virtual page tables, residency
  buffers, and debug resources are exposed through the existing bindless model
  rather than bespoke root parameters.
- Stable before soft. Temporal stability, deterministic selection, residency
  behavior, and correct eligibility rules take precedence over wider kernels and
  softer penumbra features.
- Scene data stays authoritative. The renderer consumes `casts_shadows`,
  `contact_shadows`, `resolution_hint`, cascade settings, mobility, and node
  shadow flags as authored inputs; it does not invent an alternate shadow
  authoring model for conventional maps versus virtual shadow maps.
- Resource budgets and residency must be explicit and deterministic. When
  budgets are hit, the selection policy and fallback order must be stable frame
  to frame.
- Shadowing is a system, not a pass. Extraction, scheduling, implementation
  selection, resource allocation, virtual residency, shadow rendering, lighting
  evaluation, debug tooling, and invalidation all belong to the same runtime
  contract.

## 2. Final Supported Scope

The final shadow system supports all runtime light families already represented
in the scene layer and allows the renderer to select different shadow
implementation families per light product.

### 2.1 Shadow domains

| Shadow domain | Final runtime product | Supported implementation families | Notes |
| --- | --- | --- | --- |
| Directional lights | Shadowed directional lighting with stable multi-range coverage | Conventional cascaded maps, virtual shadow maps | The canonical sun and additional shadowed directional lights use the same selection and metadata contract |
| Spot lights | Shadowed projective local lighting | Conventional single-projective maps, virtual shadow maps | One logical shadow product per active shadowed spot light |
| Point lights | Shadowed omni local lighting | Conventional cubemap shadows, virtual shadow maps | One logical shadow product per active shadowed point light |
| Contact shadows | Screen-space receiver refinement | Shared per-view refinement path | Multiplies or refines map-based/virtualized shadow visibility based on authored light settings |

Transparent, masked, transmissive, foliage, and other
material-domain-specific shadow participation is governed by the material shadow
participation policy defined in the sections below rather than by separate
shadow systems.

### 2.2 Runtime implementation families

The renderer owns the runtime shadow implementation family chosen for each
shadow product.

| Family | Intended use | Core resources | Required in final architecture |
| --- | --- | --- | --- |
| Conventional shadow maps | Baseline broad-platform path | `Texture2DArray` / `TextureCubeArray` depth pools plus structured metadata | Yes |
| Virtual shadow maps | High-end scalable path for large scenes and high detail | Physical page pool, page tables/indirection, residency feedback, per-product virtual addressing metadata | Yes |
| Future hybrid extensions | Optional future work such as ray-traced shadow queries | Family-specific resources published behind the same shared shadow-product contract | Architecture must allow this without replacing the shared contracts, but it is not a required delivery target for this document |

For this document, "virtual shadow maps" refers to virtualized, page-tabled
shadow-map resources, not variance shadow maps.

## 3. Authoritative Inputs

The renderer shadow system consumes the following authored and policy inputs.

### 3.1 Light inputs

- `scene::CommonLightProperties::casts_shadows`
- `scene::CommonLightProperties::shadow`
- `scene::CommonLightProperties::mobility`
- `scene::DirectionalLight::CascadedShadows()`
- `scene::DirectionalLight::IsSunLight()`
- `scene::DirectionalLight::GetEnvironmentContribution()`
- Light intensity, color, range, cone angles, source radius, and transform

### 3.2 Node inputs

- Effective `SceneNodeFlags::kVisible`
- Effective `SceneNodeFlags::kCastsShadows`
- Effective `SceneNodeFlags::kReceivesShadows`

### 3.3 Render-item inputs

- `RenderItemData::cast_shadows`
- `RenderItemData::receive_shadows`
- Material-domain shadow participation policy
- Geometry, transform, and bounds needed for light-space culling and virtual
  page coverage

### 3.4 Renderer policy inputs

Renderer policy selects the implementation family without changing scene
authoring.

- Quality tier and per-tier shadow policy
- Platform/backend capabilities needed by a family
- View configuration and per-view relevance
- Debug overrides that force or disable specific families
- Deterministic budget caps and residency limits
- Synthetic primary-sun shadow policy when the effective sun is authored
  through `scene::environment::Sun` rather than a scene directional light.
  Until the `Sun` component grows explicit shadow-tuning knobs, the renderer
  must use the canonical directional-light defaults for cascade count,
  split distribution, bias, and resolution hint.

## 4. Runtime Contracts

### 4.1 Light eligibility

A light is eligible to produce runtime shadows only when all of the following
are true.

- The light's owning node is effectively visible.
- `affects_world` is true.
- `casts_shadows` is true.
- The owning node's effective `kCastsShadows` flag is true.
- Mobility is not `kBaked`.

`LightManager` remains the authoritative collector for light-facing GPU data.
Its shadow payloads evolve to the final metadata contract rather than being
replaced by a second shadow-specific light collector.

Synthetic sun shadowing is renderer-owned shadow input, not a fake scene light.
When the effective sun is `SunSource::kSynthetic` and `Sun::CastsShadows()`
is true, the renderer must provide a synthetic directional shadow input to
`ShadowManager` on the same shared shadow-product contract. The resolved sun's
shadow index is published through `ShadowFrameBindings`, not through lighting
buffers.

### 4.2 Caster eligibility

Shadow caster eligibility is derived from render-item state, not only from
camera-visible draw lists.

- A render item participates in shadow rendering only when
  `cast_shadows == true`.
- Shadow caster submission is filtered by the light's shadow volume or by the
  virtual page coverage request, not by the main camera visibility result
  alone.
- Opaque and masked materials are required caster participants.
- Additional material domains join shadow casting through explicit
  material-domain shadow participation policies on the same draw metadata and
  pass classification system.
- Shadow rendering must preserve double-sided material handling where required
  by the material policy.

### 4.3 Receiver eligibility

- A shaded surface receives runtime shadows only when `receive_shadows == true`.
- Receiver participation is evaluated in the main shading path; it is not a
  separate material system.
- Contact shadow refinement obeys the same receiver gate.

### 4.4 Shadow product model

The stable renderer contract is a shadow product, not a specific pool layout.

Each active shadow product must have stable identity and metadata covering:

- Owning light identity
- Shadow domain and projection kind
- Selected implementation family
- Bias/filter metadata
- Validity and debug bits
- Family-specific addressing payload needed by shaders

Examples:

- A conventional directional light may publish one shadow product with
  cascaded addressing metadata.
- A virtual directional light may publish one shadow product with clipmap/page
  addressing metadata.
- A point light may publish one shadow product whose family-specific payload
  addresses either a cubemap array allocation or a virtual six-face page set.

Main-view and transparent shading consume the same shadow-product list and
shared light metadata regardless of implementation family.

### 4.5 Implementation selection

`ShadowManager` resolves an implementation family for each eligible shadow
product from renderer policy.

Selection inputs include:

- Light type and authored shadow settings
- Whether the light is the canonical sun
- Mobility and cacheability
- Quality tier
- Platform/backend support for the requested family
- View relevance and projected influence
- Current shadow budget and residency pressure
- Debug policy overrides

Selection must be deterministic. A product may fall back from the preferred
family only through explicit policy and stable ordering; spontaneous frame-local
thrashing between conventional and virtual families is not allowed.

Implementation choice is renderer-owned. Scene data does not gain separate
"virtual shadow map" authoring controls.

### 4.6 Mobility, caching, and invalidation

- `kRealtime`: shadow products may be regenerated every frame as needed.
- `kMixed`: shadow products may be cached and refreshed only when invalidated
  by a relevant scene or policy change.
- `kBaked`: excluded from runtime shadow-product generation.

Invalidation must be driven by the actual dependencies of the shadow product.

- Light transform or authored shadow-setting change
- Selected implementation-family change
- Caster transform change
- Caster material change when material-domain shadow participation changes
- Geometry residency change or LOD/mesh change affecting the shadow pass
- Virtual page coverage change that exposes previously unrendered regions
- Global quality-tier change affecting allocated shadow resources or residency
  policy

### 4.7 Deterministic budgeting and residency

Shadow allocation and virtual residency use deterministic priority keys so
budget pressure does not introduce temporal flicker.

Priority inputs include:

- Light type
- Whether the light is the canonical sun
- Implementation family
- Mobility
- Authored `ShadowResolutionHint`
- View relevance / projected influence
- Intensity and range for tie-breaking
- Stable scene identity for final ordering

Budgeting may reduce active shadow work in a frame, lower the chosen
implementation family, or defer low-priority page updates, but it must not
change the underlying shared metadata contracts seen by the renderer and
shaders.

## 5. GPU Resource Model

The final system exposes shared metadata buffers plus family-specific resources.

### 5.1 Shared shadow resources

| Resource | Final form | Ownership | Consumers |
| --- | --- | --- | --- |
| Shadow instance metadata | Structured buffer | Light/shadow runtime | Forward shaders, transparent shaders, debug views |
| Directional shadow metadata | Structured buffer | Light/shadow runtime | Forward shaders, debug views |
| Local shadow metadata | Structured buffer | Light/shadow runtime | Forward shaders, debug views |
| Shadow implementation metadata | Structured buffer | Shadow runtime | Sampling helpers, debug views |
| Contact shadow mask/resources | Per-view textures/buffers | Contact shadow runtime | Forward shaders, transparent shading, debug views |

The shared metadata is family-agnostic at the top level and contains the
family-specific payload needed to dispatch into the correct sampling path.

### 5.2 Conventional shadow-map resources

| Resource | Final form | Ownership | Consumers |
| --- | --- | --- | --- |
| Directional cascade pool | `Texture2DArray` depth | Conventional shadow runtime | Directional shadow passes, shaders, debug views |
| Spot shadow pool | `Texture2DArray` depth | Conventional shadow runtime | Spot shadow passes, shaders, debug views |
| Point shadow pool | `TextureCubeArray` depth | Conventional shadow runtime | Point shadow passes, shaders, debug views |

### 5.3 Virtual shadow-map resources

| Resource | Final form | Ownership | Consumers |
| --- | --- | --- | --- |
| Physical page pool | Paged depth atlas / array resource | Virtual shadow runtime | Virtual shadow passes, shaders, debug views |
| Page tables / indirection data | Texture or structured buffer | Virtual shadow runtime | Shaders, debug views |
| Per-product virtual projection metadata | Structured buffer | Virtual shadow runtime | Shaders, debug views |
| Residency feedback / request buffers | Structured/UAV buffers | Virtual shadow runtime | Residency/update passes, debug views |
| Dirty-page / cache state buffers | Structured buffer | Virtual shadow runtime | Residency/update passes, telemetry |

The exact backing resources may vary by backend, but the renderer contract must
preserve bindless publication and stable metadata identities.

### 5.4 Bindless publishing

The renderer publishes bindless slots for:

- Directional light metadata
- Directional shadow metadata
- Positional light metadata
- Shadow instance metadata
- Shadow implementation metadata
- Conventional directional shadow depth resource when available
- Conventional spot shadow depth resource when available
- Conventional point shadow depth resource when available
- Virtual shadow physical page pool when available
- Virtual shadow page tables / indirection resources when available
- Virtual shadow residency/feedback resources when debug or update passes need
  them
- Optional contact shadow mask/resources

The exact slots are published through `ViewFrameBindings` into a
shadow-owned `ShadowFrameBindings` contract according to lifetime.

- `ViewConstants` exposes only `bindless_view_frame_bindings_slot`.
- Shadow-owned frame-global and view-local routing both live behind
  `ViewFrameBindings.shadow_frame_slot`.
- Family-specific slots that are not active in a frame publish invalid
  indices; the metadata contract stays stable.

### 5.5 Metadata evolution

The existing directional shadow metadata and positional light payloads are the
starting point for the final contract. They must evolve toward a shared
family-independent shadow-product model rather than being wrapped in temporary
conventional-only structures.

Shared shadow metadata must cover:

- Shadow product identity
- Shadow domain and projection kind
- Implementation family
- Validity bits
- Receiver/debug flags
- Bias/filter metadata

Directional shadow metadata must cover:

- Directional shadow mode within the chosen family
- Cascade or clipmap count
- Split/coverage distances
- Light view-projection data or virtual projection transforms
- Family-specific addressing payload

Local shadow metadata must cover:

- Shadow-map kind (spot or point)
- Conventional projection matrices or virtual projection transforms
- Family-specific addressing payload
- Validity bits

Family-specific addressing payloads include:

- Conventional array layer/slice/cube addressing
- Virtual page-table indices, clipmap identifiers, physical-page scale/bias,
  and any indirection parameters required by sampling

## 6. Pass Architecture

The final runtime path is:

1. Scene update resolves transforms and effective node flags.
2. `LightManager` collects light contribution and shadow intent.
3. ScenePrep produces render-item data and shadow caster eligibility.
4. `ShadowManager` selects active shadow products and resolves implementation
   families.
5. Family runtimes allocate resources or virtual residency for those products.
6. Shadow caster lists are built per active shadow product or per active
   virtual page update.
7. Conventional shadow passes and/or virtual shadow update passes render depth
   into their family resources.
8. Main-view depth pre-pass runs.
9. Contact shadow refinement runs when enabled and when the view path supports
   it.
10. Light culling runs.
11. Main forward shading samples shadow products through shared shadow metadata.
12. Transparent shading samples the same shadow products where the material
   path supports it.

Each pass stage consumes the same stable shadow-product and resource model.

### 6.1 Renderer services

The final implementation is organized around renderer-owned services.

- `ShadowManager`: scheduling, policy selection, budgeting, invalidation,
  shared metadata publishing, and backend coordination
- `ConventionalShadowBackend`: allocation and scheduling for pooled
  shadow-map resources
- `VirtualShadowMapBackend`: page allocation, residency, invalidation, and
  metadata publishing for virtualized shadow products
- `ConventionalShadowRasterPass` consuming backend-neutral raster shadow jobs
- `LocalShadowPass` for spot and point products, or separate spot/point pass
  classes if backend details require it
- `VirtualShadowUpdatePass` or equivalent page-update passes
- `ContactShadowPass`

The final class split may vary, but the responsibilities above must stay
centralized rather than leaking into unrelated passes.

### 6.2 Pass and backend remediation requirements

The current conventional directional path is allowed to ship as the first
working slice, but it is not the final pass/backend shape for a multi-technique
shadow system. Before adding spot, point, or virtual shadow implementations,
the renderer must remove the directional-only architectural seams listed below.

Required end state:

- `ShadowManager` remains the top-level scheduler/publisher, but it must stop
  owning backend-specific rendering logic directly.
- Conventional raster shadow rendering must consume a backend-neutral raster
  shadow render plan rather than a directional-only published blob.
- Virtual shadow rendering must use separate page-update / residency passes,
  not branches inside the conventional raster shadow pass.
- Shader evaluation must dispatch from shared `ShadowInstanceMetadata`, then
  resolve family/domain-specific payloads behind that contract.
- Per-view shadow publication, backend render planning, and debug/introspection
  data must be separate structures with separate lifetimes.

The required remediation work is:

1. Split shadow publication from render planning.
   - Replace the current grab-all published view state with:
     - `ShadowFramePublication`: bindless slots and shared per-view GPU-facing
       publication only
     - `RasterShadowRenderPlan`: conventional raster jobs for directional,
       spot, and point products
     - backend-private state for caching/residency/debug as needed
   - `ConventionalShadowRasterPass` must not depend on a publication blob that
     also owns shading-facing SRV metadata.

2. Introduce explicit backend seams.
   - `ShadowManager` coordinates backends; it does not absorb all
     conventional/virtual implementation details in one file.
   - At minimum, the implementation must split conventional raster work from
     virtual-shadow work behind backend-owned planning/publication steps.
   - Spot, point, and virtual support must not be added by extending a single
     directional-only `if implementation_kind == ...` path.

3. Generalize the raster shadow pass contract.
   - The reusable asset is `DepthPrePass` infrastructure and partition-aware
     depth-only draw submission.
   - The non-reusable part is the current directional-only pass contract.
   - The final conventional raster pass must consume generic raster jobs:
     target resource/view, view constants snapshot, pass mask/domain policy,
     and payload reference.
   - Directional, spot, and point conventional jobs may share that pass if the
     raster contract stays generic. If they cannot, they still must be
     separate backend-owned passes, not directional branches added everywhere.

4. Move shader dispatch to the shared shadow-product contract.
   - Forward/transparent lighting must consume `ShadowInstanceMetadata` first.
   - Domain/family dispatch belongs in shadow helpers, not in light-specific
     shading code.
   - Directional payload lookup may remain one backend path internally, but it
     must be reached through the shared product contract so later techniques do
     not require a lighting-contract rewrite.

5. Strengthen invalidation and caching contracts.
   - Caching keyed only by `ViewId` is not sufficient for the final system.
   - Published shadow state must be invalidated by shadow-relevant input
     changes, including light candidates, synthetic-sun inputs, quality tier,
     backend selection, and backend-owned residency/allocation state.
   - The invalidation contract must be explicit and testable.

6. Add architecture-focused coverage before new techniques land.
   - shadow-only casters survive ScenePrep and render into shadow passes
   - main-view-only passes skip shadow-only draws
   - shadow publication/shader dispatch uses shared product metadata
   - invalidation occurs when shadow inputs or backend state change
   - backend selection/fallback does not require lighting-path changes

This remediation is not optional cleanup. It is the prerequisite that keeps
conventional, virtual, and future hybrid shadow techniques from collapsing into
directional-only special cases.

## 7. Conventional Shadow Maps

Conventional shadow maps remain the baseline broad-platform family.

### 7.1 Directional shadowing

Directional lights use cascaded shadow maps when the conventional family is
selected.

- Use camera-frustum splitting with a practical split scheme that blends linear
  and logarithmic distribution.
- Store split distances in the directional shadow metadata uploaded by the
  renderer.
- Cascade selection in shading is based on view-space or linear depth against
  those split distances.
- Neighboring cascades support a blend band to hide seams.
- Each cascade is fit against the slice of the view frustum it owns.
- The light projection is orthographic.
- Fitting must include relevant off-screen casters that can project into the
  visible region.
- Projection stabilization uses texel snapping so camera motion does not cause
  visible shimmer.
- Each cascade occupies one layer in the directional shadow `Texture2DArray`.
- A shadowed directional light owns a contiguous set of cascade layers or an
  equivalent stable addressing block.

Engine-grade acceptance criteria for the conventional directional path are:

- Use stable cascade fitting, not only naive per-frame min/max fitting. The
  renderer must derive a stable cascade bound, convert that bound to
  world-units-per-texel, and snap orthographic coverage in texel increments so
  camera motion does not produce crawl or shimmer. For the conventional
  directional path, a sphere-wrapped stable fit (or an equivalent stability-
  preserving strategy) is preferred over aggressive per-frame rectangular crop
  fitting that causes view-angle-dependent breathing.
- Fit light-space near/far depth as tightly as practical for the active
  receiver/caster region. Large fixed padding is acceptable only as a temporary
  bootstrap; the final path must reduce wasted depth range to preserve precision
  and limit acne/peter-panning pressure.
- Support cascade blend bands explicitly in shader sampling and metadata.
  Hard cascade transitions are not acceptable as the final conventional path.
- Cascade handoff must preserve effective resolution. When independently fit
  cascades do not align perfectly, the shader path may use local neighbor
  fallback or neighbor blending to preserve continuity, but it must not perform
  arbitrary "first valid cascade" searches that make pixels hop between splits
  as camera angle changes. Interval-based selection remains the stability
  baseline for conventional CSM.
- Account for PCF kernel footprint in cascade coverage. Conventional cascade
  projections and addressing must preserve a padded border so filter taps do not
  bleed outside the intended cascade coverage.
- Cascade blend widths must be controlled as part of shadow quality policy.
  Wide fixed percentage blend bands that wash out near-cascade detail are not
  acceptable as the final conventional path; blend widths must stay narrow and
  scale with actual cascade texel footprint / continuity needs.
- Use a renderer-controlled raster depth-bias policy for shadow rendering.
  For the current conventional directional path, keep constant raster depth
  bias enabled but do not stack additional raster slope-scaled bias on top of
  the receiver-side slope-aware normal/light-direction bias. Shader-side
  receiver bias augments the shadow-pass raster bias; it does not replace it.
- Use authored normal bias and constant bias as inputs to the final receiver
  bias calculation, but do not rely on shader-only biasing as the sole acne
  mitigation strategy.
- Sampling must use comparison-based filtering in the normal path when the
  backend supports it. Manual `Load()`-based depth taps are an acceptable
  bootstrap or compatibility fallback, not the target engine-grade path.
- Outside-shadow coverage behavior must be deterministic. Border handling,
  out-of-range UV behavior, and cascade edge handling must be defined so
  filtering does not create edge streaks or undefined reads.
- Shadow-caster raster state must follow explicit material-domain policy.
  Conventional directional shadows default to standard back-face culling unless
  a material/domain policy requires otherwise; front-face-only hacks are not an
  acceptable general solution.

### 7.2 Spot-light shadowing

Spot lights use a single projective shadow map per active shadowed light when
the conventional family is selected.

- The spot shadow frustum is derived from the light transform, outer cone, and
  range.
- Each active spot light owns one slice in the spot shadow `Texture2DArray`.
- Shader sampling uses the published spot projection matrix and array slice.
- Mixed-mobility spot lights may reuse cached maps until invalidated.

### 7.3 Point-light shadowing

Point lights use cubemap shadows when the conventional family is selected.

- Each active point light owns one cube allocation in the point shadow
  `TextureCubeArray`.
- The runtime stores six face transforms and any sampling parameters required by
  the shader path.
- Point-light budgeting is explicit because six-face rendering is the most
  expensive conventional shadow workload in the system.
- Mixed-mobility point lights may reuse cached cube maps until invalidated.

## 8. Virtual Shadow Maps

Virtual shadow maps are the high-end scalable family for large scenes, dense
geometry, and higher effective texel density.

### 8.1 Goals

- Decouple visible shadow detail from fixed atlas sizing
- Allocate physical depth storage only for pages that are needed
- Preserve stable shadow-product identities while allowing view-dependent page
  residency
- Reuse the same authored light inputs and shared shader contracts used by the
  conventional family

### 8.2 Directional virtual shadowing

Directional lights use virtualized multi-range coverage when the virtual family
is selected.

- Coverage may be expressed as clipmaps, camera-relative regions, or an
  equivalent paged directional scheme, but the renderer contract must keep the
  directional product identity stable.
- Directional virtual coverage must include off-screen casters that project into
  the visible region.
- Coverage movement must be stabilized to avoid visible crawl during camera
  motion.
- Metadata published to shaders includes the directional virtual coverage set,
  virtual addressing parameters, and page-table indirection needed for sampling.

### 8.3 Local virtual shadowing

Spot and point lights may use virtualized local shadow products when the
virtual family is selected.

- Spot lights publish a virtual projective shadow product with page-addressed
  coverage over the light frustum.
- Point lights publish a virtual omni shadow product, typically represented as
  six logical faces with shared product identity and family-specific page
  addressing.
- Local virtual products obey the same mobility, invalidation, and deterministic
  budgeting rules as conventional products.

### 8.4 Page residency and rendering

Virtual shadow rendering is page-driven rather than full-allocation-driven.

- Residency requests are built from visible receivers, selected lights, and
  family-specific coverage rules.
- The runtime resolves requested pages against current residency, allocates new
  physical pages, and evicts pages through deterministic policy.
- Dirty pages are updated when invalidated by light/caster/material/LOD changes.
- Page rendering reuses the same shadow-caster submission path and material
  policies used by conventional shadow rendering.
- The runtime must expose telemetry for requested pages, allocated pages,
  evicted pages, reused pages, and page-update cost.

### 8.5 Fallback rules

- If the backend/platform cannot support the virtual family for a product, the
  renderer falls back through explicit policy to the conventional family or to
  "no runtime shadow" for the lowest-priority products.
- Fallback does not change the authored shadow model.
- Debug tooling must expose the chosen family and the reason for a fallback.

## 9. Contact Shadow Refinement

Contact shadows are part of the final shadow system, not a separate lighting
feature.

- Contact refinement is enabled by the authored `contact_shadows` flag on the
  light.
- The runtime produces a per-view refinement term using the resolved depth
  buffer and the active light set.
- Contact refinement multiplies or otherwise refines the receiver shadow term
  from conventional or virtual shadow products; it does not replace map-based
  visibility for eligible lights.
- Contact refinement must obey the same receiver eligibility and debug tooling
  contracts as the rest of the shadow system.

## 10. Filtering and Bias

### 10.1 Baseline filtering

The baseline runtime filter is depth comparison plus PCF.

- Hardware comparison sampling is preferred when supported by the backend path.
- Kernel width is quality-tier driven.
- The shared metadata contract must remain compatible with wider kernels and
  future filter upgrades without replacing the shadow resource model.

For the conventional directional path, "baseline PCF" still means an
engine-grade implementation:

- comparison sampling in the default path
- explicit kernel-aware padding/border behavior
- cascade blend support
- cascade-aware filter policy so near-cascade detail is not blurred to match
  far-cascade needs
- quality-tier-controlled kernel growth without changing metadata contracts
- deterministic fallback to manual comparison taps only when the backend path
  cannot support the preferred comparison-sampling implementation

Family-specific notes:

- Conventional shadow maps use direct depth comparison against pooled map
  resources.
- Virtual shadow maps use depth comparison after resolving physical-page and
  page-table indirection.

### 10.2 Bias policy

Bias is authored and runtime-augmented.

- Use authored constant bias and normal bias from `ShadowSettings`.
- Keep renderer-controlled raster depth bias in the shadow raster path.
- Add raster depth-bias clamp where the backend exposes it and where testing
  shows it improves stability.
- Bias is evaluated per light type, implementation family, and quality tier.
- Debug tooling must expose effective bias values so acne and peter-panning can
  be diagnosed from live captures.

For the current conventional directional path, do not enable raster
slope-scaled depth bias while the receiver path already applies slope-aware
normal/light-direction bias. That combination over-biases contact regions and
promotes detached shadows.

For the current conventional directional shadow path, the bias stack is:

- raster depth bias in the shadow pass
- optional raster bias clamp
- receiver-side normal bias
- receiver-side constant/light-direction bias

The final conventional path must tune these as a combined policy. It must not
ship with shader-only biasing or with a single hardcoded constant used as the
primary acne mitigation mechanism.

### 10.3 Softness evolution

The system is designed so baseline PCF can later evolve into larger kernels or
penumbra-aware filtering without reauthoring light data or replacing the shadow
resource families.

## 11. Quality Tiers and Policy

Quality tiers are renderer policy, not scene-authoring changes.

- Authored `ShadowResolutionHint` influences allocation selection within a tier.
- Renderer-owned shadow quality tier may promote or cap conventional runtime
  allocation independently of authored scene data.
- Tier policy controls:
  - Preferred implementation family per light domain
  - Directional cascade count for conventional directional shadows
  - Directional virtual coverage density/range policy
  - Conventional directional map resolution
  - Conventional spot map resolution
  - Conventional point cube resolution
  - Virtual physical-page pool size and page size
  - Maximum active shadowed local lights
  - Maximum virtual page updates per frame
  - Contact shadow quality
- Tier changes must invalidate and rebuild affected shadow resources through the
  normal invalidation path.
- For conventional directional shadows, tier policy may promote a single
  dominant directional product to a higher resolution level when the current
  runtime shadow budget can afford it, instead of forcing all scenes to rely on
  authored resolution hints alone.

## 12. ScenePrep and Draw Submission Requirements

Shadow rendering requires explicit pass participation in ScenePrep and draw
metadata.

- `PassMask` and partitioning must support shadow-caster submission in addition
  to the existing opaque, masked, and transparent paths.
- Draw metadata must preserve the information required by both main-view
  rendering and shadow-caster rendering.
- The shadow path must reuse the existing geometry, transform, and
  material-routing systems instead of building a second geometry submission
  stack.
- Shadow-caster submission must honor alpha-tested masking when the material
  policy requires it.
- Virtual page update passes must consume the same ScenePrep outputs and caster
  routing as conventional passes; page-driven rendering is not a second scene
  extraction pipeline.

## 13. Shader Responsibilities

Forward shading is responsible for combining all shadow terms.

- Shaders consume shared shadow-product metadata and dispatch to the correct
  family-specific sampling helper based on implementation kind.
- Directional lighting samples directional shadow metadata plus the active
  family resources.
- Spot and point lighting sample local shadow metadata plus the active family
  resources.
- Receiver evaluation applies `receive_shadows` before sampling.
- Contact shadow refinement is combined only for lights that request it.
- Debug modes must be able to visualize:
  - Chosen implementation family
  - Directional cascade or virtual coverage selection
  - Local shadow terms
  - Contact refinement
  - Final combined visibility

## 14. Debugging and Validation

The final shadow system requires dedicated debug support.

- Visualize conventional directional cascade coverage and cascade index
- Visualize virtual directional coverage / clipmap selection / page residency
- Visualize spot and point shadow allocations
- Visualize virtual page requests, evictions, reuse, and physical occupancy
- Visualize shadow texel density / effective resolution
- Visualize final shadow factor and contact-refinement factor
- Show active-light budgets, allocation counts, cache hits, invalidations, and
  virtual-family fallback decisions
- Emit render markers for each shadow pass, each virtual page-update stage, and
  each shadow resource update
- Emit runtime logs at the shadow ownership boundaries so "no shadows visible"
  can be diagnosed without a GPU capture. At minimum, logs must report:
  - whether a view produced any shadow products
  - whether directional shadow resources were allocated or reused
  - whether `ShadowFrameBindings` published valid metadata/texture slots
  - whether the shadow pass executed and emitted any shadow-caster draws
  - why the shadow path skipped work for a view when it does

Automated coverage should include:

- Eligibility and gating tests
- Buffer packing/layout tests
- Conventional cascade split and stabilization tests
- Virtual residency, page-table, and invalidation tests
- Allocation and fallback-policy tests
- Sampling-contract parity tests between C++ and HLSL

## 15. Phased Delivery

Phased implementation is required, but every phase lands on the final
architecture described above.

### 15.1 Phase 1: Shared foundations plus conventional directional path

- Add `ShadowManager`, shared shadow-product metadata, and final bindless
  publishing model
- Add shadow-caster participation through ScenePrep and draw metadata
- Add conventional directional shadow resources and metadata publication
- Implement cascaded directional rendering and forward sampling through the
  family-independent shader contract
- Add baseline PCF, bias handling, and required debug views

This is the highest-value delivery path and establishes the contracts reused by
all later phases.

### 15.2 Phase 2: Conventional directional completion and tier policy

- Complete directional budgeting, cascade blending, stabilization hardening, and
  tier policy
- Replace bootstrap manual depth-tap sampling with the engine-grade
  comparison-sampling path, keeping manual comparison taps only as an explicit
  compatibility fallback when required
- Replace bootstrap loose cascade fitting with stable texel-snapped coverage and
  tighter light-space depth fitting suitable for production directional shadows
- Add kernel-aware cascade padding/border handling so wider PCF kernels remain
  correct at cascade edges
- Finalize combined raster/receiver bias policy for directional shadows with
  debug visibility of effective values
- Add mixed-mobility caching and invalidation for conventional directional
  products where policy allows it
- Expand directional multi-light scheduling through the same shared metadata and
  implementation-selection model

### 15.3 Phase 3: Multi-technique architecture remediation

- Split shading publication from backend render planning:
  - `ShadowFramePublication`
  - conventional raster render plan
  - backend-private residency/cache state
- Introduce explicit backend-owned conventional and virtual planning/execution
  seams under `ShadowManager`
- Replace the directional-only conventional pass contract with a generic raster
  shadow render-plan contract that reuses `DepthPrePass` infrastructure without
  baking directional-only assumptions into the pass interface
- Move shader evaluation to shared `ShadowInstanceMetadata`-driven dispatch so
  directional payload lookup is one implementation detail, not the public
  shading contract
- Replace `ViewId`-only shadow publication caching with explicit
  shadow-input/backend-state invalidation
- Add focused automated coverage for shadow-only casters, generic shadow
  product dispatch, and invalidation seams

Implementation status, March 9, 2026:

- Complete for the current conventional raster path.
- Live code now uses:
  - `ConventionalShadowBackend`
  - `ShadowFramePublication`
  - `RasterShadowRenderPlan`
  - `ConventionalShadowRasterPass`
- Forward lighting dispatch now starts from `ShadowInstanceMetadata`.
- Conventional shadow publication now invalidates from shadow-relevant input
  hashes rather than `ViewId` alone.
- Focused automated coverage exists for the implemented remediation seams.

### 15.4 Phase 4: Virtual shadow-map foundations

- Add `VirtualShadowMapBackend`
- Add virtual page-pool, page-table, residency, and telemetry resources
- Add policy/capability plumbing for choosing between conventional and virtual
  families
- Extend shader sampling helpers to support virtual-family dispatch without
  adding a parallel shading path

### 15.5 Phase 5: Directional virtual shadow maps

- Add directional virtual coverage selection, residency, rendering, sampling,
  invalidation, and tooling
- Preserve the same directional shadow-product identity and shared metadata
  contract used by the conventional path

### 15.6 Phase 6: Spot-light shadows on shared contracts

- Add spot shadow allocation, rendering, sampling, invalidation, and tooling on
  the existing local-shadow contracts
- Ship conventional spot shadowing first unless virtual spot shadowing is ready
  on the same contract

### 15.7 Phase 7: Point-light shadows on shared contracts

- Add point shadow allocation, rendering, sampling, invalidation, and tooling
  on the existing local-shadow contracts
- Ship conventional point shadowing first unless virtual point shadowing is
  ready on the same contract

### 15.8 Phase 8: Local virtual shadows and contact refinement

- Expand the virtual family to spot and point products
- Add per-view contact refinement generation and composition for lights that
  author `contact_shadows`

### 15.9 Phase 9: Hardening and optimization

- Tune budgets, fallback policy, residency, and invalidation
- Improve allocation reuse and cache behavior across both families
- Expand automated coverage and stress validation

Each phase extends the final system. No phase introduces a disposable
"prototype" shadow architecture.
