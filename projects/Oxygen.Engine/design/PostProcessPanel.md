# Post Process Panel Design

## Overview

The `PostProcessPanel` provides a unified interface for configuring post-processing effects in `DemoShell`. It follows the existing MVVM architecture (Service -> ViewModel -> View) and integrates with the engine's `SettingsService` for persistence.

> **Related Documents:**
>
> - [PBR Architecture](PBR.md) — unit conventions and data model
> - [Physical Lighting Roadmap](physical_lighting.md) — phased implementation plan
> - [Forward Pipeline Design](forward_pipeline.md) — rendering architecture

## Architecture

### 1. Data Model & Persistence (`PostProcessSettingsService`)

Manages persistence of settings using keys under the `post_process.` namespace.

**Namespace:** `oxygen::examples::ui`

| Category | Setting | Key | Type | Default |
| :--- | :--- | :--- | :--- | :--- |
| **Compositing** | Enabled | `post_process.compositing.enabled` | Bool | `true` |
| | Global Alpha | `post_process.compositing.alpha` | Float | `1.0` |
| **Exposure** | Enabled | `post_process.exposure.enabled` | Bool | `true` |
| | Mode | `post_process.exposure.mode` | Enum (Manual, ManualCamera, Auto) | `Manual` |
| | Compensation (EV) | `post_process.exposure.compensation` | Float | `0.0` |
| | Manual EV (EV100) | `post_process.exposure.manual_ev` | Float | `9.7` |
| **Tonemap** | Enabled | `post_process.tonemapping.enabled` | Bool | `true` |
| | Mode | `post_process.tonemapping.mode` | Enum | `ACES` |

**Camera Exposure Persistence (CameraLifecycleService):**

Manual camera exposure settings are owned by the active camera and persisted
under the `camera_rig.<camera_id>.camera.exposure.*` namespace. They are not
stored in the post-process settings service.

| Setting | Key | Type | Default |
| :--- | :--- | :--- | :--- |
| Enabled | `camera_rig.<camera_id>.camera.exposure.enabled` | Bool | `false` |
| Aperture (f-stop) | `camera_rig.<camera_id>.camera.exposure.aperture_f` | Float | `11.0` |
| Shutter (1/s) | `camera_rig.<camera_id>.camera.exposure.shutter_rate` | Float | `125.0` |
| ISO | `camera_rig.<camera_id>.camera.exposure.iso` | Float | `100.0` |

The post-process service binds to the camera lifecycle to read and update the
active camera exposure, then applies the resulting EV (EV100) to the runtime
pipeline. The post-process service does not own camera exposure data.

**Default EV Rationale:**

The default EV of `9.7` (referenced to ISO 100 / EV100) corresponds to a
typical sunny outdoor scene with:

- Aperture: f/11 (`aperture = 11.0`)
- Shutter: 1/125s (`shutter = 125.0`)
- ISO: 100 (`iso = 100.0`)

This provides a sensible starting point that matches real-world photography and industry standards.

### 2. View Model (`PostProcessVm`)

Bridges the settings service and the UI. It exposes reactive properties that
the view binds to and delegates orchestration to the service; EV conversion
for manual camera exposure is provided by the camera exposure model.

**Location:** `Examples/DemoShell/UI/PostProcessVm.h`

**Dependencies:**

- `PostProcessSettingsService` (Persistence and pipeline application)
- `ToneMapPassConfig` (Runtime Engine Config, see [forward_pipeline.md](forward_pipeline.md))

### 3. View (`PostProcessPanel`)

Implements the UI using ImGui.

**Location:** `Examples/DemoShell/UI/PostProcessPanel.cpp`

**Layout (Pro):**

