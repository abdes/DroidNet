# Asset Dependency & Caching Design (Forward-Only Model)

> Canonical feature status: see
> `implementation_plan.md#detailed-feature-matrix`. This doc captures the
> forward-only dependency + caching model actually adopted in Phase 1 and the
> planned evolution toward the async CPU pipeline (Phase 2).
>
> GPU upload / residency is explicitly out of scope for Content. Assets reach a
> terminal Content state of `DecodedCPUReady`; GPU materialization & residency
> are Renderer responsibilities.

## Core Philosophy

**Unified Approach**: Dependency tracking and asset caching are a single
concern: reference counting drives safe lifetime. We deliberately removed
reverse dependency maps to reduce memory usage and complexity. A forward graph
(each asset stores its direct dependencies) plus per-asset reference counts and
cycle detection supply required safety.

## Design Principles

1. **Reference Counting**: Track usage counts for shared assets (extends current
   dependency registration)
2. **Automatic Cascading**: Both loading and unloading cascade through
   dependency graphs automatically
3. **Safe Unloading**: Implemented via ref counts + forward dependency ordering
  (diagnostic ordering tests pending)
4. **Cycle Rejection**: Cycle detection prevents invalid graphs (introduced in
  Phase 1)
5. **Deterministic Ordering**: Load/unload proceed in forward topological order
  (depth-first) with recursion guard

## Implementation Overview

High-level flow (synchronous path; async wrapper arrives Phase 2):

1. Request comes in via `AssetLoader::LoadAsset<T>(key)`.
2. Cache lookup: hit → ref_count++ → return handle.
3. Miss: locate PAK entry & dispatch registered loader (see `AssetLoader.h`).
4. Loader decodes asset, registering forward dependencies as they are
   discovered.
5. Asset inserted with its dependency span and ref_count = 1.
6. Release path decrements ref_count; on zero triggers unloader then cascades
   through recorded dependencies.

The system is intentionally forward-only (no reverse maps); safety comes from
ref counts + cycle detection at registration time. GPU materialization is
explicitly deferred to the Renderer.

### Dependency Types

**Asset → Asset Dependencies** (`AssetKey → AssetKey`):

- Example: GeometryAsset depends on MaterialAsset
- Registration: `AddAssetDependency()` (forward-only) ✅
- Unload safety: enforced by child holding a ref (refcounts + cycle detection)
  🔄 (ordering tests pending)

**Asset → Resource Dependencies** (`AssetKey → ResourceIndexT`):

- Example: MaterialAsset → TextureResource; GeometryAsset → BufferResource
- Registration: `AddResourceDependency()` ✅ (forward-only)
- Release policy: when asset ref_count hits zero and unloader runs, it
  decrements resource tables / releases indices.

**Note**: Resources never depend on Assets. They are lower-level primitives
referenced by index (bindless-friendly). Reverse dependency maps for resources
were intentionally omitted; diagnostics rely on asset-level references.

## Key Scenarios

### Loading with Automatic Caching (Synchronous Path)

Flow summary:

1. Cache hit → increment ref_count → return.
2. Cache miss → locate PAK entry → decode via registered loader.
3. Loader registers forward dependencies.
4. Insert asset with ref_count = 1 + dependency span.
5. Return shared handle.

### Safe Unloading with Dependency Validation (Forward-Only)

Flow summary:

1. Decrement ref_count; stop if > 0.
2. Invoke unloader (releases resource refs first).
3. Recursively ReleaseAsset on each recorded dependency (recursion guard +
   cycle-proof ordering).
4. Erase asset entry.

### Shared Asset Protection

Example timeline: load geometry A (material M=1), load geometry B (M=2), release
A (M=1), release B (M=0 → unloader triggers cascade).

## Critical Implementation Details (Summarized)

StoreAsset: insert-or-increment; attach forward dependency span.

ReleaseAsset: decrement; on zero → unloader (type-dispatched) → release forward
dependencies depth-first → erase entry.

### Dependency Validation & Cycle Detection

During dependency registration, a depth-first walk over the forward graph is
performed; encountering a node already on the active path aborts the load. Error
reports include the partial path (extended context logging is a backlog item).
No reverse maps are maintained.

## State Model

Loading State (Content module):

1. Loading (IO + decode executing)
2. DecodedCPUReady (asset in cache; dependencies also ready)

GPU materialization/residency: external (Renderer). Content does not block on,
or track, GPU presence.

## Design Advantages

1. **Builds on Existing Work**: Extends current LoaderContext system without
   breaking changes
2. **Safety**: Impossible to unload something still in use (new capability)
3. **Automatic**: Cascading load/unload reduces manual management (new
   capability)
4. **Performance**: Shared assets cached, duplicate loading eliminated (new
   capability)
5. **Debugging**: Clear dependency graphs for asset analysis tools

## Design Limitations

1. **Manual Registration**: Loaders must explicitly register dependencies
   (already implemented, works well)
2. **Cycle Detection Implemented**: Cycles rejected early (diagnostics can be
  improved with better error context)
3. **Synchronous Only (Phase 1)**: Async CPU pipeline coming in Phase 2 (see
  below)

## Future: Async CPU Pipeline (Phase 2 Preview)

Planned adjustments (see implementation plan Phase 2):

- `task<AssetHandle>`-returning `LoadAsync` ending at DecodedCPUReady
- In-flight deduplication map for concurrent loads
- Cancellation tokens passed through loader context
- Non-blocking interface; initial file IO may remain blocking internally until
  async backend swapped in
- Bridge descriptor (`GpuMaterializationInfo`) populated but not consumed here

Out of scope: GPU upload queues, residency / LRU, GPU memory budgets. design)
