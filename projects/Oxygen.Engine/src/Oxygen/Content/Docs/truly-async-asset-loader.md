
# Truly async AssetLoader (design)

This document specifies a **truly asynchronous** `AssetLoader` for Oxygen
Content.

Normative terms: **MUST**, **MUST NOT**, **SHOULD**, **MAY**.

This is a design document only. No code changes are introduced here.

---

## Goals

- The runtime-facing loader is **async-first**: loading MUST NOT block the
 engine/owning thread.
- A single loader instance supports **mounted cooked containers**:
  - `.pak` containers (shipping)
  - loose cooked roots (editor iteration)
- The loader also supports **buffer-provided cooked payloads** for
 tests/demos/tooling, without conflating this with loose cooked.
- The public API is **consistent** and non-redundant:
  - No generic “Load” verb (only **Asset** or **Resource**)
  - `StartLoad*` exists wherever an awaitable async load exists
  - `StartLoad*` callback is guaranteed to run on the loader’s owning thread
- **Single source of truth** for runtime mode/policy:
  - “Offline” behavior MUST be configured on `AssetLoader` (via
  `AssetLoaderConfig`), not per-call.

## Non-goals

- Runtime import of authoring formats (PNG/GLTF/FBX/etc.). Runtime Content
 remains **cooked-only**.
- GPU uploads/residency. Content ends at **DecodedCPUReady**.
- Hot reload (separate design document).

---

## Current state (why this redesign)

Observed inconsistencies in the current surface:

- Assets are synchronous (`LoadAsset<T>`), resources are mixed
 (`LoadResourceByKey<T>` + `LoadResourceAsync<T>`), and textures have
 extra convenience (`LoadTextureAsync`, `StartLoadTexture*`).
- Callback-based “StartLoad” exists only for textures.
- Some APIs accept an `offline` parameter even though offline behavior is a
 global policy decision.

Additionally, the current dependency model requires owning-thread operations
(`AddAssetDependency`, `AddResourceDependency`), which constrains how far work
can be pushed off-thread without refactoring.

---

## Existing architecture to preserve

### Content sources (PAK and loose cooked)

The code already has an internal seam: `internal::IContentSource`.

- `PakFileSource`: resolves assets and reads descriptor/resources from a single
 `.pak` file.
- `LooseCookedSource`: resolves assets through a validated
 `container.index.bin`, and reads descriptors/resources from a cooked
 directory.

This is the correct abstraction boundary: **loaders must not care** whether
bytes came from a PAK section reader or a filesystem reader.

### Identity / locator model

This design explicitly separates **identity** from **access**.

Identity (stable keys):

- `data::AssetKey` is the runtime identity for assets.
- `ResourceKey` is the runtime identity for resources.
  - It encodes a 16-bit source id, but that id is an **opaque token** owned by
    `AssetLoader`.

Access (how bytes are obtained):

- Loaders do not consume keys directly. They consume **resolved access
  handles** produced by the loader’s resolver.
- The resolver maps identities to an `internal::IContentSource` instance plus
  a source-specific locator (e.g. PAK directory entry or loose cooked
  descriptor path).

@note The design MUST NOT rely on hardcoded numeric ranges of source ids.
`AssetLoader` already maintains an internal registry mapping `source_id ->
source instance`; resolution MUST use that mapping.

---

## Design principles

### 1) Async by design

- Public load APIs MUST be awaitable coroutines (or callback wrappers over
 them).
- Blocking I/O and CPU-heavy decode MUST be executed off the owning thread
 (typically via the configured thread pool).

### 2) Owning-thread invariants

The loader has a single **owning thread** (engine thread). The following MUST
only occur on the owning thread:

- Publishing loaded objects into the cache (or at least publishing-visible
 state transitions).
- Dependency graph mutation / reference-count manipulation.
- `StartLoad*` callback invocation (guaranteed by engine).

### 3) Single source of truth for runtime policy

Offline mode is **not** a per-call knob.

