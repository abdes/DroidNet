# Headless Graphics Backend

This module provides a headless graphics backend for testing and simulation.

## Purpose

- Emulate resource creation (buffers, textures, shaders) with minimal CPU-side state.
- Track resource state transitions and validate usage patterns.
- Simulate command recording and submission with deterministic fences.
- Record replayable logs in a later phase (JSON-based format).

Namespace: `oxygen::graphics::headless`
Module: `Oxygen.Graphics.Headless`

## Build

```shell
# Build only the library
oxybuild headless

# Build and run tests
oxyrun headsmoke
oxyrun headall
```

## Prioritized TODOs

| Status | Item |
|--------|------|
| ✅ | Implement CommandExecutor and centralize execution logic (move lambda content from `CommandQueue::Submit`). |
| ✅ | Extend `CommandContext` with runtime services (Graphics, queue, resolver, submission id). |
| ⬜️ | Implement DrawCommand/DispatchCommand and route `SetPipelineState` into recorded PSO usage. |
| ⬜️ | Add minimal PSO and ShaderStub to enable deterministic fallback draw output. |
| ⬜️ | Implement Descriptor runtime resolver to map descriptors to `NativeObject` at execute-time. |
| ✅ | Enforce resource state transitions at execute-time and improve `ExecuteBarriers` to integrate with executor. |
| ⬜️ | Implement depth/stencil backing and update `ClearDepthStencilCommand` to mutate DSV backing. |
| ⬜️ | Add headless `QueryPool` with timestamp/occlusion support. |
| ⬜️ | Implement Present/Acquire semantics for `HeadlessSurface` and expose last-presented image for tests. |
| ⬜️ | Add ReadbackManager for async/synchronous CPU readback flows and tests. |
| ⬜️ | Add submission trace serialization using `Command::Serialize()` and a small JSON schema. |

## Work planning

The headless backend already provides resource backings, command recording,
submission plumbing and several concrete command implementations (buffer
copies, buffer->texture uploads and framebuffer clears). The sections below
describe the remaining work needed to make the headless backend a
deterministic, testable execution model suitable for integration tests and
replay.

### Command execution runtime

Introduce a dedicated `CommandExecutor` that:

- prepares an execution-time `CommandContext` per submission;
- validates and flushes resource barriers before execution;
- resolves descriptors and other runtime views; and
- runs submitted `Command::Execute(ctx)` calls centrally (replace submit
  lambdas with the executor to make behavior testable).

### CommandContext improvements

Extend `CommandContext` to carry small, non-owning references to the runtime
services needed at execute time (device `Graphics`, target `CommandQueue`,
`ResourceRegistry`, `ResourceStateTracker`, optional `QueryPool` manager and
`submission_id`). Keep the context minimal and deterministic.

### Draw / Dispatch emulation

Implement headless `DrawCommand` and `DispatchCommand` and plumb
`CommandRecorder::SetPipelineState` to record PSO usage. Start with a
deterministic fallback shader stub that produces observable RT writes (e.g.
constant-color fill) so draw submissions can be tested.

### Pipeline State Object (PSO) and Shader stubs

Add a minimal PSO representation (layout, shader id, RT formats) and a
`ShaderStub` that provides reflection metadata and deterministic fallback
behavior for draw emulation.

### Descriptor runtime resolution

Implement a lightweight resolver that maps bindless descriptor indices or
descriptor table entries to `NativeObject` pointers at execute time. Integrate
the resolver with the `CommandContext` so draws and dispatches can access
bound resources.

### Resource state enforcement at execute time

Consult recorded barriers or the `ResourceStateTracker` prior to execution.
The headless backend advocates explicit barrier management. Recording-side
APIs such as `CommandRecorder::RequireResourceState` (templated) populate the
per-recording `detail::ResourceStateTracker` and `FlushBarriers()` / the
protected backend `ExecuteBarriers()` hook are the places where the recorded
pending barriers are processed.

Important contract (explicit-only transitions):

- Commands and the recorder must explicitly state required transitions using
  `RequireResourceState` / `RequireResourceStateFinal` during recording.
