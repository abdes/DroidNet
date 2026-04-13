# Content Scripting API v7 (Production)

**Date:** 2026-02-28
**Status:** Design / Reference

Audience: Engine, tools, gameplay scripting
Language target: Luau / Lua 5.1-style runtime with Luau extensions

This document defines the authoritative v7 content scripting API for Oxygen.
It is a production API, not a transitional API.

## 1. Goals

1. Provide gameplay-usable content APIs for querying and loading assets/resources.
2. Use idiomatic Lua ergonomics:
   - snake_case names
   - clear separation between module-level operations and userdata methods
3. Keep behavior deterministic and explicit under failure.
4. Expose only code-factual capabilities of `content::IAssetLoader` for v7.
5. Remove obsolete/legacy script APIs entirely.
6. Reuse existing core script packs (`oxygen.ids`, `oxygen.events`) instead of duplicating utilities.

## 2. Conventions and Contracts

## 2.1 Naming

1. All public API names are snake_case.
2. Module is `oxygen.assets` for runtime content operations.
3. No camelCase public script API.
4. Key parser/formatter helpers are leveraged from `oxygen.ids` (`uuid`).

## 2.2 Value Types

1. `resource_key` is Lua integer (`number`) mapping to `content::ResourceKey`.
2. `asset_guid` is canonical UUID string (lowercase hex + dashes).
3. Optional return values use `nil`.
4. Sequence outputs are Lua array tables (1-based).
5. Asset/resource userdata are opaque and stable only while retained by script references.

## 2.3 Error Model

1. Programmer errors (wrong arity/type/invalid UUID format): throw Lua error.
2. Cache miss/query miss: return `false` for `has_*`, `nil` for `get_*`.
3. Async load failure callback contract:
   - callback receives `nil` on failure
   - callback receives userdata on success
4. API must not silently fake success.

## 2.4 Mutability / Phase Policy

1. Read/query APIs are allowed in all script phases.
2. Async load requests are allowed in engine-approved phases.
3. Cache trim/release operations are policy-gated and may be restricted by phase.
4. If phase is disallowed, throw clear Lua error with function name and active phase.

## 2.5 Stability

1. v7 names in this spec are stable and versioned.
2. Legacy names listed in section 9 must not be exported.
3. GUID/resource-key identity is runtime content identity; no cross-run persistence guarantee.

## 3. Global Module

The module is exposed as `oxygen.assets`.

## 3.1 Availability and Health

1. `assets.available() -> boolean`
Returns whether `AsyncEngine` has an active `IAssetLoader`.

2. `assets.enabled() -> boolean`
Returns whether asset loader feature is enabled by engine config.

## 3.2 Resource Cache Query

Resources are looked up by `ResourceKey` integer.

1. `assets.has_texture(resource_key: integer) -> boolean`
2. `assets.get_texture(resource_key: integer) -> TextureResource?`
3. `assets.has_buffer(resource_key: integer) -> boolean`
4. `assets.get_buffer(resource_key: integer) -> BufferResource?`

Semantics:

1. These APIs do not trigger loading.
2. They only inspect current loader cache state.

## 3.3 Asset Cache Query

Assets are looked up by UUID string (`asset_guid`).

1. `assets.has_material(asset_guid: string) -> boolean`
2. `assets.get_material(asset_guid: string) -> MaterialAsset?`
3. `assets.has_geometry(asset_guid: string) -> boolean`
4. `assets.get_geometry(asset_guid: string) -> GeometryAsset?`
5. `assets.has_script(asset_guid: string) -> boolean`
6. `assets.get_script(asset_guid: string) -> ScriptAsset?`
7. `assets.has_input_action(asset_guid: string) -> boolean`
8. `assets.get_input_action(asset_guid: string) -> InputActionAsset?`
9. `assets.has_input_mapping_context(asset_guid: string) -> boolean`
10. `assets.get_input_mapping_context(asset_guid: string) -> InputMappingContextAsset?`

Semantics:

1. These APIs do not trigger loading.
2. They only inspect current loader cache state.

## 3.4 Async Loading

v7 uses callback-based async APIs (code-factual in `IAssetLoader::StartLoad*`).

Resources:

1. `assets.load_texture_async(resource_key: integer, on_complete: function) -> boolean`
2. `assets.load_buffer_async(resource_key: integer, on_complete: function) -> boolean`

Assets:

1. `assets.load_material_async(asset_guid: string, on_complete: function) -> boolean`
2. `assets.load_geometry_async(asset_guid: string, on_complete: function) -> boolean`
3. `assets.load_script_async(asset_guid: string, on_complete: function) -> boolean`

Input assets:

