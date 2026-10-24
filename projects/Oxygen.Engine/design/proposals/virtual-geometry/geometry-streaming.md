# Virtual geometry page loading

Status: proposal. Part of [virtual geometry](README.md). Owner module:
Content. Content stays read-only, source-scoped and GPU-independent under the
[module overview](../../../src/Oxygen/Content/Docs/overview.md).

Last updated: 2026-10-24

Read: [API boundary](#api-boundary), [request lifecycle](#request-lifecycle),
[limits](#bounds-cancellation-and-failure), [handoff](#upload-and-install-handoff),
[verification](#verification).

## API boundary

Add typed virtual-geometry manifest and page access to the existing loader
owner. There is no new file reader, resource cache, package parser or scene
node per page. Page payloads are byte ranges of a `GeometryAsset`'s cooked
buffer resources, not first-class assets. The
[Data manifest](geometry-format.md#manifest) supplies ranges, codec identity,
dependencies and integrity checks. All dependencies stay in the same mounted
source; the renderer never resolves cross-source resource indices.

What exists to build on:

- `IAssetLoader` is callback-based (`StartLoadGeometryAsset`,
  `StartLoadBuffer`, ...). Coroutine entry points (`LoadAssetAsync`,
  `LoadResourceAsync`) live on the concrete `AssetLoader`.
- `LoadRequest` carries `LoadPriority` (background, default, critical),
  `LoadIntent` (runtime, prewarm, **streaming**) and a `ContentLoadScope`,
  the source-snapshot concept.
- The loader owns the cache, `ResidencyPolicy` (budget and trim mode), pins
  (`PinAsset`, `PinResource`) and eviction events. Mounts are immutable
  generations, prepared and committed atomically.
- There is no per-request cancellation: `OperationCancelledException` is
  thrown by the coroutine APIs on shutdown only.

### Public API and reuse

Extend `IAssetLoader` only with typed manifest and page acquisition, immutable
payloads and the opaque request and source ownership Vortex needs. Use the
existing `LoadRequest`/`LoadPriority`/`LoadIntent`, source identity,
callbacks and coroutines, cache ownership and eviction facilities; page
loading does not create another asynchronous runtime. Data owns `PageId` and
the manifest types, Content owns `ResourceKey` (opaque, loader-lifetime, never
serialized). A new page-request attempt token is a Content `NamedType`, not a
replacement asset key. File offsets, loader queues, caches, GPU permits and
staging directories stay private. An RAII owner does not need a separate
public release function just because the table below names the action. See the
[shared rules](README.md#shared-rules).

| Operation               | Contract                                                                                              |
| ----------------------- | ----------------------------------------------------------------------------------------------------- |
| AcquireGeometryManifest | Immutable manifest with its owning source snapshot and revision; validates format and profile         |
| RequestGeometryPages    | Bounded set of manifest-local `PageId`s, urgency and cancellation identity; returns request ownership |
| Poll / complete page    | Typed immutable payload with manifest and page identity and a validation result                       |
| Cancel request          | Withdraw this consumer's demand; shared work may remain for other consumers                           |
| Release source snapshot | In-flight requests and payload owners keep the source alive; new acquisition follows mount policy     |

Because the interface has no per-request cancellation, the page-request owner
supplies its own cancellation identity and token; it does not overload the
shutdown exception. The public API exposes no PAK offsets, file handles, GPU
slots, command recorders or fences. Pak and loose-cooked sources yield the
same logical result. I/O and cache work are bounded by existing loader
ownership; request coalescing never transfers one caller's revision identity
to another.

## Request lifecycle

Demanded → Queued → Reading → PayloadReady, or Cancelled, Failed or Stale.
`PayloadReady` means validated transfer bytes are available. It does not mean
GPU-resident, decoded or selectable. Results always carry the source snapshot,
manifest revision and `PageId`. A request ID distinguishes retries, so an
obsolete completion cannot satisfy a request for another revision even when
indices match.

GPU demand feedback is asynchronous and may be delayed, duplicated or
dropped. The Vortex resource owner coalesces it by logical identity and
reissues persistent misses with bounded backoff and priority. A missing sample
never removes resident geometry, and nothing waits for a readback to render
the current cut.

Content may finish pages out of order. It does not enforce geometric
replacement readiness or infer hierarchy pins. It may use dependency hints for
I/O scheduling, but geometry owns decode and runtime dependency closure and
admission. Immutable payload ownership lasts while an upload borrows the
bytes; cancellation never invalidates memory referenced by an admitted upload.

## Bounds, cancellation and failure

Independent limits cover pending logical requests, in-flight I/O, CPU
compressed and decoded bytes, and the completed payload cache. Vortex adds
limits on staging, install descriptors, GPU scratch and resident pages. A page
count alone is not a byte budget. Manifest ranges and decoded maxima are
validated before any allocation or read.

Backpressure returns Busy or Deferred. It creates no overflow cache and never
blocks the render thread. Duplicate consumers coalesce and a request cancels
only after its last interested consumer releases demand. Mount replacement,
unmount and recook never retarget an old request to new source bytes: if the
old source cannot be retained under the existing mount policy, the request is
cancelled explicitly and the current GPU representation stays until an
admitted replacement exists.

Distinct failures: NotFound, UnsupportedFormat/Codec, InvalidManifest,
IntegrityFailure, SourceChanged, Cancelled, Budget/Busy, I/OFailure,
DecodeFailure. Only transient failures retry; a permanent failure stays
inspectable and never becomes an empty successful page. An integrity failure
cannot publish geometry. Request and byte accounting stay correct through
failure and shutdown, with no blocking wait in destructors.

## Upload and install handoff

Vortex owns [immutable install batches and completion permits](geometry-resources.md#completion-admission-policy).
Content only returns logical payloads and never reserves physical pages. An
admitted GPU permit can reserve abstract completion capacity while I/O
proceeds, but physical destinations are assigned only when bytes and
prerequisites are ready. The geometry resource owner alone schedules the
completion unit; Content does not independently start partially pinned groups.
The existing bounded loading interface keeps service allowance for the Active
unit's next dependency-ready batch.

`UploadCoordinator` copies payloads into independently owned device-local
install buffers. Vortex polls successful transfer completion without blocking
(`UploadTicket::TryGetResult`), then publishes an immutable, generation-stamped
staged-range directory to installation. Ordinary ring-staging allocations are
not long-lived source ranges. GPU producers resolve destinations and validate
source identity and destination tickets, so no physical address is read back.
The resource design defines directory fields, one-shot candidates, byte
accounting and recycling.

Control acknowledgement of a completion permit is required asynchronous
progress state with retained versioned results that can be polled again.
Per-entry status and statistics may be dropped: one-shot batch recycling
depends on Graphics completion or discard and closed admission, not delivery of
those acknowledgements.

Transfer decompression, resident transcode and draw-time attribute decoding
are distinct; the codec policy declares where each runs and its bounds. The
first profile transfers the compact resident encoding directly, which still
exercises real delayed loading without inventing an advanced compression
backend.

Dropping Content interest does not cancel proof of a submitted upload's
physical completion. Vortex keeps that upload ticket uncancelled until real
completion, because `UploadTicket::Cancel` only sets a logical terminal result
immediately and does not stop a submitted copy. After install submission the
immutable batch bytes are retained through Graphics release even if logical
demand disappears. No hidden GPU retry may retain a directory entry after its
one-shot recording retires; a later retry uses a new request and batch
generation. Unknown execution keeps its quarantine charges.

## Verification

- Equivalent page results from Pak and loose cooked sources.
- Duplicate requests, multiple consumers, cancellation, retry and bounded
  backpressure.
- Out-of-order completion, recook and mount change, stale namespace or
  manifest, corrupted payload.
- Byte and count limits under independently stalled I/O, upload and GPU
  install; unrelated arrivals cannot consume the Active unit's service
  allowance.
- Dropped feedback followed by re-request eventually makes progress with no
  render waits.
- Cancellation during a submitted upload does not authorize destination reuse;
  delayed installs survive ring reuse; every rejected candidate frees batch
  storage after completion even with dropped entry acknowledgements.

Tests use deterministic loader fixtures through the actual typed public
boundary, and integrated tests also consume real cooked pages; a mock page
provider alone does not qualify Content integration.
