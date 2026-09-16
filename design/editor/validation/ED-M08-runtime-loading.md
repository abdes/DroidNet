# ED-M08 — Script and asset-loading corrections

Status: **implemented and native-test validated**. The associated
[native scene checks](ED-M08-native-scene-validation.md) cover all 20 maintained
and imported RenderScene scenes. These corrections do not close M08.1 or the
M08.3 parity gate.

## Failure and runtime corrections

### Persistent bytecode cache and the missing-compiler cascade

The preserved shared `bin/Oxygen/scripts.bin` version-1 cache contains payloads
at offsets that no longer correspond to the indexed bytecode. The original cache
and its 34-entry inventory are retained under
`projects/Oxygen.Engine/out/build-ninja/ed-m08/script-cache-investigation/`.
The SHA-256 of `scripts.original.bin` is
`47fa2c48e5417d1482a7fe71854c3dcb1cb99f7f3b381a43db1759e9f61381ee`.

The old cache implementation could retain an index while another publisher
replaced the shared file and shifted its payload offsets. It accepted those bytes
without a checksum bound to the requested compile key, and could copy stale
payloads into a subsequent cache snapshot. The regression suite reproduces both
paths. Bad cached bytecode then failed slot initialization. That content failure
was reported as a module failure, so `ModuleManager` removed `ScriptingModule`;
its shutdown unregistered the Luau compiler. A later multi-script scene then
reported that no compiler was registered. Clearing the cache alone would leave
both failure paths intact.

`Engine/Scripting/ScriptCompilationService.cpp/.h` now provides:

- A version-2 cache with strict header/index, range, enum, duplicate-key and
  contiguous-payload validation. Version 1 is invalidated and recompiled; there
  is no legacy-layout reader.
- A checksum bound to the compile key and bytecode payload, checked on every
  persistent read and before copying old entries into a new snapshot. Invalid
  entries become cache misses before their bytes reach the VM.
- Coherent local index/read/publication locking, unique temporary files and
  replacement publication that preserves the previous file on failure.
- Pending compiled results retained until successful publication. Immediate and
  deferred persistence both support retry. A successful snapshot acknowledges
  only the exact results it published, preserving newer queued results.

Focused coverage includes payload/checksum corruption, malformed indexes and
version rejection, stale offsets after external replacement, stale snapshot
copying, and a Windows file-sharing failure followed by publication retry.

### Content failures, event ownership and replacement recovery

`Core/FrameContext`, `Core/EngineModule` and `Engine/ModuleManager` distinguish
content failures from genuine module failures. Slot initialization, slot hooks
and owned event callbacks still report errors, but quarantine only the offending
script instance. Healthy instances, later scenes and the compiler remain
available. Global module hooks and owner-zero event callback failures retain
module-failure handling.

`Scripting/Module/ScriptingModule` and the existing
`Scripting/Bindings/Packs/Core/EventsBindings` registry provide the instance
lifetime boundary:

- Every slot initialization, slot hook and owned event callback runs in an owner
  scope. Nested listener registrations retain that owner. A callback failure
  disconnects its owner's listeners immediately, including later callbacks in
  the same dispatch batch.
- Fault, rebuild and destruction retire listeners before releasing instance Lua
  references. Global subscriptions retain their independent lifetime. Retired
  owners cannot register new listeners; owner identifiers do not wrap and reuse.
- Liveness uses a weak scene reference, exact node/slot identity, component
  incarnation and live executable identity/content fingerprint. The runtime-only
  component incarnation is fresh for construction/copy/clone and preserved by
  storage moves. It is not serialized. Detach/re-attach with the same executable
  cannot revive the old instance's listeners; the replacement initializes anew.
- Both hook phases reconcile the live attached executable before deciding whether
  to rebuild. In-place bytecode reload and a different executable object with the
  same hash are observable without a scene mutation notification. Unchanged
  faulted instances remain quarantined.