1. `assets.load_input_action_async(asset_guid: string, on_complete: function) -> boolean`
2. `assets.load_input_mapping_context_async(asset_guid: string, on_complete: function) -> boolean`

Callback contract:

1. Called once.
2. Success: receives non-nil userdata.
3. Failure: receives `nil`.
4. Code-factual gap note: input-action and input-mapping-context async use
   deterministic cache-resolve fallback in v7 because `IAssetLoader` does not
   yet expose `StartLoadInputActionAsset` / `StartLoadInputMappingContextAsset`.

## 3.5 Lifecycle and Cache Control

1. `assets.release_resource(resource_key: integer) -> boolean`
2. `assets.release_asset(asset_guid: string) -> boolean`
3. `assets.trim_cache() -> boolean`

Semantics:

1. `release_*` maps to loader checkout release behavior.
2. `trim_cache` triggers loader cache trim and returns operation acceptance.

## 3.6 Synthetic Runtime Keys

1. `assets.mint_synthetic_texture_key() -> integer`
2. `assets.mint_synthetic_buffer_key() -> integer`

These keys are for runtime-generated resource workflows.

## 3.7 Dev-Only Mount and Maintenance APIs (Optional Policy Gate)

These APIs map to loader mount administration and may be gated behind dev build flags.

1. `assets.add_pak_file(path: string) -> boolean`
2. `assets.add_loose_cooked_root(path: string) -> boolean`
3. `assets.clear_mounts() -> boolean`

## 3.8 Procedural Runtime Content

1. `assets.create_procedural_geometry(kind: string, params?: table) -> GeometryAsset?`
2. `assets.create_default_material() -> MaterialAsset`
3. `assets.create_debug_material() -> MaterialAsset`

Supported geometry `kind` values:

1. `cube`
2. `sphere`
3. `plane`
4. `cylinder`
5. `cone`
6. `torus`
7. `quad`
8. `arrow_gizmo`

Notes:

1. Geometry generation is backed by `data::ProceduralMeshes` and returned as
   runtime geometry userdata.
2. This path creates runtime assets and does not auto-register generated assets
   in loader cache indices.

## 4. Resource Userdata API

`TextureResource` and `BufferResource` are opaque in v7.

1. `obj:is_valid() -> boolean`
2. `obj:key() -> integer`
3. `obj:type_name() -> string`
4. `obj:to_string() -> string`

No mutable decode/raw-memory write APIs in v7.

## 5. Asset Userdata API

`MaterialAsset`, `GeometryAsset`, `ScriptAsset`, `InputActionAsset`,
`InputMappingContextAsset` are opaque in v7.

1. `asset:is_valid() -> boolean`
2. `asset:guid() -> string`
3. `asset:type_name() -> string`
4. `asset:to_string() -> string`

No direct mutable asset editing in v7.

## 6. Idiomatic Lua Usage Examples

```lua
local assets = oxygen.assets

if not assets.available() then
  return
end

local guid = "01234567-89ab-cdef-0123-456789abcdef"

if not assets.has_material(guid) then
  assets.load_material_async(guid, function(mat)
    if mat == nil then
      oxygen.log.error("material load failed: " .. guid)
      return
    end
    oxygen.log.info("material loaded: " .. mat:guid())
  end)
else
  local mat = assets.get_material(guid)
  if mat ~= nil then
    oxygen.log.info("material already cached: " .. mat:guid())
  end
end
```

```lua
local assets = oxygen.assets

local tex_key = assets.mint_synthetic_texture_key()
local ok = assets.release_resource(tex_key)
if not ok then
  oxygen.log.warn("synthetic key not checked out yet")
end

assets.trim_cache()
```

## 7. Validation and Test Requirements

1. API presence tests for all v7 symbols.
2. API absence tests for removed legacy symbols.
3. Error-contract tests (wrong type, bad UUID, missing loader).
4. Async callback tests (success/failure exactly-once behavior).
5. Cache query tests (`has_*`/`get_*`) for hit/miss paths.
6. Release/trim behavior tests for accepted/rejected operations.
7. Scenario test combining async load + query + release.
8. Procedural creation tests for geometry/material userdata shape.

## 8. Event Integration (Code-Fact Status)

1. `IAssetLoader` supports eviction subscriptions in C++ (`SubscribeResourceEvictions`).
2. v7 must not claim `oxygen.assets.on_*` event APIs until scripting bridge exists.
3. When implemented, content-domain events must route via `oxygen.events`.
4. Payload identity should include at least `{ kind, key/guid, type_name, reason }`.

## 9. Removed Legacy API (Must Not Exist)

The following names must not be exported by v7:

1. `oxygen.content.*` (old namespace alias)
2. `oxygen.assets.find`
3. `oxygen.assets.find_path`
4. any camelCase variants of v7 names

## 10. Implementation Readiness Matrix

Legend:

1. `backed-now`: already supported by existing C++ API shape; binding work only.
2. `needs-hooks`: requires new C++ surface or explicit bridge hooks before binding.
3. `policy-needed`: behavior depends on engine policy decision.

## 10.1 Module and Availability

1. `assets.available`, `assets.enabled`: `backed-now` (engine + config access).

## 10.2 Resource Cache Query

1. `has/get texture/buffer`: `backed-now` (`IAssetLoader::Has/GetTexture/Buffer`).

## 10.3 Asset Cache Query

1. `has/get material/geometry/script/input action/input mapping context`: `backed-now`.

## 10.4 Async Loading

1. callback-based `load_*_async`: `backed-now` (`StartLoad*`).
2. promise/future style API: `needs-hooks` (scripting coroutine contract layer).
3. load prioritization: `needs-hooks`.

## 10.5 Lifecycle / Cache

1. `release_resource`, `release_asset`, `trim_cache`: `backed-now`.

## 10.6 Synthetic Keys

1. `mint_synthetic_texture_key`, `mint_synthetic_buffer_key`: `backed-now`.

## 10.7 Mount Administration

1. `add_pak_file`, `add_loose_cooked_root`, `clear_mounts`: `backed-now`.
2. phase/authority gating for these APIs: `policy-needed`.

## 10.8 Discovery and Metadata

1. `find_assets`, `get_asset_metadata`, `enumerate by type`: `needs-hooks`.

## 10.9 Runtime Creation / Mutation

1. primitive procedural geometry creation: `backed-now` (`data::ProceduralMeshes`).
2. default/debug procedural material creation: `backed-now` (`MaterialAsset::CreateDefault/CreateDebug`).
3. dynamic texture creation/edit path: `needs-hooks`.
4. material dynamic instances via assets module: `policy-needed` (boundary with renderer/scene APIs).

## 10.10 Events

1. content eviction events in scripts: `needs-hooks`.
2. transport channel via `oxygen.events`: `backed-now` once bridge exists.

## 11. Implementation plan

- [x] Freeze scope and traceability.
- [x] Build a requirement matrix from this document into implementation checkboxes (API item -> file(s) -> tests).
- [x] Mark each matrix item as binding-only, needs C++ hook, or policy/config.
- [x] Add a new `Content` scripting binding pack (`oxygen.assets`) and register it in scripting module startup.
- [x] Split implementation by responsibility (no god file):
  - module/availability
  - key parsing/conversion
  - resource query/load/release
  - asset query/load/release
  - optional mount admin (policy-gated)
  - procedural runtime creation
  - userdata metatables
- [x] Implement GUID string -> `data::AssetKey` conversion using `oxygen.ids` conventions.
- [x] Implement all cache query APIs (`has_*`, `get_*`) for resources and assets.
- [x] Implement all async callback load APIs (`load_*_async`) with deterministic callback contract.
- [x] Implement lifecycle APIs (`release_resource`, `release_asset`, `trim_cache`).
- [x] Implement synthetic key APIs.
- [x] Implement optional mount APIs with explicit policy gate and clear errors when disallowed.
- [x] Implement procedural runtime creation APIs for geometry/material.
- [x] Implement opaque userdata metatables for resources/assets (`is_valid`, `key/guid`, `type_name`, `__tostring`).
- [x] Remove/forbid legacy names (`oxygen.content.*`, old helpers) and add debug migration guards.
- [x] Add exhaustive tests by API group: presence, miss/hit, invalid args, loader unavailable, async callback behavior, stale handles.
- [x] Add scenario tests with realistic script flows (preload/load-on-demand/release/trim).
- [x] Run full scripting + content + engine test suites and perform export-symbol audit before release.
- [x] Update this doc with final code-factual deltas and migration notes.

## 12. Future Enhancements / Missing Features

### 12.1 Missing Features (Not in v7)

1. Promise/future/coroutine-native async asset API.
2. True async loader hooks for input action and input mapping context assets.
3. Asset discovery/registry browsing APIs.
4. Lightweight metadata query without full decode/load.
5. Streaming priority controls and preload groups.
6. Content event bridge (`IAssetLoader` eviction -> `oxygen.events`).
7. Generic hot-reload API for runtime scripts.

### 12.2 Future Enhancements

1. Performance profiling and optimization of async callback marshalling.
2. VM allocation reduction for repeated cache query hot paths.
3. Optional typed wrappers with richer readonly inspection fields.
