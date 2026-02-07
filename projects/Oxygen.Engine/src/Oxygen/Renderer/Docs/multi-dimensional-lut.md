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
- Use the centered‑bins mapping for LUT generation and runtime sampling.

---

## Current Working State (Baseline)

All files listed below are in a **working, tested state**. The sky atmosphere
renders correctly with a single 2D sky-view LUT. Any implementation must
start from this exact baseline and make only the changes documented here.

### Current Resource Creation (SkyAtmosphereLutManager.cpp)

- `CreateLutTexture()` creates plain `kTexture2D` textures (no array_size set,
 defaults to 1).
- `CreateLutViews()` creates SRV with `dimension = kTexture2D` and UAV with
 `dimension = kTexture2D`.
- All three LUTs (transmittance, sky-view, multi-scat) use the same code path.
- Textures are RGBA16F, initial_state = `kUnorderedAccess`.
- SRV/UAV are both shader-visible.

### Current Compute Dispatch (SkyAtmosphereLutComputePass.cpp)

- `SkyViewLutPassConstants` is 48 bytes (3× float4).
- Sky-view dispatch is 2D: `Dispatch(ceil(W/8), ceil(H/8), 1)`.
- `camera_altitude_m` is computed from the current view's camera position
 and hardcoded to 1.0 m default.
- The shader writes to `RWTexture2D<float4>`.

### Current HLSL sky-view LUT shader (SkyViewLut_CS.hlsl)

- Entry: `[numthreads(8, 8, 1)] void CS(uint3 dispatch_thread_id)`
- Bounds check uses `dispatch_thread_id.x/y` against `output_width/height`.
- Output: `RWTexture2D<float4> output = ResourceDescriptorHeap[...]`
- Writes `output[dispatch_thread_id.xy]`.
- `camera_altitude_m` is a single float from pass constants.

### Current HLSL sampling (SkyAtmosphereSampling.hlsli)

### Current GPU Struct (GpuSkyAtmosphereParams)

float3 rayleigh_scattering_rgb; // +32
float rayleigh_scale_height_m;  // +44

float3 mie_scattering_rgb;      // +48
float mie_scale_height_m;       // +60

float mie_g;                     // +64
float absorption_scale_height_m; // +68
uint sun_disk_enabled;           // +72
uint enabled;                    // +76

float3 absorption_rgb;           // +80
uint transmittance_lut_slot;     // +92

uint sky_view_lut_slot;          // +96
float transmittance_lut_width;   // +100
float transmittance_lut_height;  // +104
float sky_view_lut_width;        // +108

float sky_view_lut_height;       // +112
float_reserved5;                // +116  ← AVAILABLE
float _reserved6;                // +120  ← AVAILABLE
float_reserved7;                // +124  ← AVAILABLE

