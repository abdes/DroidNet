# Pass Scope Examples

This document demonstrates how to use the three pass scopes to handle different rendering scenarios, including the new `PassScope::Viewless` for view-independent operations.

## PassScope::Viewless - View-Independent Operations

These passes execute once regardless of the number of views (0 to N). Perfect for background tasks, compute operations, and global state updates.

### Compute Shader Example

```cpp
// Particle physics simulation - doesn't depend on camera views
auto particle_sim = builder.AddComputePass("ParticleSimulation")
    .SetViewless()  // Convenience method
    .SetExecutor([this](TaskExecutionContext& ctx) {
        // Simulate particle physics
        DispatchComputeShader(ctx, particle_compute_shader_,
                             particle_count_ / 64, 1, 1);
    })
    .Reads(particle_input_buffer_)
    .Outputs(particle_output_buffer_);
```

### Background GPU Tasks

```cpp
// Texture streaming - independent of rendering views
auto texture_streaming = builder.AddCopyPass("TextureStreaming")
    .SetScope(PassScope::Viewless)  // Explicit scope setting
    .SetPriority(Priority::Background)
    .SetExecutor([this](TaskExecutionContext& ctx) {
        // Stream textures from disk to GPU
        ProcessTextureStreamingQueue(ctx);
    });

// Global illumination probe updates
auto gi_probe_update = builder.AddComputePass("GIProbeUpdate")
    .SetViewless()
    .SetExecutor([this](TaskExecutionContext& ctx) {
        // Update global illumination probes
        UpdateGlobalIlluminationProbes(ctx);
    })
    .Reads(scene_geometry_)
    .Outputs(gi_probe_data_);
```

## PassScope::PerView - View-Dependent Rendering

These passes execute once per view. Skipped when no views are available (0 views scenario).

### Traditional Rendering

```cpp
// Depth pre-pass - needs camera matrices
auto depth_prepass = builder.AddRasterPass("DepthPrepass")
    .SetPerView()  // Convenience method
    .IterateAllViews()  // Execute for all available views
    .SetExecutor([this](TaskExecutionContext& ctx) {
        RenderDepthOnly(ctx);
    })
    .Reads(vertex_buffer_)
    .Outputs(depth_buffer_);

// Main geometry rendering
auto geometry_pass = builder.AddRasterPass("GeometryPass")
    .SetScope(PassScope::PerView)
    .IterateAllViews()
    .SetExecutor([this](TaskExecutionContext& ctx) {
        RenderGeometry(ctx);
    })
    .Reads(depth_buffer_)
    .Reads(material_textures_)
    .Outputs(color_buffer_);
```

## PassScope::Shared - View-Agnostic but Scene-Dependent

These passes execute once regardless of view count, but may still depend on scene data.

### Shadow Mapping

```cpp
// Shadow map generation - computed once for light sources
auto shadow_pass = builder.AddRasterPass("ShadowMapping")
    .SetShared()  // Convenience method
    .SetExecutor([this](TaskExecutionContext& ctx) {
        RenderShadowMaps(ctx);
    })
    .Reads(scene_geometry_)
    .Outputs(shadow_maps_);
```

## Zero Views Scenario Handling

With the new scope system, the render graph gracefully handles 0 views:

```cpp
// This frame has NO VIEWS (headless, compute-only, background processing)
frame_context.ClearViews();  // 0 views

// These passes will execute:
// ✅ PassScope::Viewless passes - Always execute
// ✅ PassScope::Shared passes - Execute once
// ❌ PassScope::PerView passes - Skipped (no views to iterate)

// Example compute-only frame:
auto fluid_simulation = builder.AddComputePass("FluidSimulation")
    .SetViewless()
    .SetExecutor([](TaskExecutionContext& ctx) {
        // Fluid physics - no camera needed
    });

auto async_asset_loading = builder.AddCopyPass("AssetLoading")
    .SetViewless()
    .SetPriority(Priority::Background)
    .SetExecutor([](TaskExecutionContext& ctx) {
        // Load assets in background
    });
```

## Best Practices

### When to Use Each Scope

- **PassScope::Viewless**:
  - Compute shaders that don't need camera data
  - Background asset streaming
  - Global state updates
  - Physics simulation
  - Audio processing

- **PassScope::PerView**:
  - Traditional rendering passes
  - View-dependent culling
  - Screen-space effects
  - UI rendering per view

- **PassScope::Shared**:
  - Shadow map generation
  - Light culling (view-independent)
  - Global illumination computation
  - Terrain processing

### Migration from Old Code

```cpp
// Old way (ambiguous):
if (!views.empty() && pass->GetScope() == PassScope::PerView) {
    // Execute per view
} else {
    // Execute once - but why? Shared or viewless?
}

// New way (explicit):
switch (pass->GetScope()) {
    case PassScope::PerView:
        if (!views.empty()) {
            for (auto& view : views) {
                ExecuteForView(pass, view);
            }
        }
        // Clear intent: skip when no views
        break;
    case PassScope::Shared:
        // Execute once, may use global scene data
        ExecuteShared(pass);
        break;
    case PassScope::Viewless:
        // Execute once, completely view-independent
        ExecuteViewless(pass);
        break;
}
```

This approach provides clear semantics and robust handling of the 0-views scenario.
