# Scene Environment (Scene-Global)

This folder contains the *scene-global* environment model for Oxygen.

- `SceneEnvironment` is a standalone `oxygen::Composition` that hosts a
  variable set of environment systems.
- Each environment system is an authored-data `Component` (parameters only).
- The renderer owns all derived GPU resources (LUTs, froxel volumes, cubemap
  convolutions, temporal history buffers, etc.).

This matches the design spec: environment is not a node, not a `Scene`
component, and is accessed through non-owning pointers when attached to a
scene later.

## Files

- `SceneEnvironment.h`
  - Container for systems. Minimal API:
    - `AddSystem<T>()`, `RemoveSystem<T>()`, `ReplaceSystem<>()`
    - `HasSystem<T>()`, `TryGetSystem<T>() -> observer_ptr<T>`
- `EnvironmentSystem.h`
  - Base class for all environment systems (`enabled` toggle).

## Systems and Rendering Implications

The systems below are designed to map cleanly to common modern real-time
rendering techniques (UE5/Unity/Godot-style), without prescribing a single
renderer implementation.

### SkyAtmosphere

**Component:** `SkyAtmosphere`

**Intent:** Physically-inspired atmosphere scattering for the sky background and
(optional) aerial perspective.

**Typical technique:** LUT-based atmosphere.

- **Precompute (on change)**
  - Transmittance LUT
  - Sky-view / multi-scattering LUTs (implementation choice)
- **Pass implications**
  - **Sky background pass**: sample LUTs to render the sky.
  - **Aerial perspective** (optional): apply transmittance/inscatter to distant
    geometry.
- **Shader implications**
  - Pixel shader for sky background.
  - Optional pixel/compute helper for aerial perspective integrated into forward
    shading or a separate composite.

**Coupling:** The renderer may optionally use a directional “sun” light (chosen
from scene lights) for sky lighting direction. This component does not store a
sun direction; it stores only authored atmospheric parameters.

### VolumetricClouds

**Component:** `VolumetricClouds`

**Intent:** Scene-global volumetric cloud layer.

**Typical technique:** Raymarch a layered noise volume into a low-resolution
buffer with temporal reprojection; composite into sky.

- **Pass implications**
  - **Cloud evaluation pass** (compute or PS): raymarch into a cloud color +
    transmittance buffer (often half/quarter-res).
  - **Cloud composite pass**: composite into sky/background (and/or scene color).
  - Optional **cloud shadow**: produce a projected shadow mask for sun lighting.
- **Shader implications**
  - Compute (preferred) for raymarch + reprojection.
  - Pixel shader for composite.

### Fog

**Component:** `Fog`

**Intent:** Scene-global participating media, either cheap analytic height fog
or full volumetric fog.

**Typical techniques**:

- **Exponential height fog** (analytic): evaluate fog factor per pixel using
  camera depth and world height.
- **Volumetric fog** (froxel/grid): evaluate scattering/extinction in a 3D grid
  and integrate along view.

- **Pass implications**
  - Analytic: integrated directly into forward shading.
  - Volumetric: one or more compute passes to build a froxel volume, followed by
    a composite pass into scene color.
- **Shader implications**
  - Analytic: small per-pixel function.
  - Volumetric: compute froxel evaluation + integration; PS composite.

### SkyLight (IBL)

**Component:** `SkyLight`

**Intent:** Image-based lighting (ambient diffuse irradiance + specular
prefiltered reflections).

**Typical technique:** Convolve a cubemap into diffuse irradiance and specular
prefilter mip chain; use a shared BRDF integration LUT.

- **Pass implications**
  - **IBL build/update** on change (or async):
    - diffuse irradiance convolution
    - specular prefilter chain
    - BRDF LUT (global; not per scene)
- **Shader implications**
  - Forward material shaders sample irradiance and prefiltered env maps.

**Source modes**:

- `kCapturedScene`: capture the active sky/background into a cubemap for IBL.
- `kSpecifiedCubemap`: use the authored cubemap asset.

### SkySphere (Background Fallback)

**Component:** `SkySphere`

**Intent:** Non-procedural sky/background, typically a cubemap HDRI or solid
color. Used when `SkyAtmosphere` is absent or disabled.

**Typical technique:** Full-screen background draw sampling a cubemap.

- **Pass implications**
  - **Sky background pass**: draw a background using cubemap or solid color.
- **Shader implications**
  - Simple pixel shader sampling a cubemap with optional rotation/tint.

**Note:** “SkySphere” is a legacy name, but the concept remains important as a
robust HDRI/skybox fallback and as an artistic workflow.

### PostProcessVolume

**Component:** `PostProcessVolume`

**Intent:** Scene-global post process settings (tone mapping, exposure, bloom,
minimal grading).

**Typical technique:** HDR post stack with exposure control.

- **Pass implications**
  - **Auto exposure** (if enabled): luminance reduction/histogram compute +
    temporal adaptation.
  - **Bloom**: downsample, threshold, blur, and composite.
  - **Tonemap**: final full-screen pass (ACES/Reinhard/etc.).
- **Shader implications**
  - Compute for exposure/bloom chains.
  - Pixel shader for final composite/tonemap.

## Recommended High-Level Pass Order (Forward+)

A typical per-view frame structure (conceptual):

1. Depth pre-pass (or ensure depth exists)
2. Build Forward+ light grid
3. Forward material shading (direct lights + IBL)
4. Optional volumetric fog integration and composite
5. Sky background selection:
   - prefer `SkyAtmosphere` when enabled,
   - else `SkySphere` when enabled,
   - else no sky
6. Optional clouds evaluation + composite
7. Post-processing (exposure, bloom, tonemap)

The exact ordering of sky/clouds relative to fog is renderer-specific; the key
contract is that these systems are evaluated in HDR and are compatible with the
camera exposure/tonemap pipeline.

## API and Ownership Notes

- Systems are composed by adding/removing components on `SceneEnvironment`.
- `TryGetSystem<T>()` returns a non-owning `observer_ptr<T>`.
- No system stores GPU handles or renderer-owned resources.

## Extensibility

The current systems define a minimal subset of parameters for high-quality
rendering. The engine can add more systems or extend parameter sets without
changing the `SceneEnvironment` container API.
