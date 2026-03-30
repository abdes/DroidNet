# Oxygen Graphics Engine Architecture

## Overview

The Oxygen Graphics Engine implements a modern, modular graphics system supporting Vulkan and Direct3D 12. The architecture consists of 15 specialized components organized into a hierarchical structure.

## Architecture Components

#### 🎮 Graphics Context

Central access point for all graphics subsystems.

- **Core Responsibilities**:
  - Subsystem initialization and shutdown coordination
  - Component lifetime management
  - Cross-component communication facilitation
  - Public API surface for the engine
  - Error state management

### Subsystems

#### 🖥️ Devices

Hardware interface and capability management.

- **Core Responsibilities**:
  - **Physical Device Management**
    - D3D12: Adapter enumeration and capability querying, holds DXGIFactory
    - Vulkan: Physical device selection and queue family mapping

  - **Queue Management**
    - D3D12: Direct/Compute/Copy command queue creation
    - Vulkan: Graphics/Compute/Transfer queue family handling

  - **Multi-GPU Support**
    - D3D12: Multi-adapter node masking and cross-node sharing
    - Vulkan: Device groups and memory peer access

  - **Feature Detection**
    - D3D12: CheckFeatureSupport for capabilities
    - Vulkan: Physical device feature queries and extensions

#### 🔍 Debugger

Development and profiling tools.

- **Core Responsibilities**:
  - **Performance Analysis**
    - D3D12: PIX integration
    - D3D12: optional RenderDoc frame capture via `FrameCaptureController`
    - Vulkan: Validation layers

  - **Resource Tracking**
    - Memory allocation tracking
    - Resource lifetime monitoring

  - **Validation**
    - D3D12: Debug layer
    - Vulkan: Validation layers

  - **Profiling**
    - GPU timestamp queries
    - Pipeline statistics

#### 💾 Allocator

GPU memory management system.

- **Core Responsibilities**:
  - **Memory Allocation**
    - D3D12: Heap allocation and suballocation
    - Vulkan: Memory type selection and allocation

  - **Resource Placement**
    - D3D12: Custom heap selection and residency
    - Vulkan: Memory binding and device-local allocation

  - **Defragmentation**
    - D3D12: Resource migration and compaction
    - Vulkan: Memory defragmentation and compaction

  - **Pool Management**
    - D3D12: Resource heap pooling
    - Vulkan: Memory pool management

### 🔄 Maestro (The Coordinator)

#### Core Responsibilities

- **Work Synchronization:**
  - **Fence Management**
      In Direct3D 12, create an **ID3D12Fence** for each command queue, then
      signal or wait on specific fence values to coordinate when tasks begin or
      end. The Coordinator tracks these fence values to ensure that each
      subsystem’s work completes in the correct order and avoids unintended
      overlaps.

  - **Timeline Semaphores**
      While D3D12 doesn’t provide native timeline semaphores (as Vulkan does),
      the Coordinator can mimic timeline behavior by incrementing fence values
      each submission and waiting on specific thresholds. This keeps multi-queue
      workloads in sync without explicitly tying into resource operations.

- **Execution Timeline Coordination:**
  - **Global Timeline**
      Maintain a global counter to represent the last completed segment of work.
      Each time the Coordinator processes submitted tasks, it updates the fence
      value and checks if any tasks depend on previous completions.
  - **Dependency Graph**
      If a subsystem needs other work to finish first, the Coordinator inserts
      waits on the relevant fence value. This ensures the correct sequence of
      steps—for instance, finishing a compute pass before a rendering pass that
      consumes its results.

- **Periodic Events Management:**
  - **RenderFrameBegin**
      The Coordinator triggers this event at the start of each frame, notifying
      subsystems that it’s safe to queue up draw commands, refresh dynamic data,
      or perform any pre-render setup.
  - **RenderFrameEnd**
      Once all rendering for the frame is submitted, the Coordinator signals the
      *end* event. Higher-level logic may use this signal to handle post-frame
      operations, like capturing frame stats or triggering GPU-side analytics.

