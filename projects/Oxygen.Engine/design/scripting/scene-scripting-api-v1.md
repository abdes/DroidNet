# Scene Scripting API v1 (Production)

**Date:** 2026-02-19
**Status:** Design / Reference

Audience: Engine, tools, gameplay scripting
Language target: Luau / Lua 5.1-style runtime with Luau extensions (vector type)

This document defines the authoritative v1 scene scripting API for Oxygen.
It is a production API, not a transitional API.

This document supersedes `design/Scripting_Scene_API_V1.md`.

## 1. Goals

1. Provide a complete gameplay-ready scene API surface in scripts.
2. Use idiomatic Lua ergonomics:
   - snake_case names
   - `:` method calls for object actions
   - property-style access for common state where safe
3. Keep behavior deterministic and explicit under failure.
4. Remove obsolete/legacy script APIs entirely.
5. Map cleanly to existing C++ scene systems without exposing internal implementation details.
6. Reuse existing core script packs and conventions instead of duplicating utility APIs.

## 2. Conventions and Contracts

## 2.1 Naming

1. All API names are snake_case.
2. Methods use colon call style (`node:set_parent(...)`).
3. No camelCase public script API.
4. Module responsibilities remain separated:
   - `oxygen.scene`: scene graph and scene-owned components
   - `oxygen.math`: math construction/ops (`quat`, `mat4`, helpers)
   - `oxygen.conventions`: canonical world/view/clip axes and handedness

## 2.2 Value Types

1. `vec3` uses Luau vector (`vector`).
2. `quat` uses `oxygen.math.quat(...)` userdata.
3. Arrays are Lua sequence tables (1-based).
4. Optional values return `nil`.
5. Direction defaults are sourced from `oxygen.conventions` values, not duplicated constants.

## 2.3 Error Model

1. Programmer errors (wrong arity/type): throw Lua error.
2. Runtime state misses (no match, missing component): return `nil` or `false` per function contract.
3. Dead node access:

- getters return `nil`
- mutating methods return `false`

1. API must not silently downgrade behavior.

## 2.4 Mutability / Phase Policy

1. Query and read APIs are always allowed.
2. Mutating APIs are allowed only in engine-approved phases.
3. If mutation is disallowed for current phase, throw a clear Lua error including function name and active phase.

## 2.5 Stability

1. v1 names in this spec are stable and versioned.
2. Legacy names listed in section 12 must not be exported.
3. Runtime identity semantics are explicit:

- `SceneNode` identity is a runtime handle (`NodeHandle`: index + scene_id), valid only while the node is alive.
- `scene_id` is allocated at runtime and can be reused after scene destruction.
- No API in this spec implies cross-run persistence of scene/node ids.

## 3. Global Module

The module is exposed as `oxygen.scene`.

## 3.1 Context and Parameters

1. `scene.current_node(ctx) -> SceneNode?`
Returns the node bound to the executing slot context.

2. `scene.param(ctx, key: string) -> any?`
Returns effective scripting parameter for the current slot, or `nil` when absent.

## 3.2 Node Lifecycle and Hierarchy

1. `scene.create_node(name: string, parent: SceneNode?) -> SceneNode`
Creates a node. `parent=nil` creates a root node.

2. `scene.destroy_node(node: SceneNode) -> boolean`

3. `scene.destroy_hierarchy(root: SceneNode) -> boolean`

4. `scene.reparent(node: SceneNode, new_parent: SceneNode?, preserve_world: boolean?) -> boolean`
Default `preserve_world=true`.

5. `scene.root_nodes() -> {SceneNode}`

## 3.3 Path Queries

Canonical API is `scene.query(...)`.

Convenience wrappers (defined as equivalent single-call query helpers):

1. `scene.find_one(path: string, scope: SceneNode?) -> SceneNode?`
Exact path lookup.

2. `scene.find_many(path_pattern: string, scope: SceneNode?) -> {SceneNode}`
Supports wildcard path patterns.

3. `scene.count(path_pattern: string, scope: SceneNode?) -> integer`

4. `scene.exists(path_pattern: string, scope: SceneNode?) -> boolean`

## 3.4 Query Object

1. `scene.query(path_pattern: string) -> SceneQuery`

`SceneQuery` methods:

1. `q:scope(node: SceneNode) -> SceneQuery`
2. `q:scope_many(nodes: {SceneNode}) -> SceneQuery`
3. `q:clear_scope() -> SceneQuery`
4. `q:first() -> SceneNode?`
5. `q:all() -> {SceneNode}`
6. `q:count() -> integer`
7. `q:any() -> boolean`

