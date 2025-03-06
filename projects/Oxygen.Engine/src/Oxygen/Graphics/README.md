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
