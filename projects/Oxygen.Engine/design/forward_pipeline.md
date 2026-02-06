# Forward Pipeline Design & Evolution

## Overview

The `ForwardPipeline` is a versatile, multi-view rendering pipeline that renders
each view into HDR and SDR intermediates, applies tonemapping per-view, and then
composites SDR outputs into the final backbuffer. The render graph is registered
per view as a coroutine with the `Renderer`, and the pipeline owns the pass
sequence and view-local resources.

> **Note on Engine Promotion**: This pipeline is designed to be promoted from the example/demo space into the **Oxygen Engine Core**. As such, the design prioritizes API stability, strict resource ownership, and a highly ergonomic plugin architecture.
> **Related Documents:**
>
> - [PBR Architecture](PBR.md) — unit conventions and data model
> - [Physical Lighting Roadmap](physical_lighting.md) — phased implementation plan
> - [PostProcessPanel Design](PostProcessPanel.md) — UI integration

## Definitions

- **Render Graph (Coroutine)**: In Oxygen, a render graph is not a static object hierarchy or a data-flow DAG. It is a **data-driven coroutine** registered with the `Renderer`. Execution is linear and synchronous from the perspective of the developer, using `co_await` to yield control for resource transitions or child task execution.
- **ViewContext**: A per-frame structure describing a view's camera, viewport, and output target. It acts as the "contract" between the pipeline and the engine for a single view. Defined in `src/Oxygen/Core/FrameContext.h`.
- **Interceptor Pattern**: The architectural act of re-routing a view's output target from the backbuffer to an internal HDR intermediate target before it reaches the shading phase.
- **Coroutine Contributors**: Lightweight, awaitable task generators that inject work into the main render coroutine. These replace classical "Pipeline Passes" or "Middlewares" with native `co_await`able logic.
- **Composition Phase (`OnCompositing`)**: An engine-global phase occurring after all 3D rendering. It assembles pre-tonemapped SDR intermediates into the final swapchain backbuffer.
- **HDR Chain**: A series of post-processing coroutines that operate on raw radiance (high dynamic range) data before tonemapping (e.g., Bloom, TAA).
- **SDR Chain**: Post-tonemapping coroutines that operate on color-graded, non-linear (low dynamic range) data (e.g., Vignette, Sharpness, UI).
- **Composition Submission**: A list of `CompositingTask`s (Blit, Tonemap, Blend) submitted to the `Renderer` to resolve the final frame.

---

## Lifecycle of Core Entities

Understanding how views, scenes, and surfaces flow through the engine is critical for correct pipeline implementation. The system distinguishes between the **High-Level Intent** (`FrameContext`) and the **Execution State** (`RenderContext`).

> **Error Handling:** Violations of lifecycle constraints (e.g., registering views
> after the snapshot gate) will throw an exception or abort in debug builds.
> The engine enforces strict phase ordering.

### 1. View Lifecycle

Views define *what* is being rendered from which perspective.

- **Registration & Topology (`OnSceneMutation`) - [STRUCTURAL MUTATION GATE]**:
  - The Application (or `DemoModuleBase`) registers views with the `FrameContext` via `RegisterView(view_ctx)`.
  - **Rationale**: The engine's `Renderer` assumes a fixed topology (number of views, viewports) at the start of `OnPreRender`. This stability is required to allocate per-view command recorders and specialized descriptor heaps that cannot be resized mid-flow.
  - **Interceptor Application**: The pipeline MUST redirect outputs (via `context.SetViewOutput()`) here to ensure the renderer prepares the correct render target for the subsequent shading phases.
- **Snapshot Resolution (`OnPreRender`) - [READ-ONLY SNAPSHOT GATE]**:
  - The `Renderer` invokes the `ViewResolver` to compute camera matrices and frustums, creating a `ResolvedView`.
  - **Rationale**: From this point forward, the `FrameContext` view list is effectively "frozen". The `Renderer` works from its own internal map of `ResolvedViews` to ensure consistency even if a module attempts a late registration.
- **Execution (`OnRender`)**:
  - The `Renderer` populates a `RenderContext` for each view iteration using the snapshot data.
- **Cleanup**: `UnregisterView` triggers deferred cleanup of view-specific resources at the point where they are no longer in flight on the GPU.

### 2. Scene Lifecycle

The `Scene` is the container for all renderable entities and environment data.

- **Staging (`OnFrameStart`) - [PUBLICATION GATE]**:
  - The active `Scene` is published to the `FrameContext` via `context.SetScene()`.
  - **Rationale**: High-level modules (e.g., `PostProcessSettingsService`, `CameraLifecycle`) need the scene pointer early to prepare data that will be captured in the `UnifiedSnapshot`.
