# Graphics Backend Integration Guide

## Table of contents

- [Purpose](#purpose)
- [Quick contract](#quick-contract-inputs--outputs--success)
- [Core classes you must implement](#core-classes-you-must-implement)
- [Descriptor Allocation Strategy](#descriptor-allocation-strategy)
- [Commander Component and Command Coordination](#commander-component-and-command-coordination)
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
  `ResourceRegistry` integration, a working `Commander` component for command
  coordination, and a working `RenderController` that can record & submit GPU
  work with both immediate and deferred submission modes.
- Success criteria: the backend compiles, creates a `Graphics` instance, creates
  resources, records commands via `CommandRecorder` with flexible submission
  modes, submits work to `CommandQueue` and presents (for surface-backed
  surfaces). Unit tests in `Graphics/Common/Test` should pass (where
  backend-specific tests exist).

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
    `CreateCommandListImpl()`, `CreateCommandRecorder()`.
  - The `Commander` component is automatically added and handles command
    recording coordination and deferred submission.

- `CommandQueue` (`src/.../CommandQueue.h`)
  - Implement signaling, waiting, GPU-side signal/waits, `Submit()` overloads
    and queue role reporting.

- `CommandList` and `CommandRecorder` (`src/.../CommandList.h`,
  `CommandRecorder.h`)
  - `CommandList` may remain a thin wrapper; `CommandRecorder` is the main
    recording API and must translate the engine's commands (`SetPipelineState`,
    `Draw`, `Dispatch`, `SetViewport`, `BindFrameBuffer`, resource barriers,
    etc.) into native API calls. Implement `ExecuteBarriers()`.
  - Command recorders now support flexible submission modes (immediate vs
    deferred) managed by the `Commander` component.

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

## Commander Component and Command Coordination

The `Commander` component is a central addition to the graphics backend
architecture that manages command recording coordination and submission
strategies.

### Key Features

- **Automatic Integration**: The `Commander` component is automatically added to
  the `Graphics` composition during construction. No manual setup required.
- **Flexible Submission Modes**: Supports both immediate and deferred command
  submission through `AcquireCommandRecorder()`.
- **Resource Lifecycle Management**: Integrates with `DeferredReclaimer` to
  ensure proper command list lifecycle transitions and resource cleanup.
- **Thread-Safe Coordination**: Provides thread-safe management of pending
  command submissions across multiple threads.

### New Graphics APIs

Backend implementations must provide the following new APIs:

#### `AcquireCommandRecorder()`

```cpp
virtual auto AcquireCommandRecorder(
  observer_ptr<graphics::CommandQueue> queue,
  std::shared_ptr<graphics::CommandList> command_list,
  bool immediate_submission = true)
-> std::unique_ptr<graphics::CommandRecorder,
   std::function<void(graphics::CommandRecorder*)>>;
```

Creates a command recorder with automatic submission coordination:

- **Immediate mode** (`immediate_submission = true`): Commands are submitted to
  the GPU immediately when the recorder is destroyed.
- **Deferred mode** (`immediate_submission = false`): Commands are batched and
  submitted later via `SubmitDeferredCommandLists()`.

#### `SubmitDeferredCommandLists()`

```cpp
auto SubmitDeferredCommandLists() -> void;
```

Submits all pending deferred command lists to their respective queues. Provides
efficient batching for scenarios where multiple command lists need coordinated
submission.

#### `CreateCommandRecorder()` (Pure Virtual)

```cpp
virtual auto CreateCommandRecorder(
  std::shared_ptr<graphics::CommandList> command_list,
  observer_ptr<graphics::CommandQueue> target_queue)
-> std::unique_ptr<graphics::CommandRecorder> = 0;
```

Backend factory method that creates a native command recorder. The `Commander`
component wraps this with appropriate lifecycle management.

### Usage Patterns

- Use **immediate submission** for simple command sequences that should execute
  right away.
- Use **deferred submission** when you need to batch multiple command lists for
  optimized submission or when coordinating complex multi-queue operations.
- Call `SubmitDeferredCommandLists()` at frame boundaries or other strategic
  points to flush pending work.

### Implementation Notes

- The `Commander` relies on `DeferredReclaimer` for proper resource lifecycle
  management, ensuring `OnExecuted()` callbacks are called at appropriate frame
  boundaries.
- Backends should focus on implementing `CreateCommandRecorder()` rather than
  managing submission logic directly.
- The component handles all synchronization and error handling for command
  submission coordination.

## Optional / advanced

- `IMemoryBlock` / memory allocator: implement `IMemoryBlock` and a memory
  manager if you need custom memory pooling.
- Bindless and root signatures: implement bindless root signature creation, root
  binding tables, and pipeline-state setup consistent with `PipelineState`
  design docs in `design/BindlessRenderingDesign.md`.
- PSO caching: implement backend pipeline state object caching keyed by
  `PipelineState` descriptions.
- Custom `Commander` extensions: while the base `Commander` component is
  automatically provided, backends can extend command coordination behavior if
  needed for specialized submission strategies.

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
- The `Commander` component manages command recorder lifecycle and submission
  coordination automatically. Backend implementations should focus on the
  `CreateCommandRecorder()` factory method rather than manual submission logic.
- Command recorders support both immediate and deferred submission modes.
  Backends must implement `CreateCommandRecorder()` to return recorders that
  work with the `Commander`'s submission coordination.

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
- Command submission modes: the new `AcquireCommandRecorder()` API supports both
  immediate and deferred submission. Use immediate mode for simple command
  sequences and deferred mode when you need to batch multiple command lists for
  optimized submission.
- Commander integration: the `Commander` component automatically handles command
  recorder lifecycle and deferred submission coordination. Don't manually manage
  command list submission unless you have specific requirements.

## Build / integration checklist

- Create a backend module/library (e.g., `Oxygen.Graphics.D3D12`) with its own
  CMake target. Export a C-style loader entrypoint that returns a
  `GraphicsModuleApi` with `CreateBackend`/`DestroyBackend` (see
  `BackendModule.h`).
- Backend should call `Graphics::SetDescriptorAllocator()` after creating the
  native device and before creating any controllers.
- Implement the `CreateCommandRecorder()` pure virtual method to return
  backend-specific command recorders that work with the `Commander` component's
  submission coordination.
- The `Commander` component is automatically added to the `Graphics` composition
  and handles command recording lifecycle and deferred submission. No manual
  setup required.
- Register your backend in the engine loader so tests/examples can instantiate
  it using config.

## Testing & validation

- Start with small smoke tests:
  - Create a `Graphics` instance, call `CreateBuffer`/`CreateTexture`, allocate
    descriptors, and create simple SRV/UAV/RTV views.
  - Create a `CommandRecorder` using `AcquireCommandRecorder()`, record a
    trivial clear/draw, and verify both immediate and deferred submission modes
    work correctly.
  - Test `SubmitDeferredCommandLists()` functionality to ensure batched
    submission works as expected.
  - Create a `Surface` + `RenderController` and present a cleared backbuffer.
- Run unit tests under `Graphics/Common/Test` where applicable and add
  backend-specific tests to validate native views, descriptor copying, barrier
  behavior, and command submission modes.

## Next steps / further reading

- Review design docs in `design/BindlessRenderingDesign.md`,
  `BindlessRenderingRootSignature.md` and `PipelineDesign.md`.
- Inspect `Examples/D3D12-Renderer` for a sample backend wiring and sample usage
  patterns.
