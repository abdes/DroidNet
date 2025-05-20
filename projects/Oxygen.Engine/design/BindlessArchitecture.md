# Bindless Descriptor Allocator Design

## Overview

This document outlines the design of a cross-platform bindless descriptor
allocator for modern rendering APIs (D3D12 and Vulkan). The system enables
efficient management of graphics resources using index-based identification
rather than API-specific descriptor handles.

## Design Goals

- **API Agnostic**: Works identically across D3D12 and Vulkan
- **Fully Bindless**: Resources are accessed via indices in shaders
- **High Performance**: Minimizes state changes and descriptor updates
- **Dynamic Resource Management**: Efficient allocation and recycling
- **Scalable**: Support for thousands of unique resources
- **Thread-Safe**: Concurrent resource registration and management
- **Memory Efficient**: Minimize descriptor memory footprint

## Design Rationale

### Why Bindless?

Traditional binding models require setting descriptors for each draw call,
causing significant CPU overhead at scale. Bindless rendering addresses this by:

1. **Reduced API Calls**: Bind descriptor heaps/sets once, not per material or object
2. **Simplified Material System**: Materials reference resources by index, not by descriptor
3. **Unlimited Resources**: Access to far more textures than allowed in traditional binding slots
4. **Better Batching**: Draw calls can be batched without changing descriptor states
5. **Dynamic Resource Access**: Enables data-driven shader techniques like material atlasing
6. **Lower CPU Overhead**: Fewer API calls to set descriptor tables/sets
7. **Better Cache Coherency**: Stable descriptor heap locations improve GPU cache behavior

### Architecture Decisions

1. **Non-Shader-Visible Descriptors**: Used for fast CPU-side updates
2. **Shader-Visible Descriptors**: Used for GPU access in shaders
3. **Global Index Space**: Resources identified by indices regardless of type
4. **Lazy Updates**: Descriptors copied from CPU to GPU only when needed
5. **Central Resource Management**: Single system manages all descriptors
6. **Pooled Allocation**: Reuse of descriptor slots after resource deletion
7. **Persistent Mappings**: Long-lived descriptor heaps/sets to avoid recreation costs

## Core Components

### 1. Unified Descriptor Types

```cpp
enum class ResourceViewType {
    SRV,    // Shader Resource View
    UAV,    // Unordered Access View
    CBV,    // Constant Buffer View
    RTV,    // Render Target View
    DSV,    // Depth Stencil View
    SAMPLER // Sampler
};

enum class DescriptorVisibility {
    kShaderVisible,  // GPU-accessible
    kCpuOnly         // CPU-side only, not visible to shaders
};
```

### 2. Descriptor Handle

```cpp
class DescriptorHandle {
public:
    static constexpr uint32_t kInvalidIndex = std::numeric_limits<uint32_t>::max();

    DescriptorHandle() = default;
    ~DescriptorHandle();

    // Non-copyable, movable
    DescriptorHandle(const DescriptorHandle&) = delete;
    DescriptorHandle& operator=(const DescriptorHandle&) = delete;
    DescriptorHandle(DescriptorHandle&&) noexcept;
    DescriptorHandle& operator=(DescriptorHandle&&) noexcept;

    [[nodiscard]] bool IsValid() const { return index_ != kInvalidIndex && allocator_ != nullptr; }
    [[nodiscard]] uint32_t GetIndex() const { return index_; }
    [[nodiscard]] ResourceViewType GetViewType() const { return viewType_; }
    [[nodiscard]] DescriptorVisibility GetVisibility() const { return visibility_; }
    void Release();

private:
    friend class DescriptorAllocator;

    DescriptorHandle(DescriptorAllocator* allocator, uint32_t index,
                     ResourceViewType viewType, DescriptorVisibility visibility)
        : allocator_(allocator), index_(index), viewType_(viewType), visibility_(visibility) {}

    DescriptorAllocator* allocator_ = nullptr;
    uint32_t index_ = kInvalidIndex;
    ResourceViewType viewType_ = ResourceViewType::SRV;
    DescriptorVisibility visibility_ = DescriptorVisibility::kShaderVisible;
};
```

### 3. Allocator Interface