- The loader is constructed with a runtime policy (online/offline).
- LoaderContext MUST derive policy from the loader’s stored config.

If the engine needs to change offline/online behavior, it MUST create a
separate loader instance (or explicitly reinitialize the loader).

### 4) Locator-based public API

The public API talks in terms of **identities** (keys) and a small number of
explicit **inputs**, not storage forms.

- Mounted cooked content is addressed by identity (`AssetKey`, `ResourceKey`).
- Buffer-provided cooked payloads are addressed by identity plus explicit
  bytes input (see “Resource cooked data”).

The storage form (PAK vs loose cooked) is not part of the public load methods.
It is a mount-time concern.

---

## Storage forms: what we support and why

### Mounted sources (runtime)

The runtime MUST support both PAK and loose cooked sources concurrently.

Rationale:

- PAK is required for shipping.
- Loose cooked is required for editor iteration.
- Both already share the `IContentSource` seam.

The API MUST NOT expose “PAK vs loose cooked” in its load methods. The storage
form is a mount-time choice.

### Buffer-provided cooked payloads (tests/demos/tooling)

Buffer-provided loads SHOULD exist, but SHOULD be treated as *ad hoc inputs*,
not as a third persistent “content source” category.

Rationale:

- Demos/tests frequently create cooked payloads in memory.
- Editor tools may decode a cooked blob without registering it as mounted
 content.

The buffer load path MUST integrate with caching and identity via `ResourceKey`
(typically a synthetic key minted by the loader).

Critically: buffer-provided bytes are an **input**, not a “content source” the
engine can enumerate/browse. Buffer inputs do not replace PAK or loose cooked.

---

## Access abstraction (SOLID, preserves existing seams)

This section defines the abstraction that answers: “given an identity, how do
we obtain bytes (now, and later for streaming)?”

Constraints:

- MUST keep `internal::IContentSource` relevant.
- MUST keep `serio::AnyReader` relevant for decode.
- MUST keep `LooseCookedIndex` relevant (asset discovery + descriptor
  metadata + file layout validation).
- MUST enable future async I/O and streaming.

### Internal concepts

- **Resolved asset access**
  - Produced by resolving an `AssetKey`.
  - Contains:
    - `internal::IContentSource* source`
    - `internal::AssetLocator locator` (existing `std::variant`)

- **Resolved resource access**
  - Produced by resolving a `ResourceKey`.
  - Contains:
    - `internal::IContentSource* source`
    - `ResourceKey key`
    - Resource-type-specific table and data region references (offset/size)

These resolved access objects are the “locator” in the strict sense: they are
what you need to open readers or schedule reads. Keys remain pure identity.

### I/O surface owned by a content source

This design MUST NOT introduce a new async I/O library.

Async behavior in Content comes from **coroutines + thread-pool offload** of
existing synchronous reads.

`internal::IContentSource` remains a small abstraction over *where cooked bytes
come from* (PAK sections, loose cooked files). It SHOULD stay narrow and
decode-oriented.

At minimum, a content source MUST be able to provide:

- **Descriptor reader** for an already-resolved asset locator.
- **Table reader** and **data region reader** for each resource type.

For truly async and streaming, sources MAY additionally expose *minimal*
source-local helpers for reading **byte ranges** into owned buffers.

Requirements for these helpers:

- They are **not** a general-purpose I/O framework.
- They do not introduce a new event loop, scheduler, or reactor.
- They are invoked by `AssetLoader` on its configured thread pool.
- They operate on already-known offsets/sizes (e.g. after table lookup).

Conceptually, this enables chunked/partial reads without changing the decode
interface.

@note This does not make `AnyReader` obsolete: decoding can keep using
`AnyReader` by wrapping owned bytes in a memory-backed reader (as already done
in the current buffer decode path).

### Where loose cooked fits (no confusion)

Loose cooked access remains exactly what it is today:

- `LooseCookedIndex` is used at mount time to:
  - validate presence/size/SHA of descriptor files and table/data files
  - map `AssetKey -> descriptor relative path`
  - locate resource table/data files

