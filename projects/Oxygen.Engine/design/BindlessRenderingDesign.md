# WU1: Core Types and Interfaces

**Description:**

Define the foundational types and interfaces for the descriptor system that
enables efficient management of graphics resources using index-based
identification across modern graphics APIs.

**Deliverables:**

- Core enum types for descriptor spaces and organization
- Abstract interfaces for descriptor allocation and management
- Integration with Oxygen's existing resource systems

**Prerequisites:**

None

**Dependencies:**

None

## Detailed Flow: Resource Creation/Replacement → Registration/Update → View Creation/Replacement → Registration/Update → Bindless Table Update → Rendering

1. **Resource Creation / Replacement**
   - The application creates a graphics resource (e.g., a texture or buffer) using the appropriate API (e.g., `std::make_shared<Texture>(...)`).
   - If a resource needs to be replaced (e.g., resizing a texture), a new resource instance is created.

2. **Resource Registration / Update**
   - The application registers the resource with the `ResourceRegistry` using `registry.Register(resource)`.
   - The registry keeps a strong reference to the resource and tracks it for view management.
   - If replacing a resource, the registry provides (or will provide) an API to update or replace the resource while keeping descriptor indices stable.

3. **View Creation / Replacement**
   - The application creates a view description (e.g., `TextureViewDesc`) specifying format, subresources, dimension, etc.
   - The resource provides a method (e.g., `GetNativeView(desc)`) to create a native view object for the given description.
   - If a view needs to be replaced (e.g., after resource replacement or hot-reload), a new native view is created for the same description.

4. **View Registration / Update**
   - The application registers the view with the `ResourceRegistry` using `registry.RegisterView(resource, desc)` or `registry.RegisterView(resource, native_view, desc)`.
   - The registry checks for existing views with the same description:
     - If not present, it allocates a descriptor handle and caches the view.
     - If present, it throws or returns the cached view.
   - For view replacement, the registry (future enhancement) will provide an API to update the view in-place, keeping the descriptor handle (bindless index) stable.

5. **Bindless Table Update**
   - When a new view is registered or an existing view is updated, the registry (and underlying allocator) ensures the descriptor heap or bindless table is updated with the new view at the correct index.
   - The descriptor handle (bindless index) remains stable for the resource/view, allowing shaders to access the correct descriptor.

6. **Rendering**
   - During rendering, the application or engine uses the bindless index (from the `DescriptorHandle` or `NativeObject`) to access the resource in shaders.
   - The descriptor allocator ensures all necessary descriptor heaps are bound before rendering.
   - The rendering pipeline uses the up-to-date bindless table for resource access, supporting dynamic resource/view replacement and efficient descriptor management.

## Implementation Notes

- The API is unified and generic; there are no explicit typed handles for each view type.
- View caching is integrated into `ResourceRegistry`.
- View update and replacement is supported by design (see comments in `ResourceRegistry.h`), but not yet implemented.
- Heap allocation strategy is string-keyed and maps to backend-specific heap/segment concepts (see D3D12 implementation).
- Thread safety is enforced via a global mutex per allocator/registry.
- Testing covers handles, segments, registry, and concurrency.
- Default resources and debugging/validation tools are future considerations.

## Implementation layout

- `src/Oxygen/Graphics/Common/Types/DescriptorVisibility.h`
  *Defines the DescriptorVisibility enum, representing whether a descriptor heap/pool is shader-visible (GPU) or CPU-only.*

- `src/Oxygen/Graphics/Common/Types/IndexRange.h`
  *Defines the IndexRange class for mapping between local and global descriptor indices.*

- `src/Oxygen/Graphics/Common/Types/ResourceViewType.h`
  *Defines the ResourceViewType enum, enumerating all supported resource view types (SRV, UAV, CBV, etc.).*

- `src/Oxygen/Graphics/Common/DescriptorHandle.h`
  *Defines DescriptorHandle, a type that encapsulates a stable index for a descriptor in a bindless table, with ownership and lifetime semantics.*

- `src/Oxygen/Graphics/Common/Detail/DescriptorHeapSegment.h`
  *Declares the DescriptorHeapSegment interface for managing a contiguous range of descriptor indices within a heap, and describes the contract for allocation, release, and state tracking.*

- `src/Oxygen/Graphics/Common/Detail/FixedDescriptorHeapSegment.h`
  *Implements a fixed-capacity, LIFO-recycling descriptor heap segment.*

- `src/Oxygen/Graphics/Common/Detail/StaticDescriptorHeapSegment.h`
  *Implements a static, compile-time capacity descriptor heap segment for testing and specialized use.*

- `src/Oxygen/Graphics/Common/DescriptorAllocator.h`
  *Declares the DescriptorAllocator interface and heap allocation strategy classes for managing descriptor heaps and allocation policies.*

- `src/Oxygen/Graphics/Common/ResourceRegistry.h`
  *Defines the ResourceRegistry, which manages registration, lookup, and caching of resources and their views for bindless rendering, including design notes for resource/view replacement and update.*

- `src/Oxygen/Graphics/Common/NativeObject.h`
  *Defines NativeObject, a generic wrapper for native resource/view handles and type information.*

