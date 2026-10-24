# Paged resource backing and execution

Status: proposal. Part of [virtual geometry](README.md). Owner module:
Graphics. Existing managed registration, budgets, receipts and indirect
execution are the foundation; this document adds only the gaps a paging
workload exposes.

Last updated: 2026-10-24

Read: [boundary](#boundary), [backing and capabilities](#backing-and-capabilities),
[recording and ordering](#recording-and-ordering), [D3D12](#d3d12-realization),
[verification](#verification).

## Boundary

Graphics owns physical resources, allocation admission, immutable registered
views, submission outcomes and native lifetime. Nexus owns logical page
allocation. Vortex owns frame ordering and content production. Graphics accepts
ordinary buffers and textures and command descriptions. It does not parse
`PageKey`, replacement groups, geometry error or VSM metadata. Graphics must
not link to Nexus or grow a competing resource registry or page scheduler.

Reuse, by their actual names:

| Need                            | Existing API                                                                                                                                          |
| ------------------------------- | ----------------------------------------------------------------------------------------------------------------------------------------------------- |
| Registration with initial views | `ResourceRegistry::RegisterManagedBuffer` / `RegisterManagedTexture` returning `ManagedBuffer` / `ManagedTexture` (all-or-nothing, no partial result) |
| Identity and stale rejection    | `RegistrationIdentity` bound to a `BackendIncarnationId`; `AcquireManaged`; `InvalidateManagedRegistrations`; `PollManagedRetirements`                |
| Pins versus leases              | `RegistrationOwner` (internal), `RegistrationLease` (public consumer), `RegistrationUse` (move-only admitted use)                                     |
| Recording uses                  | `CommandRecorder::RetainRegistration`, `RetainOpaqueUse`, `RecordDependency`; `RecordingUseBatch`                                                     |
| Submission                      | `CommandQueue::SubmitWithReceipt` → `SubmissionResult` (`kSubmitted`, `kDiscarded`, `kExecutionUncertain`) and `CompletionReceipt`                    |
| Budget                          | `AllocationBudget`, `AllocationReservation`, `AllocationBudgetTag` carried in `BufferDesc` and `TextureDesc`                                          |
| Indirect execution              | `CommandRecorder::ExecuteIndirect` with `IndirectCommandKind` (`kDraw`, `kDispatch`) and optional GPU count buffer                                    |
| State and barriers              | `ResourceStateTracker`, `RequireResourceState`, UAV barriers on unchanged UAV state, `ResourceStates::kIndirectArgument`                              |
| Tests                           | `SubmissionFaultTestAccess` (failure points, device loss) and the `Common/test` submission, registry and budget fixtures                              |

No new paging resource, fence, descriptor or submission facade is needed.
Graphics extends its existing capability, resource and execution APIs only
where a paging test shows a missing operation, documenting the exact exported
fields and methods. Native signatures, heaps and D3D12MA stay backend-private.
Reuse `SizeBytes`, `OffsetBytes`, `Alignment`, `ShaderVisibleIndex`,
`RegistrationIdentity` and `CompletionReceipt` rather than raw integers or
synonyms, and use `Result`, `ScopeGuard` and the special-member and export
macros.

## Backing and capabilities

Two payload layouts use existing resource descriptors:

- **Buffer**: fixed logical slot stride, `uint`-aligned raw or structured
  views; the producer owns the record format. Compute destination and raster
  SRV views refer to the same backing. Vertex pulling avoids a per-page CPU
  vertex/index binding loop.
- **Texture**: tiled 2D or array storage with explicit format, tile extent,
  layer count and views. Array layers are not automatically independent
  pages. A synthetic texture producer qualifies addressing and atomics
  without VSM policy.

### Capability query

Graphics has no capability or feature-query API today. The only related code
is a hardcoded shader-model-6.6 library check, a format plane count and a
capture controller's supported-feature list. The pool needs real answers
before it publishes, so Graphics adds one small immutable capability record
per backend incarnation, queried once at backend creation:

| Capability                         | Why the pool needs it                                              |
| ---------------------------------- | ------------------------------------------------------------------ |
| Shader model and bindless tier     | Reject configurations the shaders cannot run                       |
| Counted indirect draw/dispatch     | Reject before any production recording                             |
| Typed UAV formats and atomics      | Texture payloads and future VSM-compatible storage                 |
| Wave size range                    | Kernels must be correct for every supported width; never assume 32 |
| Resource dimension and size limits | Check pool dimensions before admission                             |
| Queue roles                        | Already expressed by `QueueRole`; report which are real            |

Optional mesh shaders, enhanced barriers, reserved resources, sampler
feedback and explicit make-resident get separate capability bits only when the
corresponding API and native tests exist. Unsupported configurations fail
before publication with a typed error.

Budget admission covers payload, tables, counters, production and selection
work, charged staging and retained old generations. A descriptor carries its
`AllocationBudgetTag`; the D3D12 factory charges the actual size reported by
the device allocation info, then checks driver headroom against the allocator
budget, so reservations are taken before the allocation lives and a rejection
is recorded. The reservation stays with the backing's real lifetime,
including GPU uses and native references. Partial setup unwinds
transactionally with `ScopeGuard`. Virtual address capacity is not a request to
allocate an equally large payload.

Descriptors are immutable once published; there is no in-place descriptor
update. A bounded pool generation registers once, with no descriptor
allocation per page hit, draw or dispatch. Generated bindless domains and raw
descriptors stay distinct. A replacement pool generation gets a new
registration identity, so a stale recording cannot attach to it by address.

## Recording and ordering

Before recording commands, attach registration and use pins to the recording
batch. Keep the existing split between public leases and internal pins so
submitted batches cannot form a cycle back to Graphics or renderer facades.
Submission transfers already prepared storage and reports one of three
outcomes: submitted, discarded or execution-uncertain. Rejection before issue
surfaces as the exception of the throwing `Submit` overloads. CPU recording is
never content success.

| Producer → consumer                                     | Required dependency                                                                    |
| ------------------------------------------------------- | -------------------------------------------------------------------------------------- |
| Request/allocator UAV writes → next compute read        | UAV barrier or valid transition through the state tracker                              |
| Payload production → mapping publication → shader reads | Producer completion within the queue plus visibility barriers for payload and metadata |
| GPU argument/count writes → indirect execute            | Transition both buffers and ranges to `kIndirectArgument` (the caller does this)       |
| Copy upload → install compute                           | `RecordDependency(receipt)` when queues differ, with correct copy/compute states       |
| All old readers → page overwrite                        | Ordered epoch dependencies; no CPU fence wait in ordinary rendering                    |
| Capture copy → snapshot consumer                        | Snapshot producer receipt and retained registrations                                   |

Same-queue FIFO gives execution order but not UAV or state barriers.
Cross-queue receipts become native GPU waits, never host waits. Pending
unsubmitted users are resolved by consumer epoch admission before a writer is
recorded; Graphics cannot infer page-content exclusion from retaining a
`Buffer` object.

The production path starts on one graphics queue. Native verification
includes deliberately delayed compute and transfer readers and producers
(`QueueRole::kTransfer` is the copy queue). Whole-pool mutation epochs are
serialized; overlapping subranges would need a separately proved access
contract. A headless ordering test never supports a claim about asynchronous
generation.

A pool reset or resize creates a new registered generation under budget
admission. Accepted old work retains the old backing. Work rejected before
issue releases safely. Execution uncertainty quarantines the affected
registrations through `InvalidateManagedRegistrations` and the existing
backend recovery. Pages, argument storage and descriptors are never recycled
on an estimated frame delay or a successful record call; retirement follows
receipts (`RetireResource`, `PollManagedRetirements`).

## D3D12 realization

Use the current device-local buffer and texture factories and the existing
D3D12MA path. Software page lookup supplies logical-to-physical indirection.
Resource alignment and logical slot stride are different constraints; a 64 KiB
hardware tile is not imposed on every geometry record.

`ExecuteIndirect` already uses two command signatures created once at device
initialization: a single-argument draw and a single-argument dispatch, with no
root signature, so they work with every pipeline. Per-draw data therefore
travels in the arguments; Vortex's convention puts the draw index in
`StartInstanceLocation`, read as `SV_StartInstanceLocation`. Plain root
constants are set separately. Two gaps need closing in the recorder, because
the call site currently validates only the buffer range:

- Validate argument and count byte ranges, stride, 4-byte alignment, command
  maxima and native dispatch dimension limits before recording.
- Partition an oversized dispatch at GPU argument construction. Selected
  coverage is never truncated at execution.

Argument and count buffers must outlive execution and carry the right states.
Required UAV formats and atomics come from the capability record, validated
before pool creation. `R32_UINT` texture atomics are a future VSM-compatible
storage option, not a mandate that geometry buffers use a shadow encoding.

Later backing optimizations have separate gates and are not placeholders:

| Capability                              | Why it is not the first page allocator                                               | Needed later                                                                  |
| --------------------------------------- | ------------------------------------------------------------------------------------ | ----------------------------------------------------------------------------- |
| Reserved resources / UpdateTileMappings | Mappings are supplied through CPU queue APIs, not shader allocation decisions        | Backing-granularity benefit, aliasing and lifetime correctness, tier handling |
| EnqueueMakeResident / Evict             | Controls backing-object residency, not content or mapping readiness                  | Memory-pressure policy, residency-fence ordering, device-loss recovery        |
| Mesh shaders / DispatchMesh             | `ShaderType::kMesh` exists but there is no mesh pipeline state or dispatch command   | Capability, PSO and signature support in Graphics, then a mesh indirect kind  |
| Enhanced barriers                       | Only legacy barriers exist; this is an optional realization of the ordering contract | Equivalent buffer, texture and cross-queue tests on a supported baseline      |

References for these are in [references](references.md#d3d12).

## Verification

- A rejected allocation or view setup leaves no registration, descriptor or
  budget charge.
- Counted draw and dispatch handle zero, exact-capacity and oversized GPU
  counts without out-of-range access.
- Producer/consumer tests read back payload and mapping bytes across UAV
  transitions.
- Readers delayed behind a GPU fence prevent overwrite while independent CPU
  progress continues.
- Discard, rejection before issue, uncertain issue and device closure
  preserve correct ownership, using `SubmissionFaultTestAccess`.
- Old and new pool generations coexist under budget; stale attachment is
  rejected.
- After warmup, fixed inputs cause no descriptor or backing growth.
- Typed UAV, limits and wave-size rejection run on native hardware.

Native debug-layer and GPU validation plus analyzed captures complement the
unit tests.
