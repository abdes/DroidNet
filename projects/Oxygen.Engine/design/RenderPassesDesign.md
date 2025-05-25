# Render Passes Design

## Overview

The Oxygen Engine render pass system is designed for modular, coroutine-based
rendering pipelines, supporting modern explicit graphics APIs (D3D12, Vulkan)
and advanced techniques such as Forward+ and deferred rendering. Each render
pass encapsulates a single stage of the pipeline (e.g., geometry, shading,
post-processing) and is implemented as a component with official naming and
metadata support.

## Forward+ Rendering in Oxygen

Forward+ rendering is the default and recommended rendering technique in Oxygen
Engine for real-time 3D scenes with many dynamic lights. Forward+ combines the
flexibility and simplicity of forward rendering with the scalability of tiled
light culling, making it well-suited for modern GPUs and complex scenes.

- **How Forward+ is applied:**
  - The engine's render pass system is designed to support Forward+ as a
    first-class approach.
  - Render passes are composed to implement the Forward+ pipeline: typically
    including a depth pre-pass, a light culling pass (using compute shaders),
    and a main shading pass.
  - The modularity of the system allows for easy extension or replacement of
    passes to support other techniques (e.g., classic forward, deferred, or
    custom pipelines), but Forward+ is the default for most Oxygen-based
    applications.
  - The choice of Forward+ is reflected in the default pass graph and in the
    engine's documentation and examples.

- **Why Forward+:**
  - Efficiently handles many dynamic lights by culling lights per screen tile.
  - Retains the benefits of forward rendering (e.g., MSAA, transparency, simple
    material models).
  - Leverages modern GPU compute capabilities for light culling.
  - Provides a good balance between performance, flexibility, and visual quality
    for a wide range of real-time applications.

## Types of Render Passes in Oxygen

Oxygen structures the rendering of a frame as a sequence of specialized render
passes, each responsible for a distinct stage of the graphics pipeline. This
modular approach, inspired by modern engine architecture, enables flexible
composition, extension, and optimization of the rendering workflow. The
following subsections describe each major pass type, its role, and its
relationship to the overall pipeline.

It is important to clarify the role of the geometry pass in different rendering
pipelines, as its presence and function can vary significantly. In deferred
rendering, the geometry pass—often called the G-buffer pass—is a distinct and
essential stage. It is responsible for writing out per-pixel material properties
(such as albedo, normals, and roughness) to multiple render targets, which are
then used in a later shading pass to compute lighting. This separation allows
for efficient handling of many lights and complex materials, as the expensive
lighting calculations are deferred until after all geometry has been processed.

In contrast, Forward+ rendering, which is the default in Oxygen, typically
merges the geometry and shading stages into a single pass. After the depth
pre-pass and light culling, geometry is drawn and shaded in one step, using the
per-tile light lists generated earlier. This approach leverages the strengths of
forward rendering—such as support for MSAA and transparency—while still scaling
efficiently to many dynamic lights. As a result, there is no separate geometry
pass in the Forward+ pipeline; instead, the main shading pass is responsible for
both drawing geometry and applying lighting. This design choice simplifies the
pipeline and reduces the number of passes, while still enabling high performance
and visual quality.

Oxygen's modular system allows for both approaches: if a deferred pipeline is
desired, a dedicated geometry/material pass can be added to the sequence,
followed by a separate shading pass. For Forward+, the geometry and shading are
combined, and the pipeline is streamlined for real-time applications with many
lights. This flexibility ensures that Oxygen can support a wide range of
rendering techniques, while making clear the rationale for the presence or
absence of a standalone geometry pass in each case.

### Depth Pre-Pass

The process typically begins with a depth pre-pass, which renders all opaque
scene geometry to populate the depth buffer before any shading occurs. This
early step is crucial for enabling early-z rejection in subsequent passes,
significantly reducing overdraw and improving performance, especially in scenes
with high geometric complexity or many dynamic lights. The depth buffer is
prepared by transitioning it to a writable state and clearing it, while geometry
buffers are set up for efficient access.

### Light Culling Pass

Following the depth pre-pass, the light culling pass comes into play,
particularly in Forward+ pipelines. Here, a compute shader analyzes the depth
buffer and clusters lights per screen tile, producing a per-tile light list that
will be used for efficient lighting calculations. This pass depends on the
results of the depth pre-pass and requires the depth buffer in a shader-readable
state, as well as buffers for light data and the output light lists.

### Shading Pass

The shading pass is where lighting calculations are performed for each visible
pixel. In Forward+ rendering, this pass combines geometry and shading, using the
depth buffer and per-tile light lists to efficiently compute lighting as
geometry is drawn. In deferred rendering, the shading pass reads from the
G-buffer and applies lighting in a full-screen pass. The resources for this
stage are transitioned to the appropriate states for reading and writing, and
the pass executes either draw calls or a full-screen quad, depending on the
pipeline.

### Shadow Map Pass

Shadow mapping is handled by dedicated shadow map passes, which render the scene
from the perspective of each shadow-casting light. These passes can often be run
in parallel with the depth pre-pass and require shadow map textures and geometry
buffers, with the shadow maps prepared for depth writing and cleared at the
start of each pass.

### Post-Processing Passes

After the main shading, a series of post-processing passes may be applied. These
passes implement screen-space effects such as tone mapping, bloom,
anti-aliasing, and color grading. Each effect typically operates on the output
color buffer and may use additional textures or buffers as needed. Resources are
transitioned to the correct states for reading and writing, and the effects are
usually applied via full-screen quads or compute dispatches.