- `design/WU1_CoreTypesAndInterfaces.md`
  *This design document.*

## Detailed Specifications

### 1. ✓ DescriptorVisibility Enum

- Defines memory locations where descriptors can reside
- Implementation detail for optimization of descriptor updates
- Values: kShaderVisible (GPU-accessible), kCpuOnly (CPU-only)
- Used by allocator to determine descriptor placement
- In D3D12 terms, this maps to whether a descriptor resides in a
     shader-visible heap or a non-shader-visible heap
- In Vulkan terms, this maps to host-visible vs device-local descriptor pools
- Renamed from DescriptorSpace to DescriptorVisibility for clarity
- Renamed NonShaderVisible to CpuOnly for better semantic meaning

### 2. ✓ IndexRange Class

- Maps between local descriptor indices (per descriptor type) and global indices
- Contains baseIndex and count fields to define a range
- Provides methods to check if an index is within range
- Used internally by allocator implementations
- An empty range (count == 0) is valid and represents a no-op or sentinel
     value (e.g., for default construction or error states)
- Bounds checking is the responsibility of the caller; IndexRange does not
     validate that its indices are within the bounds of any underlying resource
     or table (similar to std::span)

### 3. ✓ DescriptorHandle

- Represents an allocated descriptor slot with stable index for shader
     reference
- Has limited ownership semantics - can release its descriptor back to the
     allocator but doesn't own the underlying resource
- Contains back-reference to allocator for lifetime management
- Non-copyable, movable semantics to enforce ownership rules
- Clear RAII behavior with automatic release in destructor and explicit
     Release() method
- Stores ResourceViewType and DescriptorVisibility to track descriptor type
     and memory location
- Provides GetIndex(), GetViewType(), GetVisibility() and IsValid()
     inspection methods
- Does not directly expose platform-specific handles - these are
     implementation details, accessed through the DescriptorAllocator /
     DescriptorHeapSegment

### 4. ✓ DescriptorHeapSegment

- **Purpose**: Defines an interface for managing a dedicated section, or
     "segment," within a larger descriptor heap. Each segment is responsible for
     a contiguous range of descriptor handles, all intended for a specific
     `ResourceViewType` and `DescriptorVisibility`.

- **Core Responsibilities**:
  - **Lifecycle Management**: Provides unique descriptor indices upon
       allocation. Released indices become available for subsequent allocations.
  - **Boundary Adherence**: Allocations are confined to the segment's defined
       range (`baseIndex` to `baseIndex + capacity - 1`). Releases outside this
       range must fail.
  - **State Integrity**:
    - `Allocate()`: Returns a sentinel value (e.g.,
         `std::numeric_limits<uint32_t>::max()`) if the segment is full.
    - `Release(index)`: Returns `true` for a valid, successful release;
         `false` otherwise (e.g., out of bounds, already free). Re-releasing an
         index without an intermediate allocation must fail.
  - **Consistent Properties**: `GetViewType()`, `GetVisibility()`,
       `GetBaseIndex()`, and `GetCapacity()` must remain constant
       post-construction.
  - **Accurate Counts**: `GetAllocatedCount()` and
       `GetAvailableCount()` must be accurate.

- **`FixedDescriptorHeapSegment` Implementation**:
  - **Pre-allocated Capacity**: The maximum number of descriptors is fixed at
       construction time and cannot be changed later. The segment maintains a
       fixed-size pool of descriptor indices.
  - **LIFO Recycling**: Implements a Last-In, First-Out (LIFO) strategy for
       reusing descriptors from its `free_list_`. This can benefit cache
       locality.
  - **Sequential Allocation Fallback**: If the `free_list_` is empty, new
       descriptors are allocated sequentially by incrementing an internal
       counter (`next_index_`).
  - **Efficient State Tracking**: Uses a vector of boolean flags to track
       which indices have been released, allowing quick validation during
       release operations.

- **Relationship with `DescriptorAllocator`**: `DescriptorAllocator`
     implementations will typically manage collections of
     `DescriptorHeapSegment` instances (or similar sub-allocation mechanisms).
     The allocator will delegate requests for specific descriptor types and
     visibilities to the appropriate segment.

### 5. ✓ DescriptorAllocator

- Abstract interface for descriptor allocation from underlying graphics API
     heaps/pools.

- **Internal Management**: Implementations will likely manage one or more
     `DescriptorHeapSegment` instances (or analogous structures) for each
     combination of `ResourceViewType` and `DescriptorVisibility`. This allows
     the `DescriptorAllocator` to organize and sub-allocate descriptors
     efficiently.

