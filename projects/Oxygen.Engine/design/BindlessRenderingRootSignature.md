# Bindless Rendering: Single Table, Global Heap Indices

This guide outlines how to set up HLSL shaders and D3D12 root signatures for a
bindless system characterized by:

1.  **A Single Root Descriptor Table:** This table points to a contiguous region
    of a descriptor heap.
2.  **Global 0-Based Indices:** Indices provided to the shader (e.g., via a
    constant buffer) are direct, 0-based offsets from the start of this heap
    region.
3.  **Mixed Resource Types:** The heap region contains descriptors for various
    resource types (e.g., `Texture2D` SRVs, `StructuredBuffer` SRVs,
    `RWStructuredBuffer` UAVs) potentially interleaved.

The core idea is to use HLSL `register space`s to differentiate between
conceptually distinct arrays of resources, allowing the shader to use the global
heap indices directly.

## 1. HLSL Shader Declarations

*   **Distinct Register Spaces for Distinct Array Types:**
    Even if different resource arrays use the same base register (e.g., `t0` for
    SRVs, `u0` for UAVs), they **must** be declared in different `space`s if
    they represent different kinds of resources or conceptually separate
    collections. This allows the HLSL compiler to correctly type-check and
    manage these arrays.

    ```hlsl
    // Constant buffer providing global heap indices
    cbuffer ResourceIndices : register(b0, space0) { // Or any appropriate b# and space# for CBVs
        uint g_SomeTextureIndex;    // Global heap index for a Texture2D
        uint g_SomeBufferIndex;     // Global heap index for a StructuredBuffer
        uint g_SomeRWBufferIndex;   // Global heap index for an RWStructuredBuffer
        // ... other indices
    };

    // Bindless SRV Arrays (Example)
    // SRVs for textures in space 0
    Texture2D g_AllTextures[] : register(t0, space0);
    // SRVs for MyData buffers in space 1
    StructuredBuffer<MyData> g_AllStructuredBuffers[] : register(t0, space1);
    // SRVs for raw buffers in space 2
    ByteAddressBuffer g_AllRawBuffers[] : register(t0, space2);

    // Bindless UAV Arrays (Example)
    // UAVs for RWTextures in space 3
    RWTexture2D<float4> g_AllRWTextures[] : register(u0, space3);
    // UAVs for RW MyData buffers in space 4
    RWStructuredBuffer<MyData> g_AllRWStructuredBuffers[] : register(u0, space4);
    ```

*   **Using Indices:**
    Access resources using the global heap indices directly.

    ```hlsl
    Texture2D myTexture = g_AllTextures[g_SomeTextureIndex];
    MyData data = g_AllStructuredBuffers[g_SomeBufferIndex].data[element]; // Example access
    g_AllRWStructuredBuffers[g_SomeRWBufferIndex].data[element] = newData; // Example access
    ```

## 2. D3D12 Root Signature Definition (C++)

* **Single Descriptor Table:** Define one `D3D12_ROOT_PARAMETER` of type
  `D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE`.