Notes:

1. Query objects are lightweight handles over the current active scene.
2. Scope methods mutate query state and return `self` for chaining.
3. Convenience wrappers in 3.3 must not introduce behavior not expressible via `SceneQuery`.

## 3.5 Query Object Lifetime and Evaluation Semantics

`SceneQuery` in Lua is a persistent userdata handle with lazy execution.

Construction (`scene.query(pattern)`):

1. Captures the path/pattern string.
2. Captures the bound scene at creation time (scene-bound query object).
3. Uses non-owning lifetime semantics for scene access so query handles do not
keep scenes alive.

Execution (`q:first`, `q:all`, `q:count`, `q:any`):

1. Applies pattern and scope lazily when method is invoked.
2. Executes only against the bound scene captured at creation.
3. If the bound scene no longer exists, return contract-safe empty results:

- `first() -> nil`
- `all() -> {}`
- `count() -> 0`
- `any() -> false`

Exception mapping:

1. Binding code must catch C++ query exceptions due to expired scene or invalid
execution context.
2. Failures are converted to the same contract-safe results above.
3. These failures must not surface as unhandled Lua/C++ exceptions.

## 4. SceneNode API

## 4.1 Identity and State

1. `node:is_alive() -> boolean`
2. `node:runtime_id() -> table`
Shape: `{ scene_id: integer, node_index: integer }`. Runtime diagnostic identity only.
3. `node:get_name() -> string?`
4. `node:set_name(name: string) -> boolean`
5. `node:to_string() -> string`
Equivalent of `__tostring`.

## 4.2 Hierarchy

1. `node:get_parent() -> SceneNode?`
2. `node:set_parent(parent: SceneNode?, preserve_world: boolean?) -> boolean`
3. `node:is_root() -> boolean`
4. `node:has_parent() -> boolean`
5. `node:has_children() -> boolean`
6. `node:get_children() -> {SceneNode}`
7. `node:get_first_child() -> SceneNode?`
8. `node:get_next_sibling() -> SceneNode?`
9. `node:get_prev_sibling() -> SceneNode?`
10. `node:destroy() -> boolean`

## 4.3 Transform

1. `node:get_local_position() -> vec3?`
2. `node:set_local_position(v: vec3) -> boolean`
3. `node:get_local_rotation() -> quat?`
4. `node:set_local_rotation(q: quat) -> boolean`
5. `node:get_local_scale() -> vec3?`
6. `node:set_local_scale(v: vec3) -> boolean`
7. `node:set_local_transform(pos: vec3, rot: quat, scale: vec3) -> boolean`
8. `node:translate(offset: vec3, local_space: boolean?) -> boolean`
Default `local_space=true`.
9. `node:rotate(delta: quat, local_space: boolean?) -> boolean`
Default `local_space=true`.
10. `node:scale_by(factor: vec3) -> boolean`
11. `node:get_world_position() -> vec3?`
12. `node:get_world_rotation() -> quat?`
13. `node:get_world_scale() -> vec3?`
14. `node:look_at(target: vec3, up: vec3?) -> boolean`

## 4.4 Component Accessors

1. `node:camera() -> CameraComponent?`
2. `node:light() -> LightComponent?`
3. `node:renderable() -> RenderableComponent?`
4. `node:scripting() -> NodeScriptingComponent?`

Notes:

1. Accessors return `nil` when component is absent.
2. Component handles are invalidated automatically when node dies.

## 4.5 Property Aliases (Ergonomic Layer)

The following property aliases are supported for idiomatic Lua ergonomics.
They are strict one-to-one aliases to canonical methods above.

Read/write:

1. `node.name` <-> `get_name` / `set_name`
2. `node.parent` <-> `get_parent` / `set_parent`
3. `node.local_position` <-> `get_local_position` / `set_local_position`
4. `node.local_rotation` <-> `get_local_rotation` / `set_local_rotation`
5. `node.local_scale` <-> `get_local_scale` / `set_local_scale`

Read-only:

1. `node.is_alive` <-> `is_alive`
2. `node.world_position` <-> `get_world_position`
3. `node.world_rotation` <-> `get_world_rotation`
4. `node.world_scale` <-> `get_world_scale`
5. `node.camera_component` <-> `camera()`
6. `node.light_component` <-> `light()`
7. `node.renderable_component` <-> `renderable()`
8. `node.scripting_component` <-> `scripting()`