- **Responsibilities**:
  - Manages shader-visible and CPU-only descriptor spaces by potentially
       using different sets of `DescriptorHeapSegment`s for each
       `DescriptorVisibility`.
  - Provides methods for allocation (`Allocate`) and release (`Release`) of
       `DescriptorHandle`s. These operations will typically be routed to an
       appropriate `DescriptorHeapSegment`.     - Facilitates copying descriptors between visibility spaces through the
       `CopyDescriptor` method.
  - Exposes methods to get associated platform-specific handles/pointers for
       descriptors via `GetNativeHandle`.
  - Provides shader-visible heaps for bindless rendering via `GetShaderVisibleHeaps()`
       which are automatically bound during pipeline state transitions.
  - Provides utility methods like `GetRemainingDescriptorsCount`,
       `GetAllocatedDescriptorsCount`, and `Contains` for descriptor management.
  - Uses `ResourceViewType` enum to identify descriptor type (SRV, UAV, CBV,
       etc.) when interacting with segments.

- **Ownership Model**:
  - Owns the descriptor heaps/pools
  - Creates descriptor handles that reference it for lifecycle management
  - Does not own the resources nor their views (textures, buffers, etc.),
        but keeps a strong reference to the registered resources. This requires
        the resource owner to **UnRegister the resource before destroying it**.
  - Manages descriptor lifecycle, not resource lifecycle

- **Lifetime**:
  - Tied to the Renderer instance
  - Persists for the duration of rendering operations
  - Outlives individual resources but not the rendering system

### 5.1 ✓ Heap Allocation Strategy

- **Purpose**: Defines how descriptor heaps are organized and configured across
     different resource types and visibilities.

- **Interface**: `DescriptorAllocationStrategy`
  - `GetHeapKey(ResourceViewType, DescriptorVisibility)`: Maps a resource view
       type and visibility to a unique string key that identifies the heap configuration.
  - `GetHeapDescription(string)`: Retrieves the `HeapDescription` for a given heap key.

- **Default Implementation**: `DefaultDescriptorAllocationStrategy`
  - Provides a reasonable default mapping of resource types to heap configurations.
  - Uses a key format of "ViewType:Visibility" (e.g., "Texture_SRV:gpu").
  - Pre-configures appropriate capacities for different descriptor types.
  - Allows customization of heap growth parameters.

- **HeapDescription Structure**:
  - `cpu_visible_capacity`: Initial capacity for CPU-visible descriptors.
  - `shader_visible_capacity`: Initial capacity for shader-visible descriptors.
  - `allow_growth`: Whether dynamic growth is allowed when heaps are full.
  - `growth_factor`: Multiplication factor when expanding descriptor heaps.
  - `max_growth_iterations`: Maximum number of growth cycles before failing allocations.

- **Base Implementation**: `BaseDescriptorAllocator`
  - Implements the core functionality of the `DescriptorAllocator` interface.
  - Uses the `DescriptorAllocationStrategy` to configure and manage its heaps.
  - Maintains a collection of heap segments for each (type, visibility) combination.
  - Provides thread safety through mutexes for all allocation and release operations.
  - Creates new segments dynamically as needed based on the heap configuration.
  - Delegates the actual segment creation to derived classes through a virtual method.

- **Configuration**: `BaseDescriptorAllocatorConfig`
  - Contains a `heap_strategy` shared pointer that can be customized at creation time.
  - Used to initialize the `BaseDescriptorAllocator` with specific heap configurations.
  - Falls back to a `DefaultDescriptorAllocationStrategy` if none is provided.

### D3D12 Implementation Considerations

1. **Descriptor Heap Management**
   - D3D12 provides four heap types: CBV_SRV_UAV, SAMPLER, RTV, DSV
   - Only one shader-visible CBV_SRV_UAV and one SAMPLER heap can be bound at a time
   - Need separate shader-visible and CPU-only heaps for efficient descriptor management
   - For non-shader visible descriptors, use heaps with D3D12_DESCRIPTOR_HEAP_FLAG_NONE
   - For shader-visible descriptors, use D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE
   - Must respect hardware tier limits for descriptor counts

2. **Handle Translation**
   - Map global indices to D3D12_CPU_DESCRIPTOR_HANDLE and D3D12_GPU_DESCRIPTOR_HANDLE
   - Account for descriptor handle increment size for each heap type
   - Use heap offsets for efficient descriptor lookup
   - Implement fast path for common descriptor types

3. **Update & Copy Strategy**
   - Use ID3D12Device::CopyDescriptorsSimple for single descriptor copies
   - Use ID3D12Device::CopyDescriptors for batching multiple descriptor copies
   - Create resources in CPU-only heaps and copy to shader-visible when needed
   - Optimize copy operations by tracking dirty descriptors

4. **Performance Considerations**
   - Minimize heap transitions (they cause pipeline stalls)
   - Use persistent heaps to avoid recreation costs
   - Implement descriptor recycling with free lists
   - Consider using separate heaps for frequently accessed resources
   - Pre-allocate commonly used descriptors at the beginning of heaps

### Vulkan Implementation Considerations

1. **Descriptor Pool and Layout Management**
   - Create descriptor pools with VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT flag
   - Create descriptor set layouts with VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT flag
   - Enable VK_DESCRIPTOR_BINDING_VARIABLE_DESCRIPTOR_COUNT_BIT for arrays with dynamic sizes
   - Use descriptor indexing extensions to enable truly bindless operation