- **Freezing (`OnPreRender`)**:
  - The `Renderer` transfers the scene pointer to `RenderContext::scene`.
  - **Rationale**: Any scene swaps requested *during* a render loop are deferred to the *next* frame. This prevents "half-rendered" scenes where some passes use old scene data and others use new data.

### 3. Surface Lifecycle

Surfaces represent the final presentation targets (Swapchains).

- **Registration (`OnFrameStart`) - [TOPOLOGY GATE]**:
  - Managed by `AppWindow`. During `OnFrameStart`, the surface is added to `FrameContext::AddSurface()`.
  - **Rationale**: Surfaces are structural; they define the "Windows" of the application. The list is frozen during snapshot publishing to ensure consistent identification for `CompositionTask`s.
- **Compositing (`OnCompositing`) - [MUTATION ALLOWED]**:
  - **Logic**: The Pipeline determines which views are visible on which surface.
  - It submits a `CompositionSubmission` targeting the surface's backbuffer.
- **Presentation (`OnCompositing` Completion)**:
  - `SetSurfacePresentable(true)` signals the engine that command recording is finished.
  - **Rationale**: This is the final "hand-shake" that moves the surface from the "Recording" state to the "Presentation" state.

---

## Requirements Specification

To consider this implementation complete and "Engine Ready", the following requirements must be met:

| Done | ID | Requirement | Status |
| ---- | -- | ----------- | ------ |
| ✅ | REQ01 | **HDR Resource Management**: The pipeline shall automatically create and manage an intermediate HDR (`Format::kRGBA16Float`) Texture and Framebuffer. These must resize dynamically to match the output surface (window) extent. | Implemented |
| ✅ | REQ02 | **Render Target Redirection (Interceptor)**: When the HDR path is enabled, the pipeline updates each view's `ViewContext` output to use the intermediate framebuffer during `OnSceneMutation`. | Implemented |
| ✅ | REQ03 | **Exposure & Tonemapping Persistence**: The pipeline consumes staged exposure and tonemapper settings and applies them to `ToneMapPass` during per-view execution. | Implemented |
| ✅ | REQ04 | **SDR Composition**: `OnCompositing` blends pre-tonemapped SDR intermediates into the backbuffer via `CompositingTask`s. | Implemented |
| ✅ | REQ05 | **PBR ImGui Isolation**: ImGui renders in the SDR overlay stage after tonemapping has completed. | Implemented |
(Bloom, Gizmos) out of the main pipeline source while remaining `co_await`able from the main render sequence. | Pending |
| ✅ | REQ08 | **Resource Cleanup**: All intermediate GPU resources must be correctly released during resize events or shutdown via the `ClearBackbufferReferences()` hook. | Implemented |
| ✅ | REQ09 | **Multi-View Support**: The compositing logic must support multiple views (e.g., PiP), ensuring each one is correctly blended onto the final backbuffer based on its viewport. | Pending |

---

## Core Architecture

### 1. The Interceptor Pattern

The pipeline manages the lifecycle of intermediate HDR textures (`Format::kRGBA16Float`).
During `OnSceneMutation`, each view is registered with a `ViewContext` whose
output points at the HDR framebuffer; the render graph lambda then writes HDR,
tonemaps into SDR, and emits the SDR result for compositing.

### 2. High-Fidelity Composition

The transition from HDR Intermediate to SDR Backbuffer involves:

- **Exposure Control**: Managed by `ToneMapPass` using Manual EV100 or Automatic Exposure modes.
- **Tone Mapping**: ACES, Filmic, Reinhard, or None operators executed as part of the `ToneMapPass`.
- **Blending**: Alpha compositing of multiple SDR view results via `CompositingTask`.

---

## Post-Processing Stack

The `ForwardPipeline` implements an extensible post-processing stack that resides between the HDR shading and the SDR backbuffer.

### 1. ToneMapPass Primary Integration

The `ToneMapPass` is the core bridge between HDR and SDR. It is managed as a
standalone `GraphicsRenderPass` inside the per-view render coroutine.

- **Location**: `src/Oxygen/Renderer/Passes/ToneMapPass.h`
- **Operation**: It takes the HDR intermediate texture as input and writes into
  the per-view SDR texture that is later blended in `OnCompositing`.
- **Configuration**: Driven by `ToneMapPassConfig`, updated from staged settings
  in the pipeline.

**Configuration Structure:**

```cpp
struct ToneMapPassConfig {
  ExposureMode exposure_mode { ExposureMode::kManual };
  float manual_exposure { 1.0F };
  ToneMapper tone_mapper { ToneMapper::kAcesFitted };
  bool enabled { true };
  std::string debug_name { "ToneMapPass" };
};
```

### 2. Extensible Pass Chain (Future Proofing)

To support future effects (Bloom, Depth of Field, TAA, Chromatic Aberration), the pipeline adopts a "Chain" architecture:

