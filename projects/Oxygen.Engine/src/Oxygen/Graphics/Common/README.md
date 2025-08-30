# Graphics Backend Integration Guide

## Table of contents

- [Purpose](#purpose)
- [Quick contract](#quick-contract-inputs--outputs--success)
- [Core classes you must implement](#core-classes-you-must-implement)
- [Descriptor Allocation Strategy](#descriptor-allocation-strategy)
- [Queue Management Strategy](#queue-management-strategy)
- [Optional / advanced](#optional--advanced)
- [Important invariants & engine expectations](#important-invariants--engine-expectations)
- [Practical tips and gotchas](#practical-tips-and-gotchas)
- [Build / integration checklist](#build--integration-checklist)
- [Testing & validation](#testing--validation)
- [Next steps / further reading](#next-steps--further-reading)

## Purpose

This directory defines the backend-agnostic device and resource APIs used by the
Oxygen engine. A concrete graphics backend (for example Direct3D12 or Vulkan)
implements these interfaces and hooks them into the engine. This README lists
the required contracts, the classes you must implement or extend, integration
steps, important invariants, and practical tips to build a working backend.

## Quick contract (inputs / outputs / success)

- Inputs: native device/adapter handles provided by the platform (DXGI adapter,
  VkPhysicalDevice), and the engine configuration (`SerializedBackendConfig`).
- Outputs: a concrete `Graphics`-derived device, backend-specific
  `CommandQueue`/`CommandList`/`CommandRecorder`, concrete
  `Buffer`/`Texture`/`Sampler` implementations, a `DescriptorAllocator` and
  `ResourceRegistry` integration, and a working `RenderController` that can
  record & submit GPU work.
- Success criteria: the backend compiles, creates a `Graphics` instance, creates
  resources, records commands via `CommandRecorder`, submits work to
  `CommandQueue` and presents (for surface-backed surfaces). Unit tests in
  `Graphics/Common/Test` should pass (where backend-specific tests exist).

## Core classes you must implement

Implement concrete, backend-specific subclasses for at least the following
(headers referenced):

- `Graphics` (`src/.../Graphics.h`)
  - Implement device creation/initialization.
  - Implement `SetDescriptorAllocator()` usage and install a backend
    `DescriptorAllocator`.
  - Implement factory hooks: `CreateSurface()`, `CreateTexture()`,
    `CreateTextureFromNativeObject()`, `CreateBuffer()`,
    `CreateRenderController()`, `CreateCommandQueue()`,
    `CreateCommandListImpl()`.

- `CommandQueue` (`src/.../CommandQueue.h`)
  - Implement signaling, waiting, GPU-side signal/waits, `Submit()` overloads
    and queue role reporting.

- `CommandList` and `CommandRecorder` (`src/.../CommandList.h`,
  `CommandRecorder.h`)
  - `CommandList` may remain a thin wrapper; `CommandRecorder` is the main
    recording API and must translate the engine's commands (`SetPipelineState`,
    `Draw`, `Dispatch`, `SetViewport`, `BindFrameBuffer`, resource barriers,
    etc.) into native API calls. Implement `ExecuteBarriers()`.

- `DescriptorAllocator` (`src/.../DescriptorAllocator.h`) and allocation
  strategy
  - Provide descriptor heap/pool management compatible with the engine's
    bindless expectations. Implement `Allocate` / `Release` / `CopyDescriptor` /
    `GetShaderVisibleIndex` / `Reserve` / `GetDomainBaseIndex`.

- Resource types: `Buffer`, `Texture`, `Sampler`, `Framebuffer` (`Buffer.h`,
  `Texture.h`, `Sampler.h`)
  - Implement native resource creation, `GetNativeResource()`,
    `GetDescriptor()`, and the various Create*View helpers
    (`CreateShaderResourceView`, `CreateUnorderedAccessView`,
    `CreateRenderTargetView`, `CreateDepthStencilView`, etc.).
  - Implement CPU mapping, `Update()`, and GPU virtual address if supported for
    `Buffer`.

- `ShaderCompiler` and `IShaderByteCode` (`ShaderCompiler.h`,
  `ShaderByteCode.h`)
  - Provide a compiler that produces `IShaderByteCode` wrappers compatible with
    backend (DXIL/SPIR-V). Shader manager expects to be able to
    `GetShaderBytecode()` by unique id.

- `Surface` / `WindowSurface` (`Surface.h` and detail `WindowSurface`)
  - Implement swapchain/backbuffer creation, `Present()`, and backbuffer
    queries. Ensure deferred per-renderer allocation behavior described in
    `Framebuffer` comments is respected (create swapchain views only when a
    renderer attaches).

- `RenderController` (`RenderController.h`)
  - Implement `CreateCommandRecorder()` factory that returns backend
    `CommandRecorder` instances. Coordinate command list submission, per-frame
    resource management, and presentation.

## Descriptor Allocation Strategy

Descriptor allocation is critical for bindless resource usage and stable
shader-visible indices. Backends must provide a `DescriptorAllocator`
implementation that satisfies the engine contract and the following guidelines.

### Design goals

- Stable shader-visible indices: descriptors exposed to shaders must map to
  stable indices that do not change while a resource is in use by the GPU.
- Low-fragmentation allocation: allocate descriptors in pools or heaps to avoid
  heavy per-frame allocation work and GPU-visible rebind storms.
- Efficient copy/duplication: provide fast `CopyDescriptor` operations that are
  compatible with the native API (for example `CopyDescriptors` on D3D12).

### API responsibilities

- Allocate/Release: provide range-based allocation primitives that return a
  compact, stable handle (index or descriptor handle) and accept release of
  previously reserved slots.
- Reserve: support reserving a block of indices for preallocated usage (for
  example when the engine wants to ensure a domain has a contiguous range).
- GetShaderVisibleIndex: convert a local descriptor handle to a shader-visible
  index usable by the root signature or descriptor table.
- CopyDescriptor: perform fast backend-native descriptor copies between
  allocated slots.

### Headless AllocationStrategy and bindless layout

The headless backend provides a ready-to-use `AllocationStrategy` used by the
headless `DescriptorAllocator` to create deterministic, generous heap layouts
suitable for tests and simulators. See
`src/Oxygen/Graphics/Headless/Bindless/AllocationStrategy.h` for details.

### Key points about the headless `AllocationStrategy`

- It computes contiguous base indices for each view type and visibility category
  (CPU-only vs shader-visible) using a stable insertion order so test runs
  produce repeatable descriptor layouts.
- RTV/DSV view types are intentionally marked CPU-only (shader-visible capacity
  = 0) to avoid creating unnecessary shader-visible ranges in the test
  environment.
- Use this allocation strategy in headless tests or simulators where generous,
  deterministic descriptor capacity is desirable.

Backends should document how their `DescriptorAllocator` maps engine view types
to native heap/pool types and whether they use an `AllocationStrategy` to derive
base indices.

### Suggested implementation patterns

- D3D12: use CPU-visible descriptor heaps for allocation and use a single
  shader-visible heap per heap-type for GPU-visible indices; map local indices
  to a shader-visible table index. Implement a free-list for released ranges to
  reduce fragmentation.
- Vulkan: implement descriptor pool(s) per descriptor type or use a slab
  allocator with preallocated descriptor sets; provide an index translation
  layer for shader-visible binding indices.

### Performance & safety

- Prefer batch operations: allocate & commit descriptors in batches to avoid
  per-descriptor GPU work.
- Keep descriptor lifetime tied to engine semantics (frame allocator vs
  persistent resource). Document how the allocator handles descriptors that
  remain live across frames.
- Validate index ranges in debug builds and assert when copies or gets are
  requested for unallocated slots.

### Migration / fallback policies

- When native limits are reached (for example limited number of descriptor sets
  or shader-visible slots), document whether your backend will:
  - fallback to a shared descriptor table and copy views on-demand, or
  - return an error/throw, or
  - expand a software-backed table that emulates larger index spaces.

## Queue Management Strategy

Provide a predictable, reusable mapping from the engine's logical queue requests
(role + allocation/sharing preferences) to a finite set of backend queue
instances. The guidance below is backend-agnostic and applies to device backends
(D3D12, Vulkan) and simulators.

### Key principles

- Reuse: allocate backend queue objects once and return shared handles on
  subsequent `CreateCommandQueue` calls. Avoid creating a new object every
  request.
- Deterministic mapping: map logical roles (graphics/compute/transfer/present)
  to a small set (pool) of backend queues. On real backends this maps to device
  queue families/types; backends should document how roles map to the physical
  queue topology they expose.
- First-creation-wins for naming: preserve the `queue_name` provided when the
  cached queue instance is first created. Subsequent requests that map to the
  same cached queue should return the existing instance and log conflicting
  names rather than silently overwrite.

### Suggested mapping algorithm

- `QueueAllocationPreference::kAllInOne`
  - Create (on first request) a single universal queue instance and return it
    for all roles.
- `QueueAllocationPreference::kDedicated`
  - Create (on first request) one cached queue per `QueueRole` and return the
    per-role instance for requests that ask for a dedicated queue.
  - `QueueSharingPreference::kShared` permits reusing a cached queue across
    roles when appropriate; `kSeparate` requests that roles be served by
    separate cached queues.

### Small example mapping (concrete)

This example shows a compact, deterministic mapping you can implement in a
backend when the hardware exposes only a small number of queue families.

- Hardware: a device with a single graphics-capable family that supports compute
  and transfer on the same queue family.
- Mapping rules:
  - `kAllInOne` → create one universal queue (Graphics role) and reuse it.
  - `kDedicated` + Role::kGraphics -> cached graphics queue.
  - `kDedicated` + Role::kCompute -> cached compute queue (may be alias to
    graphics queue if hardware doesn't provide a separate Compute queue).
  - Named requests: if a name is supplied when creating the queue and the name
    was previously used, return the named instance (name-first policy).

This mapping keeps behavior predictable on constrained hardware while
documenting fallbacks when role-specific queues are not available.

### Thread-safety and lifetime

- Protect cache creation with a mutex to allow concurrent `CreateCommandQueue`
  calls from multiple threads.
- Return `std::shared_ptr<CommandQueue>`; keep cached handles as `shared_ptr` so
  clients may hold references safely while the backend maintains its cache.

### Behavior choices and trade-offs

- Real backends have hardware limits (number of queues per family). Backends
  should document how they behave when the requested allocation/sharing
  preferences cannot be satisfied (for example, fallback to a shared queue or
  throw/return an error). Simulators may always satisfy `kDedicated` but should
  still document their policy.
- If an API consumer truly needs a unique, never-cached queue instance, expose a
  distinct flag or API; do not change the default cached semantics.

### How `QueueSpecification` drives backend queue mapping

Higher-level queue selection is performed by creating `QueueSpecification`
objects (used by `QueueStrategy` implementations). Each `QueueSpecification`
typically contains at least these fields:

- `name` (string): an application-visible identifier. When present, the backend
  should prefer returning a cached queue associated with this name (name-first
  policy). Naming allows explicit sharing between different `QueueRole` entries
  in higher-level strategies.
- `role` (QueueRole): logical role requested (Graphics, Compute, Transfer,
  Present). The backend may map roles to hardware queue families or to per-role
  cached queues.
- `allocation_preference` (QueueAllocationPreference): `kAllInOne` or
  `kDedicated` — drives the creation of universal vs per-role queues.
- `sharing_preference` (QueueSharingPreference): hints whether this spec should
  be shared (`kShared`) or kept separate (`kSeparate`) when other specs use the
  same role or name.

### How the backend should use these fields

- If `name` is non-empty and a queue with that name exists, return the existing
  instance and log any name/role conflicts rather than silently overwriting
  (first-creation-wins for names).
- If `allocation_preference` == `kAllInOne`, the backend may return a single
  universal queue instance for all roles; if a name is supplied on first
  creation register that name for later lookups.
- If `allocation_preference` == `kDedicated`, use or create a per-role cached
  queue. If the backend cannot provide a distinct per-role queue due to hardware
  limits, document the fallback (for example returning the graphics queue or a
  shared queue).
- `sharing_preference` can be used by higher-level strategies to decide when to
  reuse names vs create separate specs; backends only need to honor this via
  their name/role mapping semantics.

### Example

```cpp
// Strategy builds specs
QueueSpecification a { "main-gfx", QueueRole::kGraphics, kDedicated, kSeparate };
QueueSpecification b { "main-gfx", QueueRole::kCompute, kDedicated, kShared };

// Backend CreateCommandQueue("main-gfx", Role::kCompute, kDedicated)
// will return the previously created named queue if 'main-gfx' exists.
```

## Optional / advanced

- `IMemoryBlock` / memory allocator: implement `IMemoryBlock` and a memory
  manager if you need custom memory pooling.
- Bindless and root signatures: implement bindless root signature creation, root
  binding tables, and pipeline-state setup consistent with `PipelineState`
  design docs in `design/BindlessRenderingDesign.md`.
- PSO caching: implement backend pipeline state object caching keyed by
  `PipelineState` descriptions.

## Important invariants & engine expectations

- Descriptor allocator must be installed exactly once per `Graphics` instance
  (see `Graphics::SetDescriptorAllocator`).
- Descriptor handles produced by `DescriptorAllocator` are treated as stable
  indices by shaders. `GetShaderVisibleIndex` must return the local index usable
  by descriptor tables bound for shaders.
- `ResourceRegistry` expects `GetNativeView()` to produce native, short-lived
  view objects that can be stored in the registry. When replacing resources, the
  registry may ask you to recreate views in-place; keep descriptor slots stable.
- `CommandRecorder` implementations must be thread-safe where specified by the
  engine (the recorder is typically used by a single render thread but created
  on the render controller's thread). Follow the `SubmissionMode` semantics.
- `Framebuffer` must provide native RTV/DSV lists compatible with the pipeline's
  `FramebufferInfo`.

## Practical tips and gotchas

- Descriptor spaces: map engine `ResourceViewType` and `DescriptorVisibility`
  cleanly to D3D12 descriptor heaps or Vulkan descriptor pool layouts. Keep the
  global base indices stable.
- Root signature / descriptor tables: prefer creating a bindless root signature
  once per PSO or device, then reuse it.
- Synchronization: implement robust CPU-side fences and GPU timeline support for
  `CommandQueue::Signal`/`Wait` and the `CommandQueue::GetCompletedValue`
  semantics.
- Resource transitions: follow the engine's `ResourceStates` mapping; implement
  barriers carefully and minimize redundant barriers for performance.
- Debugging: provide helpful debug-name support for native objects (use the name
  from `ObjectMetaData`), and validate descriptor and view compatibility early
  with asserts.

## Build / integration checklist

- Create a backend module/library (e.g., `Oxygen.Graphics.D3D12`) with its own
  CMake target. Export a C-style loader entrypoint that returns a
  `GraphicsModuleApi` with `CreateBackend`/`DestroyBackend` (see
  `BackendModule.h`).
- Backend should call `Graphics::SetDescriptorAllocator()` after creating the
  native device and before creating any controllers.
- Register your backend in the engine loader so tests/examples can instantiate
  it using config.

## Testing & validation

- Start with small smoke tests:
  - Create a `Graphics` instance, call `CreateBuffer`/`CreateTexture`, allocate
    descriptors, and create simple SRV/UAV/RTV views.
  - Create a `CommandRecorder`, record a trivial clear/draw, submit it to a
    `CommandQueue`, and wait for completion.
  - Create a `Surface` + `RenderController` and present a cleared backbuffer.
- Run unit tests under `Graphics/Common/Test` where applicable and add
  backend-specific tests to validate native views, descriptor copying, and
  barrier behavior.

## Next steps / further reading

- Review design docs in `design/BindlessRenderingDesign.md`,
  `BindlessRenderingRootSignature.md` and `PipelineDesign.md`.
- Inspect `Examples/D3D12-Renderer` for a sample backend wiring and sample usage
  patterns.