```cpp
struct DescriptorAllocatorConfig {
    // Resource limits
    uint32_t maxSRVs = 500000;                // Maximum SRVs
    uint32_t maxUAVs = 64000;                 // Maximum UAVs
    uint32_t maxCBVs = 16000;                 // Maximum CBVs
    uint32_t maxSamplers = 2048;              // Maximum samplers
    uint32_t maxRTVs = 1024;                  // Maximum RTVs
    uint32_t maxDSVs = 1024;                  // Maximum DSVs

    // Growth policy
    bool enableDynamicGrowth = true;          // Allow dynamic growth of pools/heaps
    float growthFactor = 2.0f;                // Multiplication factor when expanding
    uint32_t maxGrowthIterations = 3;         // Maximum number of growth cycles

    // Memory optimization
    bool useSeparateHeaps = false;            // Use separate heaps for different descriptor types
    uint32_t initialFreeListCapacity = 1024;  // Initial capacity for descriptor recycling lists

    // Update strategy
    bool enableBatchedUpdates = true;         // Batch descriptor updates when possible
    uint32_t maxUpdatesPerBatch = 256;        // Maximum descriptor updates per batch operation

    // Threading model
    bool enableThreadSafety = true;           // Enable thread-safe descriptor allocation
    uint32_t perThreadCacheSize = 64;         // Number of descriptors to cache per thread

    // Debug options
    bool enableValidation = true;             // Additional validation in debug builds
    bool trackDescriptorUsage = false;        // Track descriptor usage patterns (dev only)
};

class DescriptorAllocator {
public:
    virtual ~DescriptorAllocator() = default;

    // Allocate a descriptor of specified type
    [[nodiscard]] virtual DescriptorHandle Allocate(
        ResourceViewType viewType,
        DescriptorVisibility visibility = DescriptorVisibility::kShaderVisible) = 0;

    // Free a descriptor
    virtual void Free(DescriptorHandle& handle) = 0;

    // Copy from a CPU-only descriptor to a shader-visible descriptor
    virtual void CopyToShaderVisible(const DescriptorHandle& src, const DescriptorHandle& dst) = 0;

    // Get platform-specific pointer for descriptor updates (CPU side)
    [[nodiscard]] virtual NativeObject GetCpuHandle(const DescriptorHandle& handle) = 0;

    // Get platform-specific pointer for descriptor (GPU side)
    [[nodiscard]] virtual NativeObject GetGpuHandle(const DescriptorHandle& handle) = 0;

    // Prepare descriptors for use in rendering (bind heaps/sets as needed)
    virtual void PrepareForRendering(CommandList* commandList) = 0;

    // Factory method for creating platform-specific allocators
    static std::unique_ptr<DescriptorAllocator> Create(
        Device* device,
        const DescriptorAllocatorConfig& config);

    // Optional methods for advanced use cases
    virtual void BatchBegin() {}
    virtual void BatchEnd() {}
    virtual void SetDebugName(const DescriptorHandle& handle, const char* name) {}
};
```

### 4. Resource Manager

```cpp
class ResourceRegistry {
public:
    // Initialize with a device and maximum resource counts
    static std::shared_ptr<ResourceRegistry> Create(Device* device);

    explicit ResourceRegistry(std::shared_ptr<DescriptorAllocator> allocator);

    // Register different resource types with explicit view types
    TextureSrvHandle RegisterTextureSRV(
        const std::shared_ptr<Texture>& texture,
        const TextureViewDesc& desc = {});

    TextureUavHandle RegisterTextureUAV(
        const std::shared_ptr<Texture>& texture,
        const TextureViewDesc& desc = {});

    RawBufferSrvHandle RegisterBufferSRV(
        const std::shared_ptr<Buffer>& buffer,
        const BufferViewDesc& desc = {});

    RawBufferUavHandle RegisterBufferUAV(
        const std::shared_ptr<Buffer>& buffer,
        const BufferViewDesc& desc = {});

    SamplerHandle RegisterSampler(
        const SamplerDesc& samplerDesc,
        const SamplerViewDesc& desc = {});

    // Resource lookup by name
    TextureSrvHandle FindTextureSRVByName(const std::string& name);
    TextureUavHandle FindTextureUAVByName(const std::string& name);

    // Update existing bindings
    void UpdateTexture(TextureSrvHandle handle, const std::shared_ptr<Texture>& newTexture);
    void UpdateBuffer(RawBufferSrvHandle handle, const std::shared_ptr<Buffer>& newBuffer);

    // Remove and free a resource
    void Unregister(TextureSrvHandle handle);
    void Unregister(RawBufferSrvHandle handle);
    // Additional type-safe unregister overloads for each handle type

    // Prepare for rendering (bind descriptor heaps/sets as needed)
    void PrepareForRendering(CommandList* commandList);

    // Get the underlying allocator
    std::shared_ptr<DescriptorAllocator> GetAllocator() const { return allocator_; }

private:
    std::shared_ptr<DescriptorAllocator> allocator_;
    // Internal storage for registered resources and their view descriptors
};
```