- **Frame Buffering and Vsync Management:**
  - **Buffer Count Configuration**
      For double or triple buffering, the Coordinator instructs the swap chain
      (via `DXGI_SWAP_CHAIN_DESC1::BufferCount`) but doesn’t allocate or manage
      the buffers itself—that remains with the Renderer module.
  - **In-Flight Frames**
      The Coordinator tracks fence values associated with each buffer to ensure
      that the GPU has finished work on a given buffer before reusing it. This
      prevents overwriting a buffer that’s still in use on the GPU.
  - **Vsync Coordination**
      Vsync is handled by specifying the correct swap chain parameters (e.g.,
      sync interval for `Present`). The Coordinator ensures the present call
      respects the selected intervals and that fences are signaled so the engine
      smoothly proceeds to the next frame without tearing.

#### 📦 Resources

Resource creation and state tracking.

- **Core Responsibilities**:
  - **Resource State Management**
    - D3D12: Resource state barriers and transition tracking
    - Vulkan: Pipeline barriers and image layout transitions

  - **Resource Views** [Moved from PipelineArchitect]
    - D3D12: Descriptor heap management for CBV/SRV/UAV/RTV/DSV
    - Vulkan: VkImageView/VkBufferView creation and caching

  - **Memory Residency**
    - D3D12: Residency management with Evict/MakeResident
    - Vulkan: Memory allocation and priority management

  - **Resource Aliasing**
    - D3D12: Placed resource management
    - Vulkan: Sparse binding and memory aliasing

#### 🔧 PipelineArchitect

Pipeline state and binding management.

- **Core Responsibilities**:
  - **Pipeline State Objects**
    - D3D12: ID3D12PipelineState creation and caching
    - Vulkan: VkPipeline management with pipeline cache

  - **Dynamic State**
    - D3D12: Root signature management
    - Vulkan: Dynamic state and push constants

  - **Descriptor Management** [Refined scope]
    - D3D12: Root parameter layout and descriptor tables
    - Vulkan: Descriptor set layouts and pools

  - **Pipeline Derivatives**
    - D3D12: Pipeline library support
    - Vulkan: Pipeline derivatives and specialization constants

#### ⚡ Commander

Command submission and execution.

- **Core Responsibilities**:
  - **Command Pool Management**
    - D3D12: Command allocator pooling
    - Vulkan: Command pool recycling

  - **Command Recording**
    - D3D12: Command list bundles
    - D3D12: Multi-threaded command list recording
    - Vulkan: Secondary command buffers

  - **Queue Submission**
    - D3D12: Command list execution
    - Vulkan: Command buffer submission

#### 🎬 Renderer

Display and presentation system.

- **Core Responsibilities**:
  - **Display Management**
    - D3D12:
      - DXGI output enumeration and adapter selection
      - Display mode capabilities and format selection
      - HDR/SDR display configuration
      - Multi-monitor management
    - Vulkan:
      - VkSurfaceKHR creation and configuration
      - Surface format and colorspace selection
      - Display mode capabilities querying
      - Multi-display support

  - **Swapchain Management**
    - D3D12:
      - IDXGISwapChain4 creation and configuration
      - Backbuffer management and recycling
      - Present modes (immediate/fifo/mailbox)
      - Tearing support handling
    - Vulkan:
      - VkSwapchainKHR lifecycle management
      - Image acquisition and presentation
      - Present modes configuration
      - Present queue management

  - **Frame Presentation**
    - Swapchain presentation coordination
    - Present parameter configuration
    - Tearing support and VSync settings
    - Frame buffer management
    - Frame resource cycling

  - **Frame Resource Management** [Retained]
    - Per-frame command allocation tracking
    - Frame resource state tracking
    - In-flight frame counting
    - Resource barrier management

#### 📚 ShaderLibrary

Shader management system.

- **Core Responsibilities**:
  - **Shader Management**
    - D3D12: DXIL shader management
    - Vulkan: SPIR-V handling

  - **Resource Binding**
    - D3D12: Root signature compatibility
    - Vulkan: Descriptor set layout validation

  - **Shader Variants**
    - Permutation management
    - Dynamic feature toggles

#### ⚙️ ShaderCompiler

Shader compilation pipeline.