2. **Required Extensions**
   - VK_EXT_descriptor_indexing (critical for bindless functionality)
   - VK_KHR_maintenance3 (required for large descriptor sets)
   - VK_KHR_push_descriptor (helpful for short-lived descriptors)
   - VK_KHR_driver_properties (for querying descriptor limits)

3. **Update Strategy**
   - Use vkUpdateDescriptorSets for batch updates
   - Use vkUpdateDescriptorSetWithTemplate for optimized updates of common patterns
   - Implement descriptor recycling with careful pool management
   - Create dummy descriptors for unbound resources to avoid validation errors

4. **Synchronization Requirements**
   - Handle appropriate memory barriers when updating descriptors that are in use
   - Use VkAccessFlags for memory dependencies
   - Track descriptor usage across command buffers

5. **Performance Considerations**
   - Use multiple descriptor pools for concurrent allocation
   - Respect maxPerStageDescriptorSamplers and other device limits
   - Implement per-thread allocation pools for descriptor set allocation
   - Use a pool-of-pools architecture for efficient memory management
   - Consider resource binding tiers supported by the device

## 5. ✓ ResourceRegistry

- Higher-level abstraction with integrated view caching
- Manages mapping between resources and indices
- Integrates view creation with descriptor management
- Provides simplified API for resource registration
- Creates optimized descriptor update path for resources
- Features:
  - Thread-safe for multi-threaded resource registration
  - TODO: Name-based resource lookup for engine convenience
  - Type-safe handle system for shader indices
  - Efficient descriptor recycling

### Design Rationale: Integrated View Caching**

The ResourceRegistry design integrates view caching directly rather than relying
on separate view caches. This design decision offers several advantages:

1. **Simplified API Surface**
   - Single call to register a resource directly (no separate view creation step)
   - Resource registration creates views automatically using optimal parameters
   - Same code path can optimize view layouts for access patterns

2. **Reduced Redundancy**
   - No duplicate storage of view metadata between the registry and view cache
   - More memory-efficient representation of resource state
   - Single unified path for view creation and registration

3. **Performance Optimization**
   - Direct control over view creation timing and parameters
   - Ability to batch descriptor copies in the most efficient order
   - Better cache locality with views and descriptors managed together

4. **Better Resource Lifecycle Management**
   - Single owner for both view and descriptor lifetimes
   - Clearer deferred deletion patterns
   - Simplified stale reference detection

## Example API Using Descriptor Structs

> **Note:** The actual implementation uses a unified, generic API based on
> `NativeObject` and `DescriptorHandle`. Registration is performed via templated
> methods on `ResourceRegistry`, and handles are not strongly typed per view
> type. The application is responsible for explicit resource and view creation
> and registration.

```cpp
// Example: View description struct for a resource
struct TextureViewDesc {
    Format format = Format::kUnknown;
    TextureSubResourceSet subResources = TextureSubResourceSet::EntireTexture();
    TextureDimension dimension = TextureDimension::kUnknown;
    bool readOnly = true;
    // ...other fields...
};

// ResourceRegistry usage (generic, templated API)
ResourceRegistry registry;

// Register a resource (keeps a strong reference)
registry.Register(std::shared_ptr<Texture>(...));

// Register a view for a resource (returns a NativeObject handle)
TextureViewDesc desc = { .format = Format::kR8G8B8A8_UNORM, .readOnly = true };
NativeObject view_handle = registry.RegisterView(*texture, desc);

// Find a view by description
if (registry.Contains(*texture, desc)) {
    NativeObject found = registry.Find(*texture, desc);
}

// Unregister a view or resource
registry.UnRegisterView(*texture, view_handle);
registry.UnRegisterResource(*texture);
```

- All registration and lookup is performed via generic, type-safe templated methods.
- `NativeObject` encapsulates the native view or resource handle.
- `DescriptorHandle` encapsulates the shader-visible index for bindless access.
- The application is responsible for creating resources and view descriptions.

## 6. ✓ Root state and descriptor tables setup

### Automatic Bindless Setup Flow

The Oxygen Engine employs an automatic setup flow for bindless rendering that's
integrated into the pipeline state setting process. This design ensures that
bindless descriptor tables are properly configured whenever a pipeline state
is set, providing optimal descriptor management without explicit application
intervention.

Bindless rendering is implemented as a component of `Renderer` and uses the
`DescriptorAllocator` and `ResourceRegistry`. The application calls
`Renderer::AcquireCommandRecorder()` to obtain a command recorder, which is
then set up for rendering by calling `recorder->Begin()`. However, the actual
bindless setup occurs during pipeline state configuration when the application
calls `SetPipelineState()`.

Backend implementations always ensure that the constraints and invariants of the
underlying graphics API are respected, such as for D3D12, only one shader
visible heap of each type (CBV/SRV/UAV and Sampler) will be mapped. For D3D12,
it is also guaranteed that root parameter `0` will be pointed at the CBV/SRV/UAV
heap, and root parameter `1` will be pointed at the Samplers heap (if present).

Shaders can then access any descriptor in these heaps using indices. Such
indices should be exactly as they are in the `ResourceRegistry`.

The sequence diagram below illustrates the precise flow of control and method
calls when setting up the bindless infrastructure:

```
┌───────────┐          ┌──────────┐         ┌───────────────────┐       ┌───────────────┐
│MainModule │          │ Renderer │         │DescriptorAllocator│       │CommandRecorder│
└─────┬─────┘          └────┬─────┘         └──────────┬────────┘       └──────┬────────┘
      │                     │                          │                       │
      │ AcquireCommandRecorder()                       │                       │
      │────────────────────>│                          │                       │
      │                     │                          │                       │
      │                     │ CreateCommandRecorder()  │                       │
      │                     │─────────────────────────────────────────────────>│
      │                     │                          │                       │
      │                     │ Begin()                  │                       │
      │                     │─────────────────────────────────────────────────>│
      │                     │                          │                       │
      │ CommandRecorder     │                          │                       │
      │<────────────────────│                          │                       │
      │                     │                          │                       │
      │ SetPipelineState(desc)                         │                       │
      │───────────────────────────────────────────────────────────────────────>│
      │                     │                          │                       │
      │                     │GetOrCreate[Graphics/Compute]Pipeline()           │
      │                     │<─────────────────────────────────────────────────│
      │                     │                          │                       │
      │                     │                          │ GetShaderVisibleHeaps()
      │                     │                          │<──────────────────────│
      │                     │                          │                       │
      │                     │                          │ ShaderVisibleHeapInfo │
      │                     │                          │──────────────────────>│
      │                     │                          │                       │
      │                     │                          │                       │ SetupDescriptorTables()
      │                     │                          │                       │───────────────────────>│
      │                     │                          │                       │       [internal]
┌─────┴─────┐          ┌────┴─────┐         ┌──────────┴────────┐       ┌──────┴────────┐
│MainModule │          │ Renderer │         │DescriptorAllocator│       │CommandRecorder│
└───────────┘          └──────────┘         └───────────────────┘       └───────────────┘
```

### D3D12 Implementation Details

The Direct3D 12 implementation of this architecture leverages descriptor tables
and root signatures to efficiently bind large numbers of resources:

1. **Root Signature Design**
   - Root signatures contain two descriptor tables: one for CBV/SRV/UAV
     resources and one for Samplers
   - Root parameters use appropriate shader visibility flags to optimize
     descriptor access
   - Tables are configured at fixed root parameter indices for consistent shader
     access

2. **Descriptor Table Organization**
   - `kRootIndex_CBV_SRV_UAV_Table = 0` for textures, buffers, and constant buffers
   - `kRootIndex_Sampler_Table = 1` for sampler states (when present)
   - Each table maps to a D3D12 descriptor heap with
     D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE

3. **Pipeline State Binding Process**
   - When `CommandRecorder::SetPipelineState()` is called, it performs the following steps:
     1. Computes a hash of the pipeline description and calls the renderer to
        get or create a cached graphics/compute pipeline state and root
        signature
     2. Sets the root signature on the D3D12 command list using `SetGraphicsRootSignature()`
     3. Gets shader-visible heaps from `DescriptorAllocator::GetShaderVisibleHeaps()`
     4. Calls `SetupDescriptorTables()` to bind the heaps to the command list
     5. Sets the pipeline state object on the command list using `SetPipelineState()`

4. **Command List Binding Process**
   - `CommandRecorder::SetupDescriptorTables()` receives `ShaderVisibleHeapInfo` structures
   - Sets descriptor heaps on the D3D12 command list using `SetDescriptorHeaps()`
   - Root descriptor tables are set using either
     `SetGraphicsRootDescriptorTable()` or `SetComputeRootDescriptorTable()`
   - GPU descriptor handles point to the base of each descriptor heap

5. **Shader Resource Access**
   - Shaders access resources using resource indices (uint values)
   - HLSL syntax: `resources[resourceIndex].Load(...)` or
     `samplers[samplerIndex].Sample(...)`
   - These indices correspond to offsets from the base GPU descriptor handle

This approach minimizes state changes between draw calls and allows for
efficient dynamic resource binding, significantly reducing API overhead in
complex scenes. The bindless setup is automatically triggered whenever a
pipeline state is set, ensuring that descriptor tables are always properly
configured for the current rendering context.

The actual implementation leverages the `PipelineStateCache` to efficiently
manage pipeline state objects and root signatures, and the `DescriptorAllocator`
to provide shader-visible descriptor heaps that are automatically bound during
pipeline state transitions.

### Example Setup With Direct Binding of the Index Mapping CBV

In this scenario, the constant buffer containing resource indices is bound
directly to the root signature via a root descriptor (CBV), while the SRV/UAV
resources are bound through a descriptor table. This approach uses less root
signature space for the CBV but requires the CBV to be stable in GPU memory.