### UI and Overlay Pass

Finally, the UI and overlay pass renders user interface elements, debug
overlays, and other 2D content on top of the final scene. This pass is executed
after all post-processing is complete, using the final color buffer as its
target and drawing the necessary UI geometry and textures.

## Typical Render Pass Flow Diagram

Below is a high-level diagram illustrating the typical flow of render passes in
Oxygen for both Forward+ and Deferred rendering pipelines. This helps visualize
the modular structure and dependencies between passes.

**Forward+ Pipeline**

Depth Pre-Pass → Light Culling Pass → Shading Pass (geometry + lighting) → Post-Processing Passes → UI/Overlay Pass

**Deferred Pipeline**

Depth Pre-Pass → Geometry/Material Pass (G-buffer) → Light Culling Pass → Shading Pass (lighting) → Post-Processing Passes → UI/Overlay Pass

(Shadow Map Passes are typically run in parallel with the depth pre-pass and
feed shadow data into the shading pass.)

## Render Pass Resource Dependencies and State Transitions

<!--
  Render Pass Resource Dependencies and State Transitions Table (HTML version for maintainability)
  - Each <tr> is a pass; each <td> is a column.
  - Use <br> for line breaks in cells.
  - Add/remove rows as needed; comments can be placed between rows for maintainers.
-->
<table>
  <thead>
    <tr>
      <th>Pass Type</th>
      <th>Dependencies</th>
      <th>Resources Used (IN/OUT)</th>
      <th>Resource State Transitions (D3D12)</th>
    </tr>
  </thead>
  <tbody>
    <!-- Depth Pre-Pass -->
    <tr>
      <td><b>Depth Pre-Pass</b></td>
      <td>Scene geometry, camera</td>
      <td>IN: Vertex/Index buffers (read-only)<br>OUT: Depth buffer (write)</td>
      <td>Depth buffer: Transition to DEPTH_WRITE<br>Geometry: VERTEX/INDEX</td>
    </tr>
    <!-- Light Culling -->
    <tr>
      <td><b>Light Culling</b></td>
      <td>Depth buffer, light data</td>
      <td>IN: Depth buffer (SRV), light data buffer (SRV)<br>OUT: Light list buffer (UAV)</td>
      <td>Depth: DEPTH_WRITE → PIXEL/NON_PIXEL_SHADER_RESOURCE<br>Light list: UAV</td>
    </tr>
    <!-- Shading -->
    <tr>
      <td><b>Shading</b></td>
      <td>Depth, light lists, geometry</td>
      <td>IN: Depth buffer (SRV), light list buffer (SRV), geometry buffers, textures<br>OUT: Color buffer (RTV)</td>
      <td>Color: RENDER_TARGET<br>Depth: SRV<br>Light list: SRV</td>
    </tr>
    <!-- Shadow Map -->
    <tr>
      <td><b>Shadow Map</b></td>
      <td>Scene geometry, light views</td>
      <td>IN: Geometry buffers (read-only)<br>OUT: Shadow map texture (depth write)</td>
      <td>Shadow map: Transition to DEPTH_WRITE, then to SRV for shading</td>
    </tr>
    <!-- Post-Processing -->
    <tr>
      <td><b>Post-Processing</b></td>
      <td>Color buffer, depth (opt.)</td>
      <td>IN: Color buffer (SRV), depth buffer (SRV, opt.)<br>OUT: Color buffer (RTV/UAV)</td>
      <td>Color: SRV → RENDER_TARGET/UAV<br>Depth: SRV (if used)</td>
    </tr>
    <!-- UI/Overlay -->
    <tr>
      <td><b>UI/Overlay</b></td>
      <td>Final color buffer, UI data</td>
      <td>IN: UI geometry/textures (SRV)<br>OUT: Final color buffer (RTV)</td>
      <td>Color: RENDER_TARGET<br>UI: SRV</td>
    </tr>
  </tbody>
</table>

- **IN**: Resource is read by the pass (`SRV` = Shader Resource View, `UAV` =
  Unordered Access View, `RTV` = Render Target View).
- **OUT**: Resource is written by the pass.
- Resource state transitions are managed by the engine's `ResourceStateTracker`,
  which issues barriers as needed.
- Bindless resource registration and view management are handled by the
  `ResourceRegistry`, ensuring correct descriptor heap and view state for each
  pass.
- Pipeline State Objects (PSOs) are cached and bound per-pass, with root
  signature conventions enforced for bindless access.

## Render Pass Orchestration and Pipeline State Ownership

In Oxygen, the orchestration of render passes for each frame is handled by the
application's `RenderScene()` function (or equivalent), which is called by the
`RenderThread`. This function determines which passes are needed for the current
frame, their order, and their configuration. The `Renderer` is responsible for
the lifetime, ownership, and registration of all `RenderPass` instances, and
provides APIs to add, remove, or query passes. The `Renderer` also ensures that
passes are executed in the correct order and may cache or reuse passes across
frames.

Pipeline state objects (PSOs) are owned and managed by the `Renderer`, with
backend-specific implementation (such as D3D12) provided by the
`PipelineStateCache` component. Render passes do not create or own PSOs
directly; instead, they request the required PSO from the `Renderer` or its
pipeline state cache, providing a pipeline description or hash. This ensures
efficient reuse and centralized management of pipeline state.