## Implementation Details

### D3D12 Implementation

The D3D12 implementation uses descriptor heaps to store the resources:

```cpp
class D3D12DescriptorAllocator final : public DescriptorAllocator {
public:
    D3D12DescriptorAllocator(ID3D12Device* device, const DescriptorAllocatorConfig& config);
    ~D3D12DescriptorAllocator() override;

    // Core interface implementation
    [[nodiscard]] DescriptorHandle Allocate(ResourceViewType viewType, DescriptorVisibility visibility) override;
    void Free(DescriptorHandle& handle) override;
    void CopyToShaderVisible(const DescriptorHandle& src, const DescriptorHandle& dst) override;
    [[nodiscard]] NativeObject GetCpuHandle(const DescriptorHandle& handle) override;
    [[nodiscard]] NativeObject GetGpuHandle(const DescriptorHandle& handle) override;
    void PrepareForRendering(CommandList* commandList) override;

    // Optional advanced interface
    void BatchBegin() override;
    void BatchEnd() override;
    void SetDebugName(const DescriptorHandle& handle, const char* name) override;

private:
    struct HeapInfo {
        std::unique_ptr<detail::DescriptorHeap> shaderVisible;
        std::unique_ptr<detail::DescriptorHeap> cpuOnly;
        IndexRange indexRange;
        D3D12_DESCRIPTOR_HEAP_TYPE d3dType;

        // Enhanced memory management
        std::vector<uint32_t> freeList;
        std::vector<bool> allocationMap;

        // Threading support
        std::mutex mutex;

        // Batching support
        struct DeferredCopy {
            uint32_t srcIndex;
            uint32_t dstIndex;
        };
        std::vector<DeferredCopy> pendingCopies;
    };

    D3D12_DESCRIPTOR_HEAP_TYPE GetD3D12HeapType(ResourceViewType viewType) const;
    HeapInfo& GetHeapForType(ResourceViewType viewType);
    uint32_t GetGlobalIndex(ResourceViewType viewType, uint32_t localIndex) const;

    // Enhanced implementation details
    void GrowHeap(HeapInfo& heapInfo, ResourceViewType viewType);
    bool TryAllocateFromFreeList(HeapInfo& heapInfo, uint32_t& outIndex);
    void ExecuteBatchedCopies();

    std::mutex globalMutex_;
    ID3D12Device* device_;
    std::array<HeapInfo, 6> heaps_; // one per ResourceViewType
    DescriptorAllocatorConfig config_;
    bool batchModeActive_ = false;

    // Debug support
    std::unordered_map<uint32_t, std::string> debugNames_;
};
```

### Key D3D12 Considerations:

- Uses descriptor heaps with appropriate flags for shader visibility
- Generates global indices mapped to specific heap locations
- Sets descriptor heaps before rendering
- Manages descriptor size differences between types
- Handles heap fragmentation through free lists
- Uses copy queues for efficient descriptor updates
- Implements dynamic heap growth when needed
- Provides thread-safe allocation with per-heap mutexes
- Supports batched updates for improved performance
- Includes debug validation for API correctness

### Vulkan Implementation

The Vulkan implementation uses descriptor pools and sets:

```cpp
class VulkanDescriptorAllocator final : public DescriptorAllocator {
public:
    VulkanDescriptorAllocator(VkDevice device, const DescriptorAllocatorConfig& config);
    ~VulkanDescriptorAllocator() override;

    // Core interface implementation
    [[nodiscard]] DescriptorHandle Allocate(ResourceViewType viewType, DescriptorVisibility visibility) override;
    void Free(DescriptorHandle& handle) override;
    void CopyToShaderVisible(const DescriptorHandle& src, const DescriptorHandle& dst) override;
    [[nodiscard]] NativeObject GetCpuHandle(const DescriptorHandle& handle) override;
    [[nodiscard]] NativeObject GetGpuHandle(const DescriptorHandle& handle) override;
    void PrepareForRendering(CommandList* commandList) override;

    // Optional advanced interface
    void BatchBegin() override;
    void BatchEnd() override;
    void SetDebugName(const DescriptorHandle& handle, const char* name) override;

private:
    struct PoolInfo {
        VkDescriptorPool pool;
        VkDescriptorSet descriptorSet;
        std::vector<uint32_t> freeList;
        std::vector<bool> allocationMap;
        VkDescriptorType vkType;
        IndexRange indexRange;

        // Threading support
        std::mutex mutex;

        // Batching support
        struct UpdateEntry {
            uint32_t dstIndex;
            VkDescriptorImageInfo imageInfo;
            VkDescriptorBufferInfo bufferInfo;
            VkWriteDescriptorSetAccelerationStructureKHR accelStructInfo;
        };
        std::vector<UpdateEntry> pendingUpdates;
    };

    // Helper functions
    VkDescriptorType GetVulkanDescriptorType(ResourceViewType viewType) const;
    PoolInfo& GetPoolForType(ResourceViewType viewType);
    uint32_t GetGlobalIndex(ResourceViewType viewType, uint32_t localIndex) const;

    // Enhanced implementation details
    void CreateDescriptorSetLayout();
    void CreateDescriptorPool(PoolInfo& poolInfo, ResourceViewType viewType, uint32_t capacity);
    bool TryAllocateFromFreeList(PoolInfo& poolInfo, uint32_t& outIndex);
    void GrowPool(PoolInfo& poolInfo, ResourceViewType viewType);
    void ExecuteBatchedUpdates();

    // State tracking
    std::mutex globalMutex_;
    VkDevice device_;
    VkDescriptorSetLayout descriptorSetLayout_;
    std::array<PoolInfo, 6> pools_; // one per ResourceViewType
    DescriptorAllocatorConfig config_;
    bool batchModeActive_ = false;

    // Extension support tracking
    bool hasIndexingExtension_ = false;
    bool hasUpdateTemplateKhr_ = false;

    // Debug support
    std::unordered_map<uint32_t, std::string> debugNames_;
    VkDebugUtilsObjectNameInfoEXT nameInfo_ = {};
};
```

### Key Vulkan Considerations:

- Uses descriptor pools with update-after-bind flags for dynamic updates
- Creates large descriptor sets with appropriate binding points
- Handles Vulkan's validation layers and descriptor limits
- Manages descriptor synchronization requirements
- Uses descriptor templates for optimized updates when available
- Implements pool-of-pools architecture for efficient memory management
- Provides thread-safe allocation with per-pool mutexes
- Supports batched updates for improved performance
- Integrates with Vulkan debug utils for resource naming
- Handles extension availability gracefully

## Resource Management

Materials and meshes use the bindless system through a layer like this:

```cpp
struct MaterialConstants {
    // Bindless indices for textures
    uint32_t albedoTextureIndex;
    uint32_t normalTextureIndex;
    uint32_t metallicRoughnessTextureIndex;
    uint32_t emissiveTextureIndex;

    // Bindless indices for samplers
    uint32_t textureSamplerIndex;

    // Material parameters
    float4 albedoFactor;
    float metallicFactor;
    float roughnessFactor;
    float2 padding;
};

class BindlessMaterial {
public:
    void Initialize(
        ResourceRegistry& resourceRegistry,
        const std::string& albedoTexturePath,
        const std::string& normalTexturePath,
        const std::string& metallicRoughnessTexturePath,
        const std::string& emissiveTexturePath) {

        // Load textures and register with bindless system
        // Store texture indices in constants
        // Create constant buffer
    }

    void Bind(CommandList* cmdList, uint32_t slot) {
        // Only bind the constant buffer that contains indices
        cmdList->SetConstantBuffer(slot, constantBuffer_);
    }

    // Setter methods for material properties

private:
    MaterialConstants constants_;
    Buffer* constantBuffer_ = nullptr;
};
```

