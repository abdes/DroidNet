# Multi-Draw Item Support Implementation Summary

**Date**: August 14, 2025
**Status**: Complete ✅

## Problem Statement

The Oxygen Engine renderer initially supported only a single draw item per
frame. Attempting to render multiple meshes resulted in only the first mesh
being visible due to limitations in the D3D12 bindless rendering implementation.

## Root Cause Analysis

The original implementation tried to use `SV_InstanceID` with the
`firstInstance` parameter to pass draw indices to shaders:

```cpp
command_recorder.Draw(vertex_count, 1, 0, draw_index); // Used draw_index as firstInstance
```

However, in D3D12 bindless rendering:

- The pipeline has no input layout (`pInputElementDescs = nullptr`)
- This bypasses the input assembler where `SV_InstanceID` is normally generated
- `SV_InstanceID` always returned 0 regardless of the `firstInstance` parameter

## Solution: Root Constants (Microsoft's Approach)

Following Microsoft's D3D12 samples (specifically `D3D12MeshletInstancing`), we
implemented root constants for passing draw indices:

### 1. Root Signature Extension

Added `kDrawIndexConstant = 3` to the root bindings enumeration:

```cpp
enum class RootBindings : uint8_t {
  kBindlessTableSrv = 0,      // descriptor table (SRVs) t0 space0
  kSceneConstantsCbv = 1,     // direct CBV b1 space0
  kMaterialConstantsCbv = 2,  // direct CBV b2 space0
  kDrawIndexConstant = 3,     // root constant for draw index (b3 space0)
};
```

### 2. CommandRecorder API Extension

Added new methods for setting root constants:

```cpp
void SetGraphicsRoot32BitConstant(uint32_t root_parameter_index,
                                  uint32_t src_data,
                                  uint32_t dest_offset_in_32bit_values);
void SetComputeRoot32BitConstant(uint32_t root_parameter_index,
                                 uint32_t src_data,
                                 uint32_t dest_offset_in_32bit_values);
```

### 3. RenderPass Integration

Added `BindDrawIndexConstant()` method to the base RenderPass class:

```cpp
auto RenderPass::BindDrawIndexConstant(CommandRecorder& recorder,
                                       uint32_t draw_index) const -> void;
```

### 4. Draw Call Flow

Updated `IssueDrawCalls()` to bind the draw index before each draw:

```cpp
BindDrawIndexConstant(command_recorder, draw_index);
command_recorder.Draw(vertex_count, 1, 0, 0); // Normal draw call
```

### 5. Shader Updates

Modified shaders to use root constant instead of `SV_InstanceID`:

```hlsl
// Root constant declaration:
[[vk::push_constant]]
cbuffer DrawIndexConstants : register(b3, space0) {
  uint g_DrawIndex;
}

// Usage:
DrawResourceIndices drawRes = g_DrawResourceIndices[g_DrawIndex];
```

## Implementation Files Changed

### Core Implementation

- `RenderPass.h/cpp` - Added `BindDrawIndexConstant()` method and updated
  `IssueDrawCalls()`
- `CommandRecorder.h` (Common) - Added root constant method declarations
- `CommandRecorder.h/cpp` (D3D12) - Implemented D3D12-specific root constant
  methods

### Pipeline Configuration

- `ShaderPass.cpp` - Added draw index root constant binding to pipeline
- `DepthPrePass.cpp` - Added MaterialConstants placeholder and draw index
  binding

### Shaders

- `FullScreenTriangle.hlsl` - Updated to use `g_DrawIndex` root constant
- `DepthPrePass.hlsl` - Updated to use `g_DrawIndex` root constant

### Documentation

- `bindless_conventions.md` - Updated root bindings and multi-draw documentation
- `render_pass_lifecycle.md` - Updated root binding usage table
- `passes/shader_pass.md` - Added multi-draw execution details
- `passes/depth_pre_pass.md` - Added multi-draw support notes
- `implementation_plan.md` - Added Phase 2.5 completion summary

## Technical Benefits

1. **Proper D3D12 Compliance**: Uses Microsoft-recommended patterns for bindless
   rendering
2. **Scalable**: Supports arbitrary number of draw items per frame
3. **Performance**: Minimal overhead - single root constant per draw call
4. **Maintainable**: Centralized binding logic in base RenderPass class
5. **Consistent**: Same approach works across all pass types

## Testing Results

✅ **Multiple cubes now render correctly** - each with their own color based on
draw index
✅ **No "span subscript out of range" errors** - fixed root signature indexing
✅ **Clean build** - all compilation issues resolved
✅ **Runtime stability** - no crashes or GPU errors

## Future Considerations

This implementation provides a solid foundation for:

- DrawPacket systems (Phase 6)
- Instanced rendering optimizations
- More complex multi-mesh scenarios
- GPU-driven rendering techniques

The root constant approach scales well and aligns with modern D3D12 best
practices for bindless resource management.