```cpp
// Root signature layout:
// Root Parameter 0: Direct CBV binding (register b0)
// Root Parameter 1: Descriptor table for SRV/UAV (register t0+)

// 1. Create and setup pipeline state with direct CBV binding
auto pipeline_desc = graphics::GraphicsPipelineDesc::Builder{}
    .SetVertexShader({"FullScreenTriangle.hlsl"})
    .SetPixelShader({"FullScreenTriangle.hlsl"})
    .SetFramebufferLayout({.color_target_formats = {Format::kRGBA8UNorm}})
    // Direct CBV binding at root parameter 0
    .AddRootBinding({
        .binding_slot_desc = {.register_index = 0, .register_space = 0},
        .visibility = graphics::ShaderStageFlags::kVertex,
        .data = graphics::DirectBufferBinding{}
    })
    // Descriptor table for SRVs at root parameter 1
    .AddRootBinding({
        .binding_slot_desc = {.register_index = 0, .register_space = 0},
        .visibility = graphics::ShaderStageFlags::kAll,
        .data = graphics::DescriptorTableBinding{
            .view_type = graphics::ResourceViewType::kStructuredBuffer_SRV,
            .base_index = 0,  // Start of SRVs in the table
            .count = std::numeric_limits<uint32_t>::max()  // Unbounded
        }
    })
    .Build();

// 2. Setup resources for bindless access
auto& descriptor_allocator = renderer->GetDescriptorAllocator();
auto& resource_registry = renderer->GetResourceRegistry();

// Create vertex buffer SRV
auto srv_handle = descriptor_allocator.Allocate(
    graphics::ResourceViewType::kStructuredBuffer_SRV,
    graphics::DescriptorVisibility::kShaderVisible);

graphics::BufferViewDescription srv_desc{
    .view_type = graphics::ResourceViewType::kStructuredBuffer_SRV,
    .visibility = graphics::DescriptorVisibility::kShaderVisible,
    .format = Format::kUnknown,
    .stride = sizeof(Vertex)
};

auto view = vertex_buffer->GetNativeView(srv_handle, srv_desc);
resource_registry.RegisterView(*vertex_buffer, view, std::move(srv_handle), srv_desc);

// Get the shader-visible index for the SRV (this is the index the shader uses)
uint32_t vertex_srv_index = descriptor_allocator.GetShaderVisibleIndex(srv_handle);

// 3. Create constant buffer with resource indices
graphics::BufferDesc cb_desc{
    .size_bytes = sizeof(uint32_t),
    .usage = graphics::BufferUsage::kConstantBuffer,
    .memory = graphics::BufferMemory::kUpload
};
auto constant_buffer = renderer->CreateBuffer(cb_desc);

// Write the SRV index to the constant buffer
void* mapped = constant_buffer->Map();
memcpy(mapped, &vertex_srv_index, sizeof(vertex_srv_index));
constant_buffer->UnMap();

// 4. Bind during rendering
recorder.SetPipelineState(pipeline_desc);
// Direct CBV binding to root parameter 0
recorder.SetGraphicsRootConstantBufferView(0, constant_buffer->GetGpuAddress());
// Descriptor table is automatically bound by SetPipelineState()
```

### Example Setup With the Index Mapping CBV as a Range in the Descriptor Table

In this scenario, both the constant buffer (CBV) and shader resources (SRV/UAV)
are bound through a single descriptor table, but with separate ranges for CBV
and SRV bindings. This maintains the correct register mappings: CBV at register
`b0` and SRVs at register `t0, space0`.