- **Core Responsibilities**:
  - **Cross-Platform Compilation**
    - D3D12: HLSL to DXIL using DXC compiler
    - Vulkan: HLSL/GLSL to SPIR-V using SPIRV-Cross and shaderc
    - Shader model mapping and feature adaptation

  - **Optimization**
    - D3D12: FXC/DXC optimization passes
    - Vulkan: SPIR-V optimizer passes
    - Platform-specific optimizations for each target
    - Shader permutation optimization

  - **Reflection System**
    - D3D12: ID3D12ShaderReflection interface
    - Vulkan: SPIRV-Reflect capabilities
    - Resource binding analysis and validation
    - Entry point and interface verification
    - Shader resource layout generation

  - **Debug Support**
    - D3D12: PDB generation for shader debugging
    - Vulkan: SPIR-V debug information
    - Source mapping for runtime debugging
    - Error location tracking and reporting

  - **Cache Management**
    - Binary shader cache with versioning
    - Platform-specific cached formats
    - Hot reload capability
    - Incremental compilation support

  - **Error Handling**
    - Detailed compilation error reporting
    - Warning and optimization suggestions
    - API-specific validation messages
    - Runtime shader validation

#### 🌳 SceneOrganizer

Scene hierarchy and visibility management.

- **Core Responsibilities**:
  - **Acceleration Structures**
    - D3D12: DirectX Raytracing (DXR) BVH
    - Vulkan: VK_KHR_acceleration_structure

  - **Culling Systems**
    - D3D12: Compute shader-based culling with indirect arguments
    - Vulkan: Indirect draw command generation with compute

  - **Instance Management**
    - D3D12: Instance buffers and SV_InstanceID handling
    - Vulkan: VkAccelerationStructureInstanceKHR management

  - **LOD System**
    - GPU-driven LOD selection
    - Distance-based mesh streaming
    - Instance data management for LOD transitions

#### 🎨 MaterialManager

Material and texture system.

- **Core Responsibilities**:
  - **Texture Management**
    - D3D12: Tiled resource management
    - Vulkan: Sparse texture binding

  - **Material System**
    - Material parameter buffer management
    - Instance data pooling
    - Dynamic material updates

  - **Texture Streaming**
    - D3D12: Reserved resource streaming
    - Vulkan: Sparse binding for streaming

  - **Material Pipeline Integration**
    - Material-specific pipeline variants
    - Dynamic shader permutation handling

#### 💡 LightMaster

Lighting and shadow system.

- **Core Responsibilities**:
  - **Light Management**
    - Clustered light assignment
    - Light type specialization
    - GPU-driven light culling

  - **Shadow Mapping**
    - D3D12: Resource heap for shadow cascades
    - Vulkan: Dedicated shadow attachment handling

  - **Global Illumination**
    - Light probe processing
    - Indirect lighting calculation
    - Real-time GI solutions

  - **Volumetric Effects**
    - Volume texture management
    - Light scattering computation

#### ✨ Effects

Post-processing framework.

- **Core Responsibilities**:
  - **Effect Chain**
    - D3D12: UAV barriers between effects
    - Vulkan: Pipeline barriers for effect passes

  - **Render Targets**
    - D3D12: RTV/UAV management for effects
    - Vulkan: Framebuffer attachments

  - **HDR Pipeline**
    - Tone mapping
    - Color space conversion
    - HDR display support

  - **Temporal Effects**
    - History buffer management
    - Motion vector utilization
    - Temporal stability

#### 🎭 Animation

Animation processing system.

- **Core Responsibilities**:
  - **Skeletal Animation**
    - D3D12: Structured buffer for bone matrices
    - Vulkan: SSBO for skeleton data

  - **GPU Skinning**
    - Compute-based vertex skinning
    - Mesh shader integration

  - **Animation Compression**
    - Quaternion compression
    - Keyframe optimization
    - Animation streaming

  - **GPU-Driven Animation**
    - State machine processing
    - Blend tree computation
    - Instance animation updates

