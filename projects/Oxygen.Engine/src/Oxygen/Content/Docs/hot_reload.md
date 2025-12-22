# Hot reload (Content)

This document defines hot reload behavior for Content in editor-focused modes,
primarily **loose cooked content**.

Related:

- `loose_cooked_content.md`
- `deps_and_cache.md`
- `implementation_plan.md`

---

## Goals

- Detect file changes for loose cooked artifacts.
- Invalidate affected assets/resources.
- Reload assets and rebuild dependents to DecodedCPUReady.
- Provide a clear safety model for systems holding references.

## Non-goals

- Live editing of GPU resources inside Content.
- Cross-container reference resolution.

---

## What “hot reload” means in Oxygen

There are two distinct layers:

1. Content hot reload

   - Updates CPU-decoded objects and dependency graph state.

2. Renderer reaction

   - Re-materializes GPU resources if required.

This document specifies only layer (1).

---

## Core difficulty: pointer stability

Current API returns `std::shared_ptr<T>` directly.

If an asset is reloaded, the new decoded object is a new allocation; existing
`shared_ptr` instances held by the editor/renderer will not automatically see
updated data.

We therefore need an explicit policy.

### Policy options

Option A: “Replace on next acquire” (lowest risk)

- Hot reload updates the cache entry.
- Existing `shared_ptr` remain valid but point to the old version.
- Next `LoadAsset` returns the new version.
- Systems receive an event: “AssetKey X updated; reacquire/rebind.”

Option B: Indirection handles (best UX, more invasive)

- Introduce stable handles (`AssetHandle`) that point to an indirection cell.
- Hot reload updates the cell to point to the new object.
- Existing handles see the new version without reacquire.

Recommendation:

- Start with Option A for Phase 1.5.
- Move to Option B once scenes and editor workflows mature.

---

## File watching

Inputs:

- manifest file changes
- asset descriptor file changes
- resource table/data file changes

A file watcher produces events:

- Created
- Modified
- Deleted
- Renamed

The watcher should coalesce bursts (debounce) to avoid repeated invalidation.

---

## Invalidation model

Hot reload requires an invalidation graph.

Given the forward-only dependency maps, we can find dependents by:

- maintaining a reverse map (recommended once hot reload lands), or
- scanning forward edges (acceptable for small projects; debug-only today)

### Invalidation steps

1. Determine changed keys (assets/resources) from file paths.
2. Compute impacted assets:

   - the changed asset itself
   - any asset that depends on it transitively

3. For each impacted asset:

   - check-in existing cache usage as appropriate
   - remove dependency edges
   - reload asset and repopulate dependencies

The exact eviction timing is still governed by refcounts.

---

## Events and editor integration

Emit a structured event:

- `ContentHotReloadEvent { AssetKey key, OldVersionId, NewVersionId }`

Consumers:

- editor UI updates
- renderer triggers rebind/materialization
- scene systems reacquire referenced assets

---

## Versioning

To make the system observable, Content should attach a version identifier:

- monotonic generation per AssetKey
- or content hash of the cooked bytes

This enables:

- preventing redundant reload
- debugging “why did this change?”

---

## Testing strategy

- Change a material descriptor; verify material reload triggers dependent
  geometry/material references refresh.
- Change a texture resource descriptor; verify materials are impacted.
- Delete an asset descriptor; verify dependents report missing dependency.

---

## Open questions

- Should hot reload be allowed to run while async loads are in-flight?
- How do we ensure that resource table/data updates are applied atomically
  (avoid reading half-written files)?
