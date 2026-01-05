# Lighting Overview

Purpose: renderer-facing lighting integration notes (extraction, GPU formats,
culling, and pass expectations) aligned with Oxygen's bindless rendering
design.

## Source of truth

Scene-side light definitions, authored properties, and invariants live in:

- `src/Oxygen/Scene/Light/light-deisgn.md`

This document focuses on how the renderer consumes those lights; it
intentionally avoids duplicating the spec's component/property definitions.

## Principles

- Bindless-first: all GPU buffers (lights, grids, indices) are accessed via
  global bindless table indices; no new root-signature bindings beyond the
  existing draw-index constant.
- Start simple: begin with one directional light via constants, then move to a
  bindless lights buffer for multiple lights; culling/clustering comes later.
- Stable, aligned layouts: keep GPU-facing structured buffer elements aligned
  (prefer 16/32-byte friendly packing).

## Spaces & units (renderer obligations)

- Coordinate conventions match the scene spec (right-handed, Z-up,
  forward = -Y, right = +X).
- Directional/spot directions are derived from the owning node's world rotation
  using `oxygen::space::move::Forward`.
- Direction semantics: for directional lights, publish/consume direction as the
  **incoming ray direction** (light -> scene). If a BRDF uses surface-to-light
  vector `L`, then `L = -sun_dir_ws`.
- Units are per the scene spec:
  - Directional intensity: illuminance-like scale (lux semantics).
  - Point/spot intensity: luminous intensity (candela semantics).
  The renderer may apply view exposure/tonemapping, but must not reinterpret
  these units arbitrarily.

## Renderer integration (data flow)

1. Scene update resolves transforms + effective node flags.
2. Light extraction builds a frame-stable CPU snapshot.
3. Per-view culling builds view-local access structures (Forward+ grid/index).
4. Upload GPU buffers (bindless SRVs) and populate per-view constants.
5. Shading consumes the light access structures; shadow passes run when enabled.

## Extraction (CPU snapshot)

Extraction must:

- Walk nodes with light components (do not require renderables).
- Resolve world-space position/direction from node transforms.
- Apply gating rules from the scene spec:
  - Visibility (hard gate): if the node's effective `kVisible` is false, the
    light contributes nothing.
  - Contribution gate: when visible, a light contributes only when
    `affects_world` is true.
  - Shadow eligibility: `casts_shadows` is true **and** the node's effective
    `kCastsShadows` is true.
  - Mobility: `Baked` lights are excluded from runtime direct lighting.

Recommended snapshot shape (POD-style): separate arrays per light kind
(directional/point/spot), each entry containing world-space params and a stable
reference to the owning node handle (for debugging and deterministic selection).

## Culling & Forward+ (required contract)

Extraction produces a scene-global set. Before shading, the renderer builds
per-view culled light lists:

- Directional lights: infinite bounds; include if enabled by extraction.
- Point lights: sphere bounds centered at `position_ws` with radius `range`.
- Spot lights: conservative broad-phase bounds (sphere with `range`), or a cone
  bound if/when implemented.

Minimum broad-phase behavior:

- Frustum culling against the bounds above.
- Optional budget-based culling must be deterministic (stable sorting) to
  minimize temporal flicker.

Forward+ integration expectation per view:

1. Ensure a depth buffer exists (depth pre-pass or equivalent).
2. Run light culling + build a tile/cluster grid + flattened light-index list.
3. Render the main forward material pass using the grid/list.

## GPU interfaces (staged)

- Phase 5B (single-light correctness): SceneConstants carries ambient and one
  directional light (dir/color/intensity + enable flags).
- Phase 5C (multi-light correctness): upload a bindless structured buffer SRV
  containing the extracted light arrays; expose `num_lights` and a bindless slot
  index in SceneConstants.
- Later (performance): add per-view Forward+ grid + index list buffers (bindless
  SRVs) and/or per-draw light ranges as needed.

GPU-facing entries must contain, at minimum, what shaders need for evaluation
(type-specific world-space params, effective contribution flag, effective shadow
eligibility, and any packed shadow settings required by sampling code).

## Environment systems (renderer consumption)

SceneEnvironment is a scene-global object (not a node-attached light). At
runtime (and from cooked scenes), the renderer consumes SceneEnvironment data to
drive:

- Sky/background rendering: prefer Sky Atmosphere when present; otherwise use
  Sky Sphere when present; otherwise render no sky background.
- Environment lighting (IBL): when a Sky Light system is present and enabled,
  build/reuse diffuse irradiance and specular prefilter resources plus a shared
  BRDF integration LUT.

Sun coupling:

- A directional light with `environment_contribution == true` is the canonical
  sun direction for sky/environment systems.
- If multiple such directional lights are present, pick deterministically
  (recommended: highest effective intensity after per-light exposure
  compensation) and warn in debug builds.

## Pass expectations

The renderer should support (technique-specific details are flexible):

- Optional shadow map passes for lights with resolved shadow eligibility.
- Direct lighting evaluation in the Forward+ material path.
- Optional background (skybox/atmosphere) rendering when SceneEnvironment is
  present and enabled.

## Caps & limits

- `MAX_LIGHTS_UPLOADED`: implementation-defined cap for CPU snapshot and GPU
  buffer sizes.
- `MAX_LIGHTS_PER_PIXEL`: shader loop cap (safety clamp).

## Testing

- Unit tests for extraction counts, buffer packing sizes/strides, and clamping.
- Visual sanity via an example scene with 1 directional “Sun” + a few point
  lights, validating the `Forward` direction convention and `kVisible` gating.