Component Interactions and Data Flow
Frame Execution Flow
Graphics Context initiates the frame
Maestro signals frame begin event
Renderer acquires the next swapchain image
Various Subsystems record commands:
SceneOrganizer updates scene data
MaterialManager updates material parameters
LightMaster updates light data
Animation updates skeletal data
PipelineArchitect provides pipeline states
Commander submits command buffers
Maestro synchronizes between queue submissions
Renderer presents the completed frame
Maestro signals frame end event
Resource Creation Flow
Graphics Context receives resource creation request
Resources creates the resource descriptor
Allocator provides memory for the resource
Resources finalizes resource creation
Resources creates necessary views
Shader Pipeline Flow
ShaderCompiler compiles shader code
ShaderLibrary stores compiled shaders
PipelineArchitect uses shader bytecode for pipeline creation
Resources creates descriptor views based on shader reflection data

## Implementation Guidelines

### Memory Management

- Use custom allocators for different resource types
- Implement residency management for large datasets
- Pool frequently allocated resources
- Track memory budget per heap type

### Threading Model

- Main thread: Frame orchestration and presentation
- Render thread: Command list generation
- Worker threads: Asset loading and processing
- Background thread: Resource streaming

### Performance Considerations

- Minimize state changes and barriers
- Batch similar operations
- Use GPU-driven rendering techniques
- Implement efficient resource recycling
- Profile and optimize hot paths

### API Abstraction

- Backend-agnostic resource handles
- Unified synchronization primitives
- Common shader interface
- Shared resource formats

### Debug Support

- Per-component debug markers
- Resource naming conventions
- Performance counters
- Validation layers

## Frame Capture Integration

Oxygen exposes backend-owned frame capture and GPU failure tooling through the
common `graphics::FrameCaptureController` service and the D3D12 `DebugLayer`.
The service is optional and nullable, and the capture lifecycle is bracketed
from `Graphics::BeginFrame()` / `Graphics::EndFrame()` so renderer passes stay
backend-agnostic.

A new developer should think of the tooling stack as separate layers:

- the D3D12 debug layer validates API usage; `GraphicsConfig.enable_debug`
  enables it and `GraphicsConfig.enable_validation` adds GPU-based validation
- DRED (Device Removed Extended Data) records device-removal breadcrumbs and
  page-fault data; Oxygen force-enables it only when the debug layer is on and
  no capture hooks are already active in the process
- RenderDoc provides the current in-app frame capture controller; it is
  optional and loaded dynamically, never linked as a hard runtime dependency
- PIX provides both marker/event integration through WinPixEventRuntime and a
  first-class in-app GPU capture controller for D3D12
- Nsight Aftermath provides NVIDIA GPU crash dumps and, when fully initialized,
  DX12 markers, resource tracking, and shader error reporting

### Build-Time SDK Discovery

The D3D12 backend auto-discovers optional tooling packages on Windows. Repo
helper scripts download the expected layouts into `packages/`:

- `GetRenderDoc.bat` / `GetRenderDoc.ps1` -> `packages/RenderDoc`
- `GetPIX.bat` / `GetPIX.ps1` -> `packages/WinPixEventRuntime` plus installed
  PIX validation
- `GetAftermath.bat` / `GetAftermath.ps1` -> `packages/NsightAftermath`

If you keep the SDKs somewhere else, point CMake at them with cache variables:

- `OXYGEN_RENDERDOC_DIR`
- `OXYGEN_WINPIXEVENTRUNTIME_DIR`
- `OXYGEN_AFTERMATH_DIR`

Expected package contents:

- RenderDoc: `Include/renderdoc_app.h` and optionally `Bin/x64/renderdoc.dll`
- WinPixEventRuntime: `Include/pix3.h`, `Lib/x64/WinPixEventRuntime.lib`, and
  `Bin/x64/WinPixEventRuntime.dll`
- Nsight Aftermath: `Include/GFSDK_Aftermath.h` (or
  `Include/GFSDK_Aftermath_DX12.h`), `Lib/x64/GFSDK_Aftermath_Lib.x64.lib`, and
  `Bin/x64/GFSDK_Aftermath_Lib.x64.dll`

When the SDK artifacts are present, the D3D12 build deploys the matching DLLs
to the build output automatically.

### Runtime Configuration Surface

The main config knobs live in `GraphicsConfig`:

```cpp
oxygen::GraphicsConfig config {};
config.enable_debug = true;
config.enable_validation = false;
config.enable_aftermath = true;
config.frame_capture = {
  .provider = oxygen::FrameCaptureProvider::kRenderDoc,
  .init_mode = oxygen::FrameCaptureInitMode::kSearchPath,
  .from_frame = 0,
  .frame_count = 1,
  .capture_file_template = "captures/render_scene",
};
```

Equivalent JSON:

```json
{
  "enable_debug": true,
  "enable_validation": false,
  "enable_aftermath": true,
  "frame_capture": {
    "provider": "renderdoc",
    "init_mode": "search",
    "from_frame": 0,
    "frame_count": 1,
    "capture_file_template": "captures/render_scene"
  }
}
```

Useful values:

- `frame_capture.provider`: `none`, `renderdoc`, or `pix`
- `frame_capture.init_mode`: `attached`, `search`, or `path`
- `frame_capture.from_frame`: zero-based first rendered frame to capture
- `frame_capture.frame_count`: number of consecutive frames to capture; `0`
  disables configured startup capture
- `frame_capture.module_path`: explicit DLL path used only with `init_mode=path`
- `frame_capture.capture_file_template`: provider output template; RenderDoc
  forwards it directly and PIX expands it into `.wpix` capture file names

Shared init mode meanings:

- `attached`: require the capture DLL to already be loaded in the process
- `search`: use the provider's normal discovery path and local installation
  search
- `path`: load the capture DLL from `frame_capture.module_path`

Provider notes:

- RenderDoc:
  - `attached`: only bind to an already-loaded `renderdoc.dll`
  - `search`: use an already-loaded `renderdoc.dll` first, otherwise load
    `renderdoc.dll` through the normal DLL search path
  - `path`: load `renderdoc.dll` from `frame_capture.module_path`
- PIX:
  - `attached`: require `WinPixGpuCapturer.dll` to already be loaded, which is
    what you get when launching/attaching through PIX
  - `search`: call `PIXLoadLatestWinPixGpuCapturerLibrary()` before DXGI/D3D12
    startup
  - `path`: load `WinPixGpuCapturer.dll` from `frame_capture.module_path`

### Runtime Policy and Tool Interactions

Tooling is configured before DXGI factory creation and device creation. This is
important because RenderDoc/PIX interception, the D3D12 debug layer, and DRED
all care about early startup ordering.

The current policy is:

- Oxygen logs both the requested frame-capture provider and the capture layer
  already active in the process
- if PIX is selected, the D3D12 backend reports whether PIX markers, GPU
  capture discovery, timing-capture discovery, and PIX UI discovery are built
  into the current binary
- if the D3D12 debug layer is enabled and no capture hooks are active, Oxygen
  forces DRED auto-breadcrumbs, page-fault reporting, and Watson dumps on
- if RenderDoc or PIX capture hooks are already active in the process, Oxygen
  skips forced DRED and leaves DRED at the tool/OS defaults
- if `enable_aftermath` is true, Aftermath crash-dump collection starts only
  when the SDK is available and no active RenderDoc/PIX conflict exists
- Aftermath DX12 initialization is attempted only after device creation and only
  on NVIDIA adapters
- when Aftermath DX12 initialization succeeds, Oxygen enables marker capture,
  resource tracking, and shader error reporting
- when a device is removed, Oxygen prints a DRED report to the log; if
  Aftermath crash dumping is active, it writes `.nv-gpudmp` and `.nvdbg`
  artifacts under `logs/aftermath`

Current known limitations:

- the in-app `FrameCaptureController` is D3D12-only
- PIX supports status, next-frame capture, manual begin/end capture, configured
  frame-range capture, target-window tracking, and capture-file templates
- PIX intentionally does not advertise `gfx.capture.discard` or
  `gfx.capture.open_ui`; those commands remain RenderDoc-only and return an
  explicit provider-specific unsupported-operation error when PIX is active
- `gfx.capture.status` now prints both a human-readable summary and the raw
  provider state blob; for PIX the raw state includes:
  `markers_available`, `gpu_capture_available`,
  `timing_capture_available`, `pix_ui_available`, `capturer_loaded`,
  `attached`, and `capturing`