At load time:

- Resolving an `AssetKey` yields a `LooseCookedAssetLocator` containing the
  descriptor file path.
- Resource table/data reads are satisfied by opening the validated files.

In other words: the index remains the “directory” for a loose cooked container;
`AnyReader` remains the decode interface; async I/O is an implementation detail
of the source.

---

## API specification (technical English)

This section defines the **public** API surface as behavioral specs (not as a
monolithic class declaration).

### Types

- **Asset identity**: `data::AssetKey`
  - Uniquely identifies an asset across all mounted sources.

- **Resource identity**: `ResourceKey`
  - Uniquely identifies a resource within a specific mounted source.
  - The embedded source id is opaque; it is interpreted only via the loader’s
    source registry.

- **Resource cooked data**: (new) `ResourceCookedData<T>`
  - Contains:
    - a `ResourceKey` identity under which the decoded result will be cached
      (typically minted by the loader)
    - a byte span containing the cooked bytes required to decode a `T`
  - This is explicitly not “where the resource lives”; it is “bytes provided
    by the caller.”

### Lifecycle and configuration

- The loader is constructed with `AssetLoaderConfig`.
- `AssetLoaderConfig` MUST include:
  - A thread pool used for offloading blocking I/O and CPU decode.
  - A runtime policy specifying whether the loader operates in offline mode.
  - A GPU-side-effects policy used to enforce “offline means no GPU work”.

- The loader MUST be activated (nursery opened) before async loads.
- If a caller attempts to load before activation, the loader MUST fail fast
 (preferred) rather than silently falling back to synchronous work.

### Mount management

- The loader MUST support mounting cooked containers:
  - Add a `.pak` file as a mounted source.
  - Add a loose cooked root directory as a mounted source.
  - Clear all mounts.

- Mount operations SHOULD be owning-thread only.

### Asset loading

- **Awaitable asset load**
  - Input: `data::AssetKey` and the asset type `T`.
  - Output: an awaitable producing `std::shared_ptr<T>`.
  - Behavior:
    - If cached, returns the cached instance.
    - If not cached, schedules necessary I/O and decode work asynchronously.
    - Publishes the loaded asset into the cache before completion.
    - Registers dependency edges discovered during decode.
    - Deduplicates concurrent in-flight requests for the same `AssetKey`.
    - Never blocks the owning thread.

- **Callback-based asset load** (`StartLoadAsset<T>`)
  - Equivalent to starting the awaitable asset load in the loader nursery.
  - MUST invoke the callback on the owning thread.
  - MUST provide cancellation safety: if the loader is stopped, callbacks are
  either not invoked or invoked with a null result (policy must be defined
  explicitly; see “Cancellation”).

### Resource loading (mounted)

- **Awaitable resource load by key**
  - Input: `ResourceKey` and resource type `T`.
  - Output: an awaitable producing `std::shared_ptr<T>`.
  - Behavior:
    - If cached, returns the cached instance.
    - If not cached, reads table + payload and decodes on the thread pool.
    - Publishes the decoded CPU-side resource into the cache.
    - Never blocks the owning thread.
    - Deduplicates concurrent in-flight requests for the same key.

- **Callback-based resource load by key** (`StartLoadResource<T>`)
  - Starts the awaitable resource load in the loader nursery.
  - MUST invoke the callback on the owning thread.

### Resource loading (buffer-provided)

- **Awaitable resource decode from buffer**
  - Input: `ResourceCookedData<T>`.
  - Output: an awaitable producing `std::shared_ptr<T>`.
  - Behavior:
    - Decodes the cooked payload from provided bytes (thread pool).
    - Stores the result in the cache under the input’s `ResourceKey`.
    - The key SHOULD be minted by the loader to avoid collisions.

- **Callback-based resource decode from buffer** (`StartLoadResourceFromBuffer<T>`)
  - Starts the awaitable buffer decode in the loader nursery.
  - MUST invoke the callback on the owning thread.

### Cache and release

Query APIs MAY exist for tools/diagnostics, but MUST be non-blocking:

