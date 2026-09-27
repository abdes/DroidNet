# Renderer upload

Upload owns CPU-to-GPU data preparation and transfer. Existing planning and
coordinator APIs remain the foundation; immutable texture initialization adds
a bounded path for renderer-owned lookup products.

[VX-IBL-01.S7](../milestones/VX-IBL-01/README.md#s7--reusable-infrastructure)
owns implementation state, adopter migration and integration qualification.

Read: [immutable initialization](#immutable-texture-initialization),
[async ownership](#async-request-ownership), [C++ guidance](../../../../../design/oxygen/RULES.md#c).

## Immutable texture initialization

**Implementation home:** `UploadCoordinator::PrepareImmutableTexture2D` prepares
the move-only `Vortex/Upload/ImmutableTextureUpload` recording object. Shared row
packing belongs to `TextureUploadPlan::Pack2D`. Its production users are
[`IblBrdfResources::Prepare`](../../../src/Oxygen/Vortex/Environment/Internal/IblBrdfResources.cpp)
and [`BrdfEnergyResources::Prepare`](../../../src/Oxygen/Vortex/Lighting/Internal/BrdfEnergyResources.cpp).
Both LUT generators, formats, dimensions and service publication fields are unchanged.

- Accept a newly created managed 2D texture, explicit source subresources/row
  pitches and allocation-budget/debug context. Preparation returns a move-only
  upload object owning the packed staging and destination registrations;
  `Record(CommandRecorder&)` records it. The caller obtains the producer
  `CompletionReceipt` from `SubmitWithReceipt`. S7 supports the
  two existing 2D LUT consumers; 3D/cube upload remains with `UploadCoordinator`.
  Initialize full selected subresources using the canonical destination
  footprint. Boxed updates are rejected; source rows may have their own pitch.
- `TextureUploadPlan::Pack2D` replaces the local `PackTexture2DToStaging` in
  [`UploadCoordinator.cpp`](../../../src/Oxygen/Vortex/Upload/UploadCoordinator.cpp)
  and uses `UploadPlanner::PlanTexture2D` layouts. The coordinator calls the same packer;
  keep its existing tickets, queues, cancellation and 3D behavior unchanged.
- Separate preparation/recording from submission: the caller owns the explicit
  graphics-queue recording, diagnostic scope and timing-failure handling. The
  helper retains managed source/destination registrations in that recording,
  records copies and final SRV transitions, and keeps CPU source bytes only
  through packing. Final transitions remain inside the measured upload scope.
- The initialization owner publishes a candidate only for `kSubmitted` with a
  valid receipt. Discard/rejection leaves it unpublished and retryable; uncertain
  submission follows existing Graphics fault handling. Consumers attach the
  registration and record the producer dependency without a CPU wait.
- `ForwardLightPublisher` retains the BRDF-energy product beside its existing
  `LightingFrameBindings`. Deferred, base and translucent recordings attach its
  registration and producer receipt. CPU ownership does not enlarge the shader
  ABI. Lighting's allocation budget and failure classification remain intact.
  Complete products are cached; stable frames allocate no upload staging and
  issue no LUT upload. There is no new global cache.

**Checks:** pitched/short/overflowing source rows, padding, setup/view/allocation
failure, discard, rejected/uncertain submission, retry and backend close. Read
back both real LUTs exactly and execute both lighting consumers after releasing
initialization CPU temporaries. Exercise existing 2D/3D/coordinator regressions.

## Async request ownership

`SubmitAsync` owns its request and staging-provider handle; `SubmitManyAsync`
owns the request vector and provider. Both remain lazy: packing and submission
start when the coroutine executes. Source byte views remain borrowed through
packing; an owning producer capture can carry source storage with the request.
Producers are const-callable and receive a writable staging span.
Each copy recording retains its staging backing and destination through Graphics
completion, independently of coroutine/provider lifetime. Cancellation remains
prompt; partial batch failure cannot release resources still used by the GPU.
Ticket results are published before waking coroutine consumers outside the
tracker lock.

`Shutdown` drains ticketed submissions. Prepared immutable uploads belong to
their caller's recording and retire through Graphics submission receipts.
