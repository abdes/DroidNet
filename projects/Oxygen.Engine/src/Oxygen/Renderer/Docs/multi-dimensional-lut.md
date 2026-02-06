## Multi-Dimensional Sky-View LUT (Altitude Slices)

### Goal
Replace the single 2D sky-view LUT with a layered LUT (2D array) that includes
altitude slices so all consumers (main view, sky capture, reflections) sample
the correct altitude without per-view LUT regeneration.

### Design Summary
- Use a 2D texture array for the sky-view LUT: dimensions = (U, V, slices).
- Generate all altitude slices in the compute pass each time LUTs are dirty.
- Sample the correct slice at runtime using camera/capture altitude.
- Keep LUT regeneration dependent on atmosphere params and sun elevation, not
	view altitude.

### Impact Assessment (Concrete Files)
**C++ (data + systems)**
- [src/Oxygen/Renderer/Internal/SkyAtmosphereLutManager.h](src/Oxygen/Renderer/Internal/SkyAtmosphereLutManager.h)
	- Add LUT slice config and accessors.
- [src/Oxygen/Renderer/Internal/SkyAtmosphereLutManager.cpp](src/Oxygen/Renderer/Internal/SkyAtmosphereLutManager.cpp)
	- Create 2D array texture for sky-view LUT, SRV/UAV views for arrays.
- [src/Oxygen/Renderer/Types/EnvironmentStaticData.h](src/Oxygen/Renderer/Types/EnvironmentStaticData.h)
	- Add slice count and altitude mapping fields (pending confirmation below).
- [src/Oxygen/Renderer/Internal/EnvironmentStaticDataManager.cpp](src/Oxygen/Renderer/Internal/EnvironmentStaticDataManager.cpp)
	- Populate slice count and mapping values; forward to LUT manager.
- [src/Oxygen/Renderer/Passes/SkyAtmosphereLutComputePass.cpp](src/Oxygen/Renderer/Passes/SkyAtmosphereLutComputePass.cpp)
	- Expand pass constants and dispatch for altitude slices.

**HLSL (shared layout + shaders)**
- [src/Oxygen/Graphics/Direct3D12/Shaders/Renderer/EnvironmentStaticData.hlsli](src/Oxygen/Graphics/Direct3D12/Shaders/Renderer/EnvironmentStaticData.hlsli)
	- Mirror new static fields for slice count/mapping.
- [src/Oxygen/Graphics/Direct3D12/Shaders/Passes/Atmosphere/SkyViewLut_CS.hlsl](src/Oxygen/Graphics/Direct3D12/Shaders/Passes/Atmosphere/SkyViewLut_CS.hlsl)
	- Write to RWTexture2DArray and compute altitude per slice.
- [src/Oxygen/Graphics/Direct3D12/Shaders/Renderer/SkyAtmosphereSampling.hlsli](src/Oxygen/Graphics/Direct3D12/Shaders/Renderer/SkyAtmosphereSampling.hlsli)
	- Sample from Texture2DArray with manual slice blending.
- [src/Oxygen/Graphics/Direct3D12/Shaders/Renderer/AerialPerspective.hlsli](src/Oxygen/Graphics/Direct3D12/Shaders/Renderer/AerialPerspective.hlsli)
	- Use updated LUT sampling utility.

### Data Layout Changes (Needs Your Confirmation)
I plan to add new fields to the shared static environment data that affect
padding/alignment. Please confirm before implementation.

**Proposed additions**
- C++: `GpuSkyAtmosphereParams` in
	[src/Oxygen/Renderer/Types/EnvironmentStaticData.h](src/Oxygen/Renderer/Types/EnvironmentStaticData.h)
	- `uint32_t sky_view_lut_slices`
	- `uint32_t sky_view_alt_mapping_mode` (0 = linear, 1 = log)
- HLSL mirror in
	[src/Oxygen/Graphics/Direct3D12/Shaders/Renderer/EnvironmentStaticData.hlsli](src/Oxygen/Graphics/Direct3D12/Shaders/Renderer/EnvironmentStaticData.hlsli)

**Please confirm these fields and their placement** so I can update struct
layout and static_asserts.

### Assumptions (Precise)
- Slice domain uses altitude above ground in meters: $h \in [0,\;atmosphere\_height\_m]$.
- Slice selection is based on per-view/capture altitude, not on LUT generation.
- `sky_view_alt_mapping_mode` is always set (0 = linear, 1 = log); no implicit default.
- Reserved tail fields in `GpuSkyAtmosphereParams` are reused for new fields.
- `sky_view_lut_slices` is stored as uint32 in both C++ and HLSL (no float cast).

### Decisions, Defaults, and Ranges (Precise)
- `sky_view_lut_slices`
	- Default: 16
	- UI range: 4..32 (step 1)
- `sky_view_alt_mapping_mode`
	- Default: log (1)
	- UI choices: linear (0), log (1)
- Log mapping uses a fixed base derived from atmosphere height (no new params):
  - Define $H = atmosphere\_height\_m$ and $t \in [0,1]$.
  - Slice altitude for generation: $h(t) = H \cdot (2^{t} - 1)$.
  - Slice selection for sampling: $t(h) = \log_2(1 + h/H)$.
- DemoShell panel
	- Expose both slice count and mapping mode controls
	- "Regenerate LUT" button requests LUT dirty on next frame start via service

### Trackable Tasks
**T1: LUT Config + Manager**
- Add `sky_view_slices` to LUT config; expose getters for slice count.
- Create sky-view LUT as 2D array (U, V, slices).
- Create SRV/UAV for array texture; update bindless view type.

**T2: Shared Data Layout (C++/HLSL)**
- Add new fields to `GpuSkyAtmosphereParams` (pending confirmation).
- Mirror in HLSL and update size/alignment static asserts.

**T3: Static Data Population**
- Fill slice count and altitude mapping in EnvironmentStaticDataManager.
- Ensure LUT manager `UpdateParameters` is called with new fields so dirty
	tracking includes slice configuration changes.

**T4: Compute Pass (SkyView LUT)**
- Extend `SkyViewLutPassConstants` to include slice info and altitude mapping.
- Update compute dispatch to generate all slices (z dimension or loop).
- Write to `RWTexture2DArray` at `slice_index`.

**T5: Runtime Sampling**
- Update `SampleSkyViewLut` to use `Texture2DArray`.
- Implement `AltitudeToSlice` + manual slice blending (linear or log).
- Use updated sampling in sky pass and aerial perspective.

**T6: Validation Checklist**
- Verify no LUT regen on camera altitude changes.
- Verify sky capture/reflections no longer show horizon ghost sun.
- Check LUT memory use and GPU time against baseline.

**T7: DemoShell UI/VM/Service Wiring**
- UI panel: add controls for `sky_view_lut_slices`, `sky_view_alt_mapping_mode`,
	and a "Regenerate LUT" button (UI only, no renderer calls).
- VM: add getters/setters for the new settings and a command to request LUT
	regeneration; forward to service.
- Service: persist new settings, mark LUT dirty on changes, and implement
	"regen on next frame start" by triggering LUT dirty in the renderer at frame
	start (service is the only integration point).

### Notes on Slice Mapping
- Default slice count target: 8 or 16 (tunable).
- Mapping choice is required: linear or log.
- Mapping recommendation: log spacing for near-ground precision.
- Manual slice blending avoids cross-slice blur artifacts.

### References (Exact)
- https://ebruneton.github.io/precomputed_atmospheric_scattering/
- https://en.wikipedia.org/wiki/Lookup_table
- https://en.wikipedia.org/wiki/Texture_mapping
