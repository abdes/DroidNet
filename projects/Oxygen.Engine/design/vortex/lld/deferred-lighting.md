# Deferred Lighting LLD

**Stage:** 12 — Deferred Direct Lighting
**Deliverable:** D.6
**Status:** Implemented; broader EX07 qualification is tracked in the implementation plan.

## Exposure-package integration

[LightingService](lighting-service.md) owns stage-12 deferred lighting, using the
[canonical indexed ABI](lighting-gpu-abi.md#shadow-association-and-deferred-draws)
and [production BRDF model 2](../../renderer-core/physically-based-rendering.md#production-local-lighting-and-brdf-model-2).
Physical light records are shared with forward shading; 80-byte per-draw constants
carry geometry and indices. Source attenuation, analytic source response and
energy compensation are shared helpers.

Accumulate P-scaled radiance into the existing pre-exposed emissive SceneColor.
Missing required inputs fail the view; valid disabled or empty direct lighting
preserves emissive. HDR formats and cumulative range checks follow the
[SceneTextures inventory](scene-textures.md#exposure-hdr-domain-and-format-inventory).
Exposure history remains owned by PostProcessService.

## 1. Scope and Context

### 1.1 What This Covers

`LightingService::RenderDeferredLighting` invokes `DeferredLightPass` for each
view. One fullscreen draw evaluates all directional sources, reusing GBuffer
reconstruction and material/view preparation. Each local light uses one bounded
volume draw. Static sky diffuse, when enabled, uses a separate fullscreen draw.
All draws add to SceneColor.

### 1.2 Stage Position

| Position                | Stage                                                       | Notes |
| ----------------------- | ----------------------------------------------------------- | ----- |
| Predecessor             | Stage 10 (RebuildSceneTextures) — GBuffers now SRV-readable |       |
| Predecessors (reserved) | Stage 11 (MatComposite post — stub)                         |       |
| **This**                | **Stage 12 — Deferred Direct Lighting**                     |       |
| Successor               | Stage 13 (IndirectLighting — reserved)                      |       |

### 1.3 Architectural Authority

- [ARCHITECTURE.md §6.2](../ARCHITECTURE.md) — stage table, row 12
- [ARCHITECTURE.md §6.3.1](../ARCHITECTURE.md) — deferred-core invariants
- [DESIGN.md §7](../DESIGN.md) — deferred lighting design
- UE5 reference: `RenderLights` / `RenderDeferredLighting` families

### 1.4 Required Invariants For This Module

This module must preserve the following invariants from
[ARCHITECTURE.md §6.3.1](../ARCHITECTURE.md):

- stage 12 is dispatched by `SceneRenderer` through `LightingService`
- the deferred-light shader family belongs to the Lighting domain
- scene-texture access flows through published `ViewFrameBindings` →
  `SceneTextureBindings`; no local binding synthesis is allowed

## 2. Interface Contracts

`LightingService::RenderDeferredLighting` receives the current render context,
command recorder, SceneTextures, immutable frame selection, indexed shadow data
and shadow surfaces, and static-sky availability. Its boolean result propagates
recording failure. The service owns publication and dispatch; `DeferredLightPass`
owns draw preparation, PSOs, framebuffers and persistent sphere/cone geometry.

Per-draw constants and CBV descriptors use the renderer's frame-safe allocation
and retention infrastructure. Light records, per-view bindings and shadow
references retain their published frame/view identity until GPU use completes.
The exact interface and lifetimes are defined by the
[service contract](lighting-service.md#2-canonical-data-and-interfaces) and
[GPU ABI](lighting-gpu-abi.md).

## 3. Light Types and Rendering Strategy

### 3.1 Rendering approach

| Source              | Geometry                       | Draw policy                                                                    |
| ------------------- | ------------------------------ | ------------------------------------------------------------------------------ |
| Directional array   | Procedural fullscreen triangle | One draw per view; one shared surface/BRDF preparation and a loop over sources |
| Point               | Persistent sphere proxy        | One bounded-volume draw per light                                              |
| Ordinary spot       | Persistent cone proxy          | One bounded-volume draw per light, regardless of source radius                 |
| 90-degree soft spot | Persistent sphere proxy        | One bounded-volume draw; center-cone shader still rejects the back hemisphere  |

### 3.2 Local-light volume modes

- Camera outside: render front faces with `GREATER_EQUAL` for reversed Z or
  `LESS_EQUAL` for ordinary Z.
- Camera inside or near-plane overlap: render back faces with `ALWAYS` depth.
- Non-perspective views: use back faces and `ALWAYS`; this is a conservative
  raster policy, not a claim that the camera lies inside the volume.

There is no stencil-mark pass. Depth writes and stencil are disabled. Proxy Z
clipping is disabled so an exit surface beyond the far plane cannot remove
visible receivers; XY/W clipping, culling and depth policy remain. The pixel
shader reconstructs the receiver and checks actual center-based range/cone
support before expensive shading.

### 3.3 Per-Light Data Access

Draws use bindless-selected constant-buffer views.
The per-draw geometry/index contract is `DeferredLightConstants`; shaders do
not receive it through a fixed `register(b1)` pass binding. Instead:

1. `LightingService` uploads one geometry/index `DeferredLightConstants` record per draw
   into `Vortex.DeferredLight.Constants`
2. it creates one shader-visible CBV view per record
3. it passes the selected CBV index through the root constant
   `g_PassConstantsIndex`
4. the shader reads
   `ConstantBuffer<DeferredLightConstants> light_constants = ResourceDescriptorHeap[g_PassConstantsIndex];`

The obsolete physical-light constant payload is removed from this interface. Use the canonical shared evaluation record plus the [80-byte geometry/index constants](lighting-gpu-abi.md#shadow-association-and-deferred-draws); no second intensity/cone authority.

This preserves bindless pass routing while removing duplicated physical values.
Stage 12 remains owned by LightingService; spatial-evaluation optimization
must preserve that owner and the same canonical physical source model.

## 4. Data Flow and Dependencies

### 4.1 Inputs

| Source                                                   | Data                                                  | Purpose                                              |
| -------------------------------------------------------- | ----------------------------------------------------- | ---------------------------------------------------- |
| Published `SceneTextureBindings` via `ViewFrameBindings` | GBufferNormal/Material/BaseColor/CustomData (SRV)     | Material data for BRDF evaluation                    |
| Published `SceneTextureBindings` via `ViewFrameBindings` | SceneDepth (SRV)                                      | Position reconstruction                              |
| SceneTextures                                            | SceneColor (RTV)                                      | Accumulation target                                  |
| Scene                                                    | Light list (position, color, type, radius, etc.)      | Per-light parameters                                 |
| `ViewConstants.hlsli` globals                            | `view_matrix`, `projection_matrix`, `camera_position` | View-space transforms and camera data                |
| Root constants                                           | `g_PassConstantsIndex`                                | Selects the current light's CBV in the bindless heap |

### 4.2 Outputs

| Product    | Target                         | Blend Mode          |
| ---------- | ------------------------------ | ------------------- |
| SceneColor | SceneTextures::GetSceneColor() | Additive (ONE, ONE) |

### 4.3 SceneTextures State

```text
Before stage 12:
  GBufferNormal/Material/BaseColor/CustomData = SRV (readable, from stage 10 transition)
  SceneColor    = contains emissive from BasePass (stage 9)
  SceneDepth    = SRV (readable)

After stage 12:
  GBufferNormal/Material/BaseColor/CustomData = SRV (unchanged)
  SceneColor    = emissive + direct lighting accumulated
```

### 4.4 Execution Flow

1. Load current-view scene and lighting bindings and prepare draw packets.
2. Ensure service-owned local proxy buffers and matching framebuffers exist.
3. Emit one directional-array fullscreen draw when direct directionals are active.
4. Emit one sphere/cone draw per local packet with the appropriate volume mode.
5. Emit the separate static-sky diffuse draw when available and enabled.
6. Accumulate with additive blending, retaining existing emissive and coverage.

## 5. Shader Contracts

### 5.1 Directional array

`DeferredLightDirectional.hlsl` reconstructs a covered surface once and prepares
`GgxDirectContext` once. It loops over `LightingFrameBindings.directional_count`,
retaining each source's indexed shadow visibility, atmosphere transport and
illuminance. It accumulates unexposed contributions and applies pre-exposure once.
The static-sky draw uses its separate diffuse branch.

### 5.2 Point and spot lights

`DeferredLightPoint.hlsl` and `DeferredLightSpot.hlsl` share this sequence:

1. Reject invalid bindings and background depth; reconstruct world position.
2. Resolve the canonical local record using the draw's selection index.
3. `PrepareLocalEmitterInput` computes center direction, inverse distance and
   range/cone attenuation once; reject zero contribution before material work.
4. Decode the material and reject surfaces outside the analytic source horizon.
5. Resolve indexed shadow visibility once; reject fully shadowed receivers.
6. Prepare shared GGX terms, evaluate analytic source lobes, multiply visibility
   and apply pre-exposure once.

The source family is specialized per shader. There is no runtime quadrature or
shadow lookup inside a source-sampling loop. Ordinary spots use projected shadow
records; points and 90-degree soft spots use cube records. Source radius changes
analytic diffuse/specular response without expanding range or cone support.

### 5.3 Shared helpers and data

`DeferredLightingCommon.hlsli` owns deferred geometry/binding access;
`DeferredShadingCommon.hlsli` owns deferred surface reconstruction and material
decoding. `FiniteEmitter.hlsli` and `LocalLightAttenuation.hlsli` supply the same
source evaluation used by forward lighting. `Shared/BRDFCommon.hlsli` owns the
shared direct/indirect BRDF and compact energy lookup.

View transforms and perspective/orthographic view directions use the standard
Vortex view contract. Draw constants contain no duplicate intensity or cone
parameters; their exact layout is in the
[80-byte geometry/index ABI](lighting-gpu-abi.md#shadow-association-and-deferred-draws).

### 5.4 Shader entrypoints

| Entrypoint                   | Responsibility                                                  |
| ---------------------------- | --------------------------------------------------------------- |
| `DeferredLightDirectionalVS` | Procedural fullscreen triangle                                  |
| `DeferredLightDirectionalPS` | Directional-array shading or static-sky diffuse                 |
| `DeferredLightPointVS`       | Load and transform stored sphere proxy vertices                 |
| `DeferredLightPointPS`       | Shared analytic point-source shading                            |
| `DeferredLightSpotVS`        | Load and transform stored cone/sphere proxy vertices            |
| `DeferredLightSpotPS`        | Shared analytic spot-source shading and center-cone attenuation |

## 6. Light Volume Geometry

### 6.1 Sphere proxy

`DeferredLightPass` lazily generates the sphere vertices on the CPU and retains
a structured buffer/SRV for reuse. Point and hemispherical spot draws select this
buffer and transform it to the light's center/range influence volume. Vertex
shaders load stored positions using `SV_VertexID`.

### 6.2 Cone proxy

Ordinary spots use the same service-owned persistent-buffer pattern for the cone
(currently 144 vertices). Its transform derives from center range, direction and
outer cone angle. Increasing source radius does not change the proxy. No proxy
vertices are regenerated per pixel or per frame.

### 6.3 Fullscreen Triangle (Directional)

Generated procedurally from `SV_VertexID` — no vertex buffer needed.
Uses `FullscreenTriangle.hlsli` from Shared/.

## 7. PSO Configuration

### 7.1 Directional Light PSO

```text
RasterizerState:  CullNone (fullscreen triangle)
DepthStencil:     Depth test DISABLED (fullscreen, always shade)
                  Stencil DISABLED
BlendState:       SrcBlend=ONE, DestBlend=ONE (additive)
RTV:              SceneColor (its published HDR format)
DSV:              None (no depth test)
```

### 7.2 Outside-Volume Lighting PSO (Point/Spot)

```text
RasterizerState:  CullBack (render front faces)
DepthStencil:     Depth test LESS_EQUAL / GREATER_EQUAL
                  Stencil DISABLED
                  Depth write DISABLED
BlendState:       SrcBlend=ONE, DestBlend=ONE (additive)
RTV:              SceneColor (its published HDR format)
DSV:              SceneDepth (depth read)
```

### 7.3 Inside-Volume Lighting PSO (Point/Spot)

```text
RasterizerState:  CullFront (render back faces)
DepthStencil:     Depth test ALWAYS
                  Stencil DISABLED
                  Depth write DISABLED
BlendState:       SrcBlend=ONE, DestBlend=ONE (additive)
RTV:              SceneColor (its published HDR format)
DSV:              SceneDepth (depth read)
```

### 7.4 Non-Perspective Bounded-Volume PSO (Point/Spot)

```text
RasterizerState:  CullFront (render back faces)
DepthStencil:     Depth test ALWAYS
                  Stencil DISABLED
                  Depth write DISABLED
BlendState:       SrcBlend=ONE, DestBlend=ONE (additive)
RTV:              SceneColor (its published HDR format)
DSV:              SceneDepth (depth read)
```

Local-light PSOs disable proxy Z clipping as described in section 3.2.
Non-perspective mode does not imply a geometrically inside camera.

## 8. Stage Integration

### 8.1 Dispatch Contract

SceneRenderer dispatches stage 12 through `LightingService` after establishing
the current view and published products in `RenderContext`.

### 8.2 Null-Safe Behavior

If no active direct or static-sky draws remain, the pass emits no lighting draws.
SceneColor retains the existing emissive contribution from BasePass.

### 8.3 Capability Gate

Requires `kDeferredShading` + `kLightingData`. If capabilities are absent,
stage 12 is skipped.

## 9. Resource Management

### 9.1 GPU resources

| Resource                                | Lifetime and ownership                                                                    |
| --------------------------------------- | ----------------------------------------------------------------------------------------- |
| Sphere/cone structured buffers and SRVs | Persistent, service-owned through DeferredLightPass; lazy initialization                  |
| Draw constants and CBVs                 | Per frame/draw; retained through GPU completion                                           |
| Directional/local PSOs                  | Cached, keyed by formats, depth convention, volume mode and shader variants               |
| Framebuffers                            | Rebuilt when referenced scene attachments change                                          |
| Energy LUT                              | One immutable 32x32 RG32F texture shared across views through LightingService publication |

### 9.2 Performance considerations

Directional batching avoids repeated surface reconstruction and material/view
preparation across sources covering the same pixels. Local volume rasterization,
center-attenuation rejection and horizon rejection precede expensive BRDF work.
Source evaluation has fixed work; shadow filtering occurs once per receiver.
Local draw/submission and shaded-overlap cost still scale with relevant lights.

The [PBR rationale](../../renderer-core/physically-based-rendering.md#design-tradeoffs-and-rejected-alternatives)
explains the approximation and LUT choices. The
[EX07 plan](../milestones/exposure/EX07/README.md) owns measured
culling/submission improvements and many-light qualification. A tiled/clustered
deferred replacement requires profiling evidence and a revised service/ABI design.

## 10. Testability Approach

1. **Single directional light:** Render a white sphere on gray plane with
   one white directional light. Verify SceneColor shows correct
   diffuse+specular shading. Compare the shared model with independent diffuse/specular references.
2. **Point light bounded volume:** Place a point light with small range.
   Verify that pixels outside the light range show zero lighting contribution
   (only emissive).
3. **Multi-light accumulation:** Add 3 colored lights (red, green, blue).
   Verify additive accumulation produces expected color mixing.
4. **RenderDoc validation:** Inspect SceneColor after stage 12.
   Verify correct light accumulation, no banding, and correct outside-volume /
   inside-volume local-light products.