```cpp
// Root signature layout:
// Root Parameter 0: Descriptor table containing two ranges:
//   - Range 0: CBV at register b0 (heap index 0)
//   - Range 1: SRVs at register t0, space0 (heap index 1+)

// 1. Create and setup pipeline state with unified descriptor table
auto pipeline_desc = graphics::GraphicsPipelineDesc::Builder{}
    .SetVertexShader({"FullScreenTriangle.hlsl"})
    .SetPixelShader({"FullScreenTriangle.hlsl"})
    .SetFramebufferLayout({.color_target_formats = {Format::kRGBA8UNorm}})
    // CBV range in the descriptor table (register b0)
    .AddRootBinding({
        .binding_slot_desc = {.register_index = 0, .register_space = 0},
        .visibility = graphics::ShaderStageFlags::kAll,
        .data = graphics::DescriptorTableBinding{
            .view_type = graphics::ResourceViewType::kConstantBuffer,
            .base_index = 0,  // CBV at heap index 0
            .count = 1        // Only one CBV
        }
    })
    // SRV range in the same descriptor table (register t0, space0)
    .AddRootBinding({
        .binding_slot_desc = {.register_index = 0, .register_space = 0},
        .visibility = graphics::ShaderStageFlags::kAll,
        .data = graphics::DescriptorTableBinding{
            .view_type = graphics::ResourceViewType::kStructuredBuffer_SRV,
            .base_index = 1,  // SRVs start at heap index 1
            .count = std::numeric_limits<uint32_t>::max()  // Unbounded SRVs
        }
    })
    .Build();

// 2. Setup resources following engine invariants
auto& descriptor_allocator = renderer->GetDescriptorAllocator();
auto& resource_registry = renderer->GetResourceRegistry();

// === CBV Setup (Always at heap index 0) ===
auto cbv_handle = descriptor_allocator.Allocate(
    graphics::ResourceViewType::kConstantBuffer,
    graphics::DescriptorVisibility::kShaderVisible);

// The CBV must be allocated at heap index 0 for engine compatibility
// (This is ensured by the DescriptorAllocator implementation)

graphics::BufferDesc cb_desc{
    .size_bytes = sizeof(uint32_t),
    .usage = graphics::BufferUsage::kConstantBuffer,
    .memory = graphics::BufferMemory::kUpload
};
auto constant_buffer = renderer->CreateBuffer(cb_desc);

graphics::BufferViewDescription cbv_desc{
    .view_type = graphics::ResourceViewType::kConstantBuffer,
    .visibility = graphics::DescriptorVisibility::kShaderVisible
};

auto cbv_view = constant_buffer->GetNativeView(cbv_handle, cbv_desc);
resource_registry.RegisterView(*constant_buffer, cbv_view, std::move(cbv_handle), cbv_desc);

// === SRV Setup (Starting at heap index 1+) ===
auto srv_handle = descriptor_allocator.Allocate(
    graphics::ResourceViewType::kStructuredBuffer_SRV,
    graphics::DescriptorVisibility::kShaderVisible);

graphics::BufferViewDescription srv_desc{
    .view_type = graphics::ResourceViewType::kStructuredBuffer_SRV,
    .visibility = graphics::DescriptorVisibility::kShaderVisible,
    .format = Format::kUnknown,
    .stride = sizeof(Vertex)
};

auto srv_view = vertex_buffer->GetNativeView(srv_handle, srv_desc);
resource_registry.RegisterView(*vertex_buffer, srv_view, std::move(srv_handle), srv_desc);

// Get shader-visible index (this will be >= 1 for SRVs)
uint32_t vertex_srv_shader_index = descriptor_allocator.GetShaderVisibleIndex(srv_handle);

// 3. Update constant buffer with correct SRV index
// CRITICAL: Write the shader-visible index, not the global heap index
// This index corresponds to the offset within the bound descriptor table
void* mapped = constant_buffer->Map();
memcpy(mapped, &vertex_srv_shader_index, sizeof(vertex_srv_shader_index));
constant_buffer->UnMap();

// 4. Rendering - descriptor table is automatically bound
recorder.SetPipelineState(pipeline_desc);
// No additional binding needed - SetPipelineState() automatically:
// - Sets root signature with descriptor table at root parameter 0
// - Binds shader-visible heaps via SetupDescriptorTables()
// - Ensures CBV is at table offset 0, SRVs at table offset 1+

// 5. Shader access pattern (HLSL)
/*
// In the vertex shader (FullScreenTriangle.hlsl):
cbuffer IndexMappingCB : register(b0) {
    uint vertexBufferIndex;  // Contains the SRV index
};

StructuredBuffer<Vertex> resources[] : register(t0, space0);

// Access the vertex buffer using the index from the CBV:
Vertex vertex = resources[vertexBufferIndex].Load(vertexId);
*/
```

**Key Differences:**

1. **Direct CBV Binding**: The constant buffer is bound directly via
   `SetGraphicsRootConstantBufferView()`, requiring a stable GPU address but
   using less root signature space.

2. **Table-Based CBV**: The constant buffer is part of the descriptor table,
   following the engine's invariant that CBVs are always at heap index 0. This
   provides consistency but uses more descriptor table space.

3. **Index Handling**: In both cases, the SRV indices written to the constant
   buffer must match the shader-visible indices returned by
   `GetShaderVisibleIndex()`, not global heap indices.

4. **Automatic Binding**: The engine's `SetPipelineState()` automatically
   handles descriptor table binding for the unified approach, while direct
   binding requires explicit `SetGraphicsRootConstantBufferView()` calls.

## 7. ✓ Testing Implemented

- `src/Oxygen/Graphics/Common/Test/DescriptorHandle_test.cpp`
  *Unit tests for DescriptorHandle, covering creation, movement, validity, and RAII behavior.*

- `src/Oxygen/Graphics/Common/Test/DescriptorHeapSegment_test.cpp`
  *Unit tests for descriptor heap segments, covering allocation, release, recycling, and error conditions.*

- `src/Oxygen/Graphics/Common/Test/Bindless/StaticDescriptorHeapSegment_test.cpp`
  *Unit tests for StaticDescriptorHeapSegment, covering all supported ResourceViewTypes.*

- `src/Oxygen/Graphics/Common/Test/ResourceRegistry_test.cpp`
  *Unit tests for ResourceRegistry, covering registration, view caching, error handling, concurrency, and lifecycle.*

- `src/Oxygen/Graphics/Common/Test/Bindless/BaseDescriptorAllocator_Basic_test.cpp`
  *Unit tests for basic allocation and recycling behavior of the base descriptor allocator.*

- `src/Oxygen/Graphics/Common/Test/Bindless/BaseDescriptorAllocator_Concurrency_test.cpp`
  *Unit tests for multi-threaded allocation and release in the base descriptor allocator.*

- `src/Oxygen/Graphics/Common/CMakeLists.txt`
  *Build configuration for the common graphics module and its tests.*

- `src/Oxygen/Graphics/Direct3D12/Bindless/DescriptorHeapSegment.h`
  *Declares the D3D12-specific DescriptorHeapSegment, managing a segment of a D3D12 descriptor heap.*

