# Graphics Pipeline and Pipeline State

## Introduction

Modern graphics APIs like D3D12 and Vulkan use a monolithic pipeline state
object (PSO) model for rendering. A PSO encapsulates all fixed-function and
programmable state (shaders, input layouts, rasterizer, depth-stencil, blend
states, etc.) into a single, immutable object. This approach enables the driver
to validate and optimize the pipeline up front, resulting in better performance
and predictability compared to older, piecemeal state-setting APIs.

In bindless rendering, instead of binding individual resources (textures,
buffers) per draw call, large arrays of resources are made available to shaders,
which access them using indices. This greatly reduces CPU overhead and increases
flexibility and scalability for complex scenes.

## Modern Graphics Pipeline

Below is a diagram of the modern graphics pipeline. Arrows indicate which
resources (vertex buffers, index buffers, constant/uniform buffers, textures,
etc.) are accessed at each stage, closely matching the D3D12 and Vulkan models.

```
    ┌─────────────────────────────┐
    │      Memory Resources       │
    │ ─────────────────────────── │
    │ • Vertex Buffers            │
    │ • Index Buffers             │
    │ • Constant/Uniform Buffers  │
    │ • Textures                  │
    │ • Samplers                  │
    │ • UAVs/Storage Buffers      │
    └────────────┬────────────────┘
                 │
                 │
                 ▼
    ┌──────────────────────────────┐
    │      Input Assembler         │ ◄── Vertex Buffers, Index Buffers
    └────────────┬─────────────────┘
                 │
                 ▼
    ┌──────────────────────────────┐
    │        Vertex Shader         │ ◄── Constant Buffers, Textures, Samplers
    └────────────┬─────────────────┘
                 │
                 ▼
    ┌──────────────────────────────┐
    │ Tessellation Control Shader  │ ◄── Constant Buffers, Textures, Samplers
    └────────────┬─────────────────┘
                 │
                 ▼
    ┌──────────────────────────────┐
    │   Tessellation Primitive     │
    │        Generator             │
    └────────────┬─────────────────┘
                 │
                 ▼
    ┌──────────────────────────────┐
    │ Tessellation Evaluation      │ ◄── Constant Buffers, Textures, Samplers
    │         Shader               │
    └────────────┬─────────────────┘
                 │
                 ▼
    ┌──────────────────────────────┐
    │      Geometry Shader         │ ◄── Constant Buffers, Textures, Samplers
    └────────────┬─────────────────┘
                 │
                 ▼
    ┌──────────────────────────────┐
    │         Rasterizer           │
    └────────────┬─────────────────┘
                 │
                 ▼
    ┌──────────────────────────────┐
    │      Fragment Shader         │ ◄── Constant Buffers, Textures, Samplers, UAVs
    └────────────┬─────────────────┘
                 │
                 ▼
    ┌──────────────────────────────┐
    │  Color Blending, Depth/      │ ◄── Render Targets, Depth/Stencil Buffers
    │   Stencil Testing (Output    │
    │         Merger)              │
    └────────────┬─────────────────┘
                 │
                 ▼
    ┌──────────────────────────────┐
    │        Framebuffer           │
    └──────────────────────────────┘
```

**Customization Points (mapped to our design):**
- **Vertex Input Layout:** Set in `GraphicsPipelineDesc` (Input Assembler)
- **Vertex Shader:** Set in `GraphicsPipelineDesc` (Vertex Shader)
- **Tessellation Control/Evaluation Shaders:** Set in `GraphicsPipelineDesc` (optional stages)
- **Tessellation State:** Set in `GraphicsPipelineDesc` (if used)
- **Geometry Shader:** Set in `GraphicsPipelineDesc` (optional)
- **Rasterizer State:** Set in `GraphicsPipelineDesc` (Rasterizer)
- **Fragment Shader:** Set in `GraphicsPipelineDesc` (Fragment Shader)
- **Blend, Depth-Stencil, Render Target States:** Set in `GraphicsPipelineDesc` (Output Merger)
- **Framebuffer:** Set via `Framebuffer` (defines render target formats, sample counts)
- **Dynamic State:** (e.g., viewport, scissor) set at draw time, not baked into PSO

