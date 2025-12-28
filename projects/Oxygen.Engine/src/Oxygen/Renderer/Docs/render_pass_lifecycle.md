# Render Pass Lifecycle

Applies to all subclasses of `RenderPass` (e.g. `DepthPrePass`, `ShaderPass`).

## Overview

The render pass lifecycle is a two-stage coroutine-based pipeline:

```text
PrepareResources()  ->  Execute()
      ↓                    ↓
  [Resource Setup]    [Draw Submission]
```

Both stages are asynchronous (C++20 coroutines) and designed for modern explicit graphics APIs (D3D12, Vulkan).

## Stage Details

### PrepareResources() - Resource State Setup

**Flow:**

```cpp
PrepareResources(const RenderContext& context, CommandRecorder& recorder)
  ├─ ValidateConfig()              // Sanity-check configuration
  ├─ NeedRebuildPipelineState()    // Check if PSO rebuild needed
  ├─ CreatePipelineStateDesc()     // (if needed) Build PSO descriptor
  └─ co_await DoPrepareResources() // Subclass: issue barriers, setup views
```

| Step | Virtual Method | Purpose | Notes |
| ---- | -------------- | ------- | ----- |
| **Validation** | `ValidateConfig()` | Sanity-check config & required context (e.g., depth/color texture availability). | Called first. Must not record GPU commands. Throws `std::runtime_error` on failure. |
| **PSO Decision** | `NeedRebuildPipelineState()` + `CreatePipelineStateDesc()` | Determine if PSO description must be (re)built due to format/sample count changes. | Descriptor cached in `last_built_pso_desc_`. Actual PSO caching occurs in `CommandRecorder::SetPipelineState()`. |
| **Resource Setup** | `DoPrepareResources(CommandRecorder&)` | Issue resource state transitions (barriers) and prepare descriptor views. | Async coroutine. Must flush barriers after transitions. **No draw/dispatch calls here.** |

**Example (ShaderPass):**

```cpp
auto ShaderPass::DoPrepareResources(CommandRecorder& recorder) -> co::Co<> {
  // Transition color target to RENDER_TARGET
  recorder.RequireResourceState(GetColorTexture(), kRenderTarget);
  // Transition depth target to DEPTH_READ
  recorder.RequireResourceState(depth_texture, kDepthRead);
  // Flush all barriers before proceeding
  recorder.FlushBarriers();
  co_return;
}
```

### Execute() - Draw Submission

**Flow:**

```cpp
Execute(const RenderContext& context, CommandRecorder& recorder)
  ├─ recorder.SetPipelineState()   // Set cached PSO + bindless root signature
  ├─ BindIndicesBuffer()           // (via bindless table)
  ├─ BindSceneConstantsBuffer()    // Root CBV at index 1
  ├─ try { co_await DoExecute() } // Subclass: issue draw calls
  └─ catch exceptions and re-throw // Error handling
```

| Step | Method | Purpose | Notes |
| ---- | ------ | ------- | ----- |
| **Pipeline** | `recorder.SetPipelineState()` | Set the graphics pipeline state (PSO) and bindless root signature. | Base class does this automatically. |
| **Scene Constants** | `BindSceneConstantsBuffer()` | Bind the per-frame scene constants (camera, lights, etc.) as root CBV. | Accessed via root parameter index 1. |
| **Index Buffer** | `BindIndicesBuffer()` | Make index buffer accessible (via bindless descriptor table). | Modern bindless model: no explicit root binding needed. |
| **Draw Submission** | `DoExecute(CommandRecorder&)` | Issue actual draw/dispatch commands. | Async coroutine. May iterate passes and emit draw calls. |

**Example (ShaderPass):**

```cpp
auto ShaderPass::DoExecute(CommandRecorder& recorder) -> co::Co<> {
  // Setup render target view and clear
  SetupRenderTargets(recorder);
  // Setup viewport and scissors
  SetupViewPortAndScissors(recorder);
  // Issue draws for opaque geometry
  IssueDrawCallsOverPass(recorder, PassMaskBit::kOpaque);
  co_return;
}
```

## Root Binding Model

Modern bindless rendering uses a minimal root signature (defined in generated `binding::RootParam`):

| Index | Binding Type | Purpose | Used By |
| ----- | ------------ | ------- | ------- |
| 0 | **Bindless Table SRV** | Descriptor heap with all bindless resources (geometry, materials, draw metadata, matrices). | All passes (accessed in shaders via `g_BindlessTable[index]`) |
| 1 | **Scene Constants CBV** | Per-frame camera, lighting, and global constants. | All passes (via direct GPU virtual address) |
| 2 | **Material Constants CBV** | Per-material shader constants (optional, may be null). | ShaderPass, TransparentPass (if applicable) |
| 3 | **Draw Index Constant** | 32-bit index into `DrawResourceIndices` array for multi-draw support. | All passes (set before each draw via `SetGraphicsRoot32BitConstant`) |

**Key Points:**

- All passes bind the same root signature for consistency.
- Bindless table (index 0) is pre-bound during `SetPipelineState()`.
- Scene constants (index 1) are bound in `BindSceneConstantsBuffer()` using GPU virtual address.
- Draw index (index 3) is set per-draw in `BindDrawIndexConstant()` to select the correct material/transform.
- Material constants are optional and only bound when available.