```
Total: 128 bytes. Three reserved fields available at offsets 116, 120, 124.

---

## Impact Assessment (Concrete Files)

### C++ (data + systems)
- [src/Oxygen/Renderer/Internal/SkyAtmosphereLutManager.h](src/Oxygen/Renderer/Internal/SkyAtmosphereLutManager.h)
 - Add `sky_view_slices` to `SkyAtmosphereLutConfig` (default 16).
 - Add `GetSkyViewLutSlices()` accessor.
 - Update `ISkyAtmosphereLutProvider` interface if slice count is
  needed downstream.
- [src/Oxygen/Renderer/Internal/SkyAtmosphereLutManager.cpp](src/Oxygen/Renderer/Internal/SkyAtmosphereLutManager.cpp)
 - **`CreateLutTexture`**: Pass `array_size` parameter. For sky-view,
  set `desc.array_size = config_.sky_view_slices` and
  `desc.texture_type = TextureType::kTexture2DArray`.
 - **`CreateLutViews`**: Pass `dimension` parameter. For sky-view,
  SRV and UAV must use `TextureType::kTexture2DArray` with
  `sub_resources = {0, slices, 0, 1}`.
 - **Do NOT change transmittance or multi-scat** — they stay as
  plain `kTexture2D`.
- [src/Oxygen/Renderer/Types/EnvironmentStaticData.h](src/Oxygen/Renderer/Types/EnvironmentStaticData.h)
 - Replace `_reserved5` → `sky_view_lut_slices` (uint32_t).
 - Replace `_reserved6` → `sky_view_alt_mapping_mode` (uint32_t).
 - `_reserved7` stays reserved (leave as float 0.0 for alignment).
 - **No size change** — still 128 bytes. Update static_assert comment only.
- [src/Oxygen/Renderer/Internal/EnvironmentStaticDataManager.cpp](src/Oxygen/Renderer/Internal/EnvironmentStaticDataManager.cpp)
 - In `PopulateAtmosphere()`: populate `sky_view_lut_slices` and
  `sky_view_alt_mapping_mode` from the LUT provider.
- [src/Oxygen/Renderer/Passes/SkyAtmosphereLutComputePass.cpp](src/Oxygen/Renderer/Passes/SkyAtmosphereLutComputePass.cpp)
 - Expand `SkyViewLutPassConstants` with slice fields (see below).
 - Change dispatch from 2D to 3D:
  `Dispatch(ceil(W/8), ceil(H/8), slices)`.
 - **Remove** `camera_altitude_m` from pass constants (altitude is
  now derived per slice inside the shader).
- [src/Oxygen/Renderer/Internal/ISkyAtmosphereLutProvider.h](src/Oxygen/Renderer/Internal/ISkyAtmosphereLutProvider.h)
 - Add `GetSkyViewLutSlices()` pure virtual (returns uint32_t).

### HLSL (shared layout + shaders)
- [EnvironmentStaticData.hlsli](src/Oxygen/Graphics/Direct3D12/Shaders/Renderer/EnvironmentStaticData.hlsli)
 - Replace `_reserved5` → `uint sky_view_lut_slices`.
 - Replace `_reserved6` → `uint sky_view_alt_mapping_mode`.
 - `_reserved7` stays.
- [SkyViewLut_CS.hlsl](src/Oxygen/Graphics/Direct3D12/Shaders/Atmosphere/SkyViewLut_CS.hlsl)
 - Output changes from `RWTexture2D<float4>` to
  `RWTexture2DArray<float4>`.
 - Thread group stays `[numthreads(8, 8, 1)]` — the Z dimension
  comes from `Dispatch(..., ..., slices)`.
 - Use `dispatch_thread_id.z` as slice index.
 - Compute `camera_altitude_m` per slice from the mapping function.
 - Write `output[uint3(dispatch_thread_id.xy, slice_index)]`.
- [SkyAtmosphereSampling.hlsli](src/Oxygen/Graphics/Direct3D12/Shaders/Renderer/SkyAtmosphereSampling.hlsli)
 - `SampleSkyViewLut()` signature adds `uint slices`,
  `uint alt_mapping_mode` params.
 - Load `Texture2DArray<float4>` instead of `Texture2D<float4>`.
 - Compute fractional slice from altitude, sample two neighboring
  slices, lerp.
 - Zenith filter must also use array indexing for its 4 samples.
- [AerialPerspective.hlsli](src/Oxygen/Graphics/Direct3D12/Shaders/Renderer/AerialPerspective.hlsli)
 - Update `SampleSkyViewLut()` call to pass new parameters.
- [SkySphereCommon.hlsli](src/Oxygen/Graphics/Direct3D12/Shaders/Atmosphere/SkySphereCommon.hlsli)
 - Update `ComputeAtmosphereSkyColor()` call chain — it calls
  `SampleSkyViewLut()` internally.

---

## Data Layout Changes (Confirmed)

Replace 2 of 3 reserved fields in `GpuSkyAtmosphereParams`:

| Offset | Old | New | Type |
|--------|-----|-----|------|
| 116 | `_reserved5` | `sky_view_lut_slices` | `uint32_t` / `uint` |
| 120 | `_reserved6` | `sky_view_alt_mapping_mode` | `uint32_t` / `uint` |
| 124 | `_reserved7` | `_reserved7` (keep) | `float` / `float` |

**No size change.** Still 128 bytes. No static_assert breakage.

---

## Assumptions (Precise)
- Slice domain uses altitude above ground in meters: $h \in [0,\;atmosphere\_height\_m]$.
- Slice selection is based on per-view/capture altitude, not on LUT generation.
- `sky_view_alt_mapping_mode` is always set (0 = linear, 1 = log); no implicit default.
- Reserved tail fields in `GpuSkyAtmosphereParams` are reused for new fields.
- `sky_view_lut_slices` is stored as uint32 in both C++ and HLSL (no float cast).

## Decisions, Defaults, and Ranges (Precise)
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

---

## Implementation Pitfalls and Mitigations

These are the specific things that went wrong in the failed first attempt, or
that could easily go wrong. Each must be handled explicitly.

### P1: Texture Creation — `array_size` and `texture_type` Must Match
**Problem:** Creating a texture with `array_size > 1` but
`texture_type = kTexture2D` produces a D3D12 resource with
`DepthOrArraySize = N` but SRV/UAV descriptors that expect a 2D (non-array)
view. The GPU silently reads slice 0 only or produces garbage.

**Mitigation:** In `CreateLutTexture()`, when creating the sky-view LUT:
```cpp
desc.array_size = slices;
desc.texture_type = TextureType::kTexture2DArray;
```

The function must accept additional parameters (or become specialized) for the
sky-view case. **Do not** change transmittance or multi-scat textures.

### P2: View Descriptors — SRV/UAV `dimension` Must Be `kTexture2DArray`

**Problem:** Creating SRV/UAV with `dimension = kTexture2D` for a
`kTexture2DArray` resource produces a D3D12 descriptor that only sees
slice 0. The compute shader writes to all slices via
`RWTexture2DArray`, but the SRV from the rendering pass only reads
slice 0 → altitude slices are invisible.

**Mitigation:** In `CreateLutViews()`, when creating views for sky-view LUT:

```cpp
srv_desc.dimension = TextureType::kTexture2DArray;
srv_desc.sub_resources.base_array_slice = 0;
srv_desc.sub_resources.num_array_slices = slices;
uav_desc.dimension = TextureType::kTexture2DArray;
uav_desc.sub_resources.base_array_slice = 0;
uav_desc.sub_resources.num_array_slices = slices;
```

The D3D12 backend already supports this path (`D3D12_SRV_DIMENSION_TEXTURE2DARRAY`
and `D3D12_UAV_DIMENSION_TEXTURE2DARRAY` — confirmed in `Texture.cpp`).

### P3: Compute Dispatch Z Dimension — Not Thread Group Size

**Problem:** The dispatch must use `Dispatch(gx, gy, slices)` where
`slices` is the raw slice count (not `ceil(slices/8)`). Each Z thread group
contains exactly 1 thread (numthreads is `(8,8,1)`), so `dispatch_thread_id.z`
directly equals the slice index.

**Mitigation:** The dispatch call should be:

```cpp
recorder.Dispatch(
    (sky_view_width + kThreadGroupSizeX - 1) / kThreadGroupSizeX,
    (sky_view_height + kThreadGroupSizeY - 1) / kThreadGroupSizeY,
    slices);  // NOT (slices + 7) / 8