- `src/Oxygen/Graphics/Direct3D12/Bindless/DescriptorHeapSegment.cpp`
  *Implements the D3D12 DescriptorHeapSegment, including heap creation, handle translation, and resource management.*

- `src/Oxygen/Graphics/Direct3D12/Bindless/D3D12HeapAllocationStrategy.h`
  *Declares the D3D12 heap allocation strategy, mapping view types and visibilities to D3D12 heap types and configurations.*

- `src/Oxygen/Graphics/Direct3D12/Bindless/D3D12HeapAllocationStrategy.cpp`
  *Implements the D3D12 heap allocation strategy, including heap key construction and capacity management.*

- `src/Oxygen/Graphics/Direct3D12/Test/DescriptorHeapSegment_test.cpp`
  *Unit tests for the D3D12 DescriptorHeapSegment implementation.*

- `src/Oxygen/Graphics/Direct3D12/Test/Bindless/D3D12HeapAllocationStrategy_test.cpp`
  *Unit tests for the D3D12 heap allocation strategy.*

## 8. ✓ Thread Safety Design

The `BaseDescriptorAllocator` ensures thread safety through several mechanisms:

- **Synchronized Core Operations**: All allocation and release operations are
  protected by a mutex to prevent race conditions when multiple threads access
  the same descriptor heap.
- **Safe Index Management**: The implementation ensures that indices are
  uniquely allocated and properly tracked even under concurrent access.
- **Atomic State Management**: Segment state changes are atomic operations to
  prevent partial updates.
- **Fail-Fast Error Handling**: Any thread-safety violations result in clear
  error messages rather than undefined behavior.

These protections allow descriptors to be safely allocated and released from any
thread in the engine, enabling parallel processing of resource loading and view
creation.

## Alternatives Considered

Throughout the implementation, several design alternatives were considered:

1. **Segmentation Strategy**:
   - **Adopted**: Fixed-size segments with growth through creation of additional segments
   - **Alternative**: Dynamically resizable segments using vector reallocation
   - **Rationale**: Fixed segments provide better cache locality and avoid costly vector reallocations

2. **Heap Mapping Strategy**:
   - **Adopted**: String-based heap key mapping with declarative configuration
   - **Alternative**: Enum-based direct mapping with constexpr lookup tables
   - **Rationale**: String-based approach provides better extensibility and debugging, at minimal runtime cost since mapping occurs only during initialization

3. **Thread Safety Approach**:
   - **Adopted**: Global mutex per allocator
   - **Alternative**: Fine-grained locking per segment
   - **Rationale**: Simpler implementation with negligible performance impact for most use cases. Applications with extreme thread contention can implement custom allocators.

4. **Memory Management**:
   - **Adopted**: LIFO recycling for better cache locality
   - **Alternative**: More complex strategies like defragmentation or best-fit
   - **Rationale**: LIFO provides good performance characteristics while maintaining simplicity

## Future Enhancements & Considerations

- **Default Resources**: Implement a system for default/fallback resources
  (e.g., white texture, identity normal map) to handle missing or invalid
  resources gracefully.
- **View Update/Replacement**: Implement the `UpdateView` and resource
  replacement APIs to allow in-place view updates and resource hot-swapping
  while keeping descriptor indices stable.
- **Debugging & Validation Tools**: Integrate tools for descriptor validation,
  resource versioning, and GPU-based resource debugging.
- **Resource Versioning**: Add explicit resource versioning to detect when
  resources or views have been updated or replaced.
- **Update Batching**: Provide explicit API/documentation for update batching
  and efficient descriptor copy operations.
- **Fine-grained Locking**: Optionally support fine-grained locking per segment
  for applications with extreme thread contention.
- **Memory Management Enhancements**: Consider more advanced memory management
  strategies such as generational indices, defragmentation, or best-fit
  allocation.
- **API Extensions**: Consider adding higher-level helpers or more ergonomic
  APIs for common scenarios, while keeping the core API generic and explicit.
- **Backend-specific Optimizations**: Continue to refine backend abstractions
  and optimize for D3D12/Vulkan/other APIs as needed.

## Key Features from Leading Engines

1. **Tiered Resource Access**
   - Fast paths for frequently accessed resources (engine defaults, etc.)
   - Placement at the beginning of descriptor arrays for better cache behavior
   - Special handling for common resources (shadow maps, environment probes, etc.)

2. **Smart Batching**
   - Coalescing descriptor updates to minimize API calls
   - Tracking dirty states to avoid redundant updates
   - Pipeline optimizations to reduce GPU stalls during updates

3. **Default Resources**
   - Engine-provided fallbacks (white texture, identity normal map)
   - Automatic substitution when accessing invalid indices
   - Debug visualizations for invalid resources

4. **Debug Validation**
   - Resource annotations and debug names
   - Validation of descriptor access patterns
   - Tracking resource versioning to detect stale descriptors
   - Usage statistics and performance analysis tools

5. **Memory Management**
   - Descriptor recycling with generational indices
   - Efficient free list implementation
   - Deferred resource cleanup with frame-based lifetime tracking