See: [bindless conventions](bindless_conventions.md).

## PreparedSceneFrame Integration

Passes access finalized scene data via `context_.current_view.prepared_frame`:

```cpp
struct PreparedSceneFrame {
  std::span<const std::byte> draw_metadata_bytes;  // Packed DrawMetadata records
  std::span<const float> world_matrices;           // 16 floats per matrix
  std::span<const float> normal_matrices;          // 16 floats per matrix
  std::span<const PartitionRange> partitions;      // Pass mask -> [begin, end) ranges

  // Bindless SRV slots (captured at finalization)
  uint32_t bindless_worlds_slot;
  uint32_t bindless_normals_slot;
  uint32_t bindless_materials_slot;
  uint32_t bindless_draw_metadata_slot;
};
```

**Flow:**

1. Renderer calls `ScenePrepPipeline::Collect()` to collect visible items from the scene.
2. Renderer calls `ScenePrepPipeline::Finalize()` to:
   - Emit draw metadata per item (one `DrawMetadata` per visible submesh)
   - Sort and partition by pass mask
   - Upload transforms, materials, and draw metadata to GPU
   - Capture bindless SRV slots
3. Renderer creates `PreparedSceneFrame` with spans into the finalized arrays.
4. Each pass queries the frame via `context_.current_view.prepared_frame` and iterates `partitions` to find draw ranges for its pass.
5. `IssueDrawCallsOverPass()` emits draws only for partitions marked with the requested pass bit.

**Example (iterating opaque draws):**

```cpp
auto ShaderPass::DoExecute(CommandRecorder& recorder) -> co::Co<> {
  const auto psf = Context().current_view.prepared_frame;
  if (!psf || !psf->IsValid() || psf->draw_metadata_bytes.empty()) {
    co_return;  // No scene data
  }

  const auto* records = reinterpret_cast<const DrawMetadata*>(
    psf->draw_metadata_bytes.data());

  for (const auto& partition : psf->partitions) {
    if (partition.pass_mask.IsSet(PassMaskBit::kOpaque)) {
      // Emit draws in [partition.begin, partition.end)
      for (auto i = partition.begin; i < partition.end; ++i) {
        const auto& md = records[i];
        recorder.DrawIndexedInstanced(md.index_count, 1, md.first_index,
                                      md.base_vertex, 0);
      }
    }
  }
  co_return;
}
```

## Error Handling

**Exceptions in DoExecute():**

- Caught by base class `Execute()` coroutine
- Logged with diagnostic information
- Re-thrown to propagate to the caller

**Caller Responsibility:**

- Render graph coroutines should handle exceptions or surface them as frame failures.
- Failed passes should be logged but should not crash the engine.

**Example:**

```cpp
try {
  co_await pass.Execute(context, recorder);
} catch (const std::exception& ex) {
  LOG_F(ERROR, "Pass '{}' failed: {}", pass.GetName(), ex.what());
  // Mark frame as partially rendered, continue with other passes
}
```

## Coroutine Contract

Both `PrepareResources()` and `Execute()` are C++20 coroutines (`co::Co<>`):

- **Non-blocking:** Suspend at `co_await` points to enable parallel async work
- **Resumable:** Engine scheduler resumes when awaited operations complete
- **Composable:** Render graph can await multiple passes concurrently

**Typical Frame Flow:**

```cpp
// In render graph (per-view)
co_await depth_pass.PrepareResources(context, recorder);
co_await depth_pass.Execute(context, recorder);

co_await shader_pass.PrepareResources(context, recorder);
co_await shader_pass.Execute(context, recorder);

co_await transparent_pass.PrepareResources(context, recorder);
co_await transparent_pass.Execute(context, recorder);
```

## Subclass Implementation Checklist

When implementing a new render pass, override these virtual methods:

1. **`ValidateConfig()`** - Verify configuration and context state
   - Throw `std::runtime_error` if validation fails
   - Example: Check required textures are available

2. **`CreatePipelineStateDesc()`** - Build the graphics pipeline descriptor
   - Return `graphics::GraphicsPipelineDesc` with shader stages, blend state, etc.
   - Called only when `NeedRebuildPipelineState()` returns true

3. **`NeedRebuildPipelineState()`** - Determine if PSO rebuild is needed
   - Return true if format, sample count, or other PSO-affecting values changed
   - Return false if PSO can be reused from cache

4. **`DoPrepareResources(CommandRecorder&)`** - Setup resource state
   - Issue `RequireResourceState()` calls for all inputs/outputs
   - Allocate and prepare descriptor views
   - Call `FlushBarriers()` before returning
   - Do **not** issue draw calls

5. **`DoExecute(CommandRecorder&)`** - Emit draw commands
   - Setup render targets (clear, set viewport/scissors)
   - Call `IssueDrawCallsOverPass()` to submit draws
   - Optionally bind additional resources (sampler tables, etc.)

## Related Documentation

- [Scene Preparation Pipeline](scene_prep.md) - Details on item collection and finalization
- [Bindless Rendering Conventions](bindless_conventions.md) - Descriptor table layout and shader access patterns
- [Draw Metadata](render_items.md) - Structure and layout of finalized draw records