Notes:

1. Property aliases are syntax sugar only; canonical method contracts remain authoritative.
2. No property aliases are provided for heavy operations (`destroy`, `reparent`, query, environment mutations).
3. Axis constants/helpers are not duplicated on `SceneNode`; use `oxygen.conventions` and `oxygen.math` instead.

## 5. Camera Component API

## 5.1 Attach / Detach

On `SceneNode`:

1. `node:attach_perspective_camera(opts: table?) -> CameraComponent`
2. `node:attach_orthographic_camera(opts: table?) -> CameraComponent`
3. `node:detach_camera() -> boolean`

`CameraComponent`:

1. `camera:type() -> "perspective" | "orthographic"`

## 5.2 Common

1. `camera:get_viewport() -> table?`
Shape: `{x, y, width, height}`
2. `camera:set_viewport(vp: table?) -> boolean`
`nil` clears override.
3. `camera:get_exposure() -> table`
Shape: `{aperture_f, shutter_rate, iso, ev}`
4. `camera:set_exposure(exposure: table) -> boolean`
Accepts `{aperture_f, shutter_rate, iso}`.

## 5.3 Perspective

1. `camera:get_fov_y_radians() -> number`
2. `camera:set_fov_y_radians(v: number) -> boolean`
3. `camera:get_aspect_ratio() -> number`
4. `camera:set_aspect_ratio(v: number) -> boolean`
5. `camera:get_near_plane() -> number`
6. `camera:set_near_plane(v: number) -> boolean`
7. `camera:get_far_plane() -> number`
8. `camera:set_far_plane(v: number) -> boolean`

## 5.4 Orthographic

1. `camera:get_extents() -> table`
Shape: `{left, right, bottom, top, near_plane, far_plane}`
2. `camera:set_extents(extents: table) -> boolean`

## 6. Light Component API

## 6.1 Attach / Detach

On `SceneNode`:

1. `node:attach_directional_light(opts: table?) -> LightComponent`
2. `node:attach_point_light(opts: table?) -> LightComponent`
3. `node:attach_spot_light(opts: table?) -> LightComponent`
4. `node:detach_light() -> boolean`

`LightComponent`:

1. `light:type() -> "directional" | "point" | "spot"`

## 6.2 Common Properties

1. `light:get_affects_world() -> boolean`
2. `light:set_affects_world(v: boolean) -> boolean`
3. `light:get_color_rgb() -> vec3`
4. `light:set_color_rgb(v: vec3) -> boolean`
5. `light:get_mobility() -> "realtime" | "mixed" | "baked"`
6. `light:set_mobility(v: string) -> boolean`
7. `light:get_casts_shadows() -> boolean`
8. `light:set_casts_shadows(v: boolean) -> boolean`
9. `light:get_exposure_compensation_ev() -> number`
10. `light:set_exposure_compensation_ev(v: number) -> boolean`
11. `light:get_shadow_settings() -> table`
12. `light:set_shadow_settings(v: table) -> boolean`

## 6.3 Directional

1. `light:get_intensity_lux() -> number`
2. `light:set_intensity_lux(v: number) -> boolean`
3. `light:get_angular_size_radians() -> number`
4. `light:set_angular_size_radians(v: number) -> boolean`
5. `light:get_environment_contribution() -> boolean`
6. `light:set_environment_contribution(v: boolean) -> boolean`
7. `light:get_is_sun_light() -> boolean`
8. `light:set_is_sun_light(v: boolean) -> boolean`
9. `light:get_cascaded_shadows() -> table`
10. `light:set_cascaded_shadows(v: table) -> boolean`

## 6.4 Point / Spot

1. `light:get_range() -> number`
2. `light:set_range(v: number) -> boolean`
3. `light:get_attenuation_model() -> "inverse_square" | "linear" | "custom_exponent"`
4. `light:set_attenuation_model(v: string) -> boolean`
5. `light:get_decay_exponent() -> number`
6. `light:set_decay_exponent(v: number) -> boolean`
7. `light:get_source_radius() -> number`
8. `light:set_source_radius(v: number) -> boolean`
9. `light:get_luminous_flux_lm() -> number`
10. `light:set_luminous_flux_lm(v: number) -> boolean`

Spot-only:

