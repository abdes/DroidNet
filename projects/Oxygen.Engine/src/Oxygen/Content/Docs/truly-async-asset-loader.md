
# Async AssetLoader (architecture)

This document describes the shipped architecture for Oxygen’s async-first Content loader.

Scope:

- Cooked-only asset/resource loading from mounted containers (`.pak` and loose cooked roots).
- Buffer-provided cooked payload decoding (tests/demos/tooling).
- CPU-side decode + caching + dependency tracking.

Out of scope:

- Importing authoring formats (PNG/GLTF/FBX/etc.).
- GPU uploads/residency. The Content subsystem ends at CPU-decoded objects.
- Hot reload and priority/urgency scheduling.

---

## Key Terms

- **Owning thread**: the engine thread that owns an `AssetLoader` instance.
- **Asset**: identified by `oxygen::data::AssetKey`.
- **Resource**: identified by `oxygen::content::ResourceKey`.
- **Content source**: implementation of `oxygen::content::internal::IContentSource` (`PakFileSource`,
  `LooseCookedSource`).

Internal decode/publish bridge types:

- `oxygen::content::LoaderContext` (value-type decode context).
- `oxygen::content::internal::DependencyCollector` (identity-only dependency handoff).
- `oxygen::content::internal::SourceToken` (opaque handle for a mounted source).
- `oxygen::content::internal::ResourceRef` (container-relative resource reference bound to `ResourceKey` during
  publish).

---

## Design Invariants

### Threading

- **Owning-thread only**:
  - Mutating loader state (mount registry, caches, dependency graph).
  - Invoking `StartLoad*` callbacks.
  - Binding `ResourceRef -> ResourceKey`.

- **Worker thread** (thread pool):
  - Blocking I/O required by loads.
  - CPU decode of descriptors and resource payloads.

Rationale: correctness (single-writer state) and a hard guarantee that decode cannot re-enter the loader from worker
threads.

### “Decode is pure”

Loader functions are pure decode: they must not call back into `AssetLoader`, perform nested loads, or mutate the
dependency graph. Worker-thread decode can only record dependencies via `DependencyCollector`.

Rationale: keeps worker-thread work side-effect free and makes cancellation and in-flight dedup safe.

### Identity is stable; access is ephemeral

- `AssetKey` and `ResourceKey` are stable identities.
- Locators/readers/paths are transient access state and must not be stored in caches or dependency graphs.

Rationale: prevents accidental lifetime/threading bugs and keeps caches portable.

---

## Identity Model

### `ResourceKey` source-id segregation

`ResourceKey` embeds a 16-bit source id. The runtime uses a codified segregation contract to keep namespaces disjoint:

- Mounted PAK sources use dense ids starting at `0`.
- Mounted loose cooked roots use a reserved high range starting at `0x8000`.
- Synthetic/buffer-provided resources use the reserved sentinel `0xFFFF`.

These constants live in `Oxygen/Content/Constants.h` and must remain consistent with `ResourceKey` packing.

Rationale: synthetic keys cannot collide with mounted sources, and loose cooked ids remain disjoint from PAK ids without
requiring filesystem-derived identity.

### `SourceToken` and `ResourceRef`

Decode code uses `SourceToken` (minted at mount time) as an identity-safe handle for “the source being decoded from”.
Resource dependencies are recorded as either:

- `ResourceKey` (already bound identity), or
- `internal::ResourceRef { SourceToken, TypeId, resource_index }`.

Binding rule (owning thread): resolve `SourceToken` to the loader-owned source id, map `TypeId` to a `ResourceTypeList`
index, then pack into `ResourceKey`.

Rationale: decode stays free of loader internals while publish remains the only place aware of key encoding.

---

## Components

### `AssetLoader` (orchestrator)

Responsibilities:

- Own the mount registry and source-id/token maps.
- Orchestrate async load pipelines (resolve/read/decode/publish).
- Maintain caches and the dependency graph.
- Provide in-flight request deduplication keyed by `AssetKey` / `ResourceKey`.

### `internal::IContentSource`

Responsibilities:

- Resolve identity to source-local locators.
- Provide readers for descriptor and resource data regions.

The loader does not expose storage form (PAK vs loose cooked) in load APIs.

Rationale: keeps the public API identity-based and isolates storage concerns to mount time.

