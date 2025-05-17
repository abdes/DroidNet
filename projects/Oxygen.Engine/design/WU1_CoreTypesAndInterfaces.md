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

**Files Created/Modified:**

- `src/Oxygen/Graphics/Common/Types/DescriptorVisibility.h` - Defines memory residence for descriptors
- `src/Oxygen/Graphics/Common/Types/IndexRange.h` - Defines mapping between local and global index spaces
- `src/Oxygen/Graphics/Common/DescriptorHandle.h` - Defines descriptor handle type
- `src/Oxygen/Graphics/Common/Detail/DescriptorHeapSegment.h` - Defines `DescriptorHeapSegment` interface and `FixedDescriptorHeapSegment` implementation.
- `src/Oxygen/Graphics/Common/DescriptorAllocator.h` - Defines allocator interface
- `src/Oxygen/Graphics/Common/ResourceRegistry.h` - Defines resource registry
- `src/Oxygen/Graphics/Common/CMakeLists.txt` - Added new files
- `src/Oxygen/Graphics/Common/Test/DescriptorHeapSegment_test.cpp` - Unit tests for `DescriptorHeapSegment` implementations.
- `design/WU1_CoreTypesAndInterfaces.md` - This design document, updated with `DescriptorHeapSegment` details.

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

   - Represents an allocated descriptor slot with stable index for shader reference
   - Has limited ownership semantics - can release its descriptor back to the allocator
     but doesn't own the underlying resource
   - Contains back-reference to allocator for lifetime management
   - Non-copyable, movable semantics to enforce ownership rules
   - Clear RAII behavior with automatic release in destructor and explicit Release() method
   - Stores ResourceViewType and DescriptorVisibility to track descriptor type and memory location
   - Provides GetIndex(), GetViewType(), GetVisibility() and IsValid() inspection methods
   - Does not directly expose platform-specific handles - these are accessed through the DescriptorAllocator

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
     - **Efficient State Tracking**: Uses a vector of boolean flags to track which
       indices have been released, allowing quick validation during release operations.

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
       appropriate `DescriptorHeapSegment`.
     - Facilitates copying descriptors between visibility spaces through the
       `CopyDescriptor` method.
     - Exposes methods to get associated platform-specific handles/pointers for
       descriptors via `GetNativeHandle`.
     - Prepares resources for rendering via `PrepareForRendering` by ensuring necessary
       heaps/sets are bound.
     - Provides utility methods like `GetRemainingDescriptorsCount`, `GetAllocatedDescriptorsCount`,
       and `Contains` for descriptor management.
     - Uses `ResourceViewType` enum to identify descriptor type (SRV, UAV, CBV,
       etc.) when interacting with segments.

   - **Ownership Model**:
      - Owns the descriptor heaps/pools
      - Creates descriptor handles that reference it for lifecycle management
      - Does not own the resources being described (textures, buffers, etc.)
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

## 5. ResourceRegistry

   - Higher-level abstraction with integrated view caching
   - Manages mapping between resources and indices
   - Integrates view creation with descriptor management
   - Provides simplified API for resource registration
   - Creates optimized descriptor update path for resources
   - Features:
     - Thread-safe for multi-threaded resource registration
     - Name-based resource lookup for engine convenience
     - Lazy creation of views only when needed
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

5. **Implementation Flow**
   ```
   Resource → ResourceRegistry → View Creation → Descriptor Update → Shader Index
   ```
   (compared to previous flow)
   ```
   Resource → DefaultViewCache → Views → ResourceRegistry → Shader Indices
   ```

## Example API Using Descriptor Structs

```cpp
// View descriptor structs with defaults
struct TextureViewDesc {
    Format format = Format::kUnknown;  // Default to texture's native format
    TextureSubResourceSet subResources = TextureSubResourceSet::EntireTexture();
    TextureDimension dimension = TextureDimension::kUnknown;  // Default to texture's dimension
    std::string debugName;  // Optional name for lookup
    bool readOnly = true;   // Read-only access (SRV vs UAV)
};

struct BufferViewDesc {
    Format format = Format::kUnknown;
    BufferRange range = {};  // Default to entire buffer
    uint32_t stride = 0;     // For structured buffers
    std::string debugName;   // Optional name for lookup
    bool readOnly = true;    // Read-only access (SRV vs UAV)
};

struct SamplerViewDesc {
    SamplerDesc samplerDesc = {};  // Default sampler parameters
    std::string debugName;         // Optional name for lookup
};

// Resource registry with explicit methods and descriptors
class ResourceRegistry {
public:
    // Texture registration with explicit view type in method name
    TextureSrvHandle RegisterTextureSRV(
        const std::shared_ptr<Texture>& texture,
        const TextureViewDesc& desc = {});

    TextureUavHandle RegisterTextureUAV(
        const std::shared_ptr<Texture>& texture,
        const TextureViewDesc& desc = {});

    // Buffer registration with explicit view type in method name
    RawBufferSrvHandle RegisterBufferSRV(
        const std::shared_ptr<Buffer>& buffer,
        const BufferViewDesc& desc = {});

    RawBufferUavHandle RegisterBufferUAV(
        const std::shared_ptr<Buffer>& buffer,
        const BufferViewDesc& desc = {});

    // Sampler registration
    SamplerHandle RegisterSampler(
        const SamplerDesc& samplerDesc,
        const SamplerViewDesc& desc = {});

    // Resource lookup by name
    TextureSrvHandle FindTextureSRVByName(const std::string& name);
    TextureUavHandle FindTextureUAVByName(const std::string& name);

    // Update existing bindings
    void UpdateTexture(TextureSrvHandle handle, const std::shared_ptr<Texture>& newTexture);
    void UpdateBuffer(RawBufferSrvHandle handle, const std::shared_ptr<Buffer>& newBuffer);

    // Unregister resources
    void Unregister(TextureSrvHandle handle);
    void Unregister(RawBufferSrvHandle handle);
    // Additional type-safe unregister overloads for each handle type
};
```