1. `light:get_inner_cone_angle_radians() -> number`
2. `light:set_inner_cone_angle_radians(v: number) -> boolean`
3. `light:get_outer_cone_angle_radians() -> number`
4. `light:set_outer_cone_angle_radians(v: number) -> boolean`

## 7. Renderable Component API

## 7.1 Presence and Geometry

1. `renderable:has_geometry() -> boolean`
2. `renderable:set_geometry(asset: string|userdata) -> boolean`
3. `renderable:get_geometry() -> any?`
4. `renderable:detach() -> boolean`

## 7.2 LOD and Selection

1. `renderable:get_lod_policy() -> table`
2. `renderable:set_lod_policy(policy: table) -> boolean`
3. `renderable:get_active_lod_index() -> integer?`
4. `renderable:get_effective_lod_count() -> integer`

## 7.3 Submesh and Materials

1. `renderable:is_submesh_visible(lod: integer, submesh: integer) -> boolean`
2. `renderable:set_submesh_visible(lod: integer, submesh: integer, visible: boolean) -> boolean`
3. `renderable:set_all_submeshes_visible(visible: boolean) -> boolean`
4. `renderable:set_material_override(lod: integer, submesh: integer, material: string|userdata) -> boolean`
5. `renderable:clear_material_override(lod: integer, submesh: integer) -> boolean`
6. `renderable:resolve_submesh_material(lod: integer, submesh: integer) -> any?`

## 7.4 Bounds

1. `renderable:get_world_bounding_sphere() -> vec4`
2. `renderable:get_world_submesh_aabb(submesh: integer) -> table?`
Shape: `{min=vec3, max=vec3}`

## 8. Node Scripting Component API

1. `script:slots() -> {SlotRef}`
2. `script:add_slot(script_asset: string|userdata) -> boolean`
3. `script:remove_slot(slot: SlotRef) -> boolean`
4. `script:set_param(slot: SlotRef, key: string, value: any) -> boolean`
5. `script:get_param(slot: SlotRef, key: string) -> any?`
6. `script:params(slot: SlotRef) -> table`
Returns effective parameters.

Optional engine-tools APIs (may be gated by build flag):

1. `script:mark_slot_ready(slot: SlotRef, executable: userdata) -> boolean`
2. `script:mark_slot_compile_failed(slot: SlotRef, diagnostic: string) -> boolean`

## 9. Scene Environment API

On `oxygen.scene`:

1. `scene.has_environment() -> boolean`
2. `scene.get_environment() -> Environment?`
3. `scene.ensure_environment() -> Environment`
4. `scene.clear_environment() -> boolean`

`Environment` object:

1. `env:systems() -> {string}`
Canonical system names.
2. `env:has_system(name: string) -> boolean`
3. `env:remove_system(name: string) -> boolean`

Typed ensure accessors:

1. `env:ensure_fog() -> FogSystem`
2. `env:ensure_sky_atmosphere() -> SkyAtmosphereSystem`
3. `env:ensure_sky_light() -> SkyLightSystem`
4. `env:ensure_sky_sphere() -> SkySphereSystem`
5. `env:ensure_sun() -> SunSystem`
6. `env:ensure_clouds() -> VolumetricCloudsSystem`
7. `env:ensure_post_process() -> PostProcessSystem`

Typed getters:

1. `env:fog() -> FogSystem?`
2. `env:sky_atmosphere() -> SkyAtmosphereSystem?`
3. `env:sky_light() -> SkyLightSystem?`
4. `env:sky_sphere() -> SkySphereSystem?`
5. `env:sun() -> SunSystem?`
6. `env:clouds() -> VolumetricCloudsSystem?`
7. `env:post_process() -> PostProcessSystem?`

## 10. Environment System APIs

Only stable authored/runtime knobs are exposed.

## 10.1 FogSystem

1. `get_model` / `set_model("exponential_height"|"volumetric")`
2. `get_extinction_sigma_t_per_meter` / `set_extinction_sigma_t_per_meter`
3. `get_height_falloff_per_meter` / `set_height_falloff_per_meter`
4. `get_height_offset_meters` / `set_height_offset_meters`
5. `get_start_distance_meters` / `set_start_distance_meters`
6. `get_max_opacity` / `set_max_opacity`
7. `get_single_scattering_albedo_rgb` / `set_single_scattering_albedo_rgb`
8. `get_anisotropy` / `set_anisotropy`

## 10.2 SkyAtmosphereSystem

