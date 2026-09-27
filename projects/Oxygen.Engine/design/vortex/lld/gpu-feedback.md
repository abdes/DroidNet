# Renderer GPU feedback

Renderer infrastructure owns bounded transport of asynchronous GPU results.
Graphics owns readback execution and completion; each consuming service owns
request identity, polling order and interpretation.

[`GpuFeedbackPool`](../../../src/Oxygen/Vortex/Internal/GpuFeedback.h) provides the
transport below; [VX-IBL-01.S7](../milestones/VX-IBL-01/README.md#s7--reusable-infrastructure)
owns adopter state and integration qualification.

Read: [contract](#bounded-gpu-feedback), [C++ guidance](../../../../../design/oxygen/RULES.md#c).

## Bounded GPU feedback

**Implementation home:** `Vortex/Internal/GpuFeedback.*`, above Graphics `ReadbackManager`.
Adopters are [`IblGpuValidation`](../../../src/Oxygen/Vortex/Environment/Internal/IblGpuValidation.cpp),
[exposure status transport](../../../src/Oxygen/Vortex/PostProcess/PostProcessService.cpp)
and [`SpatialLightGrid` demand](../../../src/Oxygen/Vortex/Lighting/Internal/SpatialLightGrid.cpp).

`Reserve` returns a move-only `GpuFeedbackReservation`, distinguishing busy,
unavailable, allocation and backend failures. `EnqueueCopy` uses the caller's
recorder; `Commit` enables delivery after accepted submission. `IsReady` supports
FIFO admission without mapping stale jobs. `Poll<T>` copies a trivially copyable
payload into aligned CPU storage: an empty optional means pending, a value means
ready, and a `ReadbackError` means transport failure. IBL requires exact payload
size; exposure and light-grid accept at least their payload size. `Reset` or
destruction relinquishes delivery. `InspectStats` reports capacity, occupied slots
and created readbacks for bounded-reuse qualification.

An opaque recording-use signal is attached before enqueueing, covering failures
after partial recording. Callbacks retain only that signal. Payload readiness may
precede Graphics' private completion; capacity recycling waits for use release.
The helper never submits, waits, changes source identity or chooses a latest result.

- Provide a fixed-capacity pool of reusable buffer-readback requests and typed
  polling for trivially copyable payloads. A move-only reservation separates
  allocation from copy recording and terminal completion. Caller-owned job metadata
  holds the identity, revisions, source lease and reservation; the helper has no
  environment/exposure/light-grid schema or global latest-result policy.
- Reserve, enqueue on the caller's recorder, commit after accepted submission,
  poll one specified request, then release/recycle. A recorded request occupies
  its slot before submission; reuse waits for Graphics to resolve submission,
  discard or cancellation. For a borrowed recorder, rely on the existing
  readback/recording-use completion machinery rather than capturing the pool in
  a callback. The light-grid copy stays in its existing compute recording.
  A full pool returns busy without waiting, evicting pending work or allocating overflow capacity. Failed
  recording/submission releases the reservation safely. Distinguish pending,
  ready payload and transport failure from payload validity.
- Moving a reservation transfers one request/slot and leaves the source empty.
  Dropping an unused reservation frees its slot immediately. After `EnqueueCopy`,
  dropping it abandons delivery but keeps that request charged to pool capacity
  until existing Graphics/readback completion or discard makes cleanup safe.
  This includes an unsubmitted borrowed recorder and a submitted copy waiting
  behind a fence. Do not rearm live staging, replace it with overflow allocation,
  capture the pool in a completion callback or wait for the GPU.
- Mapped guards end before `ResetForReuse`; preserve each adopter's existing
  payload-length check. A failed/cancelled request is discarded, not rearmed as
  complete. CPU owners retain Graphics until readbacks are destroyed; submitted
  work retains internal use pins and never the facade-owning pool.
- Keep IBL's three pending requests, slot+generation+revision checks, request
  order and diagnostic-only behavior. Preserve its retry after unavailable
  readback and its separate GPU-invalid output handling.
- Exposure retains per-view/lifetime capacity, FIFO processing, settings/control/
  precision epochs, deferred latest-request coalescing and transition
  acknowledgements. Light-grid feedback retains one pending copy per frame slot,
  sequence-based demand updates and its existing placement in light-grid work.
  The helper must support these different polling orders without changing them.

**Checks:** pool full/reuse, map failure, cancellation/discard, delayed/out-of-order
completion, stale view/scene identity and teardown. Drop a recorded reservation
before borrowed-recorder discard, and drop one while an accepted copy is blocked
behind a GPU fence; neither case may reuse live staging or grow the pool. Exercise exposure transitions
and precision acknowledgements, IBL invalid-metadata/retry cases and light-grid
capacity feedback. No synchronous map, new copy submission or additional readback
frequency in the production loop.