- **Exposure** (Collapsing Header, primary)
  - [x] Toggle: Enable Exposure
  - [x] Combo: Mode [Manual (EV) | Manual (Camera) | Automatic]
  - *Manual (EV)*:
    - [x] Slider: EV [0.0, 16.0]
    - [x] Drag: Compensation (EV) [-10.0, +10.0] (disabled)
  - *Manual (Camera)*:
    - [x] Drag: Aperture (f/) [1.4, 32.0]
    - [x] Drag: Shutter (1/s) [1/8000, 60]
    - [x] Drag: ISO [100, 6400]
    - [x] Text: Computed EV (Read-only)
    - [x] Drag: Compensation (EV) [-10.0, +10.0] (disabled)
  - *Automatic*:
    - [x] Drag: Compensation (EV) [-10.0, +10.0]
    - [ ] Drag: Adaptation Speed Up (EV/s)
    - [ ] Drag: Adaptation Speed Down (EV/s)
  - [x] Read-only: Final Exposure (linear)

- **Tone Mapping** (Collapsing Header)
  - [x] Toggle: Enabled
  - [x] Combo: Operator [ACES | Filmic | Reinhard | None]
  - [ ] Toggle: Apply before UI (debug)

- **Compositing** (Collapsing Header)
  - [x] Toggle: Enabled
  - [x] Drag: Global Alpha [0.0, 1.0]
  - [ ] Toggle: Skip Tonemap for HDR backbuffer (debug)

- **General** (Advanced)
  - [ ] Button: Reset to Defaults
  - [ ] Read-only: Active Pipeline Path (HDR->SDR or Direct)

**Tonemapper Options:**

| Engine Enum | UI Label | Description |
| ----------- | -------- | ----------- |
| `ToneMapper::kAcesFitted` | ACES | Industry-standard filmic curve (default) |
| `ToneMapper::kFilmic` | Filmic | Classic filmic S-curve |
| `ToneMapper::kReinhard` | Reinhard | Simple luminance mapping |
| `ToneMapper::kNone` | None | Passthrough (debug only) |

### 4. Engine Integration (The Interceptor & Coroutine Graph)

The `PostProcessPanel` directly influences the structure of the `ForwardPipeline` render coroutine. For a deeper dive into the rendering architecture, see [Forward Pipeline Design](forward_pipeline.md).