- The executor should consult `state_tracker->GetPendingBarriers()` (or call
  the recorder's `FlushBarriers()`) and then call the backend's
  `ExecuteBarriers()` implementation to apply/validate them. `ResourceBarrierCommand`
  is the canonical command that represents in-stream transitions.
- The executor MUST NOT invent or silently perform implicit transitions that
  were not recorded. If a recorded 'before' state does not match the observed
  state, prefer a clear validation error or deterministic fallback defined by
  the backend (do not auto-promote states silently).

This keeps the execution model deterministic and matches the project's
philosophy: recording must express intent; execution validates and enforces it.

### Depth/stencil backing and DSV semantics

Implement depth/stencil backing writes and update `ClearDepthStencilCommand`
to mutate texture backing for DSV formats.

### Query and timestamp emulation

Add a headless `QueryPool` that supports timestamp and occlusion-like queries
using host timers or deterministic counters.

### Present / Surface semantics

Implement acquire/present semantics for `HeadlessSurface` so tests can assert
on presentation order and read back the last-presented image.

### Readback and staging helpers

Provide synchronous readback helpers (already present) and optionally an
asynchronous `ReadbackManager` for non-blocking readback flows and simulated
transfer timings.

### Diagnostic / serialization

Use `Command::Serialize()` to emit submission traces in a JSON or text format
for deterministic replay and debugging.

### Complete commands table (prioritized)

The table below lists GPU/command-stream operations that appear in D3D12 and
Vulkan and are relevant for headless emulation. Each row shows current
headless status, a short summary and API mappings. The ordering reflects the
implementation priority for making headless useful for testing and replay.

| Priority | Status | Command | D3D12 / Vulkan mapping | Summary |
|---:|:---:|---|---|---|
| 1 | ✅ | CopyBuffer / CopyBufferCommand | CopyBufferRegion / vkCmdCopyBuffer | Buffer-to-buffer raw byte copies. Already implemented. |
| 1 | ⬜️ | CopyTextureToBuffer / CopyTextureRegion | CopyTextureRegion / vkCmdCopyImageToBuffer | Texture->buffer readback (row/slice pitch, blocks). Important for readback tests. |
| 1 | ✅ | ResourceBarrierCommand / PipelineBarrier | ResourceBarrier / vkCmdPipelineBarrier | Explicit resource transitions / memory barriers; executor enforces recorded barriers (implemented). |
| 1 | ⬜️ | PresentCommand / Present emulation | Present (swapchain) / vkQueuePresentKHR | Surface acquire/present semantics; expose last-presented image for tests. |
| 1 | ✅ | BufferToTextureCommand | CopyBufferRegion+subresource / vkCmdCopyBufferToImage | Buffer->texture uploads (implemented, supports block formats). |
| 1 | ✅ | QueueSignalCommand / QueueWaitCommand | Signal/Wait (fences/timeline) / vkCmdSetEvent/wait or timeline semaphores | Queue-side signal/wait implemented via `QueueSignalCommand`/`QueueWaitCommand` calling `CommandQueue` wait/signal. |
| 2 | ⬜️ | CopyTextureToTexture | CopySubresourceRegion / vkCmdCopyImage | Image-to-image copy, mip/array-aware. |
| 2 | ⬜️ | DispatchCommand | Dispatch / vkCmdDispatch | Compute dispatch path; validate bindings and optionally perform simple emulation. |
| 2 | ⬜️ | CopyTextureRegion variants (row/align) | vkCmdCopyImage / CopyTextureRegion | Variants for pitched copies and block layouts. |
| 3 | ⬜️ | DrawCommand / DrawIndexedCommand | DrawInstanced / vkCmdDrawIndexed | Graphics draws; requires PSO, descriptor resolution, and fallback shader behavior. |
| 3 | ⬜️ | BeginRenderPass / EndRenderPass | OMSetRenderTargets / vkCmdBeginRenderPass / vkCmdEndRenderPass | Render-pass lifecycle to model load/store and subpass semantics. |
| 3 | ⬜️ | Resolve / Blit | ResolveSubresource / vkCmdResolveImage / vkCmdBlitImage | MSAA resolves and image blits/format conversions. |
| 4 | ⬜️ | ExecuteIndirect / DrawIndirect / DispatchIndirect | ExecuteIndirect / vkCmdDrawIndirect / vkCmdDispatchIndirect | Indirect execution reading parameters from GPU buffers. |
| 4 | ⬜️ | Secondary/Bundle execution | ExecuteBundle / vkCmdExecuteCommands | Execute secondary/ bundled command buffers. Useful for multi-threaded recording semantics. |
| 4 | ⬜️ | CopyBufferToBuffer (parity name) | CopyBufferRegion / vkCmdCopyBuffer | Alias for buffer-to-buffer copies. |
| 5 | ⬜️ | QueryBegin / QueryEnd / ResolveQuery | BeginQuery/EndQuery / vkCmdBeginQuery/vkCmdEndQuery / vkCmdCopyQueryPoolResults | Timestamps, occlusion and query pool resolves. |
| 5 | ⬜️ | UpdateBuffer / FillBuffer / UpdateSubresource | UpdateSubresource / vkCmdUpdateBuffer / vkCmdFillBuffer | Small CPU-side updates to buffers (useful for upload helpers). |
| 5 | ⬜️ | ClearColorImage / ClearDepthStencilImage / ClearAttachments | ClearRenderTargetView / vkCmdClearColorImage / vkCmdClearAttachments | Clear operations targeting images/attachments (beyond the current simple clear). |
| 6 | ⬜️ | BindDescriptorSets / RootDescriptorTable / SetDescriptorHeaps | IASetVertexBuffers / SetGraphicsRootDescriptorTable / vkCmdBindDescriptorSets | Descriptor binding and updates; runtime resolver needed for execute-time mapping. |
| 6 | ⬜️ | SetVertexBuffers / SetIndexBuffer / InputAssembler | IASetVertexBuffers / IASetIndexBuffer / vkCmdBindVertexBuffers / vkCmdBindIndexBuffer | Vertex/index buffer binding used by Draw. |
| 6 | ⬜️ | SetViewport / SetScissor / Dynamic state | RSSetViewports / RSSetScissorRects / vkCmdSetViewport / vkCmdSetScissor | Dynamic rasterizer state changes. |
| 7 | ⬜️ | PushConstants / SetRootConstants | SetGraphicsRoot32BitConstant / vkCmdPushConstants | Small inline constants per-draw. |
| 7 | ⬜️ | SetBlendConstants / DepthBias / StencilMasks | OMSetBlendFactor / vkCmdSetBlendConstants etc. | Small dynamic pipeline state changes. |
| 8 | ⬜️ | Event / SetEvent / WaitEvents | SetEvent/WaitFor (CPU/GPU) / vkCmdSetEvent / vkCmdWaitEvents | Finer-grained GPU events and host signals/waits. |
| 9 | ⬜️ | Misc advanced: mesh tasks, ray-tracing dispatch | DrawMeshTasks / DispatchRays / VK_ray_tracing commands | Advanced features—defer until core model is stable. |

Notes

- Status: ✅ implemented in headless today; ⬜️ missing or partial and suggested for implementation.
- Priority guidance: 1 = immediate (readback / sync / present), 2 = compute / transfers,
  3 = draw / renderpass, 4 = indirect / bundles, 5 = queries / clears, 6 = binding / dynamic state,
  7-9 = advanced features.

The following sections contain detailed command entries (purpose, API mapping,
headless semantics and the minimal `CommandContext` fields needed to execute
them).

CopyBuffer / CopyBufferCommand

- Purpose: copy raw bytes between two buffers.
- Mapping: D3D12 CopyBufferRegion; vkCmdCopyBuffer.
- Headless semantics: synchronous CPU-backed copy using `Buffer::ReadBacking()`/
  `WriteBacking()`. Implemented.
- Minimal `CommandContext` needs: `detail::ResourceStateTracker*` (optional for
  validation), `submission_id` for logging.

CopyTextureToBuffer / CopyTextureRegion

- Purpose: read back texture subresource(s) into a linear buffer for CPU access.
- Mapping: D3D12 CopyTextureRegion; vkCmdCopyImageToBuffer.
- Headless semantics: compute per-row or per-block offsets using the texture
  layout strategy, read texture backing and write into target buffer backing
  honoring row and slice pitches.
- Minimal `CommandContext` needs: `ResourceRegistry*` if buffer is referenced
  via descriptor, `detail::ResourceStateTracker*`, `submission_id`.

BufferToTextureCommand

- Purpose: upload buffer data into a texture.
- Mapping: CopyBufferRegion->subresource; vkCmdCopyBufferToImage.
- Headless semantics: implemented; uses texture layout strategy to compute
  offsets and writes rows/blocks into `Texture::WriteBacking()`.
- Minimal `CommandContext` needs: `detail::ResourceStateTracker*`,
  `submission_id`.

CopyTextureToTexture

- Purpose: image-to-image copy between textures or subresources.
- Mapping: CopySubresourceRegion / vkCmdCopyImage.
- Headless semantics: compute offsets for src/dst subresources and copy bytes; handle overlapping copies carefully.
- Minimal CommandContext needs: `ResourceStateTracker*`, `submission_id`.

Resolve / Blit

- Purpose: MSAA resolve or format conversion blit operations.
- Mapping: ResolveSubresource / vkCmdResolveImage / vkCmdBlitImage.
- Headless semantics: read multisampled backing if simulated, perform sample resolve (average or simple pick) and write into single-sample target; for blit perform per-pixel conversion if needed.
- Minimal CommandContext needs: `ResourceStateTracker*`, `submission_id`.

ClearColorImage / ClearDepthStencilImage / ClearAttachments

- Purpose: clear image data for attachments or whole images.
- Mapping: ClearRenderTargetView / vkCmdClearColorImage / vkCmdClearDepthStencilImage / vkCmdClearAttachments.
- Headless semantics: write clear pattern into texture backing; for DSV implement full-depth/stencil backing mutation.
- Minimal CommandContext needs: `submission_id`, `ResourceStateTracker*`.

ResourceBarrierCommand / PipelineBarrier

- Purpose: enforce resource transitions, memory visibility and ordering.
- Mapping: D3D12 ResourceBarrier; vkCmdPipelineBarrier.
- Headless semantics: validate recorded 'before' matches observed; update observed state to 'after'. The executor must enforce recorded transitions; it must not invent implicit transitions.
- Minimal CommandContext needs: `ResourceStateTracker*` (required), `submission_id`.

BeginRenderPass / EndRenderPass

- Purpose: begin/end render passes with load/store ops and subpass attachments.
- Mapping: vkCmdBeginRenderPass / vkCmdEndRenderPass; D3D12 uses OMSetRenderTargets + explicit barriers.
- Headless semantics: set current framebuffer state in CommandContext, apply load operations (clears or preserve), ensure subsequent clears/draws target the pass attachments.
- Minimal CommandContext needs: `graphics*` (to access surfaces), `ResourceStateTracker*`, `submission_id`.

DrawCommand / DrawIndexedCommand

- Purpose: output primitives to bound render targets.
- Mapping: Draw/DrawIndexed / vkCmdDrawIndexed.
- Headless semantics: validate PSO and bound vertex/index buffers, resolve descriptors, and produce deterministic fallback output (constant color fill or trivial shading) into render target backing.
- Minimal CommandContext needs: `DescriptorResolver*`, `ResourceStateTracker*`, `submission_id`, `queue`.

DispatchCommand

- Purpose: execute compute workloads.
- Mapping: Dispatch / vkCmdDispatch.
- Headless semantics: optional simple compute emulation (e.g., element-wise transform) or validation-only; update buffer backings as needed.
- Minimal CommandContext needs: `DescriptorResolver*`, `ResourceStateTracker*`, `submission_id`.

ExecuteIndirect / DrawIndirect / DispatchIndirect

- Purpose: perform indirect draws/dispatch reading parameters from buffers.
- Mapping: ExecuteIndirect / vkCmdDrawIndirect / vkCmdDispatchIndirect.
- Headless semantics: read parameters from the specified buffer backing at execute time, then perform Draw/Dispatch using those parameters.
- Minimal CommandContext needs: `DescriptorResolver*`, `ResourceStateTracker*`, `submission_id`.

Secondary/Bundle execution

- Purpose: execute secondary or bundled commands from a primary submission.
- Mapping: ExecuteBundle / vkCmdExecuteCommands.
- Headless semantics: executor must expand secondary buffers' command lists and execute their commands inline maintaining ordering.
- Minimal CommandContext needs: same as top-level execution context; `submission_id`.

QueryBegin / QueryEnd / ResolveQuery

- Purpose: gather GPU profiling counters (timestamps, occlusion).
- Mapping: BeginQuery/EndQuery; vkCmdWriteTimestamp; vkCmdCopyQueryPoolResults.
- Headless semantics: record timestamp or increment counters at execution time and write results into query pool storage; provide GetResults API.
- Minimal CommandContext needs: `QueryPoolManager*`, `submission_id`.

UpdateBuffer / FillBuffer / UpdateSubresource

- Purpose: small CPU-driven updates to buffers (helper paths).
- Mapping: UpdateSubresource / vkCmdUpdateBuffer / vkCmdFillBuffer.
- Headless semantics: directly write into buffer backing; can be implemented synchronously.
- Minimal CommandContext needs: `submission_id`.

BindDescriptorSets / SetDescriptorHeaps / RootDescriptorTable

- Purpose: bind resource descriptor tables for shader access.
- Mapping: SetGraphicsRootDescriptorTable / vkCmdBindDescriptorSets.
- Headless semantics: recorder stores descriptor updates; executor/resolver must map descriptor indices to `NativeObject` pointers when a draw/dispatch executes.
- Minimal CommandContext needs: `DescriptorResolver*`, `submission_id`.

SetVertexBuffers / SetIndexBuffer

- Purpose: bind input assembly buffers for draws.
- Mapping: IASetVertexBuffers / vkCmdBindVertexBuffers.
- Headless semantics: store references to buffer backings; Draw will use them to validate and (optionally) read vertex data.
- Minimal CommandContext needs: `DescriptorResolver*` (if vertex buffers via descriptors), `submission_id`.

SetViewport / SetScissor / Dynamic state

- Purpose: update rasterizer dynamic state.
- Mapping: RSSetViewports / vkCmdSetViewport.
- Headless semantics: store dynamic state in CommandContext so draws use the current viewport/scissor.
- Minimal CommandContext needs: `submission_id`.

PushConstants / SetRootConstants

- Purpose: provide small immediate constants to shaders.
- Mapping: SetGraphicsRoot32BitConstant / vkCmdPushConstants.
- Headless semantics: record value in command list state and make available to Draw emulation.
- Minimal CommandContext needs: `submission_id`.

Event / SetEvent / WaitEvents / TimelineSemaphore

- Purpose: GPU-side events and host-visible timeline semaphores for cross-queue sync.
- Mapping: SetEvent/WaitEvents / vkCmdSetEvent/vkCmdWaitEvents / timeline semaphores.
- Headless semantics: timeline/semaphore object with atomic value + condition_variable; QueueSignal/QueueWait commands interact with these objects. Currently implemented via LambdaCommand calling queue APIs; prefer explicit command objects and TimelineSemaphore type.
- Minimal CommandContext needs: `graphics*` (to find queue/timeline), `submission_id`.

PresentCommand

- Purpose: mark a surface image as presented and advance frame ownership.
- Mapping: Present / vkQueuePresentKHR.
- Headless semantics: record which image index was presented; optionally copy the image backing into an externally-accessible buffer for test assertions.
- Minimal CommandContext needs: `graphics*`, `submission_id`.

Misc advanced: mesh tasks, ray-tracing dispatch

- Purpose: advanced pipeline features (mesh shaders, ray tracing).
- Mapping: DX12/RTX specific APIs; Vulkan ray-tracing extension.
- Headless semantics: defer until core command model is stable; implement only if required by tests.
- Minimal CommandContext needs: `DescriptorResolver*`, `submission_id`.

### Minimal CommandContext data contract (execution-time)

Design goal: include only the minimum data the headless `Execute()`
implementations need to perform their work, validate usage, and resolve
resources. Keep the data small and explicit so the executor stays
deterministic and testable.

Proposed `CommandContext` (lean, reuse existing common types):

- `oxygen::graphics::Graphics* graphics` (observer pointer)
  - Access to device-scoped services: `GetDescriptorAllocator()`,
    `GetResourceRegistry()`, `GetShader()` and queue factories. Use only for
    short-lived queries during command execution; do not assume ownership.
- `oxygen::graphics::CommandQueue* queue` (observer pointer)
  - The queue executing this submission. Use `QueueSignalCommand`/`QueueWaitCommand`
    or `Signal()/Wait()` helpers on the queue. The executor must ensure the
    queue outlives the execution window.
- `uint64_t submission_id` (monotonic per-submission id)
  - Deterministic id used for logging, correlating timestamps/queries and
    test assertions.
- `oxygen::graphics::ResourceRegistry* registry` (observer pointer)
  - Reuse the existing `ResourceRegistry` for descriptor/view resolution.
    Commands can call `registry->Find(...)` or the registry helpers to obtain
    `NativeObject` view objects. The registry is thread-safe for lookup.
- `oxygen::graphics::detail::ResourceStateTracker* state_tracker` (observer pointer)
  - Reuse the existing tracker to validate and record resource state
    transitions. Prefer calling `GetPendingBarriers()` / `RequireResourceState()`
    and let the executor flush/validate barriers.
- `QueryPoolManager* queries` (observer pointer, optional)
  - Optional headless query pool manager (when implemented) used by
    timestamp/occlusion commands.

Notes / contracts:

- Keep the `CommandContext` minimal: store only non-owning pointers and
  primitives. Construct a fresh context per submission in the `CommandExecutor`.
- Do not re-implement descriptor resolution: use `ResourceRegistry` and
  `DescriptorHandle` APIs from `Common`.
- `ResourceRegistry` lookups are thread-safe; resource lifetime is managed by
  the registry for registered views. Commands must ensure referenced resources
  remain valid for the duration of execution (executor responsibility).
- `ResourceStateTracker` is the authoritative place for validating and
  updating resource states; the executor should consult it before executing
  commands and apply/flush barriers as needed.
- Keep blocking waits explicit and documented: calling `queue->Wait()` may
  block the executing thread; prefer timeline signal/wait command objects when
  cross-queue ordering is required.

Lifetime & cancellation contracts

- Execution model assumption: each `CommandQueue` has a dedicated worker
  thread (the `SerialExecutor`) that executes submissions in-order. The
  `CommandExecutor` constructs a fresh `CommandContext` for each submission
  and passes it to every `Command::Execute(ctx)` in that submission.

- Observer pointer lifetimes: all pointers inside `CommandContext` are
  non-owning. The executor guarantees that objects referenced by the context
  (device `Graphics`, `CommandQueue`, `ResourceRegistry`, `ResourceStateTracker`,
  and optional `QueryPoolManager`) remain alive for the duration of the
  submission execution unless a cancellation is requested.

- Cancellation semantics (must-stop-now):
  - The `CommandExecutor` exposes a cancellation token that may be triggered
    by the owner (for example, on device shutdown, test teardown, or when a
    submission must be aborted).
  - When cancellation is requested, the executor MUST immediately stop
    executing further commands in the current submission and return control to
    the thread that drives the executor. No further calls into
    `Command::Execute()` may be made after cancellation completes.
  - Commands must be written defensively: they must not hold long-lived
    pointers derived from the context beyond the scope of `Execute()` and
    should avoid performing non-interruptible blocking waits. If a command
    needs to perform a long operation, it must periodically poll the
    executor's cancellation token and abort early if cancellation is set.

- Post-cancellation state guarantees:
  - On cancellation, the executor is responsible for leaving the global state
    in a consistent and documented state. Minimal guarantees:
    - No partial writes that would break resource invariants (if partial
      writes are possible, they must be rolled back or the resource left in a
      well-documented transitional state). Prefer making changes atomic at the
      command granularity where feasible.
    - Resource trackers (`ResourceStateTracker`) must be left consistent: if
      a command started a state transition and did not finish, the executor
      must either roll back the transition or mark the tracked state as
      indeterminate so subsequent submissions can detect and recover.

- Shutdown ordering recommendations:
  - Request cancellation for all executors first.
  - Join per-queue worker threads and drain any short-lived queues.
  - Then destroy global objects (`Graphics`, `ResourceRegistry`, `DescriptorAllocator`),
    ensuring no executor thread may still reference them.

- Debugging and assertions:
  - In debug builds, the executor should assert that cancellation has been
    observed by any command that took longer than an implementation-defined
    threshold. Instrumentation (trace logs and submission ids) should be used
    to correlate partially-executed submissions for troubleshooting.

Notes

- Keep the `CommandContext` light: store only non-owning pointers and
  primitives. Avoid copying heavy structures into the context.
- The executor constructs a fresh `CommandContext` per submission, sets
  `submission_id` to a deterministic counter and passes the same context to
  each `Command::Execute(ctx)` in that submission.
