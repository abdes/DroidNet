# Eviction and GPU Residency (minimal spec)

This document specifies the **minimum** required behavior to keep renderer-side GPU residency consistent with Content
cache lifetime.

Normative terms: **MUST**, **MUST NOT**, **SHOULD**, **MAY**.

---

## Scope

This spec covers:

- **Content-side eviction** of decoded CPU objects cached by `AssetLoader`.
- **Cache policy and budgeting** for CPU-side resources (and assets when relevant).
- **Renderer-side GPU residency** owned by long-lived systems.
- The **contract** that links the two, including cross-thread delivery and
  in-flight load handling.

This spec intentionally does **not** mandate a specific renderer architecture
(`BufferBinder`, etc.). The goal is correctness with minimal churn and clear
production-grade behavior.

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
- **Eviction reason**: why eviction occurred once the entry became eligible
  (refcount reached zero). Reasons are observational, not additional triggers.
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
- `AnyCache` remains key-agnostic; `AssetLoader` is responsible for mapping
  cache keys to `ResourceKey` and emitting eviction notifications.
- Eviction can only occur after refcount reaches zero. Budget trims, clear,
  or shutdown requests do not evict live entries; they only affect eligible
  entries.
- **Current Oxygen behavior**: assets/resources remain cached until
  `AssetLoader::ReleaseAsset` / `ReleaseResource` is called and the internal
  refcount reaches zero. There is no automatic LRU/trim eviction of unused
  entries yet.
- This spec does not require a particular cache implementation, only the
  observable behavior.

### R2: Renderer GPU owners subscribe

Each renderer subsystem that holds long-lived GPU residency keyed by
`content::ResourceKey` MUST subscribe to eviction notifications for the
resource types it mirrors on the GPU.

At minimum, the following owners are in scope:

- `TextureBinder` (GPU textures + descriptors) for `data::TextureResource`.
- `GeometryUploader` (GPU buffers + descriptors/handles) for
  `data::BufferResource` or for any equivalent buffer residency it owns.

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

### R6: Cache budgeting and eviction policy

Content caches MUST have explicit budgets per resource class (textures, buffers
and other heavy payloads). Budgets MUST be expressed in bytes, not counts.

- The cache MUST maintain a cost model for each cached payload.
- Cache eviction MUST be triggered when a budget is exceeded.
- Refcount reaching zero is a **necessary** condition for eviction, not a
  sufficient one, unless policy explicitly permits immediate eviction.
- A background or explicit `Trim` step MAY be used to perform eviction when
  budgets are exceeded.

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

## Observability / diagnostics (minimal)

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

### Cache budgeting

- When resource memory exceeds the configured budget, Content MUST evict
  eligible entries until the budget is met.
- Eviction MUST only affect entries with no active checkouts.

---

## Implementation plan grounded in Oxygen

This plan assumes the existing systems are retained and only the glue and
contract gaps are closed.

### Current foundation (already present)

- Content cache with refcount-based lifetime tracking.
- `AssetLoader` provides async loading and resource identity (`ResourceKey`).
- Renderer upload coordination and bindless stable indices.

### Remaining gaps to close (minimal)

1. **Eviction event payload must carry `ResourceKey` + `TypeId` + reason.**
  Cache hashes are not sufficient to identify the resource instance.

2. **Renderer-side teardown hooks** must subscribe to eviction notifications
  and release GPU residency (textures, buffers, descriptors) on the render
  thread.

3. **In-flight completion suppression** to prevent late uploads from
  re-resurrecting evicted entries.

4. **Budgeting + LRU trim for eligible entries** (refcount == 0) to keep
  memory bounded without violating ownership.

5. **Telemetry and diagnostics** to validate correctness while implementing
  steps 1–4.

### Concrete steps (minimal churn)