* **Multiple Descriptor Ranges:** This table will contain multiple
  `D3D12_DESCRIPTOR_RANGE1` entries, one for each unique
  `(D3D12_DESCRIPTOR_RANGE_TYPE, RegisterSpace)` combination used in your
  shaders.
  * All ranges typically use `BaseShaderRegister = 0` because the HLSL arrays
    are declared starting at `t0` or `u0` within their respective spaces.
  * `NumDescriptors` should be large enough to encompass all descriptors of that
    type and space you intend to make accessible (often set to `UINT_MAX` or a
    very large number for truly "bindless" access within that space, assuming
    your heap is managed accordingly).
  * `Flags` can be `D3D12_DESCRIPTOR_RANGE_FLAG_DATA_VOLATILE |
    D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE` for typical bindless
    scenarios.
  * `OffsetInDescriptorsFromTableStart` is usually
    `D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND` for each subsequent range if they are
    all mapping to the same physical block of descriptors that the table points
    to. The key is that the *shader indices are global*, so this C++ side setup
    primarily informs the runtime about the *layout and types* of resources
    expected by the shader at different logical register spaces.

    ```cpp
    // Example:
    D3D12_DESCRIPTOR_RANGE1 ranges[5]; // Number of unique (Type, Space) combinations

    // For: Texture2D g_AllTextures[] : register(t0, space0);
    ranges[0].RangeType                         = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    ranges[0].NumDescriptors                    = UINT_MAX; // Or a large number
    ranges[0].BaseShaderRegister                = 0;
    ranges[0].RegisterSpace                     = 0;
    ranges[0].Flags                             = D3D12_DESCRIPTOR_RANGE_FLAG_DATA_VOLATILE | D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE;
    ranges[0].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    // For: StructuredBuffer<MyData> g_AllStructuredBuffers[] : register(t0, space1);
    ranges[1].RangeType                         = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    ranges[1].NumDescriptors                    = UINT_MAX;
    ranges[1].BaseShaderRegister                = 0;
    ranges[1].RegisterSpace                     = 1;
    ranges[1].Flags                             = D3D12_DESCRIPTOR_RANGE_FLAG_DATA_VOLATILE | D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE;
    ranges[1].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    // For: ByteAddressBuffer g_AllRawBuffers[] : register(t0, space2);
    ranges[2].RangeType                         = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    ranges[2].NumDescriptors                    = UINT_MAX;
    ranges[2].BaseShaderRegister                = 0;
    ranges[2].RegisterSpace                     = 2;
    ranges[2].Flags                             = D3D12_DESCRIPTOR_RANGE_FLAG_DATA_VOLATILE | D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE;
    ranges[2].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    // For: RWTexture2D<float4> g_AllRWTextures[] : register(u0, space3);
    ranges[3].RangeType                         = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
    ranges[3].NumDescriptors                    = UINT_MAX;
    ranges[3].BaseShaderRegister                = 0;
    ranges[3].RegisterSpace                     = 3;
    ranges[3].Flags                             = D3D12_DESCRIPTOR_RANGE_FLAG_DATA_VOLATILE; // UAVs don't have DESCRIPTORS_VOLATILE
    ranges[3].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    // For: RWStructuredBuffer<MyData> g_AllRWStructuredBuffers[] : register(u0, space4);
    ranges[4].RangeType                         = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
    ranges[4].NumDescriptors                    = UINT_MAX;
    ranges[4].BaseShaderRegister                = 0;
    ranges[4].RegisterSpace                     = 4;
    ranges[4].Flags                             = D3D12_DESCRIPTOR_RANGE_FLAG_DATA_VOLATILE;
    ranges[4].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    D3D12_ROOT_DESCRIPTOR_TABLE1 descriptorTable;
    descriptorTable.NumDescriptorRanges = ARRAYSIZE(ranges);
    descriptorTable.pDescriptorRanges   = ranges;

    D3D12_ROOT_PARAMETER1 rootParam[1]; // Plus other params like root CBVs
    rootParam[0].ParameterType    = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParam[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL; // Or specific stage
    rootParam[0].DescriptorTable  = descriptorTable;
    ```

## 3. Populating the Descriptor Heap and Table

* Place your actual resource descriptors (SRVs, UAVs) into a
  `D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV` heap.
* When setting the descriptor table for a command list (e.g., via
  `SetGraphicsRootDescriptorTable`), provide the GPU descriptor handle to the
  *start* of the heap region that these global indices refer to.

### Summary of Benefits

* **Simplified Shader Indexing:** Shader code uses global heap indices directly
  without needing complex offset calculations based on register numbers.
* **Clear Separation:** `register space`s provide a clear distinction in HLSL
  for different categories of bindless resources, aiding compiler type checking
  and reducing ambiguity.
* **Flexibility:** Allows mixing various resource types within the same logical
  heap region pointed to by the single table.

This setup was crucial in resolving the "llvm::cast<X>()" internal compiler
error in the `LightCulling.hlsl` shader by ensuring each bindless array
(`Texture2D[]`, `StructuredBuffer<GPULight>[]`, `RWStructuredBuffer<uint>[]`)
was mapped to a unique register space.