Expose all stable parameters from `SkyAtmosphere`:
planet radius, atmosphere height, ground albedo, Rayleigh/Mie/Ozone scattering profiles, anisotropy, multi-scattering factor, sun disk enable, aerial perspective controls.

## 10.3 SkyLightSystem

Source, cubemap resource, intensity multipliers, tint, diffuse/specular intensity.

## 10.4 SkySphereSystem

Source, cubemap resource, solid color, intensity, rotation, tint.

## 10.5 SunSystem

Sun source, world direction, azimuth/elevation, color, illuminance (lux), disk angular radius, cast shadows, temperature, optional referenced scene light.

## 10.6 VolumetricCloudsSystem

Base altitude, layer thickness, coverage, extinction, albedo, phase anisotropy, wind direction/speed, shadow strength.

## 10.7 PostProcessSystem

Tonemapper, exposure mode and parameters, adaptation speeds, metering mode, bloom, saturation, contrast, vignette.

## 11. Idiomatic Lua Usage Examples

```lua
local scene = oxygen.scene
local conv = oxygen.conventions

local player = scene.find_one("World/Player")
if player and player:is_alive() then
  player:translate(vector(0, 0, 1))
end

local enemies = scene.query("**/Enemy"):all()
for i = 1, #enemies do
  enemies[i]:set_parent(player, true)
end

local env = scene.ensure_environment()
local sun = env:ensure_sun()
sun:set_illuminance_lx(90000.0)
sun:set_azimuth_elevation_degrees(45.0, 30.0)

if player then
  player:look_at(vector(0, 0, 0), conv.view.up)
end
```

```lua
-- Property alias style (same semantics as canonical methods)
local node = scene.create_node("Enemy")
node.local_position = vector(10, 0, 5)
node.name = "Boss_Level1"
print(node.world_position)
```

## 12. Removed Legacy API (Must Not Exist)

The following names are explicitly removed from v1 and must not be exported:

1. `oxygen.scene.find`
2. `oxygen.scene.find_path`
3. `oxygen.scene.create`
4. `oxygen.scene.current`
5. `oxygen.scene.get_param`
6. `oxygen.transform.*`
7. Legacy mixed-style context helper methods intended as transitional shims.

## 13. Validation and Test Requirements

1. API presence tests for all v1 symbols.
2. API absence tests for all removed legacy symbols.
3. Error-contract tests (type misuse, dead node behavior).
4. Hierarchy mutation semantics tests (preserve world transform).
5. Query correctness tests for exact and wildcard paths.
6. Camera/light/environment roundtrip tests for stable authored parameters.
7. Performance sanity tests for repeated `find_many` and scoped query use.
8. Renderable handle interchange tests:
`set_geometry` / `set_material_override` must accept both token strings and
`oxygen.assets` userdata, and reject wrong userdata kinds deterministically.

## 14. Event Integration (Code-Fact Status)

1. The current Scene module does not expose a built-in scene-mutation signal API
for node/component/environment lifecycle events.
2. Therefore, v1 must not claim guaranteed `scene.node.*` or
`scene.environment.*` events until C++ emits them at mutation commit points.
3. When implemented, scene-domain events must flow through `oxygen.events`
(core pack) instead of a parallel scene-specific bus.
4. Event payload identity must use runtime handle parts
(`scene_id`, `node_index`) and document non-persistence across runs.

### 14.1 C++ Hook Backlog (Concrete Tasks)

1. Add Scene mutation notifications in `Scene` write paths:
`CreateNode*`, `DestroyNode*`, `ReparentNode*`, `SetEnvironment`,
`ClearEnvironment`.
2. Define a compact event payload struct in Scene module for scripting bridge:
`scene_id`, `node_index`, `parent_index` (optional), and event kind.
3. Emit notifications only after mutation commit succeeds (never pre-commit).
4. Add a scripting-runtime bridge entry point that forwards committed scene
notifications to `oxygen.events.emit`.
5. Reserve canonical names under `scene.node.*` and `scene.environment.*`,
and reject ad-hoc names to keep contract stable.
6. Add tests in Scene + Scripting verifying emission ordering, no duplicates,
and no emission on failed/rolled-back mutations.

## 15. Implementation Readiness Matrix

This section classifies v1 APIs by implementation readiness against current C++
code facts.

Legend:

1. `backed-now`: already supported by existing C++ API shape; binding work only.
2. `needs-hooks`: requires new C++ surface or explicit engine hooks before binding.
3. `policy-needed`: behavior depends on engine policy decision (phase/authority).