1. **Add eviction event plumbing in Content.**
   - Extend the cache eviction callback to emit `{ResourceKey, TypeId, reason}`.
   - Emit eviction events on the Content owning thread.
   - Keep `AnyCache` unaware of `ResourceKey`; mapping happens in
    `AssetLoader`.
   - Add **basic telemetry counters** for evictions and cache hits/misses.

   **LLD (step 1)**

   **Data model**
   - Add `content::EvictionReason` enum in `Oxygen/Content/IAssetLoader.h` or a
     new `EvictionEvents.h` (preferred) with minimal values:
     `kRefCountZero`, `kClear`, `kShutdown`.
   - Add `content::EvictionEvent` struct:
     - `ResourceKey key`.
     - `TypeId type_id`.
     - `EvictionReason reason`.
     - Optional diagnostic fields (debug-only): `uint64_t cache_key_hash`.

   **AssetLoader internal bookkeeping**
   - Add a reverse map from cache key hash to `ResourceKey` for resources:
     `std::unordered_map<uint64_t, ResourceKey> resource_key_by_hash_`.
   - Populate on successful resource cache insert in
     `LoadResourceAsync`/`LoadResourceAsyncFromCookedErased` and on
     `content_cache_.Store` for resources.
   - Remove mapping on eviction after emitting the event.

   **Eviction emission points**
   - In `AssetLoader::ReleaseResource`, the `AnyCache::OnEviction` callback
     MUST emit a `EvictionEvent` with reason `kRefCountZero`.
   - In `AssetLoader::ReleaseAsset`, if dependent resource evictions occur,
     events MUST be emitted via the same callback.
   - In `AssetLoader::ClearMounts`, if cache is cleared, emit with `kClear`.
   - During shutdown teardown, emit with `kShutdown`.

   **Threading**
   - Emission occurs on the Content owning thread (same thread as the cache
     operations in `AssetLoader`).

   **Failure handling**
   - If `resource_key_by_hash_` has no entry, log a warning and emit no event.
     This preserves correctness and avoids accidental cross-key teardown.
   - Increment `evictions_total`. Increment `evictions_due_to_trim` only when
     eviction occurs during a `TrimCaches()` pass.

2. **Expose eviction subscription in `IAssetLoader`.**
   - Allow renderer subsystems to subscribe for specific resource types.
   - Ensure at-most-once delivery per eviction event.
   - Expose telemetry snapshot API for early validation.

   **LLD (step 2)**

   **API surface**
   - Add a minimal RAII subscription type and a single subscribe method:
     - `using EvictionHandler = std::function<void(const EvictionEvent&)>;`
     - `class EvictionSubscription` (move-only; destructor auto-unsubscribes).
     - `virtual auto SubscribeResourceEvictions(TypeId resource_type,
         EvictionHandler handler) -> EvictionSubscription = 0;`
   - Naming rationale:
     - **EvictionHandler** signals fire-and-forget callbacks.
     - **EvictionSubscription** communicates RAII ownership.
     - **SubscribeResourceEvictions** is explicit about resource scope.

   **AssetLoader implementation**
   - Maintain `std::unordered_map<TypeId, std::vector<Subscriber>>`.
   - Each `Subscriber` holds the handler and an internal id.
   - `EvictionSubscription` stores `(owner_ptr, resource_type, id)` and erases
     on destruction.
   - Deliver events only to subscribers of `event.type_id`.

   **Delivery guarantees**
   - At-most-once: enforced by AssetLoader event generation (single emission
     per cache eviction) and by delivering exactly once per event.
   - In-order per key: guaranteed by emitting events on the owning thread in
     the same order as cache eviction callbacks.

   **Lifetime safety**
   - Subscriptions are removed automatically when `EvictionSubscription` is
     destroyed or when `AssetLoader::Stop()` clears subscribers.

3. **TextureBinder integration.**
   - Subscribe to texture evictions.
   - On eviction, schedule render-thread teardown and repoint/invalidate SRV
     per chosen policy.
   - Drop or ignore upload completions if the entry is no longer resident.

4. **GeometryUploader integration.**
   - Subscribe to buffer evictions (or a buffer residency owner if distinct).
   - Release GPU buffers and descriptor views via deferred reclaimer.
   - Guard against late upload completions for evicted handles.

5. **Targeted tests.**
   - Verify eviction triggers teardown and no descriptor leaks.
   - Verify in-flight upload completion does not resurrect.
   - Verify budget trim only evicts non-checked-out entries.