## Usage Examples

```cpp
// Minimal case - register texture SRV with defaults
auto albedoHandle = registry.RegisterTextureSRV(albedoTexture);

// Named registration with default view parameters
auto normalHandle = registry.RegisterTextureSRV(normalTexture, {
    .debugName = "material/metal/normal"
});

// Fully specified UAV registration
auto heightfieldHandle = registry.RegisterTextureUAV(heightfieldTexture, {
    .format = Format::kR32_FLOAT,
    .subResources = {0, 1, 0, 1},  // Just base mip
    .dimension = TextureDimension::kTexture2D,
    .debugName = "terrain/heightfield_data",
    .readOnly = false  // Explicit UAV
});

// Use handle in material parameters
MaterialConstants materialData;
materialData.albedoTextureIndex = albedoHandle.GetIndex();
materialData.normalTextureIndex = normalHandle.GetIndex();

// Later, update a texture without changing its index
registry.UpdateTexture(albedoHandle, newAlbedoTexture);

// Look up by name when needed
auto terrainHeightfield = registry.FindTextureUAVByName("terrain/heightfield_data");
```

## Implementation Notes

1. **Backend Abstraction**:
   - Backend-specific view creation now happens within the ResourceRegistry implementations
   - Default implementations to provide a common base
   - Each backend specializes for optimal performance

2. **Resource Tracking**:
   - Registry tracks resource versions and handles
   - Weak references to resources for proper lifecycle management
   - Thread-safe reference counting for concurrent registration

3. **Memory Management**:
   - Reference-counted view and descriptor objects
   - Pooled allocators for descriptor handles
   - Recycling of slots from deleted resources

4. **API Balance**:
   - Low-level API still available when needed
   - Helper functions for common scenarios
   - Typed handle approach prevents errors

## Additional notes

1. Thread-Safety Boundaries: Be explicit about which operations are thread-safe
   and which are not. Particularly, descriptor allocation may be thread-safe,
   but resources should only be bound from the render thread.
2. Default Resources: Consider implementing a system for "default" resources
   (white texture, identity normal map, etc.) to handle missing or invalid
   resources gracefully.
3. Update Batching: Be explicit about how update batching works in the API and
   documentation.
4. Resource Versioning: Consider adding an explicit resource versioning
   mechanism to detect when resources have been updated.
5. Debugging Tools: Plan for debugging tools like descriptor validation and
   GPU-based resource debugging from the start.
6. Documentation Format: Use consistent doxygen formatting for all public APIs
   to ensure comprehensive documentation.
7. Memory Management: Be explicit about the deferred resource cleanup mechanism
   and how it integrates with the per-frame resource manager.

## Testing Implemented

- `src/Oxygen/Graphics/Common/Test/DescriptorHandle_test.cpp` - Tests handle creation, movement, validity, and RAII behavior
  - Tests resource type tracking using ResourceViewType
  - Tests visibility tracking using DescriptorVisibility
  - Tests move semantics and ownership transfer
  - Tests proper release behavior in destructors
- `src/Oxygen/Graphics/Common/Test/DescriptorHeapSegment_test.cpp` - Comprehensive tests for descriptor heap segments, covering:
  - Construction, initial state, and core accessor methods.
  - Sequential allocation until segment is full.
  - Allocation from empty segment and full segment (expecting sentinel).
  - Single descriptor release and immediate reallocation (LIFO for `FixedDescriptorHeapSegment`).
  - Multiple descriptor releases and subsequent reallocations.
  - Releasing unallocated indices (never allocated, or beyond current `next_index_`).
  - Releasing already released indices (double release).
  - Releasing indices out of the segment's bounds.
  - Behavior with zero-capacity segments (where applicable).
  - Typed tests to ensure consistent behavior across different configurations.
- `src/Oxygen/Graphics/Common/Test/ResourceRegistry_test.cpp` - Integrated view cache functionality
- `src/Oxygen/Graphics/Common/Test/CMakeLists.txt` - Added tests to build system
- `src/Oxygen/Graphics/Common/Test/Bindless/BaseDescriptorAllocator_Basic_test.cpp` - Core allocation tests:
  - Segment creation on first allocation
  - Reuse of existing segments for subsequent allocations
  - Descriptor recycling and handle reuse
  - Multiple visibility and view type combinations
  - Descriptor copying between visibility spaces
  - Error handling for invalid releases

- `src/Oxygen/Graphics/Common/Test/Bindless/BaseDescriptorAllocator_Concurrency_test.cpp` - Multi-threaded stress testing:
  - Allocations and releases from multiple concurrent threads
  - Thread safety verification with different resource types
  - Resource index verification across thread boundaries
  - Race condition detection
  - Proper state management when multiple threads operate on the same heap

## Thread Safety Design

The `BaseDescriptorAllocator` ensures thread safety through several mechanisms:

- **Synchronized Core Operations**: All allocation and release operations are protected by a mutex to prevent race conditions when multiple threads access the same descriptor heap.
- **Safe Index Management**: The implementation ensures that indices are uniquely allocated and properly tracked even under concurrent access.
- **Atomic State Management**: Segment state changes are atomic operations to prevent partial updates.
- **Fail-Fast Error Handling**: Any thread-safety violations result in clear error messages rather than undefined behavior.

These protections allow descriptors to be safely allocated and released from any thread in the engine, enabling parallel processing of resource loading and view creation.

## Design Evolution and Alternatives Considered

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