- “Get cached asset/resource if present” (non-loading).
- “Has cached asset/resource” (non-loading).

- Release APIs MUST remain explicit and owning-thread:
  - Releasing an asset triggers release of its dependency tree.
  - Releasing a resource decrements its usage count.

### Cancellation and Stop()

- `Stop()` cancels in-flight work.
- Awaitables MUST complete promptly after cancellation by completing
  exceptionally with `OperationCancelledException`.

---

## Truly async implementation strategy (high level)

This section describes the refactor needed to make assets truly async while
preserving dependency correctness.

### Problem: current loaders register dependencies directly

Existing loader functions call back into `AssetLoader` (dependency registration
and sometimes nested loads). Those operations are owning-thread only, but we
want decode to happen off-thread.

### Required change: split decode from publish

Introduce an internal pipeline split:

1. **Resolve** (owning thread)

- Determine which mounted source contains the locator.
- Produce a source-specific internal locator.

1. **Read** (thread pool)

- Read descriptor bytes and any required table/payload bytes.
- Outputs immutable byte buffers.

1. **Decode** (thread pool)

- Parse bytes into a typed object **without touching AssetLoader**.
- Collect dependency edges into a local collection.

1. **Publish** (owning thread)

- Store the object in the cache.
- Apply the dependency edges via `AddAssetDependency` / `AddResourceDependency`.
- Fulfill all awaiting callers and invoke StartLoad callbacks.

This implies a new internal contract for loader functions:

- Loader functions MUST be pure decode:
  - Input: readers/bytes + an output dependency collector.
  - Output: decoded CPU-side object plus discovered dependencies.
  - MUST NOT perform nested loads.

Nested loads become the responsibility of the orchestrator coroutine, which can
schedule resource loads in parallel and then publish dependencies in one place.

---

## Dependency collection (second-level design)

This section specifies how dependencies are represented during async decode,
without mixing identity with access and without requiring loader callbacks from
worker threads.

### Constraints

- Dependency registration MUST happen on the owning thread.
- Worker-thread decode MUST NOT call back into `AssetLoader`.
- The dependency graph stored by the runtime cache MUST use stable identities
  (`AssetKey` / `ResourceKey`).
- The design MUST work for both PAK and loose cooked.

### What dependencies exist

Oxygen Content uses forward-only edges:

- Asset → Asset: dependent `AssetKey` holds another `AssetKey` alive.
- Asset → Resource: dependent `AssetKey` holds a `ResourceKey` alive.

Resources are leaf nodes (no resource → * edges).

### Two candidate representations

#### Option A: Collect raw identity keys

Decode produces an object plus a list of dependencies expressed only as keys:

- Asset dependencies: `data::AssetKey`
- Resource dependencies: `ResourceKey`

How decode constructs `ResourceKey` without calling the loader:

- The orchestrator provides the decode step with an opaque **source token**
  representing “the container being decoded from”.
- The decode step uses a small, pure “resource key builder” value to construct
  `ResourceKey` from:
  - source token
  - resource type
  - resource index

`SourceToken` is defined as an internal strong type (opaque handle) backed by
`oxygen::NamedType`.

- Representation: `oxygen::content::internal::SourceToken`
  - Underlying type: `uint32_t`
  - Skills: `Comparable`, `Hashable`, `Printable`
  - `to_string(SourceToken)` MUST exist for logging via `nostd::to_string`.
- Minting: The loader mints a fresh token at mount time.
- Resolution: The orchestrator/publish step uses the loader’s source registry
  to map `SourceToken -> loader-owned source id`.
- Opacity: The token MUST NOT encode or reveal storage form.

Pros:

- Simple: dependencies stored are exactly what the cache/deps system already
  expects.
- Stable: keys are portable across threads and do not capture access objects.
- Easy to deduplicate: in-flight maps naturally key on identities.

Cons:

- Requires a well-defined, stable `ResourceKey` encoding contract.
- Decode must know resource type indices (or call a helper) to build keys.
- If key encoding changes, decode code and tooling must be updated.

