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

## 1. Design Rules

- One shadow architecture for all phases. Delivery phases may activate subsets
  of producers and consumers, but they must not introduce temporary shadow-only
  APIs, duplicate metadata layouts, or parallel shading paths.
- Bindless-first. Shadow maps, metadata buffers, and debug resources are
  exposed through the existing bindless model rather than bespoke root
  parameters.
- Stable before soft. Temporal stability, deterministic selection, and correct
  eligibility rules take precedence over wide filters and soft-shadow features.
- Scene data stays authoritative. The renderer consumes
  `casts_shadows`, `contact_shadows`, `resolution_hint`, cascade settings,
  mobility, and node shadow flags as authored inputs; it does not invent an
  alternate shadow authoring model.
- Resource budgets must be explicit and deterministic. When budgets are hit,
  the selection policy must be stable frame to frame.
- Shadowing is a system, not a pass. Extraction, scheduling, resource
  allocation, shadow rendering, lighting evaluation, debug tooling, and
  invalidation all belong to the same runtime contract.

## 2. Final Supported Scope

The final shadow system supports all runtime light families already represented
in the scene layer.

| Shadow domain | Final runtime product | Primary resource form | Notes |
| --- | --- | --- | --- |
| Directional lights | Cascaded shadow maps | `Texture2DArray` depth resource | Supports the canonical sun path and additional shadowed directional lights through the same allocator and metadata contract |
| Spot lights | Single-projective shadow maps | `Texture2DArray` depth resource | One slice per shadowed spot light |
| Point lights | Cubemap shadow maps | `TextureCubeArray` depth resource | Six faces per shadowed point light |
| Contact shadows | Screen-space receiver refinement | Per-view mask/utility textures | Multiplies or refines map shadows based on authored light settings |

Transparent, masked, transmissive, foliage, and other material-domain-specific
shadow participation is governed by the material shadow participation policy
defined in the sections below rather than by separate shadow systems.

## 3. Authoritative Inputs

The renderer shadow system consumes the following authored inputs.

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
- Geometry, transform, and bounds needed for light-space culling

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

### 4.2 Caster eligibility

Shadow caster eligibility is derived from render-item state, not only from
camera-visible draw lists.

- A render item participates in shadow rendering only when
  `cast_shadows == true`.
- Shadow caster submission is filtered by the light's shadow volume, not by the
  main camera visibility result alone.
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

### 4.4 Mobility and invalidation

- `kRealtime`: shadow maps may be regenerated every frame as needed.
- `kMixed`: shadow maps may be cached and refreshed only when invalidated by a
  relevant scene change.
- `kBaked`: excluded from runtime shadow-map generation.

Invalidation must be driven by the actual dependencies of the shadow product.

- Light transform or authored shadow-setting change
- Caster transform change
- Caster material change when material-domain shadow participation changes
- Geometry residency change or LOD/mesh change affecting the shadow pass
- Global quality-tier change affecting allocated shadow resources

### 4.5 Deterministic budgeting

Shadow allocation uses deterministic priority keys so budget pressure does not
introduce temporal flicker.

Priority inputs include:

- Light type
- Whether the light is the canonical sun
- Mobility
- Authored `ShadowResolutionHint`
- View relevance / projected influence
- Intensity and range for tie-breaking
- Stable scene identity for final ordering

Budgeting may reduce active shadow work in a frame, but it must not change the
underlying metadata contracts seen by the renderer and shaders.

## 5. GPU Resource Model

The final system uses three shadow depth pools plus metadata buffers.

| Resource | Final form | Ownership | Consumers |
| --- | --- | --- | --- |
| Directional cascade pool | `Texture2DArray` depth | Shadow runtime | Directional shadow passes, forward shaders, debug views |
| Spot shadow pool | `Texture2DArray` depth | Shadow runtime | Spot shadow passes, forward shaders, debug views |
| Point shadow pool | `TextureCubeArray` depth | Shadow runtime | Point shadow passes, forward shaders, debug views |
| Directional shadow metadata | Structured buffer | Light/shadow runtime | Forward shaders, debug views |
| Local shadow metadata | Structured buffer | Light/shadow runtime | Forward shaders, debug views |
| Contact shadow mask/resources | Per-view textures/buffers | Contact shadow runtime | Forward shaders, transparent shading, debug views |

### 5.1 Bindless publishing

The renderer publishes bindless slots for:

- Directional light metadata
- Directional shadow metadata
- Positional light metadata
- Directional shadow depth resource
- Spot shadow depth resource
- Point shadow depth resource
- Optional contact shadow mask/resources

