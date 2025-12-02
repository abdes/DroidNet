# Multi-View Rendering: Architecture Principles

- **PRINCIPLE-01**: Upload view-independent resources once per frame; all views share the same GPU data via bindless indices
- **PRINCIPLE-02**: Design resources as view-agnostic (geometry, materials, transforms are world-space, not view-space)
- **PRINCIPLE-03**: Renderer executes render graph once per view (one view = one camera); only camera and framebuffer change between views
- **PRINCIPLE-04**: Single view can produce multiple outputs (lighted + wireframe) within one graph execution; orthogonal to multi-view
- **PRINCIPLE-05**: Renderer orchestrates view iteration; application registers views and graph, not iteration mechanics
- **PRINCIPLE-06**: Per-view culling produces different draw lists; underlying uploaded resources remain shared
- **PRINCIPLE-07**: Application defines view-to-surface mapping; engine handles presentation based on application's ViewMetadata configuration
- **PRINCIPLE-08**: Clean layer separation (Engine, Upload, Renderer, Application)
- **PRINCIPLE-09**: Multi-view outputs collected for compositing as separate phase after views complete

---

## PRINCIPLE-01

All geometry, materials, and transforms are uploaded to GPU **once per frame**. Every view
references the same uploaded data via shared resource indices. Transforms represent object positions
in world space—they are inherently view-independent. Only the camera's view of these transforms
changes per-view, not the transforms themselves. This eliminates redundant uploads and minimizes
bandwidth/memory footprint.

## PRINCIPLE-02

Heavy data structures are designed without view-specific coupling: vertex/index buffers, material
constants, transform buffers (world matrices, normal matrices), and texture arrays contain
world-space data. A scene with 1000 objects and 3 views uploads 1000 transforms total, not 3000.
View-dependent data (camera matrices, culling results) is handled separately without duplicating the
underlying resources.

## PRINCIPLE-03

A **view** is defined by a unique camera (position, orientation, projection). The Renderer executes
the render graph **once per camera/view**. Between view iterations, only per-view state (camera
matrices, framebuffer) is updated. Uploaded resources (transforms, materials, geometry) remain
constant across all views.

**Exception**: Passes marked as **View-Independent** (e.g., global shadow maps, simulation) execute
only during the first view's iteration (or a preamble) and reuse results for subsequent views. This
avoids redundant GPU work while maintaining a uniform graph structure.

## PRINCIPLE-04

Within a **single view's graph execution** (one camera), the graph can produce multiple outputs:
lighted rendering + wireframe rendering, or color + depth + geometry buffers. This is **multi-target
rendering** from the same camera, accomplished via conditional passes or multiple render targets
within the graph. This is orthogonal to multi-view: multi-view = loop over cameras (PRINCIPLE-03),
multi-target = multiple outputs per camera (PRINCIPLE-04). Example: 3 editor viewports, each
producing lighted + wireframe = 3 graph executions (multi-view), each producing 2 outputs
(multi-target) = 6 total outputs. Applications can query current view identity to customize behavior
per-view if needed.

## PRINCIPLE-05

The **Renderer controls multi-view iteration**, not the application. Applications register views
during frame setup and provide the render graph. The Renderer handles view loop mechanics: iterating
registered views, updating per-view state, clearing retained items between views, and capturing
outputs. This centralizes multi-view complexity in the rendering system where it belongs.
Applications focus on "what to render" (views, graph), not "how to iterate" (order, state
management).

## PRINCIPLE-06

Each view can have its own culling results and draw lists because different camera
positions/frustums produce different visibility sets. However, all views share the same uploaded
transform/material/geometry buffers. Per-view culling only affects which draws are submitted, not
which resources are uploaded. Per-view draw metadata may differ, but the shared resource indices for
transforms/materials remain constant across views.

## PRINCIPLE-07

The application defines how view outputs map to presentation surfaces via metadata when registering
views. The engine executes presentation based on this configuration: direct presentation to
surfaces, hidden (off-screen rendering), or deferred for compositing. The application controls "what
presents where"; the engine controls "when and how" based on policy. This separates presentation
intent (application domain) from presentation mechanics (engine domain).

## PRINCIPLE-08

Each layer has a single responsibility with clear interfaces:

- **Application Layer**: Provides render graph definition, view configurations (cameras, viewports,
  metadata), render targets, and view-to-surface mapping policies
- **Renderer Layer**: Owns per-view rendering state, manages render passes, orchestrates view
  iteration, executes render graph per-view, captures view outputs
- **Upload Layer**: Handles CPU-GPU resource synchronization—staging, upload coordination,
  view-agnostic resource preparation
- **Engine Layer**: Owns frame-wide state, manages frame lifecycle, surfaces, scenes, timing, module
  coordination

This separation makes the system maintainable and extensible. Each layer evolves independently with
stable interfaces between them.

## PRINCIPLE-09

After each view completes rendering, the Renderer captures its output. A separate **compositing
phase** consumes these outputs to produce final presentation. This phase runs after the main render
loop and before presentation. It has access to `CommandRecorder` and the collected `view_outputs`.
It does not strictly require a full Render Graph; it can be a simple sequence of blits or UI draw
calls. This decouples view rendering (scene generation) from presentation strategy (window layout).

## PRINCIPLE-10

Passes communicate implicitly via the **RenderContext** (Blackboard Pattern). Passes "publish"
outputs (textures, buffers) to the context, and subsequent passes "subscribe" by looking for them.
The Renderer does not enforce an explicit dependency graph. It is the Application's responsibility
to configure pass order and render targets correctly. This allows passes to be loosely coupled and
highly configurable via coroutines.

---

**Document Status**: Draft
**Last Updated**: 2025-12-02
**Part of**: Multi-View Rendering Design Series