6. **Budgeting + LRU trim (new).**

   NOTE/TODO: Think about fitting cache trimming to an engine dictated frame
   time budget. Only a certain number of evictions can happen per frame, while
   respecting dependencies and not leaving orphaned resource.

   **LLD (step 6)**

   **Design goal**
   - Keep memory bounded while preserving the refcount ownership model.
   - Evict only **eligible** entries (refcount == 0), never live entries.

   **Data model**
   - Introduce `content::CacheBudget` (per resource class) with byte budgets.
   - Add `content::CacheClass` enum: `kTexture`, `kBuffer`, `kAsset` (optional).
   - Track per-entry `last_access_time` (updated on successful load or cache
     hit).
   - Track per-entry `eligible_since` timestamp when a release call makes an
     entry *potentially* evictable; eligibility is validated via
     `AnyCache::IsCheckedOut()` at trim time.
   - **Implementation location**: store these timestamps in `AssetLoader`
     metadata maps keyed by the cache hash (not inside `AnyCache`).

   **AnyCache integration**
   - Keep `AnyCache` key-agnostic and refcount-based; no policy changes needed.
   - Add a **trim pass** in `AssetLoader` that iterates cache keys and evicts
     eligible entries based on LRU when `Consumed() > Budget()`.
   - Use `AnyCache::Keys()` + `IsCheckedOut()` and `GetTypeId()` to filter
     eligible entries, then call `Remove(key)` to evict.
   - Update metadata in `AssetLoader` on:
     - successful cache hits (`Get*` / `Load*` returning cached entries),
     - new cache inserts (`Store`),
     - release calls (`ReleaseAsset` / `ReleaseResource`) to set
       `eligible_since`.

   **LRU rules**
   - Only entries with refcount == 0 participate in LRU ordering.
   - LRU ordering uses `eligible_since` (oldest eligible evicted first).
   - `eligible_since` is updated only after a release call and only used when
     `IsCheckedOut(key)` is false.
   - LRU trim runs:
     - on explicit `TrimCaches()` calls (engine frame policy), or
     - after successful `ReleaseAsset/ReleaseResource` calls.

   **Budgeting policy**
   - Budget is enforced per cache class (textures vs buffers vs assets).
   - Cost is estimated from decoded resource size (bytes), stored alongside
     the cache entry in `AssetLoader` metadata maps.
   - If budget is exceeded, evict LRU-eligible entries until under budget or
     no eligible entries remain.

   **API surface**
   - Add `AssetLoader::SetCacheBudget(CacheClass, std::size_t bytes)`.
   - Add `AssetLoader::SetEvictionGracePeriod(CacheClass,
     std::chrono::seconds)`.
   - Add `AssetLoader::TrimCaches()` (explicit call from engine loop).
   - Add `AssetLoader::GetCacheTelemetry(CacheClass)`.

   **Threading**
   - Trim runs on the Content owning thread only.

   **Eviction reason**
   - Trim-triggered evictions still report `kRefCountZero` (eligibility cause),
     not a separate budget reason, to keep the event model minimal.

    **Telemetry (usage-driven)**

   **Usage scenarios**
   - **Live tuning**: verify budgets and grace periods prevent thrash when
     swapping scenes rapidly.
   - **Regression tracking**: detect spikes in decoded bytes or eviction churn
     after asset/content changes.
   - **Operational visibility**: answer “what was trimmed last frame?” versus
     “what is the steady-state cache health?”.

   **Telemetry model**
   - `CacheTelemetryGlobal` (cumulative since process start or reset):
     - `bytes_used`, `bytes_peak`, `entry_count`.
     - `cache_hits`, `cache_misses`.
     - `evictions_total`, `evictions_due_to_trim`.
     - `trim_invocations`.
   - `CacheTelemetryLastTrim` (stats for the most recent trim pass):
     - `bytes_before`, `bytes_after`, `bytes_evicted`.
     - `entries_before`, `entries_after`, `entries_evicted`.
     - `evictions_due_to_grace_period` (skipped because too young).
     - `oldest_evicted_age_seconds`, `newest_evicted_age_seconds`.
     - `duration_ms`.
   - `CacheTelemetry` returned by API includes both:
     - `global` + `last_trim` + `last_trim_timestamp`.

This plan keeps the existing ownership model and upload orchestration intact
and focuses on the missing residency contract edges.