## Shader Access

### HLSL (D3D12)

```hlsl
// Material constants buffer
cbuffer MaterialConstants : register(b0)
{
    uint albedoTextureIndex;
    uint normalTextureIndex;
    uint metallicRoughnessTextureIndex;
    uint emissiveTextureIndex;
    uint textureSamplerIndex;
    float4 albedoFactor;
    float metallicFactor;
    float roughnessFactor;
    float2 padding;
};

// Bindless resources
Texture2D<float4> AllTextures[] : register(t0, space1);
SamplerState AllSamplers[] : register(s0, space1);

float4 GetAlbedo(float2 uv)
{
    // Dynamic indexing into texture array using bindless index
    return AllTextures[albedoTextureIndex].Sample(AllSamplers[textureSamplerIndex], uv) * albedoFactor;
}
```

### GLSL (Vulkan)

```glsl
// Material constants buffer
layout(set = 0, binding = 0) uniform MaterialConstants {
    uint albedoTextureIndex;
    uint normalTextureIndex;
    uint metallicRoughnessTextureIndex;
    uint emissiveTextureIndex;
    uint textureSamplerIndex;
    vec4 albedoFactor;
    float metallicFactor;
    float roughnessFactor;
    vec2 padding;
} material;

// Bindless resources
layout(set = 1, binding = 0) uniform sampler2D bindlessTextures[];

vec4 getAlbedo(vec2 uv)
{
    // Dynamic indexing into texture array using bindless index
    return texture(bindlessTextures[material.albedoTextureIndex], uv) * material.albedoFactor;
}
```

## Memory Management

### Resource Lifetime and Garbage Collection

The bindless system needs robust management of resource lifetimes to prevent dangling references:

1. **Reference Counting**: Track resource usage to know when descriptors can be freed
2. **Deferred Deletion**: Queue resources for deletion after rendering completes
3. **Validation**: Debug layers to catch access to freed resources
4. **Default Resources**: Fall back to default textures when accessing invalid indices

### Thread Safety Considerations

For multi-threaded rendering engines:

1. **Fine-grained Locking**: Each descriptor pool has its own mutex
2. **Lock-free Allocation**: Fast paths for common operations avoid locks
3. **Thread-local Caches**: Per-thread descriptor caches for batch allocation
4. **Atomic Operations**: Use atomics for reference counting

## Performance Optimizations

1. **Descriptor Recycling**: Maintain free lists to quickly reuse freed slots
2. **Batch Updates**: Group descriptor updates to minimize API calls
3. **Persistent Mappings**: Keep descriptors in persistently mapped memory
4. **Update Coalescing**: Combine multiple descriptor updates into a single operation
5. **Tiered Access**: Frequently used resources in the first heap/set for better cache behavior

## Limitations and Considerations

1. **API Limits**: Respect hardware/driver limits on descriptor counts
2. **Memory Usage**: Large descriptor heaps consume GPU memory
3. **Validation**: Access out-of-bounds indices causes undefined behavior
4. **Debugging**: More complex to debug than traditional binding models
5. **API Differences**: Handle divergent behavior between D3D12 and Vulkan

## Future Directions

1. **Ray Tracing Integration**: Extend for ray tracing descriptors
2. **Automatic Mipmap Generation**: Track and generate mipmaps automatically
3. **Descriptor Compression**: Techniques to reduce descriptor memory footprint
4. **Machine Learning Integration**: Support for ML resources and operations
5. **Virtual Texturing**: Integration with sparse/virtual texture systems

## Conclusion

The bindless descriptor system enables significant performance improvements for modern rendering engines by reducing API overhead and allowing more flexible access to resources. While more complex than traditional binding models, the benefits in draw call throughput, material flexibility, and management simplicity make it essential for high-performance real-time graphics.
