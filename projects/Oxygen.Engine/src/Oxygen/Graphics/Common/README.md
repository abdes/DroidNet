# Graphics Backend Integration Guide

## Table of contents

- [Purpose](#purpose)
- [Quick contract (inputs / outputs / success)](#quick-contract-inputs--outputs--success)
- [Core classes you must implement](#core-classes-you-must-implement)
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