1. **Standard PBR (Offscreen -> Composite)**:
    - **Why**: Required for physically correct Tone Mapping (HDR -> SDR), Exposure, and Alpha Compositing (UI over Scene).
    - **Mechanism**: The **Interceptor Pattern** ([REQ02](forward_pipeline.md#requirements-specification)) redirects view outputs (ViewContext) to an internal `Format::kRGBA16Float` HDR intermediate.
    - **Resolution**: In the `OnCompositing` phase, the pipeline executes the **Post-Processing Chain** (`ToneMapPass`, etc.) to resolve to the SDR Swapchain.
2. **Passthrough (Direct -> Swapchain)**:
    - **Why**: High-performance path for debug views or simple assets where post-processing is undesirable.
    - **Mechanism**: Views render directly to the Swapchain Backbuffer. Post-processing controls are bypassed.

**Compositing Flow (Standard PBR):**

```text
┌─────────────────────────────────────────────────────────────────┐
│ OnSceneMutation                                                  │
│   └─ context.SetViewOutput(view_id, hdr_framebuffer)            │
├─────────────────────────────────────────────────────────────────┤
│ OnRender                                                         │
│   └─ Scene rendered to HDR Intermediate (Format::kRGBA16Float)  │
├─────────────────────────────────────────────────────────────────┤
│ OnCompositing                                                    │
│   ├─ ToneMapPass: HDR -> SDR with exposure & tonemapping        │
│   ├─ CompositingPass: Alpha blend views to backbuffer           │
│   └─ ImGuiPass: UI rendered directly to SDR backbuffer          │
└─────────────────────────────────────────────────────────────────┘
```

**Data Flow:**

```text
PostProcessPanel (UI)
  │
  ▼
PostProcessVm (Cached State)
  │
  ▼
PostProcessSettingsService (Persistence + Orchestration)
  │
  ▼
CameraLifecycleService (Active Camera Exposure)
    │
    ▼
ForwardPipeline.staged_ (Runtime Buffer)
    │
    ▼
ToneMapPassConfig (Pass Configuration)
    │
    ▼
ToneMapPass.DoExecute() (GPU Execution)
```

### 5. Engine Pass Configuration

The `ForwardPipeline` manages configuration for rendering passes. Each pass defines its own configuration struct, following the extensible pattern:

**ToneMapPassConfig** (`src/Oxygen/Renderer/Passes/ToneMapPass.h`):

```cpp
struct ToneMapPassConfig {
  ExposureMode exposure_mode { ExposureMode::kManual };
  float manual_exposure { 1.0F };  // Computed from EV100
  ToneMapper tone_mapper { ToneMapper::kAcesFitted };
  bool enabled { true };
  std::string debug_name { "ToneMapPass" };
};
```

**CompositingPassConfig** (`src/Oxygen/Renderer/Passes/CompositingPass.h`):

```cpp
struct CompositingPassConfig {
  std::shared_ptr<graphics::Texture> source_texture {};
  ViewPort viewport {};
  float alpha { 1.0F };
  std::string debug_name { "CompositingPass" };
};
```

**Design Note:** Future post-processing passes (Bloom, FXAA, DOF) will follow this same pattern, each defining their own `*PassConfig` struct. The pipeline orchestrates these passes without requiring a monolithic configuration object.

---

## Implementation Plan

### Phase A: Foundation (UI & Data)

- [x] Define Services & VM (`oxygen::examples::ui`).
- [x] Create `PostProcessPanel` UI (Basic exposure, tonemapping, and compositing controls).
- [x] Register panel in `DemoShellUi`.

### Phase B: Rendering Refactor (Engine Integration)

> Realign `ForwardPipeline` with the **Coroutine Graph** and **Interceptor Pattern** as specified in [forward_pipeline.md, REQ01-REQ10](forward_pipeline.md#requirements-specification).

- [x] **Interceptor Implementation** (REQ02): Implement target redirection in `OnSceneMutation` using `context.SetViewOutput()`.
- [x] **Lifecycle Management** (REQ01, REQ08): Clean management of HDR intermediates in `OnFrameStart` and `ClearBackbufferReferences`.
- [x] **Composition Logic** (REQ04): Implement the awaitable compositing sequence in `OnCompositing`.

### Phase C: PostProcess Integration (Exposure & Tonemapping)

- [x] **ToneMapPass Implementation** (REQ03): Move staged settings (Exposure/Tonemap) into the `ToneMapPass` execution.
- [x] **Camera Controls**: Implement "Manual (Camera)" exposure mode (Aperture/Shutter/ISO to EV100 conversion).
  - Formula: $EV100 = \log_2(\frac{N^2}{t}) - \log_2(\frac{ISO}{100})$
  - See [physical_lighting.md, Phase 2](physical_lighting.md#phase-2--exposure-system-manual--physical) for `CameraExposure` struct specification.
- [x] **Coroutine Contributors** (REQ07): Register post-process passes as awaitable contributors to the main pipeline. (Implemented — post-process passes are executed as awaitable coroutines; tonemapping runs per-view via `ToneMapPass` in `ForwardPipeline`; `CompositingTaskType::kTonemap` has been removed.)
- [x] **UI Validation**: Verify UI values correctly update the `ForwardPipeline::staged_` data in real-time.

---

## Appendix: EV100 Reference Values

| Scene | Typical EV100 | Notes |
| ----- | ------------- | ----- |
| Sunny Outdoor | 14-16 | Bright daylight |
| Overcast | 12-14 | Cloudy day |
| Shade | 10-12 | Open shade |
| Indoor (bright) | 7-9 | Well-lit interior |
| Indoor (dim) | 4-6 | Ambient lighting |
| Night (street) | 2-4 | Street lighting |
| Night (dark) | -2 to 2 | Moonlight, starlight |

The UI slider range of [-6.0, 16.0] covers the full range of typical photographic scenarios.