**Bindless Resource Access:**

- Resource arrays (textures, buffers) are not shown in the pipeline diagram but
  are accessible in shaders via indices, enabled by bindless descriptor
  tables/sets.

- In the current Oxygen engine implementation, bindless tables are set up and
  prepared for rendering in the `Bindless` component's `PrepareForRender(
  CommandRecorder&)` method (see
  `src/Oxygen/Graphics/Common/Detail/Bindless.h`). This is called automatically
  by the `Renderer` when you acquire a `CommandRecorder` for graphics or compute
  work (see `Renderer::AcquireCommandRecorder` in
  `src/Oxygen/Graphics/Common/Renderer.h`).

- For further details—including descriptor allocation, resource/view
  registration, and automatic table updates—see the design in
  `design/BindlessRenderingDesign.md`. This document covers the full flow from
  resource creation to bindless table updates and rendering, as well as API,
  thread safety, and backend-specific strategies.

**PSO and Bindless Workflow:**

```
[Create PSO] → [Create Resource Arrays] → [Set PSO] → [Set Resource Arrays] → [Draw]
```

This design is focused solely on modern D3D12 and Vulkan APIs, with no backward
compatibility requirements. It leverages the strengths of immutable pipeline
state and bindless resource access for maximum efficiency and flexibility in
contemporary 3D engines.

## Design Principles

The Oxygen Graphics layer is architected around a clear separation of
backend-agnostic interfaces and backend-specific implementations. The core
philosophy is to provide a modern, explicit, and type-safe API for pipeline and
resource management, while enabling high performance and flexibility across
multiple graphics APIs (such as D3D12 and Vulkan).

Key design principles for pipeline support in Oxygen:

- **Backend-Agnostic Abstraction:** The `Common` layer defines all core types,
  interfaces, and contracts for pipelines, descriptors, and resources. This
  ensures that application and engine code is written against a stable,
  API-neutral interface, maximizing portability and testability.

- **Backend Specialization via Composition:** Backend-specific implementations
  (e.g., D3D12) hook into the `Common` layer by implementing the required
  interfaces and extending the core abstractions. This is achieved through
  composition and dependency injection, not inheritance, allowing for
  backend-specific optimizations without polluting the core API.

- **Explicit, Strongly-Typed State:** Pipeline state is described using
  composable, strongly-typed descriptors (e.g., `GraphicsPipelineDesc`,
  `RasterizerStateDesc`). This approach leverages C++ features like `enum class`
  and `std::optional` for clarity, safety, and future extensibility.

- **Immutable Pipeline State Objects:** Pipeline state objects (PSOs) are
  immutable after creation, mirroring the design of modern APIs. This enables
  upfront validation and driver optimization, and ensures predictable rendering
  behavior.

- **Bindless Resource Model:** The engine uses a bindless resource model, where
  large arrays of resources (textures, buffers, etc.) are made available to
  shaders via indices. The `ResourceRegistry` and `DescriptorAllocator` manage
  registration, indexing, and descriptor table updates, abstracting away
  API-specific details while ensuring efficient access and update.

- **Thread Safety and Lifetime Management:**

  - Rendering in Oxygen is orchestrated by one or more dedicated render threads
    (see `src/Oxygen/Graphics/Common/Detail/RenderThread.h`). Each
    `RenderThread` manages its own frame lifecycle, task queue, and
    synchronization, enabling parallel and decoupled rendering workflows.

  - Multiple render threads may exist, for example to support multi-device,
    multi-window, or background rendering scenarios. Each thread operates
    independently, but all interact with the shared pipeline and resource
    management infrastructure.

  - Pipeline state objects (PSOs) themselves are immutable after creation and do
    not require synchronization for use—once constructed, they are only read
    from multiple threads. However, the process of creating, looking up, or
    caching PSOs must be thread-safe, as multiple threads may attempt to create
    or retrieve the same PSO concurrently. Synchronization is required only
    during these creation and registration phases to avoid redundant PSO
    creation and ensure correct sharing.
  - Resource lifetimes and descriptor management are handled with explicit
    ownership and thread-safe registries, ensuring correctness and predictable
    behavior even in complex, multi-threaded rendering environments.

- **Asynchronous and Scalable:**

  - The Oxygen Graphics layer is designed to support high scalability and
    parallelism through the use of C++ coroutines, with full integration via the
    OxCo coroutine library. Asynchronous operations such as pipeline creation,
    resource uploads, and descriptor updates can be expressed as coroutines,
    allowing the engine to overlap CPU and GPU work, reduce stalls, and maximize
    throughput.

  - Coroutines are particularly beneficial for scenarios such as:
    - **Asynchronous Pipeline Creation:** Creating complex PSOs (e.g., with many
      shaders or state permutations) can be offloaded to background threads or
      scheduled as coroutines, so the main render thread is never blocked
      waiting for driver compilation.

    - **Resource Streaming and Upload:** Large textures, buffers, or mesh data
      can be loaded and uploaded to the GPU incrementally, with coroutines
      yielding control while waiting for I/O or GPU fences, enabling smooth
      streaming and progressive loading.

    - **Frame Pipelining:** Multi-frame rendering workflows (e.g., multiple
      frames in flight, async readbacks, or GPU-driven pipelines) can be
      naturally expressed as coroutine chains, improving code clarity and
      reducing callback complexity.

  - This coroutine-based approach matches or exceeds the flexibility of
    best-in-class engines, enabling fine-grained scheduling, cancellation, and
    composition of asynchronous tasks, while avoiding the overhead and
    complexity of thread pools or explicit futures.

- **Extensible and Testable:**
  - The Oxygen Graphics layer is designed so that pipeline creation and
    management can be easily extended and robustly tested. Extensibility means:

    - New pipeline state types, configuration options, or backend-specific
      pipeline features (such as D3D12 root signature extensions or Vulkan
      pipeline layout flags) can be added by extending the backend-specific
      implementation, without changing the core API or breaking existing code.

    - The backend-agnostic `Common` layer defines the contracts for pipeline
      creation, caching, and lookup, while backend layers (e.g., D3D12, Vulkan)
      provide their own implementations for pipeline compilation, native handle
      management, and API-specific optimizations.

    - Subtle backend-specific requirements—such as pipeline library usage in
      D3D12, pipeline cache integration in Vulkan, or support for dynamic
      state—are handled in the backend, but the extensible design allows these
      features to be surfaced to the application if needed.

  - Testability is achieved by providing clear interfaces for pipeline creation,
    lookup, and destruction, allowing both backend-agnostic and backend-specific
    unit tests. This ensures that pipeline management logic is correct,
    race-free, and robust across all supported platforms and APIs.

- **Minimal Overhead, Maximal Control:** The API avoids hidden state and
  implicit behavior, giving advanced users full control over resource and
  pipeline management, while providing sensible defaults and helpers for common
  scenarios.

## Impact Analysis

This section analyzes the impact of the pipeline design and management changes
on the existing Oxygen codebase, identifying all affected classes, new classes
to be written, and the nature of the impact. It also provides a list of files to
change and new files to create.

### Impacted Classes and Components

- **Pipeline State Abstractions (new: src/Oxygen/Graphics/Common/PipelineState.h, .cpp)**
  - Defines the data structures and interfaces for representing pipeline state
    objects and their descriptors in a backend-agnostic way.
  - Provides the means to construct, compare, and describe pipeline state
    objects, but not the orchestration or lifecycle management.

- **Renderer (src/Oxygen/Graphics/Common/Renderer.h, .cpp)**
  - Will require updates to support new pipeline creation, caching, and lookup
    APIs, either directly or by delegating to a dedicated pipeline management
    component (e.g., `Pipelines`), similar to how bindless resources and render
    threads are managed as separate components.

  - Must implement robust pipeline state object (PSO) caching and lookup,
    leveraging native driver features:
    - For D3D12: Integrate with `ID3D12PipelineLibrary` for efficient PSO
      storage and retrieval, minimizing redundant compilation and enabling fast
      PSO reuse across frames and device restarts.
    - For Vulkan: Integrate with `VkPipelineCache` to persist and reuse pipeline
      binaries, reducing pipeline creation latency and supporting cache
      serialization/deserialization.
    - The Renderer (or the dedicated pipeline manager) must maintain a mapping
      from high-level pipeline descriptors to native PSOs, ensuring thread-safe,
      deduplicated creation and fast lookup.

  - May need to expose new interfaces for asynchronous/coroutine-based pipeline
    creation and management, allowing pipeline creation to be scheduled or
    awaited without blocking the render thread.

  - Integration with backend-specific pipeline management (e.g., D3D12, Vulkan)
    must be coordinated here, ensuring that all API-specific requirements for
    pipeline compatibility, cache usage, and resource lifetime are respected.

- **CommandRecorder (src/Oxygen/Graphics/Direct3D12/CommandRecorder.cpp, .h)**
  - Will use the new pipeline state abstraction for binding (setting) PSOs
    during command recording.
  - **Note:** CommandRecorder is not responsible for PSO creation, caching, or
    lookup. PSO creation (including coroutine-based or deferred creation) is
    handled by the Renderer or a dedicated pipeline manager. CommandRecorder
    only binds already-created PSOs as part of command recording, ensuring fast
    and predictable command buffer construction.

- **D3D12 Renderer (src/Oxygen/Graphics/Direct3D12/Renderer.h, .cpp)**
  - Handles D3D12-specific details such as pipeline state streams, root
    signature integration, and PSO creation/caching, but does not manage
    orchestration or lifecycle. This class is responsible for integrating
    D3D12-specific pipeline state utilities and providing backend-specific
    pipeline management as required by the core Renderer or pipeline manager.

- **D3D12 CommandRecorder (src/Oxygen/Graphics/Direct3D12/CommandRecorder.h, .cpp)**
  - Updated to use the new pipeline state abstraction for binding PSOs during
    command recording. No responsibility for PSO creation, caching, or lookup;
    only binds already-created PSOs provided by the Renderer or pipeline
    manager.

- **D3D12 Pipeline State Utilities (new: src/Oxygen/Graphics/Direct3D12/PipelineStateUtils.h, .cpp)**
  - Provides translation and helper functions for creating D3D12-specific
    pipeline state structures from backend-agnostic descriptors.
  - Used by the backend-specific Renderer or its pipeline manager component.

- **Unit Tests (test/and src/Oxygen/Graphics/Common/Test/)**
  - New and updated tests for pipeline creation, caching, concurrency, and error
    handling.

### New Classes/Files to Create

- `src/Oxygen/Graphics/Common/PipelineState.h` / `.cpp`
- `src/Oxygen/Graphics/Direct3D12/PipelineStateUtils.h` / `.cpp`

### Files to Change

- `src/Oxygen/Graphics/Common/Renderer.h` / `.cpp`
- `src/Oxygen/Graphics/Direct3D12/Renderer.h` / `.cpp`
- `src/Oxygen/Graphics/Common/CommandList.h` / `.cpp`
- `src/Oxygen/Graphics/Common/CommandRecorder.h` / `.cpp`
- `src/Oxygen/Graphics/Direct3D12/Renderer.h` / `.cpp`
- `src/Oxygen/Graphics/Direct3D12/CommandList.h` / `.cpp`
- `src/Oxygen/Graphics/Direct3D12/CommandRecorder.h` / `.cpp`
- `src/Oxygen/Graphics/Direct3D12/PipelineStateUtils.h` / `.cpp`
- `src/Oxygen/Graphics/Direct3D12/Detail/dx12_utils.h` / `.cpp`
- `src/Oxygen/Graphics/Common/Test/` (add/modify unit tests)