#### Option B: Collect typed locators with late binding

Decode produces an object plus dependencies in a “not-yet-identity” form, e.g.:

- Asset dependencies: `AssetKey` (already identity)
- Resource dependencies: `ResourceRef` such as:
  - “resource of type X at index i in the current container”
  - optionally “resource in container token S”

Then, on the owning thread during Publish:

- Bind each `ResourceRef` into a `ResourceKey` using the resolver and the
  loader’s registry.

Pros:

- Decode does not need to understand key encoding.
- Late binding can validate that referenced indices exist before emitting keys.
- More flexible if future containers change how resources are addressed.

Cons:

- More complex types and more edge cases.
- Until binding, dependencies are not cache identities, which complicates
  in-flight dedup and dependency storage.
- Easy to accidentally leak access concerns into long-lived objects if locator
  structs capture source pointers/paths.

### Decision: hybrid with identity at the boundary (approved)

The dependency graph stored in `AssetLoader` MUST be identity-only
(`AssetKey`, `ResourceKey`).

During worker-thread decode, resource dependencies MAY be represented as either:

- already-built `ResourceKey` (preferred when feasible), or
- `ResourceRef` (container-relative reference) which is bound to `ResourceKey`
  on the owning thread during Publish.

Rationale:

- Keeps cache/deps stable and simple.
- Avoids carrying file paths/readers/locators beyond the load operation.
- Allows decode code to remain agnostic of key encoding when desired.

### `ResourceRef` (concrete shape)

`ResourceRef` is an **internal** type used only as a short-lived dependency
representation between:

- the orchestrator coroutine (which knows the current source context), and
- worker-thread decode (which wants to record “container-relative” resource
  references), and
- owning-thread Publish (which binds references into `ResourceKey`).

It MUST NOT be part of the runtime-facing/public `AssetLoader` API surface.

If loader implementations live outside the `AssetLoader` translation unit,
`ResourceRef` MAY be exposed via an `internal::*` header/namespace, but it MUST
remain semantically internal (not stable ABI, not intended for general engine
code).

`ResourceRef` MUST be identity-safe:

- MUST NOT contain `IContentSource*`, filesystem paths, readers, or other access
  objects.
- MUST be trivially copyable between threads.

`ResourceRef` MUST contain:

- `SourceToken source`
  - An opaque token provided by the orchestrator representing “the container
    currently being decoded from”.
  - It MUST be sufficient to derive the `ResourceKey` source id via the loader
    registry.
- `oxygen::TypeId resource_type_id`
  - The runtime type id of the resource type (`BufferResource`,
    `TextureResource`, ...).
- `data::pak::ResourceIndexT resource_index`

Binding rule (owning thread, during Publish):

1. Resolve the `SourceToken` to the loader-owned source id.
2. Map `resource_type_id` to a resource type index in `ResourceTypeList`.
3. Construct `ResourceKey` using (source id, resource type index,
   resource_index).

@note The binding step is the only place that needs to understand the
`ResourceKey` encoding. Decode code only needs `TypeId` and an index.

### Async-only public API implication

Public APIs MUST be async-only. There MUST NOT be synchronous load APIs.

- Awaitable APIs cover coroutine callers.
- `StartLoad*` covers non-coroutine callers.

Rationale: blocking the frame is never acceptable; urgency/priority can be
introduced later without changing the API shape.

### In-flight deduplication

The loader SHOULD maintain an internal map of “in-flight” tasks keyed by
`AssetKey` and `ResourceKey` so concurrent requests join the same task.

### Loose cooked specifics

Loose cooked already enforces runtime correctness via:

- mandatory `container.index.bin`
- file existence/size/SHA validation
- table/data pairing validation

The async design keeps those validations at mount time.
At load time, loose cooked becomes just another byte supplier.

---

## Deferred topics (future)

- Request urgency/priority semantics.
  - The API is async-only; priority MAY be introduced later.
  - Priority is intentionally unspecified in this design document.