```

The shader bounds check stays: `if (dispatch_thread_id.z >= slice_count) return;`

### P4: HLSL Output Type Must Change in Lock-Step

**Problem:** If C++ dispatches 3D but the shader still declares
`RWTexture2D<float4>`, the write `output[dispatch_thread_id.xy]` will
overwrite slice 0 for every Z value. All slices end up identical.

**Mitigation:** The shader output declaration and write must be:

```hlsl
RWTexture2DArray<float4> output = ResourceDescriptorHeap[...];
output[uint3(dispatch_thread_id.xy, dispatch_thread_id.z)] = result;
```

The UAV descriptor must already be `kTexture2DArray` (see P2).

### P5: Shader Recompilation Required

**Problem:** Shaders are pre-compiled to `bin/Oxygen/shaders.bin` by ShaderBake.
Changing `.hlsl` or `.hlsli` files without rebuilding produces a stale binary.
The engine at runtime loads the stale binary, not the source files.

**Mitigation:** After every HLSL change, rebuild shaders:

```
cmake --build --preset windows-msvc --target oxygen-shader-bake
cmake --build --preset windows-msvc --target oxygen-renderer oxygen-examples-texturedcube
```

Or use the all-in-one build that includes the bake step.

### P6: Pass Constants Struct Layout — C++ and HLSL Must Exactly Match

**Problem:** `SkyViewLutPassConstants` is defined both in C++
(`SkyAtmosphereLutComputePass.cpp`) and HLSL (`SkyViewLut_CS.hlsl`).
Adding fields to one without the other (or in a different order) causes
silent data corruption — the shader reads wrong values.

**Mitigation:** The new struct layout must be identical in both:

```
struct SkyViewLutPassConstants {
    uint output_uav_index;            // +0
    uint transmittance_srv_index;     // +4
    uint multi_scat_srv_index;        // +8
    uint output_width;                // +12

