# Eviction and GPU Residency (minimal spec)

This document specifies the **minimum** required behavior to keep renderer-side GPU residency consistent with Content
cache lifetime.

Normative terms: **MUST**, **MUST NOT**, **SHOULD**, **MAY**.

---

## Scope

This spec covers:

- **Content-side eviction** of decoded CPU objects cached by `AssetLoader`.
- **Renderer-side GPU residency** owned by long-lived systems.
- The **contract** that links the two.

This spec intentionally does **not** mandate a specific renderer architecture (`BufferBinder`, etc.). The goal is
correctness with minimal churn.

---

## Definitions

- **Resource identity**: `content::ResourceKey`.
- **CPU resource**: decoded `data::BufferResource`, `data::TextureResource`, etc.
- **GPU residency**: renderer-owned GPU objects associated with a `ResourceKey`, e.g. `graphics::Buffer`,
  `graphics::Texture`, descriptor heap entries, and any internal tracking state.
- **Eviction**: the moment Content decides a cached CPU object for a `ResourceKey` is no longer kept alive by the cache
  and is eligible to be destroyed/reclaimed.

---

## Requirements

### R1: Content publishes eviction events

Content MUST provide an **eviction notification** that is keyed by `content::ResourceKey`.

- The notification MUST be emitted exactly once per actual eviction of a resource instance.
- The notification MUST be emitted on the **Content owning thread**.
- The notification MUST be emitted after the cache stops keeping the CPU resource alive (i.e. eviction is real), but
  before any dependent systems rely on the resource being present.

Notes:

- This spec does not require a particular cache implementation, only the observable behavior.

### R2: Renderer GPU owners subscribe

Each renderer subsystem that holds long-lived GPU residency keyed by `content::ResourceKey` MUST subscribe to eviction
notifications for the resource types it mirrors on the GPU.

At minimum, the following owners are in scope:

- `TextureBinder` (GPU textures + descriptors) for `data::TextureResource`.
- `GeometryUploader` (GPU buffers + descriptors/handles) for `data::BufferResource`.

### R3: Eviction triggers GPU residency teardown

On receiving an eviction notification for key `K`, the owning renderer subsystem MUST ensure that all GPU residency
associated with `K` is eventually released.

- The teardown MUST occur on the renderer/graphics owning thread.
- The teardown MAY be deferred (e.g. end-of-frame) if required by GPU safety.
- The teardown MUST NOT leak descriptor heap entries or internal bookkeeping.

### R4: Stable handle behavior

Some renderer APIs return stable shader-visible handles/indices.

If a stable handle exists for `K` at eviction time, the renderer subsystem MUST choose one of the following behaviors
(explicitly documented by the subsystem):

- **Repoint**: keep the handle stable but repoint the underlying descriptor to a placeholder/error resource.
- **Invalidate**: make the handle invalid for future use.

Whichever behavior is chosen:

- It MUST be deterministic.
- It MUST NOT crash when used after eviction.

### R5: No dependency on CPU pointer lifetime

Renderer GPU residency MUST be keyed by `ResourceKey` and MUST NOT depend on the lifetime of the decoded CPU object
pointer.

Rationale: eviction breaks CPU pointer stability by design.

---

## Threading and ordering

### T1: Thread ownership

- Content eviction notifications originate on the Content owning thread.
- GPU object destruction/release MUST execute on the renderer/graphics owning thread.

### T2: Cross-thread delivery

If eviction notifications are delivered across threads, the delivery mechanism MUST preserve:

- **At-most-once** delivery per eviction event.
- **In-order delivery** per `ResourceKey` (relative ordering for different keys is unspecified).

---

## Observability / diagnostics (minimal)

Systems SHOULD log (debug-level) when:

- A resource is evicted (`ResourceKey`, type id).
- A GPU residency teardown is scheduled and when it completes.

This MUST NOT spam logs on hot paths in release builds.

---

## Minimum test requirements

The project SHOULD include focused tests that validate the contract without integration-heavy setups.

### Texture residency

- Given a `TextureBinder` entry for key `K`, when Content emits eviction for `K`, the binder eventually releases GPU
 resources for `K` and does not leak descriptor allocations.

### Buffer residency

- Given a `GeometryUploader` (or the owning system for GPU buffers) entry for key `K`, when Content emits eviction for
 `K`, the uploader eventually releases GPU resources for `K`.

---

## Non-goals

- Defining a full renderer residency framework.
- Introducing new bindless handle types or changing shader interfaces.
- Replacing existing ownership systems unless required for correctness.