## 15.1 Scene Core

1. `scene.current_node`, `scene.param`: `backed-now`
Via existing binding context helpers in scripting common bindings.
2. `scene.create_node`, `scene.destroy_node`, `scene.destroy_hierarchy`,
`scene.reparent`, `scene.root_nodes`: `backed-now`
Backed by `Scene` node factory/reparent APIs.
3. mutation phase enforcement: `policy-needed`
Requires a finalized phase contract in scripting runtime.

## 15.2 Query

1. `scene.query(...)` and `SceneQuery` (`first/all/count/any`, scope): `backed-now`
Backed by `SceneQuery` immediate + scoped traversal APIs.
2. persistent Lua query userdata with lazy execution and scene-bound semantics:
`backed-now`
Implemented in bindings by storing pattern/scope and invoking C++ query methods
on demand.
3. batch query exposure to Lua object (`q:batch`): `needs-hooks`
Possible with current C++ batch API, but needs clear Lua callback contract and error model.
4. path wrappers (`find_one/find_many/count/exists`): `backed-now`
Thin wrappers over query object methods.

## 15.3 SceneNode Identity and Hierarchy

1. `is_alive`, `runtime_id`, `name`, parent/sibling/children, destroy: `backed-now`
Backed by `SceneNode` + `Scene` APIs.

## 15.4 Transform

1. local/world get/set, set_local_transform, translate/rotate/scale_by, look_at:
`backed-now`
Backed by `SceneNode::Transform`.

## 15.5 Camera

1. attach/detach and perspective/orthographic parameter read-write: `backed-now`
Backed by `SceneNode` camera attach/replace and camera component APIs.
2. exact Lua-facing camera variant abstraction (`CameraComponent` unification):
`needs-hooks`
Binding layer must define and maintain a stable tagged variant surface.

## 15.6 Light

1. attach/detach directional/point/spot and type-specific authored properties:
`backed-now`
Backed by light component headers and node light attach APIs.
2. unified `LightComponent` dynamic dispatch layer: `needs-hooks`
Binding layer needs a strict variant/metatable strategy.

## 15.7 Renderable

1. geometry, LOD policy, submesh visibility/material override, bounds helpers:
`backed-now`
Backed by `SceneNode::Renderable`.
2. script-friendly asset handle interchange (`string|userdata` policy):
`backed-now`
Finalized v1 policy:
`set_geometry` and `set_material_override` accept either string token or
`oxygen.assets` userdata (`GeometryAsset` / `MaterialAsset`).
`get_geometry` / `resolve_submesh_material` return string token when the source
is token-backed synthetic mapping, otherwise they return asset userdata.

## 15.8 Node Scripting Component

1. slot enumeration/add/remove, parameter get/set/effective params: `backed-now`
Backed by `SceneNode::Scripting`.
2. compile-state mutation helpers (`mark_slot_ready`, `mark_slot_compile_failed`):
`policy-needed`
Should be gated to tools/runtime authority contexts.

## 15.9 Scene Environment

1. scene-level presence/get/clear: `backed-now`
Backed by `Scene::HasEnvironment/GetEnvironment/ClearEnvironment`.
2. typed system ensure/get/remove/list on `SceneEnvironment`: `needs-hooks`
Current C++ API is template-typed and not runtime-name-driven; Lua needs
non-template dispatch adapters per system type.

## 15.10 Environment Systems

1. Fog/SkyAtmosphere/SkyLight/SkySphere/Sun/Clouds/PostProcess parameter
read-write: `backed-now`
Each system has concrete setter/getter surface in headers.
2. runtime-name generic system API (`has_system(name)`, `remove_system(name)`):
`needs-hooks`
Requires explicit type-name registry/dispatch map in bindings layer.

## 15.11 Events

1. guaranteed scene mutation events (`scene.node.*`, `scene.environment.*`):
`needs-hooks`
No current Scene mutation event stream exists at commit points.
2. transport channel: `backed-now` via `oxygen.events` once hooks exist.

## 16. Implementation plan