    uint output_height;               // +16
    uint transmittance_width;         // +20
    uint transmittance_height;        // +24
    uint slice_count;                 // +28   ← NEW (replaces camera_altitude_m)

    float sun_cos_zenith;             // +32
    uint atmosphere_flags;            // +36
    uint alt_mapping_mode;            // +40   ← NEW (replaces _pad0)
    float atmosphere_height_m;        // +44   ← NEW (replaces _pad1)

    float planet_radius_m;            // +48   ← NEW (4th float4, new row)
    uint _pad0;                       // +52
    uint _pad1;                       // +56
    uint _pad2;                       // +60
};
```

Update `sizeof` static_assert to 64 bytes (was 48). The C++ struct uses
`alignas(16)` so the 4th float4 row is properly aligned.

**Note:** `camera_altitude_m` is **removed** from pass constants. Each slice
computes its own altitude from `slice_index`, `slice_count`,
`atmosphere_height_m`, and `alt_mapping_mode` inside the shader.

### P7: SampleSkyViewLut — Must Load `Texture2DArray`, Not `Texture2D`

**Problem:** The sampling function in `SkyAtmosphereSampling.hlsli` currently
declares `Texture2D<float4> lut = ResourceDescriptorHeap[lut_slot]`. If the
resource is a Texture2DArray but the HLSL type is Texture2D, the GPU reads
only array slice 0 for every sample (no error, just wrong data).

**Mitigation:** Change to `Texture2DArray<float4>` and use
`lut.SampleLevel(sampler, float3(uv, slice_w), 0)` where `slice_w` is the
**integer** array index (D3D12 Texture2DArray indexing uses integer W).

**Alternative (preferred for blending):** Use `lut.Load(int4(x, y, slice, 0))`
for the two neighboring slices, then lerp manually. This gives explicit
control over blending and avoids any HW trilinear ambiguity.

### P8: Zenith Filter Must Also Use Array Indexing

**Problem:** The zenith filter in `SampleSkyViewLut()` does 4 azimuth-offset
samples for zenith smoothing. These must also use the array index.

**Mitigation:** Each of the 4 samples must load from the correct slice(s)
and blend. Apply the same two-slice lerp logic to all 4 azimuth samples.

### P9: `ComputeAtmosphereSkyColor` Calling Convention

**Problem:** `ComputeAtmosphereSkyColor()` is called from `SkySphereCommon.hlsli`
with the `atmo` struct. It internally calls `SampleSkyViewLut()`. The new
parameters (slices, alt_mapping_mode) are now in the `atmo` struct, so the
calling code doesn't need signature changes — it just needs the struct fields
populated.

**Mitigation:** Ensure the struct fields `sky_view_lut_slices` and
`sky_view_alt_mapping_mode` are populated before the shader runs (via
`EnvironmentStaticDataManager`). The `SampleSkyViewLut` function should read
these directly from the `atmo` parameter.

### P10: Resource Barriers — State Tracking for Texture2DArray

**Problem:** The `DoPrepareResources` in `SkyAtmosphereLutComputePass.cpp`
calls `BeginTrackingResourceState()` and `RequireResourceState()` for the
sky-view texture. A Texture2DArray uses the same D3D12 resource state model
as Texture2D (all subresources share state unless per-subresource tracking
is enabled). The existing code works as-is.

**Mitigation:** No change needed for barriers. The existing state transitions
(`kUnorderedAccess` → `kShaderResource`) apply to the entire array.

### P11: `CreateLutTexture` and `CreateLutViews` Must Become Flexible

**Problem:** Currently, `CreateLutTexture()` and `CreateLutViews()` take only
`(width, height, is_rgba, debug_name)`. For the sky-view array, we also need
`array_size` and `texture_type`.

**Mitigation:** Add optional parameters or create a separate overload. Two
approaches:

1. **Add parameters:** `CreateLutTexture(w, h, array_size, tex_type, is_rgba, name)`
2. **Specialize:** Add a `CreateSkyViewLutTexture()` method.

Option 1 is cleaner. Update the `CreateLutViews()` the same way — it needs the
`dimension` to set on the SRV/UAV descriptors.

### P12: `GetSkyViewLutSize()` Width/Height Should Not Change Semantics

**Problem:** The existing accessor `GetSkyViewLutSize()` returns
`Extent<uint32_t>{width, height}`. This is used by
`EnvironmentStaticDataManager` to populate `sky_view_lut_width/height`.
Within the 2D array, each slice is still (width × height). The size accessor
should continue to return per-slice dimensions, not total texels.

**Mitigation:** Keep `GetSkyViewLutSize()` returning `{384, 216}`. Add a new
`GetSkyViewLutSlices()` returning the slice count (default 16).

### P13: Upload System (BrdfLutManager-Style) — Already Supports Arrays

**Problem:** The `UploadKind` enum has no `kTexture2DArray` value.

**Finding:** This is not a problem. The `kTexture2D` upload path already
supports array textures:

- The planner reads `array_size` from the destination texture descriptor.
- Each `UploadSubresource` carries an `array_slice` field.
- The D3D12 `CopyBufferToTexture` computes the correct subresource index:
 `subresource = arraySlice * mipLevels + mipLevel`.

**Mitigation:** No changes to the upload system. If CPU-side uploads are ever
needed for array slices, populate multiple `UploadSubresource` entries with
`array_slice = 0..N-1`. Our sky-view LUT is GPU-generated via compute, so
this path is **not used** for the sky-view LUT.

### P14: SkyCapturePass Samples at Fixed Altitude (~0m)

**Problem:** `SkyCapturePass` captures sky from the origin (altitude ≈ 0m).
With the multi-slice LUT, it must sample the slice corresponding to its
capture altitude.

**Mitigation:** The shader already uses `GetCameraAltitudeM()` which
returns the capture camera altitude. The new `SampleSkyViewLut` will
use this altitude to select the correct slice. No pass-level change is
needed — the fix is entirely in the sampling function.

### P15: Dirty Tracking Must Include Slice Config

**Problem:** If `sky_view_slices` or `alt_mapping_mode` changes but the
atmosphere params don't, the LUT manager won't know to regenerate. This
leaves stale data in the wrong number of slices.

**Mitigation:** `UpdateParameters()` already compares cached params. Add
`sky_view_lut_slices` and `sky_view_alt_mapping_mode` to `CachedParams`
in the LUT manager. Additionally, changing slice count means the
**texture itself must be destroyed and recreated** (different array_size),
not just regenerated.

### P16: Texture Recreation on Slice Count Change

**Problem:** Changing `sky_view_slices` from 16 to 8 requires a new texture
with `array_size = 8`. You cannot reuse a texture with `array_size = 16`.

**Mitigation:** In `UpdateParameters()` or `SetSkyViewLutSlices()`, if the
slice count changes: call `CleanupResources()` → set `resources_created_ = false`
→ mark dirty. The next frame will call `EnsureResourcesCreated()` which
creates fresh textures. This also means the SRV/UAV descriptors must be
reallocated (new `ShaderVisibleIndex` values). The
`EnvironmentStaticDataManager` will pick up the new slot indices on the
next `PopulateAtmosphere()` call.

---

## Trackable Tasks

**T1: LUT Config + Manager (C++)**

- Add `sky_view_slices` to `SkyAtmosphereLutConfig` (default 16).
- Add `GetSkyViewLutSlices()` accessor to manager + interface.
- Modify `CreateLutTexture()` to accept `array_size` and `texture_type`
 parameters [P1, P11].
- Modify `CreateLutViews()` to accept `dimension` and `sub_resources`
 [P2, P11].
- Update `EnsureResourcesCreated()` to pass `(config_.sky_view_slices,
 kTexture2DArray)` for sky-view only.
- Add slice count to `CachedParams` for dirty tracking [P15].
- Handle texture recreation on slice count change [P16].

**T2: Shared Data Layout (C++/HLSL)**

- C++: Replace `_reserved5` → `sky_view_lut_slices`, `_reserved6` →
 `sky_view_alt_mapping_mode` in `GpuSkyAtmosphereParams`.
- HLSL: Mirror the same two field renames in
 `EnvironmentStaticData.hlsli`.
- **Verify** no size change (128 bytes) — both static_asserts pass.

**T3: Static Data Population (C++)**

- `EnvironmentStaticDataManager::PopulateAtmosphere()`: populate
 `sky_view_lut_slices` and `sky_view_alt_mapping_mode` from the
 LUT provider.
- Add `GetSkyViewLutSlices()` + `GetAltMappingMode()` to
 `ISkyAtmosphereLutProvider`.

**T4: Compute Pass — SkyView LUT (C++ + HLSL)**

- C++ `SkyViewLutPassConstants`: Expand to 64 bytes, add `slice_count`,
 `alt_mapping_mode`, `atmosphere_height_m`, `planet_radius_m` [P6].
- C++ Dispatch: Change from `Dispatch(gx, gy, 1)` to
 `Dispatch(gx, gy, slices)` [P3].
- Remove `camera_altitude_m` from pass constants (now computed
 per-slice in shader).
- HLSL: Update `SkyViewLutPassConstants` struct to match [P6].
- HLSL: Change `RWTexture2D<float4>` → `RWTexture2DArray<float4>` [P4].
- HLSL: Add `GetSliceAltitudeM()` helper using `dispatch_thread_id.z`.
- HLSL: Write to `output[uint3(xy, z)]` instead of `output[xy]`.
- **REBUILD SHADERS** [P5].

**T5: Runtime Sampling (HLSL)**

- `SkyAtmosphereSampling.hlsli`:
 	- `SampleSkyViewLut()`: Change `Texture2D` → `Texture2DArray` [P7].
 	- Add `AltitudeToSliceFrac()` helper (linear or log) that returns a
  fractional slice index.
 	- For each sample: Load from `floor(slice)` and `ceil(slice)`, lerp
  by frac.
 	- Update zenith filter to also use array indexing [P8].
 	- Read `atmo.sky_view_lut_slices` and `atmo.sky_view_alt_mapping_mode`
  from the struct [P9].
- `AerialPerspective.hlsli`: Update `SampleSkyViewLut()` call [P9].
- `SkySphereCommon.hlsli`: No direct changes needed — it calls
 `ComputeAtmosphereSkyColor()` which calls `SampleSkyViewLut()`.
 The new params come from the `atmo` struct.
- **REBUILD SHADERS** [P5].

**T6: Validation Checklist**

- Verify no LUT regen on camera altitude changes.
- Verify sky capture at altitude 0 still renders correctly [P14].
- Verify changing slice count triggers texture recreation [P16].
- Check that `EnvironmentStaticData` size is still 416 bytes.
- Check that `GpuSkyAtmosphereParams` size is still 128 bytes.
- Verify no D3D12 validation errors (especially
 `RESOURCE_BARRIER_BEFORE_AFTER_MISMATCH`) [P10].
- Check RenderDoc: sky-view LUT dispatch should show Z groups = slices.
- Check RenderDoc: SRV descriptor should show `Texture2DArray` with
 correct array size.
- Check LUT memory increase: 384 × 216 × 16 slices × 8 bytes =
 ~10 MB (acceptable).

**T7: DemoShell UI/VM/Service Wiring**

- UI panel: add controls for `sky_view_lut_slices`,
 `sky_view_alt_mapping_mode`, and a "Regenerate LUT" button.
- VM: add getters/setters for the new settings and a command to
 request LUT regeneration; forward to service.
- Service: persist new settings, mark LUT dirty on changes, and
 implement "regen on next frame start" by triggering LUT dirty in
 the renderer at frame start.

---

## Notes on Slice Mapping

- Default slice count target: 16 (tunable 4..32).
- Mapping choice is required: linear or log.
- Mapping recommendation: log spacing for near-ground precision.
- Manual slice blending (lerp between integer slices) avoids
 cross-slice blur artifacts.

## References (Exact)

- <https://ebruneton.github.io/precomputed_atmospheric_scattering/>
- <https://en.wikipedia.org/wiki/Lookup_table>
- <https://en.wikipedia.org/wiki/Texture_mapping>