- **HDR Chain (Pre-Tonemap)**: Passes that operate on HDR data (e.g., Bloom, TAA). These are inserted before `ToneMapPass`.
- **LDR Chain (Post-Tonemap)**: Passes that operate on SDR data (e.g., FXAA, Vignette, Sharpening). These are executed after `ToneMapPass` but before final UI/Gizmo composition.
- **Pipeline hooks**: Future versions will expose `AddPostProcessPass<T>(...)` to allow modules to inject specialized passes into these chains.

**Design Principle:** Each post-process pass defines its own configuration struct (e.g., `BloomPassConfig`, `FXAAPassConfig`). The pipeline manages a heterogeneous collection of pass configurations, enabling type-safe, extensible post-processing without a monolithic config struct.

---

## Extensibility Hooks

To support complex applications like `MultiView` or an Editor, the pipeline provides two primary extensibility points.

### 1. Composable Coroutine Architecture

The pipeline avoids a static "graph" in favor of a **dynamic composition of coroutines**. Extensions are provided as **Coroutine Contributors**.

- **Mechanism**: The pipeline's main render lambda `co_await`s a sequence of registered sub-coroutines.
- **Data-Driven Configuration**: Each sub-coroutine is configured via data structures (e.g., `ToneMapPassConfig`) rather than being hardcoded into a class hierarchy.
- **Execution**: The `Renderer` executes the resulting top-level coroutine, moving through the `co_await` suspension points as GPU work is prepared.

### 2. Standardized Composition Hooks

Applications can inject their own awaitable tasks into the final backbuffer resolution sequence.

- **Mechanism**: The pipeline exposes an `Observation` point where applications can register **Composition Task Generators**.
- **Efficiency**: Because these are coroutines, they can perform complex multi-stage work (like TAA or Bloom) with minimal boilerplate by simply `co_await`ing sub-passes.

### 3. Overlay Management

Overlays are categorized based on their coordinate space and dynamic range requirements.

#### A. Scene-Space Overlays (Editor Gizmos, 3D Grids)

These overlays exist relative to the 3D world and often require depth testing against the scene geometry.

- **Execution**: Rendered during the main view's render graph execution.
- **Rendering**: Implemented via a dedicated `WireframePass` with depth enabled
  for pure wireframe. Overlay wireframe is rendered after tonemap in SDR.
- **Space**: Pure wireframe is rendered into HDR; overlay wireframe is rendered
  into SDR to avoid exposure/tonemapping shifts.

#### B. Screen-Space Overlays (Debug Text, Performance Graphs)

Non-diegetic information that should remain crisp regardless of scene exposure.

- **Execution**: Rendered during `OnCompositing`.
- **Rendering**: Directly into the **SDR Backbuffer** (Swapchain).
- **Mechanism**: Managed via an `OverlayRegistry` in the pipeline where systems can submit draw commands or pre-rendered textures.

---

## ImGui & UI Handling

In a PBR workflow, UI elements must reside in SDR space to avoid being "blown out" by exposure compensation or distorted by tone mapping curves.

- **Direct Mode**: ImGui remains part of the view's render graph (legacy/simple path).
- **HDR Mode**:
    1. Pipeline renders the scene to the HDR intermediate.
    2. Pipeline tonemaps the intermediate to the per-view SDR texture.
    3. Pipeline executes the `ImGuiPass` against the SDR framebuffer during the
       overlay stage, before compositing.
- **Integration**: The pipeline handles state transitions and ensures ImGui is
  drawn in SDR space to avoid exposure/tonemapping artifacts.

---

## Developer Implementation Guide

## Cohesive Design Enhancements

To keep render-mode and tonemapping behavior consistent, consolidate all
decisions into a single policy object that is computed once per view and then
consumed by the render coroutine. This avoids implicit coupling and reduces
the risk of new features reintroducing exposure bugs.

### 1. RenderPolicy (Per-View)

Introduce a `RenderPolicy` that captures every decision required to render a
view. It is computed in `OnSceneMutation` (after view intent is known) and
stored alongside the view's runtime state.

**Responsibility:** select render targets, pass ordering, exposure/tonemap
behavior, and overlay placement based on render mode and view flags.

```cpp
struct RenderPolicy {
  RenderMode render_mode;
  bool render_hdr_scene;
  bool render_sky;
  bool render_transparent;

  bool run_tonemap;
  engine::ExposureMode exposure_mode;
  float manual_exposure;
  engine::ToneMapper tone_mapper;

  bool render_wireframe;
  bool wireframe_after_tonemap;
  bool wireframe_apply_exposure_compensation;
};
```

### 2. RenderMode Matrix (Single Source of Truth)

Define a small lookup table that translates `(render_mode, force_wireframe)`
into a `RenderPolicy`. The matrix is the sole place where the pipeline decides
tonemap neutrality, wireframe placement, and exposure compensation.

