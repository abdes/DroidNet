# Renderer upload

Upload owns CPU-to-GPU data preparation and transfer. Existing planning and
coordinator APIs remain the foundation; immutable texture initialization adds
a bounded path for renderer-owned lookup products.

The capability below is planned; [VX-IBL-01.S7](../milestones/VX-IBL-01/README.md#s7--reusable-infrastructure)
owns adoption, implementation state and integration qualification.

Read: [contract](#immutable-texture-initialization), [C++ guidance](../RULES.md#c-for-shared-infrastructure).

## Immutable texture initialization

**Implementation home:** `Vortex/Upload/ImmutableTextureUpload.*`, with shared packing in the
existing Upload module. Migrate
[`IblBrdfResources::Prepare`](../../../src/Oxygen/Vortex/Environment/Internal/IblBrdfResources.cpp)
and [`BrdfEnergyResources::Prepare`](../../../src/Oxygen/Vortex/Lighting/Internal/BrdfEnergyResources.cpp).
Keep both LUT generators, formats, dimensions and service publication fields.

- Accept a newly created managed 2D texture, explicit source subresources/row
  pitches and allocation-budget/debug context. Preparation returns a move-only
  upload object owning the packed staging and destination registrations;
  `Record(CommandRecorder&)` records it. The caller obtains the producer
  `CompletionReceipt` from `SubmitWithReceipt`. S7 supports the
  two existing 2D LUT consumers; 3D/cube upload remains with `UploadCoordinator`.
- Extract and reuse `PackTexture2DToStaging` from
  [`UploadCoordinator.cpp`](../../../src/Oxygen/Vortex/Upload/UploadCoordinator.cpp)
  and use `UploadPlanner::PlanTexture2D`. The coordinator calls the same packer;
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
- Move the older BRDF-energy resource/staging retirement to this managed path,
  including publication through `ForwardLightPublisher` and the deferred/base/
  translucent recording sites that consume `LightingFrameBindings`. Carry CPU
  resource leases/dependencies alongside existing bindings; do not enlarge the
  shader ABI to carry ownership. Preserve Lighting's allocation
  budget and failure classification. Cache a complete product; no per-frame
  upload, staging allocation or new global cache.

**Checks:** pitched/short/overflowing source rows, padding, setup/view/allocation
failure, discard, rejected/uncertain submission, retry and backend close. Read
back both real LUTs exactly and execute both lighting consumers after releasing
initialization CPU temporaries. Exercise existing 2D/3D/coordinator regressions.