The exact slots live in `SceneConstants` and/or `EnvironmentDynamicData`
according to lifetime.

- Frame-global shadow resources belong in `SceneConstants`.
- View-local shadow refinement products belong in `EnvironmentDynamicData`.

### 5.2 Metadata evolution

The existing `DirectionalLightShadows` and positional light payloads are the
starting point for the final contract. They must be extended in place to carry
the metadata required by final sampling rather than replaced by temporary
structures.

Directional shadow metadata must cover:

- Cascade count
- Cascade split distances
- Cascade light view-projection matrices
- Cascade resource addressing (array layer or equivalent addressing info)
- Bias/filter metadata needed by shader sampling
- Validity bits for cascades and light entries

Local shadow metadata must cover:

- Shadow-map kind (spot or point)
- Resource addressing
- Spot projection matrix or point-face transforms
- Bias/filter metadata
- Validity bits

## 6. Pass Architecture

The final runtime path is:

1. Scene update resolves transforms and effective node flags.
2. `LightManager` collects light contribution and shadow intent.
3. ScenePrep produces render-item data and shadow caster eligibility.
4. Shadow runtime selects active shadowed lights and allocates resources.
5. Shadow caster lists are built per active shadow product.
6. Directional, spot, and point shadow passes render depth into the shadow
   pools.
7. Main-view depth pre-pass runs.
8. Contact shadow refinement runs when enabled and when the view path supports
   it.
9. Light culling runs.
10. Main forward shading samples shadow maps and contact refinement.
11. Transparent shading samples the same shadow products where the material
    path supports it.

Each pass stage consumes the same stable shadow metadata and resource model.

### 6.1 Renderer services

The final implementation is organized around renderer-owned services.

- `ShadowManager`: scheduling, budgeting, resource allocation, invalidation, and
  metadata publishing
- `DirectionalShadowPass`
- `LocalShadowPass` for spot and point products, or separate spot/point pass
  classes if backend details require it
- `ContactShadowPass`

The final class split may vary, but the responsibilities above must stay
centralized rather than leaking into unrelated passes.

## 7. Directional Shadowing

Directional lights use cascaded shadow maps as the final runtime product.

### 7.1 Cascade strategy

- Use camera-frustum splitting with a practical split scheme that blends linear
  and logarithmic distribution.
- Store split distances in the directional shadow metadata uploaded by the
  renderer.
- Cascade selection in shading is based on view-space or linear depth against
  those split distances.
- Neighboring cascades support a blend band to hide seams.

### 7.2 Cascade fitting

- Each cascade is fit against the slice of the view frustum it owns.
- The light projection is orthographic.
- Fitting must include relevant off-screen casters that can project into the
  visible region.
- Projection stabilization uses texel snapping so camera motion does not cause
  visible shimmer.

### 7.3 Directional resource addressing

- Each cascade occupies one layer in the directional shadow `Texture2DArray`.
- A shadowed directional light owns a contiguous set of cascade layers or an
  equivalent stable addressing block.
- Metadata published to shaders includes the layer index for each cascade.

### 7.4 Canonical sun path

- The canonical sun is the highest-priority directional light for environment
  systems and the highest-value directional shadow consumer.
- The shadow system does not special-case the sun into a separate shadow
  architecture; it is scheduled through the same directional metadata and
  resource contracts, with policy allowed to prioritize it.

## 8. Spot-Light Shadowing

Spot lights use a single projective shadow map per active shadowed light.

- The spot shadow frustum is derived from the light transform, outer cone, and
  range.
- Each active spot light owns one slice in the spot shadow `Texture2DArray`.
- Shader sampling uses the published spot projection matrix and array slice.
- Mixed-mobility spot lights may reuse cached maps until invalidated.

## 9. Point-Light Shadowing

Point lights use cubemap shadows as the final runtime product.

- Each active point light owns one cube allocation in the point shadow
  `TextureCubeArray`.
- The runtime stores six face transforms and any sampling parameters required by
  the shader path.
- Point-light budgeting is explicit because six-face rendering is the most
  expensive shadow workload in the system.
- Mixed-mobility point lights may reuse cached cube maps until invalidated.

## 10. Contact Shadow Refinement

Contact shadows are part of the final shadow system, not a separate lighting
feature.

- Contact refinement is enabled by the authored `contact_shadows` flag on the
  light.
- The runtime produces a per-view refinement term using the resolved depth
  buffer and the active light set.
- Contact refinement multiplies or otherwise refines the receiver shadow term
  from shadow maps; it does not replace shadow-map visibility for eligible
  lights.