**Benefits:** new passes and modes share the same decisions; no scattered
conditionals.

### 3. Color-Space Routing Layer

Add a lightweight routing helper that explicitly declares the color space for
each pass (HDR or SDR) and binds the correct target. This makes it impossible
to accidentally draw HDR debug content into SDR or vice versa.

```cpp
enum class ColorSpace { kHdr, kSdr };
struct PassRoute {
  ColorSpace space;
  std::shared_ptr<graphics::Texture> color_target;
};
```

### 4. Enforced Invariants

Document and enforce these invariants with debug asserts and logs:

- Overlay wireframe must run after tonemap into SDR.
- Pure wireframe must neutralize tonemap or disable exposure compensation.
- ImGui always renders in SDR.
- Debug shading modes must not activate for pure wireframe.

### 5. One-Place Configuration Flow

Expose a single `ApplyPolicy()` method in the pipeline that consumes the policy
and configures all pass configs for the frame. This prevents partial updates
from leaking between passes.

---

### Render Modes and Wireframe

The pipeline supports three render modes: solid, pure wireframe, and overlay
wireframe. Each mode has explicit exposure/tonemap handling:

- **Pure wireframe**: The view renders only the wireframe pass into the HDR
  target. Tonemapping is forced neutral (manual exposure 1.0, tone mapper none)
  for that view, and the wireframe shader disables exposure compensation.
- **Overlay wireframe**: The scene renders normally and is tonemapped into the
  SDR target. The wireframe pass then draws into the SDR target after tonemap,
  preserving UI-selected line color.
- **Solid**: Standard HDR shading and tonemapping flow.

This section provides a starting point for implementing this specification. Study these files to understand existing patterns and where to apply changes.

### 1. Key Framework Patterns (To Study)

- **Manual Compositing**: `Examples/MultiView/MainModule.cpp`
  - Look at `OnCompositing` to see how `CompositingTask`s are created and submitted via `renderer.RegisterComposition`.
- **View Management**: `src/Oxygen/Core/FrameContext.h`
  - Study `RegisterView`, `GetViewContext`, and `SetViewOutput`. This is how you will implement the "Interceptor" pattern to redirect rendering to HDR targets.
- **Pass Implementation**: `src/Oxygen/Renderer/Passes/CompositingPass.cpp`
  - Excellent reference for how to implement a full-screen quad pass that samples from a source texture (useful for understanding how `ToneMapPass` should work).
- **Format Specification**: `src/Oxygen/Core/Types/Format.h`
  - Use `Format::kRGBA16Float` for HDR intermediate textures.

### 2. Implementation Checklist (Target Files)

| Task | File | Notes |
| ---- | ---- | ----- |
| **Resource Lifecycle** | `Examples/DemoShell/Runtime/ForwardPipeline.cpp` | Create/resize HDR + SDR per-view textures and release them in `ClearBackbufferReferences()`. |
| **The Shader Bridge** | `src/Oxygen/Renderer/Passes/ToneMapPass.cpp` | Maintain the HDR to SDR tonemap pass; keep exposure control per-view. |
| **Configuration** | `Examples/DemoShell/DemoShell.cpp` | Wire settings to the pipeline and apply at `OnFrameStart`. |
| **Rendering Redirect** | `Examples/DemoShell/Runtime/ForwardPipeline.cpp` | Register each view with the correct output framebuffer during `OnSceneMutation`. |

---

## Development Tasks

| ID | Task | Notes |
| -- | ---- | ----- |
| T01 | Define `RenderPolicy` struct | Add in ForwardPipeline runtime state and document fields. |
| T02 | Implement RenderMode matrix | Single function mapping `RenderMode` + flags to policy. |
| T03 | Add color-space routing helper | Map passes to HDR/SDR targets explicitly. |
| T04 | Centralize pass configuration | `ApplyPolicy()` sets configs once per frame. |
| T05 | Add invariants and asserts | Enforce wireframe and ImGui placement. |
| T06 | Update UI behavior | Disable debug modes when pure wireframe is selected. |
| T07 | Add logging hooks | Optional debug logs for policy decisions. |
| T08 | Add tests/validation plan | Render-mode matrix unit tests + smoke tests. |

### 3. HDR Intermediate Format

Use `Format::kRGBA16Float` (64 bits per pixel, half-precision float RGBA) for all HDR intermediate textures. This provides:

- Sufficient dynamic range for HDR content (±65504 range per channel)
- Good precision for post-processing operations
- Reasonable memory bandwidth

```cpp
// Example: Creating an HDR intermediate texture
graphics::TextureDesc hdr_desc {
  .width = surface_width,
  .height = surface_height,
  .format = Format::kRGBA16Float,
  .usage = TextureUsage::kRenderTarget | TextureUsage::kShaderResource,
  .debug_name = "HDR_Intermediate",
};
```
