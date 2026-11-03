# Eviction and GPU Residency

This document specifies the behavior that keeps renderer-side GPU residency
consistent with Content cache lifetime, and the Content-side budget and trim
API that drives eviction.

Normative terms: **MUST**, **MUST NOT**, **SHOULD**, **MAY**.

- [Scope](#scope)
- [Definitions](#definitions)
- [Requirements](#requirements)
- [Threading and ordering](#threading-and-ordering)
- [Cache budget and trim](#cache-budget-and-trim)
- [Eviction notifications](#eviction-notifications)
- [Observability](#observability)
- [Minimum test requirements](#minimum-test-requirements)

---

## Scope

This spec covers:

- **Content-side eviction** of decoded CPU objects cached by `AssetLoader`.
- **Cache budget and trim** for those objects.
- **Renderer-side GPU residency** owned by long-lived systems.
- The **contract** that links the two, including cross-thread delivery and
  in-flight load handling.

This spec intentionally does **not** mandate a specific renderer architecture.
The goal is correctness with minimal churn.

---

## Definitions

- **Resource identity**: `content::ResourceKey`.
- **CPU resource**: decoded `data::BufferResource`, `data::TextureResource`, etc.
- **GPU residency**: renderer-owned GPU objects associated with a `ResourceKey`,
  e.g. `graphics::Buffer`, `graphics::Texture`, descriptor heap entries, and any
  internal tracking state.
- **Eviction**: the moment Content decides a cached CPU object for a
  `ResourceKey` is no longer kept alive by the cache and is eligible to be
  destroyed/reclaimed.
- **Eviction reason**: why the cache retired the entry
  (`content::EvictionReason`).
- **In-flight**: an asset/resource load that has been requested and has not
  yet completed on the Content thread.

---

## Requirements

### R1: Content publishes eviction events

Content MUST provide an **eviction notification** that is keyed by
`content::ResourceKey` and includes the resource type.

- The notification MUST be emitted exactly once per actual eviction of a
  resource instance.
- The notification MUST be emitted on the **Content owning thread**.
- The notification MUST be emitted after the cache stops keeping the CPU
  resource alive (i.e. eviction is real), but before any dependent systems
  rely on the resource being present.
- The notification MUST include an **eviction reason**.
- The notification payload MUST carry the original `content::ResourceKey`
  (not a hash) and the resource `TypeId`.

Notes:

- Cache key hashes are not sufficient for downstream systems; they are not
  reversible and cannot reliably identify a resource instance.
- `AnyCache` remains key-agnostic; `AssetLoader` maps cache keys to
  `ResourceKey` and emits eviction notifications.
- Releasing the last usage of an entry does not evict it: the cache keeps one
  baseline residency reference until a trim, `ClearMounts` or shutdown removes
  the entry (see [Cache budget and trim](#cache-budget-and-trim)).
- A trim only evicts entries nothing uses: active callers, bound children
  and pins retain their entries. `ClearMounts`, remounting and shutdown retire every
  entry.

### R2: Renderer GPU owners subscribe

Each renderer subsystem that holds long-lived GPU residency keyed by
`content::ResourceKey` or an asset key MUST subscribe to eviction
notifications for the types it mirrors on the GPU.

Current subscribers (`Vortex/Resources`):

- `TextureBinder`: `data::TextureResource`.
- `GeometryUploader`: `data::GeometryAsset`.
- `MaterialBinder`: `data::MaterialAsset`.

Each callback only queues the event; the subscriber processes the queue on its
frame:

- `TextureBinder`, a bounded number per frame, waits while a resident lease
  holds the texture, then retires
  its GPU textures and the entry's handle, so late upload callbacks are
  dropped, and binds the placeholder at the same bindless index: the index is
  stable and the texture reloads on its next use.
- `GeometryUploader`, a bounded number of LODs per frame, releases the
  geometry's buffers and its slot and bumps the handle generation: the old
  handle no longer resolves.
- `MaterialBinder` holds its queue until a frame slot is set, then releases
  the evicted material's atlas entries and handle slot: the old handle no
  longer resolves.

### R3: Eviction triggers GPU residency teardown

On receiving an eviction notification for key `K`, the owning renderer subsystem MUST ensure that all GPU residency
associated with `K` is eventually released.

- The teardown MUST occur on the renderer/graphics owning thread.
- The teardown MAY be deferred (e.g. end-of-frame) if required by GPU safety.
- The teardown MUST NOT leak descriptor heap entries or internal bookkeeping.
- If the resource is currently in-flight for upload, the GPU owner MUST ensure
  that completion does not resurrect the entry after eviction.

### R4: Stable handle behavior

Some renderer APIs return stable shader-visible handles/indices.

If a stable handle exists for `K` at eviction time, the renderer subsystem MUST choose one of the following behaviors
(explicitly documented by the subsystem):

- **Repoint**: keep the handle stable but repoint the underlying descriptor to a placeholder/error resource.
- **Invalidate**: make the handle invalid for future use.

Whichever behavior is chosen:

- It MUST be deterministic.
- It MUST NOT crash when used after eviction.
- The chosen policy MUST be documented per subsystem.

### R5: No dependency on CPU pointer lifetime

Renderer GPU residency MUST be keyed by `ResourceKey` and MUST NOT depend on
the lifetime of the decoded CPU object pointer.

Rationale: eviction breaks CPU pointer stability by design.

### R6: Cache budget

The content cache has one explicit budget, in bytes, for all cached entries
(see [Cache budget and trim](#cache-budget-and-trim)).

- Refcount reaching zero is a **necessary** condition for eviction, not a
  sufficient one.
- Trim is the only step that evicts for budget reasons; it runs when the
  caller asks for it or, under `kAutoOnOverBudget`, when the cache is over
  budget.

### R7: In-flight load and eviction coordination

If eviction occurs while a resource is in-flight:

- The loader MUST avoid resurrecting the resource on completion.
- The renderer MUST ignore late completions for evicted entries.
- Cancellation is preferred when supported by the load pipeline, but must
  remain safe if cancellation is best-effort.

### R8: Asset eviction as a transitive trigger

When an asset is evicted, Content MUST treat its dependent resources as
eligible for eviction via normal reference counting. The asset eviction itself
is not a direct signal to renderer systems unless they also maintain GPU
residency keyed by that asset identity.

---

## Threading and ordering

### T1: Thread ownership

- Content eviction notifications originate on the Content owning thread.
- GPU object destruction/release MUST execute on the renderer/graphics owning thread.

### T2: Cross-thread delivery

If eviction notifications are delivered across threads, the delivery mechanism MUST preserve:

- **At-most-once** delivery per eviction event.
- **In-order delivery** per `ResourceKey` (relative ordering for different keys
  is unspecified).
- **No resurrection**: a later load completion MUST NOT be delivered as a
  visible GPU-resident state after an eviction notification for the same key.

### T3: Renderer-side scheduling

Renderer subsystems SHOULD queue eviction work onto the renderer/graphics
thread and execute teardown in a GPU-safe phase (end-of-frame or deferred
reclaimer). They MUST NOT destroy GPU resources on the Content thread.

---

## Cache budget and trim

### Cost rule

`ResidencyPolicy::cache_budget_bytes` is in bytes. An entry costs the cooked
bytes it holds, through the cache's cost function (`CookedBytes` in
`AssetLoader.cpp`):

- a resource costs its payload size (`GetDataSize()`);
- an asset costs the size of the cooked descriptor it was decoded from
  (`data::Asset::GetDescriptorSize()`).

Both are fixed once the entry is decoded. The cost is cooked bytes, not the
size of the decoded CPU objects.

### Policy

`ResidencyPolicy` (`Content/ResidencyPolicy.h`):

| Field                    | Default                  | Meaning                           |
| ------------------------ | ------------------------ | --------------------------------- |
| `cache_budget_bytes`     | `UINT64_MAX` (unlimited) | Budget for all cached entries     |
| `trim_mode`              | `kManual`                | `kManual` or `kAutoOnOverBudget`  |
| `default_priority_class` | `kDefault`               | Default priority for load intents |

`IAssetLoader::SetResidencyPolicy` applies a policy on the owning thread and
throws `std::invalid_argument` when the budget is 0. It then runs the
automatic budget check (below). `GetResidencyPolicy` reads the policy back.

`QueryResidencyPolicyState` returns a `ResidencyPolicyState` snapshot, on the
owning thread: the policy, `cache_entries`, `consumed_bytes`,
`checked_out_items`, `over_budget`, and the cumulative trim counters
`trim_attempts`, `reclaimed_items`, `reclaimed_bytes` and `blocked_roots`.
`consumed_bytes` and `reclaimed_bytes` are bytes under the cost rule above.

### Trim

`IAssetLoader::TrimCache` runs a manual trim pass (`ExecuteTrimPass`); the
dev-only console command `cntt.trim_cache` calls it. A pass:

1. Drains all pending release records.
2. Repeatedly removes every cache entry that nothing uses, draining the
   release records those removals return, until a round removes nothing.
   Entries retained by active callers, bound children or pins stay.
3. Updates the trim counters and logs a summary (roots trimmed, orphan
   resources, reclaimed items and bytes, blocked entries).
4. Emits `EvictionReason::kTrim` events for the removed entries, and for
   resource mappings that no longer have a cached entry.

A pass removes every eligible entry; it does not stop when the budget is
met and orders nothing by recency.

### Automatic trim

`MaybeAutoTrimOnBudgetPressure` runs after a cache publication:

- a publication the cache cannot admit forces a pass, then publishes once
  more (`asset_store_failed`, `resource_store_failed`); a second failure
  is recorded as a store-retry failure;
- a successful publication that leaves the cache over budget runs a pass
  (`asset_store_succeeded`, `resource_store_over_budget`);
- `SetResidencyPolicy` checks the same condition (`policy_updated`).

The check always drains pending releases first. It trims only when
`trim_mode` is `kAutoOnOverBudget`; under `kManual` the budget is observed
(`over_budget`, `consumed_bytes`) and enforced only through `TrimCache`.

---

## Eviction notifications

`IAssetLoader::SubscribeResourceEvictions(type, handler)` registers a handler
for one `TypeId` and returns a move-only RAII `EvictionSubscription` that
unsubscribes on destruction. It runs on the owning thread, as do the handlers.

`content::EvictionEvent` (`Content/EvictionEvents.h`) carries `asset_key` (set
for assets), `key` (the `ResourceKey` of a resource), `type_id` and `reason`.
`EvictionReason` is one of:

| Reason          | Raised by                                         |
| --------------- | ------------------------------------------------- |
| `kRefCountZero` | The cache retired an entry through its own policy |
| `kTrim`         | A trim pass removed the entry                     |
| `kClear`        | `ClearMounts`, remounting a mounted pak or root   |
| `kShutdown`     | Loader shutdown                                   |

A handler that throws is logged and does not stop delivery to the other
subscribers. A nested notification for the cache key being notified is
ignored.

---

## Observability

Systems SHOULD log (debug-level) when:

- A resource is evicted (`ResourceKey`, type id).
- A GPU residency teardown is scheduled and when it completes.
- An in-flight load completion is discarded due to eviction.

This MUST NOT spam logs on hot paths in release builds.

---

## Minimum test requirements

The project SHOULD include focused tests that validate the contract without integration-heavy setups.

### Texture residency

- Given a `TextureBinder` entry for key `K`, when Content emits eviction for
  `K`, the binder eventually releases GPU resources for `K` and does not leak
  descriptor allocations.
- Given an in-flight texture upload for `K`, when Content evicts `K`, the final
  upload completion MUST NOT repoint the descriptor back to a resident texture.

### Buffer residency

- Given a `GeometryUploader` (or the owning system for GPU buffers) entry for
  key `K`, when Content emits eviction for `K`, the uploader eventually releases
  GPU resources for `K`.
- Given a buffer upload in-flight for `K`, eviction must prevent late
  completion from resurrecting the handle.

### Cache budget

- When consumed bytes exceed the configured budget under `kAutoOnOverBudget`,
  Content MUST trim the entries nothing uses.
- Eviction MUST only affect entries with no active checkouts.
