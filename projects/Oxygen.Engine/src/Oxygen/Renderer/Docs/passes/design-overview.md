# Render Passes Design

## Overview

The Oxygen Engine uses a **pure Forward+ rendering architecture**, implemented
through a modular, coroutine-based render pass system designed for modern
explicit graphics APIs (D3D12, Vulkan). Each render pass encapsulates a single
stage of the pipeline and is implemented as a component with official naming
and metadata support.

This document provides a high-level introduction to Forward+ rendering and
Oxygen's pass architecture. For implementation details of individual passes,
see the dedicated documentation files linked throughout.

## Forward+ Rendering

Forward+ (also called Tiled Forward Shading) is a rendering technique that
combines the flexibility of forward rendering with efficient light culling to
handle many dynamic lights in real-time.

### Classic Forward vs Forward+

In classic forward rendering, each draw call evaluates all lights in the scene,
leading to O(objects × lights) shader complexity. This becomes prohibitively
expensive with many lights.

Forward+ solves this by dividing the screen into tiles (typically 16×16 or
32×32 pixels) and culling lights against each tile's depth bounds. During
shading, each pixel only evaluates lights that affect its tile, reducing
complexity dramatically.

### Pipeline Stages

A typical Forward+ pipeline consists of these conceptual stages:

1. **Depth Pre-Pass** — Render all opaque geometry to populate the depth buffer
   without color output. This enables early-z rejection in later passes and
   provides depth data for light culling.

2. **Light Culling** — A compute shader analyzes the depth buffer per tile,
   determines min/max depth bounds, and builds a list of lights affecting each
   tile. The result is a per-tile light index buffer.

3. **Forward Shading** — Geometry is drawn and shaded in a single pass. Each
   pixel samples the per-tile light list and evaluates only the relevant
   lights. PBR material evaluation and all lighting (direct, shadows, IBL)
   happen here.

4. **Transparency** — Transparent geometry is rendered after opaques, using
   the same lighting model. Blending requires back-to-front ordering or
   order-independent transparency (OIT) techniques.

5. **Post-Processing** — Screen-space effects such as tone mapping, bloom,
   FXAA, and color grading are applied to the final color buffer.

### Why Forward+

Oxygen uses Forward+ as its exclusive rendering technique for these reasons:

| Advantage | Explanation |
| --- | --- |
| Transparency support | All lighting code lives in the forward shader, so transparents receive identical lighting to opaques |
| MSAA compatibility | Forward rendering naturally supports hardware MSAA without complex resolve steps |
| Material flexibility | Arbitrary BRDFs can be used without G-buffer bandwidth constraints |
| Memory efficiency | No G-buffer storage (albedo, normals, roughness, etc.) required |
| Modern GPU fit | Compute-based light culling leverages GPU parallelism effectively |

## Oxygen's Pass Architecture

Oxygen implements Forward+ through a concrete set of render passes, orchestrated
via coroutines in the application's render graph.

### Implemented Passes

| Pass | Class | Status | Documentation |
| --- | --- | --- | --- |
| Depth Pre-Pass | `DepthPrePass` | ✅ Implemented | [depth_pre_pass.md](depth_pre_pass.md) |
| Forward Shading | `ShaderPass` | ✅ Implemented | [shader_pass.md](shader_pass.md) |
| Transparency | `TransparentPass` | ✅ Implemented | (pending) |
| Light Culling | — | 🔲 Planned | — |

### Current Pipeline Sequence

The `RenderGraph` executes passes in the following order:

```text
DepthPrePass → ShaderPass → TransparentPass → [Post-Processing] → [UI]
```

Each pass follows the two-phase coroutine protocol:

1. **PrepareResources** — Transition resources to required states, set up
   descriptor tables, insert barriers
2. **Execute** — Issue draw/dispatch calls with resources in correct states

### RenderPass Base Class

All passes inherit from `RenderPass`, which provides:

- Coroutine entry points (`PrepareResources`, `Execute`)
- Naming/metadata via `Named` interface
- Access to `RenderContext` for scene data and cross-pass communication

Derived passes override `DoPrepareResources()` and `DoExecute()` to implement
their specific logic.

### Cross-Pass Communication