### Loader functions and `LoaderContext`

Loader functions decode cooked bytes into typed CPU objects. `LoaderContext` provides:

- `desc_reader` and `data_readers` (decode inputs).
- `work_offline` (policy signal: no GPU side effects).
- `current_asset_key` (for identity-only dependency recording).
- `source_token` and optional `dependency_collector`.
- `parse_only` for tooling/unit tests.

Rationale: explicit and minimal decode contract that works in both runtime and tooling contexts.

---

## Lifecycle

### Construction

The loader is constructed with `AssetLoaderConfig`, including:

- Thread pool for offloading blocking I/O and CPU decode.
- Offline policy (no GPU side effects).

### Activation

Async loads require the loader to be activated (nursery opened). Load attempts before activation fail fast.

### Mount management

Mount operations (`AddPakFile`, `AddLooseCookedRoot`, `ClearMounts`) are owning-thread only. Mounting may perform
blocking validation work (e.g. loose cooked index validation).

Rationale: mount changes mutate global loader state and must be serialized.

### Stop/cancellation

`Stop()` cancels in-flight work. Awaitables complete exceptionally (e.g. `OperationCancelledException`). Callback-based
`StartLoad*` APIs report failure by invoking callbacks with `nullptr`.

---

## Load Pipelines

### Asset load (`LoadAssetAsync<T>`)

1. **Resolve** (owning thread): find the source + locator for the `AssetKey`.
2. **Read** (thread pool): read descriptor/table/payload bytes as needed.
3. **Decode** (thread pool): run the registered loader function; record dependencies via `DependencyCollector`.
4. **Publish** (owning thread):
   - Store the decoded object in the cache.
   - Bind `ResourceRef` dependencies to `ResourceKey`.
   - Mutate the dependency graph.
   - Fulfill awaiters / invoke `StartLoad*` callbacks.

### Resource load (`LoadResourceAsync<T>(ResourceKey)`)

Same structure as asset loads, scoped to a single resource identity.

### Cooked-bytes resource load (`LoadResourceAsync(CookedResourceData<T>)`)

The caller provides cooked bytes and an explicit cache identity. The loader:

- Decodes on the thread pool.
- Publishes to the cache under the provided key.

Synthetic keys for this path must use the synthetic source id (`0xFFFF`).

---

## Dependency Model

The runtime dependency graph is identity-only:

- Asset → Asset: `AssetKey` depends on `AssetKey`.
- Asset → Resource: `AssetKey` depends on `ResourceKey`.

Resources are treated as leaf nodes.

Worker-thread decode never mutates this graph directly; it only records identity dependencies into
`DependencyCollector`, which is applied during publish.

Rationale: ensures dependency graph consistency and avoids worker-thread re-entrancy into `AssetLoader`.

---

## Public API Contract (summary)

This document does not restate the full header API; it specifies behavioral contracts the API upholds.

- Async-first entrypoints are coroutine-based (`LoadAssetAsync<T>`, `LoadResourceAsync<T>(ResourceKey)`, and
  cooked-bytes overloads taking `CookedResourceData<T>`).
- Callback bridges (`StartLoadAsset<T>`, `StartLoadResource<T>`, and typed conveniences like `StartLoadTexture`) are
  strictly adapters over the coroutine APIs:
  - MUST be called from the owning thread.
  - MUST invoke callbacks on the owning thread.
  - Report failure by invoking callbacks with `nullptr`.
  - Require the loader to be activated.

Decode-time dependency recording uses `DependencyCollector`:

- In normal runtime loads, loaders record dependencies into the collector.
- For tooling and parsing-only tests, `parse_only=true` disables dependency recording.

---

## Caching and Concurrency

- Assets and resources are cached by identity (`AssetKey` / `ResourceKey`).
- In-flight request deduplication ensures concurrent loads of the same identity join a single underlying task.
- Cache publication and dependency-graph mutation occur during the owning-thread publish step, providing a single point
  of serialization for state.

---

## References

- `Oxygen/Content/Constants.h` (source-id segregation constants)
- `Oxygen/Content/LoaderContext.h` (decode contract)
- `Oxygen/Content/Internal/DependencyCollector.h` and `Oxygen/Content/Internal/ResourceRef.h` (decode/publish dependency
  handoff)