- `frame_capture.init_mode=attached` for PIX is honest: if the PIX GPU capturer
  is not already loaded, Oxygen leaves DRED/Aftermath available and reports the
  missing capturer instead of forcing a false tooling conflict
- with the current debug-layer configuration, Aftermath crash-dump collection
  can enable successfully while `GFSDK_Aftermath_DX12_Initialize` still fails
  with `D3DDebugLayerNotCompatible`; in that state you still get Aftermath
  crash-dump collection, but not full DX12 marker/resource tracking
- RenderDoc or PIX capture hooks and Aftermath are intentionally not combined in
  one process by Oxygen's startup policy

### How a Developer Uses This

The easiest reference demo is
[`Examples/RenderScene/README.md`](../../../Examples/RenderScene/README.md).
It exposes the common capture CLI used by the D3D12 examples.

Typical commands from `out/build-ninja` are:

```powershell
out/build-ninja/bin/Debug/Oxygen.Examples.RenderScene.exe --capture-provider renderdoc --capture-load search --capture-from-frame 0 --capture-frame-count 1
```

```powershell
out/build-ninja/bin/Debug/Oxygen.Examples.RenderScene.exe --capture-provider renderdoc --capture-load path --capture-library C:/Tools/RenderDoc/renderdoc.dll
```

```powershell
out/build-ninja/bin/Debug/Oxygen.Examples.RenderScene.exe --capture-provider pix --capture-load search --capture-from-frame 1 --capture-frame-count 1 --capture-output out/build-ninja/pix-captures/render_scene
```

```powershell
out/build-ninja/bin/Debug/Oxygen.Examples.RenderScene.exe --capture-provider pix --capture-load path --capture-library "C:/Program Files/Microsoft PIX/2602.25/WinPixGpuCapturer.dll" --capture-from-frame 1 --capture-frame-count 1 --capture-output out/build-ninja/pix-captures/render_scene
```

Useful details:

- `--capture-from-frame` is zero-based; frame `0` is the first rendered frame
- PIX startup frame-range capture requires `--capture-from-frame > 0`
- PIX `--capture-output` is treated as a template prefix; the engine generates a
  `.wpix` filename such as `render_scene_frame_0001.wpix`
- PIX `--capture-load attached` expects the process to have been launched or
  attached through PIX already
- run `Oxygen.Examples.RenderScene.exe help-advanced` to see the hidden
  development-only capture flags
- when a frame-capture provider is active, the dev console exposes
  `gfx.capture.status`, `gfx.capture.frame`, `gfx.capture.begin`,
  and `gfx.capture.end`
- `gfx.capture.discard` and `gfx.capture.open_ui` remain RenderDoc-only
- `gfx.capture.status` is the quickest way to see whether the provider is ready,
  which features it supports, and the raw provider state blob
- if you only want Aftermath diagnostics, leave `frame_capture.provider` at
  `none`; that lets Oxygen try DRED + Aftermath instead of disabling Aftermath
  for an active capture tool

### What to Look for in the Log

Healthy startup with no capture tool usually shows lines like:

- `D3D12 frame capture layer requested by GraphicsConfig: none`
- `D3D12 debug layer enabled (gpu_validation=off)`
- `Forced DRED enabled (auto-breadcrumbs, page faults, Watson dumps)`
- `Aftermath integration enabled`
- `Aftermath: crash dump collection enabled`

Healthy startup under RenderDoc usually shows lines like:

- `D3D12 frame capture layer requested by GraphicsConfig: RenderDoc`
- `D3D12 frame capture layer active in this process: RenderDoc`
- `RenderDoc API initialized successfully before DXGI/D3D12 startup`
- `Skipping forced DRED configuration because RenderDoc capture hooks are active; leaving DRED at tool/OS defaults`
- `Aftermath integration disabled because RenderDoc capture hooks are active in this process`

Healthy startup under PIX search/path capture usually shows lines like:

- `PIX frame capture controller ready: module='C:/Program Files/Microsoft PIX/.../WinPixGpuCapturer.dll' ...`
- `D3D12 frame capture layer requested by GraphicsConfig: PIX`
- `D3D12 frame capture layer active in this process: PIX`
- `PIX tooling support compiled in: markers=true gpu_capture=true timing_capture=true ui=true`
- `Skipping forced DRED configuration because PIX capture hooks are active; leaving DRED at tool/OS defaults`
- `Aftermath integration disabled because PIX capture hooks are active in this process`
- `PIX configured frame-range capture requested from frame 1 for 1 frame(s): ...render_scene_frame_0001.wpix`

Healthy startup under PIX attached-only without an injected capturer usually
shows lines like:

- `PIX frame capture initialization failed: PIX GPU capturer is not loaded in this process`
- `D3D12 frame capture layer requested by GraphicsConfig: PIX`
- `Forced DRED enabled (auto-breadcrumbs, page faults, Watson dumps)`
- `Aftermath integration enabled`
- `PIX capture request rejected: PIX GPU capturer is not loaded in this process`

Important troubleshooting messages:

- `RenderDoc frame capture requested by GraphicsConfig, but the backend was built without RenderDoc support`
- `PIX frame capture requested by GraphicsConfig, but the backend was built without PIX support`
- `PIX frame capture initialization failed: PIX GPU capturer is not loaded in this process`
- `PIX configured startup capture rejected: configured PIX startup capture requires from_frame > 0`
- `PIX capture request rejected: PIX GPU capturer is not loaded in this process`
- `Aftermath requested by GraphicsConfig, but the backend was built without Nsight Aftermath support`
- `Aftermath: DX12_Initialize failed (D3DDebugLayerNotCompatible, ...)`
- `No DRED data available`

## Design Patterns

- Command pattern for render operations
- Factory pattern for resource creation
- Observer pattern for state changes
- Strategy pattern for backend selection
- Pool pattern for resource management

## Future Considerations

- Ray tracing integration
- Machine learning acceleration
- Variable rate shading
- Mesh shader pipeline
- Advanced upscaling techniques

## Component Interactions and Data Flow

### Frame Execution Flow (Asynchronous Coroutines)

The rendering pipeline operates as a network of interconnected asynchronous
tasks, with natural suspension points where operations await GPU completion:

1. **Frame Initialization**
   - **Graphics Context** spawns the frame execution coroutine
   - This coroutine establishes frame-level state and dispatches parallel tasks

2. **Event Signaling**
   - **Maestro** asynchronously broadcasts the frame begin event
   - Subsystems subscribe to this event and activate their respective coroutines

3. **Resource Acquisition**
   - **Renderer's** swapchain image acquisition suspends until a buffer is available
   - Upon resumption, it signals downstream coroutines that surface resources are ready

4. **Parallel Command Recording**
   - Multiple subsystem coroutines execute concurrently:
     - **SceneOrganizer** generates scene data asynchronously, yielding when dependencies arise
     - **MaterialManager** updates parameter buffers in parallel with other systems
     - **LightMaster** asynchronously processes light updates and shadow information
     - **Animation** transforms skeletal data without blocking other subsystems

5. **Pipeline Resolution**
   - **PipelineArchitect** awaits shader and state requests, resuming when dependencies resolve
   - Caches responses to avoid redundant pipeline creation

6. **Submission Coordination**
   - **Commander** awaits completion of all recording coroutines
   - Batches commands and submits them without blocking the main thread

7. **Synchronization**
   - **Maestro** places synchronization points between queue submissions
   - Yields execution until critical GPU operations complete

8. **Presentation**
   - **Renderer** presentation coroutine suspends until the optimal present moment
   - Resumes when timing conditions align with VSync requirements

9. **Completion Signaling**
   - **Maestro** signals frame end after presentation completes
   - New frame coroutines can begin while previous frame finalization continues in parallel

### Resource Creation Flow

1. **Graphics Context** receives resource creation request
2. **Resources** creates the resource descriptor
3. **Allocator** provides memory for the resource
4. **Resources** finalizes resource creation
5. **Resources** creates necessary views

### Shader Pipeline Flow

1. **ShaderCompiler** compiles shader code
2. **ShaderLibrary** stores compiled shaders
3. **PipelineArchitect** uses shader bytecode for pipeline creation
4. **Resources** creates descriptor views based on shader reflection data