Passes can query each other through the `RenderContext`:

- `RegisterPass<T>()` — Register a pass instance for lookup
- `GetPass<T>()` — Retrieve a registered pass by type

This enables patterns like the `ShaderPass` querying the `DepthPrePass` for
depth buffer configuration.

## Resource Dependencies

The following table summarizes resource flow between passes:

| Pass | Input Resources | Output Resources | State Transitions |
| --- | --- | --- | --- |
| Depth Pre-Pass | Geometry buffers | Depth buffer | Depth → DEPTH_WRITE |
| Light Culling | Depth buffer (SRV), light data | Per-tile light list (UAV) | Depth → SHADER_RESOURCE |
| Forward Shading | Depth (SRV), light lists, geometry, textures | Color buffer (RTV) | Color → RENDER_TARGET |
| Transparency | Depth (SRV), light lists, geometry, textures | Color buffer (RTV, blend) | Color → RENDER_TARGET |
| Post-Processing | Color buffer (SRV) | Color buffer (RTV/UAV) | Color → SRV → RENDER_TARGET |

Resource state transitions are managed through explicit barriers in each pass's
`PrepareResources` phase.

## Bindless Architecture

All passes share a common root signature layout for bindless resource access:

| Slot | Type | Content |
| --- | --- | --- |
| t0 | SRV | Unbounded descriptor table (all textures/buffers) |
| b1 | CBV | SceneConstants (camera matrices, light counts) |
| b2 | Root Constants | DrawMetadata offset |
| b3 | CBV | EnvironmentDynamicData (sky, fog parameters) |

This architecture eliminates per-draw descriptor binding overhead and enables
GPU-driven rendering patterns.

## PSO Management

Each pass owns its Pipeline State Object variants. For geometry passes, this
typically includes 4 pre-compiled variants:

| Variant | PassMask Bits | Use Case |
| --- | --- | --- |
| Opaque, single-sided | `kOpaque` only | Standard solid geometry |
| Opaque, double-sided | `kOpaque + kDoubleSided` | Foliage, cloth |
| Masked, single-sided | `kMasked` only | Cutout materials |
| Masked, double-sided | `kMasked + kDoubleSided` | Foliage with alpha test |

PSO selection occurs per-drawcall based on the `PassMask` flags in `DrawMetadata`.

## Orchestration

Pass orchestration is handled by the application's `RenderGraph` (or equivalent
render scene function), which:

1. Creates and configures pass instances
2. Wires shared resources (color texture, depth texture) between passes
3. Executes passes in sequence via coroutine `co_await`

Example from `RenderGraph::RunPasses()`:

```cpp
co_await depth_pass_->PrepareResources(ctx, recorder);
co_await depth_pass_->Execute(ctx, recorder);

co_await shader_pass_->PrepareResources(ctx, recorder);
co_await shader_pass_->Execute(ctx, recorder);

co_await transparent_pass_->PrepareResources(ctx, recorder);
co_await transparent_pass_->Execute(ctx, recorder);
```

## Future Enhancements

### Light Culling Pass

The Light Culling compute pass will analyze the depth buffer per tile, build
min/max depth bounds, and produce a per-tile light index buffer consumed by
`ShaderPass` and `TransparentPass`.

### Shadow Mapping

Dedicated shadow passes will render depth from each shadow-casting light's
perspective, producing shadow map atlases consumed during forward shading.

### Environment System Integration

The SceneEnvironment system will provide:

- **SkyLight** — IBL (Image-Based Lighting) via HDR environment cubemaps
- **SkyAtmosphere** — Procedural atmospheric scattering
- **SkySphere** — Simple skydome for background rendering
- **Fog** — Distance and height-based fog
- **VolumetricClouds** — Volumetric cloud rendering
- **PostProcessVolume** — Per-volume post-processing overrides

These components will integrate with the forward shading pass through the
existing `EnvironmentDynamicData` constant buffer.

## See Also

- [data_flow.md](data_flow.md) — Scene preparation and GPU data flow
- [depth_pre_pass.md](depth_pre_pass.md) — Depth Pre-Pass implementation
- [shader_pass.md](shader_pass.md) — Forward Shading Pass implementation