- Compiler output and cooked resources may have declared hash zero. A centralized
  runtime fingerprint hashes their actual bytecode when the declared hash is
  absent; authored resource metadata is unchanged. This costs a bytecode scan at
  reconciliation/listener-liveness checks for hash-zero content. Declared nonzero
  hashes retain constant-time comparison.
- Events emitted by callbacks remain queued for the next dispatch, with payload
  references intact. Only the initial dispatch batch is consumed and released.

The new in-place reload regression covers 16 combinations: declared or raw
compiler hash zero; gameplay or scene-mutation as the first phase after reload;
and healthy, initialization-faulted, gameplay-faulted or mutation-faulted original
instances. It checks unchanged-instance quarantine, old-listener retirement,
fresh initialization exactly once and resumed hooks/listeners. A separate test
uses two executable objects sharing identical bytecode and the same hash.

### Accepted callback and direct-load drain

`Content/AssetLoader.cpp/.h` now keeps accepted-load lifetime tickets independently
of the shared I/O table. Clearing that table during `Stop()` cannot make the drain
report completion while decoding, publication or accepted callback work remains.
Started direct asset/resource/cooked-resource awaits retain their tickets through
completion. Accepted callback loads retain theirs through delivery or cancellation,
including follow-up work accepted by a callback. Callback exceptions propagate
once instead of invoking the same callback again with a null result.

Coverage includes queued loads that never create I/O entries, callback-enqueued
follow-ups, cancellation releasing callback captures, throwing callbacks and the
three direct-load paths. The direct-load regressions suspend a real worker
decoder, call `Stop()`, prove the drain remains blocked, then release decoding and
verify publication completes before clearing mounts.

## Verified native test evidence

The XML files were parsed and agree with their corresponding logs: no failures,
disabled cases or skipped cases in either recorded CTest run. These are Debug
runs against `projects/Oxygen.Engine/out/build-ninja`.

| Recorded run | Result | Evidence |
| --- | --- | --- |
| Seven native suite targets | 7/7 pass: AssetLoader, FrameContext, ModuleManager, ScriptingComponent, Scripting Module, CompilationService and Bindings | [Log](../../../artifacts/ed-m08/script-runtime/review-fixes-tests.log), [XML](../../../artifacts/ed-m08/script-runtime/review-fixes-tests.xml) |
| Explicit review regressions | 6/6 pass: three direct-load drains, in-place reload, same-hash executable replacement and failed-publication retry | [Log](../../../artifacts/ed-m08/script-runtime/review-regressions.log), [XML](../../../artifacts/ed-m08/script-runtime/review-regressions.xml) |
| Direct-load drain baseline | 0/3 pass before the direct-load ticket correction; all three observed premature drain completion while decoding was held | [Failing baseline](../../../artifacts/ed-m08/script-runtime/direct-drain-baseline-tests.log), [passing rerun](../../../artifacts/ed-m08/script-runtime/review-regressions.log) |

The three baseline-to-passing cases are
`AssetLoaderAsyncTest.StopDrainsDirectAssetDecodeAndPublication`,
`StopDrainsDirectResourceDecodeAndPublication` and
`StopDrainsDirectCookedResourceDecodeAndPublication`. Their failure assertions
remain in the passing tests; acceptance thresholds were not relaxed.

The suite run is timestamped `2026-09-16T05:41:42` in its XML; the focused run is
timestamped `2026-09-16T05:43:26`. These records establish native test validation,
not final scene image approval.

## Native scene evidence and scope

The final native scene record links the matched builds, recooked roots, exact
scene/source identities, script-error checks, image review and settings recovery.
It includes the multi-script scene and both physics-domain scenes. Still images
alone do not prove animation, every script hook or physics correctness; the
regression suites establish failure isolation, replacement and drain behavior.

Cache integrity, content-error classification, listener ownership, component
incarnation, executable reconciliation and loader draining are ordinary runtime
behavior. Decoder suspension and self-reattach helpers remain in native tests.
Capture runners, profiles and recovery copies remain under the build tree;
no qualification protocol or example schema enters normal engine/editor/SDK
runtime builds.