- Contact refinement must obey the same receiver eligibility and debug tooling
  contracts as map shadows.

## 11. Filtering and Bias

### 11.1 Baseline filtering

The baseline runtime filter is depth comparison plus PCF.

- Hardware comparison sampling is preferred when supported by the backend path.
- Kernel width is quality-tier driven.
- The metadata contract must remain compatible with wider kernels and future
  filter upgrades without replacing the shadow resource model.

### 11.2 Bias policy

Bias is authored and runtime-augmented.

- Use authored constant bias and normal bias from `ShadowSettings`.
- Add renderer-controlled slope-scaled depth bias in the shadow raster path.
- Bias is evaluated per light type and quality tier.
- Debug tooling must expose effective bias values so acne and peter-panning can
  be diagnosed from live captures.

### 11.3 Softness evolution

The system is designed so baseline PCF can later evolve into larger kernels or
penumbra-aware filtering without reauthoring light data or replacing the shadow
resource pools.

## 12. Quality Tiers and Budgets

Quality tiers are renderer policy, not scene-authoring changes.

- Authored `ShadowResolutionHint` influences allocation selection within a tier.
- Tier policy controls:
  - Directional cascade count
  - Directional map resolution
  - Spot map resolution
  - Point cube resolution
  - Maximum active shadowed local lights
  - Contact shadow quality
- Tier changes must invalidate and rebuild affected shadow resources through the
  normal invalidation path.

## 13. ScenePrep and Draw Submission Requirements

Shadow rendering requires explicit pass participation in ScenePrep and draw
metadata.

- `PassMask` and partitioning must support shadow-caster submission in addition
  to the existing opaque, masked, and transparent paths.
- Draw metadata must preserve the information required by both main-view
  rendering and shadow-caster rendering.
- The shadow pass path must reuse the existing geometry, transform, and
  material-routing systems instead of building a second geometry submission
  stack.
- Shadow-caster submission must honor alpha-tested masking when the material
  policy requires it.

## 14. Shader Responsibilities

Forward shading is responsible for combining all shadow terms.

- Directional lighting samples directional shadow metadata and the directional
  depth resource.
- Spot and point lighting sample their corresponding local shadow metadata and
  depth resources.
- Receiver evaluation applies `receive_shadows` before sampling.
- Contact shadow refinement is combined only for lights that request it.
- Debug modes must be able to visualize cascade selection, local shadow terms,
  contact refinement, and final combined visibility.

## 15. Debugging and Validation

The final shadow system requires dedicated debug support.

- Visualize directional cascade coverage and cascade index
- Visualize spot and point shadow allocations
- Visualize shadow texel density / effective resolution
- Visualize final shadow factor and contact-refinement factor
- Show active-light budgets, allocation counts, cache hits, and invalidations
- Emit render markers for each shadow pass and each shadow resource update

Automated coverage should include:

- Eligibility and gating tests
- Buffer packing/layout tests
- Cascade split and stabilization tests
- Allocation and invalidation tests
- Sampling-contract parity tests between C++ and HLSL

## 16. Phased Delivery

Phased implementation is required, but every phase lands on the final
architecture described above.

### 16.1 Phase 1: Shadow foundations plus directional path

- Add `ShadowManager` and final bindless publishing model
- Add shadow-caster participation through ScenePrep and draw metadata
- Add directional shadow depth pool and metadata publication
- Implement cascaded directional rendering and forward sampling
- Add baseline PCF, bias handling, and required debug views

This is the highest-value delivery path and establishes the contracts reused by
all later phases.

### 16.2 Phase 2: Directional completion and quality tiers

- Complete directional budgeting, cascade blending, stabilization hardening, and
  tier policy
- Add mixed-mobility caching and invalidation for directional products where
  policy allows it
- Expand directional multi-light scheduling through the same metadata and
  resource model

### 16.3 Phase 3: Spot-light shadows

- Add spot shadow allocation, rendering, sampling, invalidation, and tooling on
  the existing local-shadow contracts

### 16.4 Phase 4: Point-light shadows

- Add point cubemap allocation, rendering, sampling, invalidation, and tooling
  on the existing local-shadow contracts

### 16.5 Phase 5: Contact shadow refinement

- Add per-view contact refinement generation and composition for lights that
  author `contact_shadows`

### 16.6 Phase 6: Hardening and optimization

- Tune budgets and invalidation
- Improve allocation reuse and cache behavior
- Expand automated coverage and stress validation

Each phase extends the final system. No phase introduces a disposable
"prototype" shadow architecture.