- [x] Freeze scope and traceability.
- [x] Build a requirement matrix from `design/scripting/scene-scripting-api-v1.md` into implementation checkboxes (API item -> file(s) -> tests).
- [x] Mark each matrix item as binding-only, needs Scene C++ hook, or policy/config.
- [x] Implement complete `oxygen.scene` table and all submodules/types with final names/signatures.
- [x] Add deterministic stubs/placeholders for not-yet-backed APIs during bring-up.
- [x] Implement scene identity, node identity, lookup, hierarchy, lifecycle-safe validity checks.
- [x] Implement transform APIs and aliases using existing math/conventions bindings (no duplication).
- [x] Implement query userdata carrying scene weak reference + pattern string.
- [x] Ensure lazy query evaluation on `first/all/count/any` only.
- [x] Enforce deterministic expired-scene query behavior (`nil`, `{}`, `0`, `false`) with exception mapping.
- [x] Split scene node component bindings into grouped files by component type (`camera`, `light`, `renderable`, `scripting`) and keep `SceneNodeComponentBindings.cpp` as registrar-only.
- [x] Implement camera component bindings per v1 surface (attach/detach/type + perspective/orthographic + viewport/exposure).
- [x] Implement remaining light component bindings per v1 surface.
- [x] Implement `light_get_shadow_settings` / `light_set_shadow_settings`.
- [x] Implement `light_get_cascaded_shadows` / `light_set_cascaded_shadows`.
- [x] Implement renderable asset-handle marshalling for `renderable_set_geometry` / `renderable_get_geometry`.
- [x] Implement material-handle marshalling for `renderable_set_material_override` / `renderable_resolve_submesh_material`.
- [x] Implement full node scripting component API (`slots`, `add_slot`, `remove_slot`, `set_param`, `get_param`, `params`), beyond current attach/detach/has/slots_count.
- [x] Add remaining capability guards (`has_*`, availability checks) and type-safe argument validation for all component calls.
- [x] Implement full v1 environment APIs and per-system parameter read/write surface.
- [x] Wire environment ensure/get/remove/list to existing engine systems (`fog`, `sky_atmosphere`, `sky_light`, `sky_sphere`, `sun`, `clouds`, `post_process`).
- [x] Implement only events that are code-factual today and route through `oxygen.events` conventions. (current code-factual status: no scene mutation events exported; scene pack does not create a parallel event bus)
- [x] For missing scene mutation/event hooks, create concrete C++ hook tasks and deterministic temporary behavior as documented. (temporary behavior: deterministic no-op/no emission for scene-domain events until Scene C++ hooks exist; hook backlog captured in Section 14)
- [x] Remove old scene APIs listed as obsolete in this doc.
- [x] Add compatibility guards so old names fail loudly with migration hints in debug builds (if allowed by policy).
- [x] Add exhaustive binding tests by API group: happy path, invalid args, stale handles, scene teardown, query edge cases.
- [x] Add regression tests for observed runtime failures (nil value calls, metatable gaps, etc.).
- [x] Add scenario tests with real Lua scripts on populated scene graphs (hierarchy + components + queries + environment).
- [x] Clean up scene binding code paths for `clang-tidy` implicit bool conversion issues (`readability-implicit-bool-conversion`) in modified files.
- [x] Validate and implement idiomatic Lua property/method ergonomics for `SceneNode` aliases.
- [ ] Measure tick-path allocations/VM overhead and optimize userdata/metatable caching.
- [x] Update this design doc with final code-factual deltas and add migration notes.
- [x] Run full scripting + scene test suites and perform export-symbol audit before release. (`Oxygen.Scene` + `Oxygen.Scripting` test suites passing; `dumpbin /exports` audit run for `Oxygen.Scripting-d.dll`)

## 17. Future Enhancements / Missing Features

### 17.1 Missing Features (Not in v1)

1. Scene mutation event emission from C++ commit points
(`CreateNode*`, `DestroyNode*`, `ReparentNode*`, `SetEnvironment`,
`ClearEnvironment`) into `oxygen.events`.
2. Stable scene event payload contract implementation (`scene_id`,
`node_index`, optional `parent_index`, event kind) and bridge tests.
3. Query batch Lua API (`q:batch`) with finalized callback/error contract.
4. Optional tools-authority scripting compile-state APIs
(`mark_slot_ready`, `mark_slot_compile_failed`) once policy gates are approved.

### 17.2 Future Enhancements

1. Tick-path allocation and VM-overhead profiling plus optimization
(userdata/metatable caching and hot-path allocation reduction).
2. Additional performance regression tests focused on high-frequency query and
component mutation script workloads.
3. Extended diagnostics/telemetry for scripting scene hot paths
(timings, alloc counts, failure counters) for production tuning.
