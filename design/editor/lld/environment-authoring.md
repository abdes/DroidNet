# Environment Authoring LLD

Status: **Final V0.1 contract**. Delivery and rendered evidence are recorded in
[IMPLEMENTATION_STATUS.md](../IMPLEMENTATION_STATUS.md).

## 1. Purpose and ownership

Define scene atmosphere, captured-sky lighting, background and post-process
source values and their live/cooked behavior. Directional-light atmospheric
assignment belongs to each light, not to a scene Sun pointer. Every editable
field below has a required runtime effect; storage or queue acceptance alone
cannot satisfy the contract.

| Owner                                       | Responsibility                                                                                |
| ------------------------------------------- | --------------------------------------------------------------------------------------------- |
| World                                       | Canonical SceneEnvironmentData and per-light assignment source, defaults, DTOs and invariants |
| WorldEditor inspector/commands              | Mode-aware controls, finite/cross-field validation, atomic edits/history/dirty state          |
| ContentPipeline/native cooker               | Complete source-to-native schema/record mapping and diagnostics                               |
| Runtime/Interop/engine                      | Current-state application, role resolution, lighting/capture invalidation and rendering       |
| SceneEnvironment/PostProcessVolume/SkyLight | Native ordinary scene-system semantics and creation defaults                                  |

Environment is scene scope, never editor startup preferences. Editor-only Hide
belongs to workspace/view state and does not alter these values. See
[property inspector](property-inspector.md), [scene model](scene-authoring-model.md),
[live sync](live-engine-sync.md), [content pipeline](content-pipeline.md) and
[standalone qualification](standalone-runtime-validation.md).

## 2. Canonical source and validation

SceneEnvironmentData contains AtmosphereEnabled, SkyAtmosphere, SkySphere,
SkyLight, Background, Fog and PostProcess. PostProcess is the sole exposure/tonemapping
source; duplicate legacy mirrors are removed through migration. Each directional
light owns AtmosphereLightSlot. No SceneEnvironmentData.SunNodeId or independent
IsSunLight/Contributes authoring value remains.

Table paths are relative to SceneEnvironmentData. Numeric fields are float32
unless stated otherwise. Reject every non-finite scalar/vector component before
clamping, unit conversion or mutation; reject converted overflow. Undefined
enums and invalid cross-field combinations produce scoped failures. Optional
inactive values remain valid and persisted. Controls may use soft drag ranges,
but their stored bounds are the table bounds. Invalid saved/cooked data is not
silently repaired during normal runtime loading.

New-scene defaults below come from native scene-system constructors/constants,
not demo presets or current conflicting managed defaults. Defaults are explicitly
emitted so separate processes agree. The qualification fixture deliberately
selects Manual EV 9.7 and is not the new-scene exposure default.

## 3. Sky atmosphere fields

Primary controls expose enable, sun disks and sky luminance. The existing
physical/aerial fields remain supported under Advanced; an unimplemented path
must be fixed rather than removing its field silently.

| Source path                                     | Default / unit              | Bounds after finite validation | Conditional UI and effect                                                             |
| ----------------------------------------------- | --------------------------- | ------------------------------ | ------------------------------------------------------------------------------------- |
| AtmosphereEnabled                               | true; bool                  | Boolean                        | Primary; enables atmospheric sky/scattering, independent of light direct illumination |
| SkyAtmosphere.SunDiskEnabled                    | true; bool                  | Boolean                        | Atmosphere On; visible analytic disks for both assigned sources                       |
| SkyAtmosphere.PlanetRadiusMeters                | 6360000 m                   | Clamp >=1                      | Advanced, Atmosphere On; planetary geometry/scattering; km display divides by1000     |
| SkyAtmosphere.AtmosphereHeightMeters            | 100000 m                    | Clamp >=1                      | Advanced, Atmosphere On; atmosphere extent; km display                                |
| SkyAtmosphere.GroundAlbedoRgb                   | (0.4,0.4,0.4); linear RGB   | Clamp each [0,1]               | Advanced, Atmosphere On; ground scattering response                                   |
| SkyAtmosphere.RayleighScaleHeightMeters         | 8000 m                      | Clamp >=1                      | Advanced, Atmosphere On; Rayleigh vertical density; km display                        |
| SkyAtmosphere.MieScaleHeightMeters              | 1200 m                      | Clamp >=1                      | Advanced, Atmosphere On; Mie vertical density; km display                             |
| SkyAtmosphere.MieAnisotropy                     | 0.8; dimensionless          | Clamp [-0.999,0.999]           | Advanced, Atmosphere On; scattering directionality                                    |
| SkyAtmosphere.SkyLuminanceFactorRgb             | (1,1,1); linear multipliers | Clamp components >=0           | Atmosphere On; sky and aerial luminance scaling through the canonical native mapping  |
| SkyAtmosphere.AerialPerspectiveDistanceScale    | 1; multiplier               | Clamp >=0                      | Advanced, Atmosphere On; aerial optical-distance scaling                              |
| SkyAtmosphere.AerialScatteringStrength          | 1; multiplier               | Clamp >=0                      | Advanced, Atmosphere On; aerial scattering contribution                               |
| SkyAtmosphere.AerialPerspectiveStartDepthMeters | 100 m                       | Reject <0                      | Advanced, Atmosphere On; aerial start distance; exact native minimum is0m             |
| SkyAtmosphere.HeightFogContribution             | 1; multiplier               | Clamp >=0                      | Advanced, atmosphere and height-fog contribution applicable; controls their coupling  |

All metre values remain metres in source/native scene state; presentation in km
is not a second storage unit. Preserve existing native enum/API names. Fixed
engine coefficients and other fields outside this table do not automatically
become editor sliders. Height Fog Contribution couples the atmosphere to the
authored fog below.

Canonical native defaults are grounded in Core/Types/Atmosphere.h,
Scene/Environment/SkyAtmosphere.h and EnvironmentSystem.h. Height is 100 km and
Aerial Start is 100 m; 80 km and 0 m are not the native creation defaults.

### Fog

`SceneEnvironmentData.Fog` mirrors the native `Fog` system: every field the
renderer consumes, in native units, with native defaults except Enabled, which
is off so scenes without authored fog render none. The Fog section shows
Enabled, Height Fog, Density (σt, 1/m), Height Falloff (1/m), Height Offset
(m), Max Opacity, Inscattering luminance and Sky Ambient scale, with
disclosures for the second layer (density, falloff, offset), distances (start,
end and cutoff; 0 is unlimited), the directional inscattering lobe
(luminance, exponent, start), volumetric fog (enabled, scattering
distribution, albedo, emissive, extinction scale, distance where 0 uses the
camera far plane, start, near fade-in, sky light scattering and fog-colored
light scattering) and rendering (main pass, holdout, reflection and sky
capture visibility).

Validation rejects non-finite values, negative densities, falloffs,
distances, luminances and scales, Max Opacity and albedo outside 0–1,
scattering distribution outside ±0.99 and a directional exponent outside
(0, 1000]. The environment command applies fog with the atmosphere and post
process in one scene mutation; editor panes request height and local fog from
the scene as the runtime does. The cooked descriptor carries the full native
`fog` record; the inscattering cubemap fields stay at native defaults because
the renderer does not sample them.

### Local fog volumes

`LocalFogVolumeComponent` places a sphere of fog on a node: 5 m in radius at
unit scale, sized and positioned by the node's transform. One per node; it is
added from the node inspector's Add Component menu or created on a new root
node from the viewport's Quick Add menu. Its fields mirror the native
`LocalFogVolume` with native defaults: Enabled, Density (the radial
extinction, 1/m at the center, fading to zero at the edge) and Height Density
(1/m, both ≥0), Height Falloff (≥0), Height Offset (m, relative to the center),
Phase (0 to 0.999), linear Albedo (0–1), Emissive luminance (≥0) and Sort
Priority (integer, −127 to 127). Edits travel as descriptor-only property
entries to a native applier that rejects an invalid candidate whole. The
cooked descriptor lists the volumes in `local_fog_volumes` against their node
indices, and editor panes request the local fog pass while any enabled volume
exists, as the runtime does. The viewport draws a fog icon at each volume and,
while it is selected, its sphere: a silhouette circle and three faint great
circles at the radius the renderer uses (5 m times the largest world-scale
axis). The sphere has no handle; the node's scale sizes it.

## 4. Backdrop, Sky Sphere, Sky Light and background

### Engine-shaped storage

Scenes store the engine's own records: AtmosphereEnabled, SkySphere, SkyLight
and Background, each with its enabled flag, exactly as the cooked scene records
them. Cooking and live sync copy them through. Scenes saved with the former
BackgroundColor load with an enabled Background of that color.

| Source path                            | Default / unit           | Bounds                   | Effect                                            |
| -------------------------------------- | ------------------------ | ------------------------ | ------------------------------------------------- |
| SkySphere.Enabled                      | false; bool              | Boolean                  | May show behind the scene                         |
| SkySphere.Source                       | Cubemap; enum            | Cubemap, SolidColor      | What the sphere shows                             |
| SkySphere.Cubemap                      | none; cube texture asset | Absolute asset URI       | Backdrop radiance                                 |
| SkySphere.SolidColorRgb                | (0,0,0); linear RGB      | Finite, >= 0             | Solid backdrop radiance                           |
| SkySphere.IlluminanceLux               | 0; lux                   | Finite, >= 0             | Calibrates the sky; 0 keeps it as imported        |
| SkySphere.Intensity                    | 1; multiplier            | Finite, >= 0             | Scales the shown radiance                         |
| SkySphere.RotationRadians              | 0; radians               | Finite                   | Turns the cubemap around up                       |
| SkySphere.TintRgb                      | (1,1,1); linear RGB      | Finite, >= 0             | Multiplies the shown radiance                     |
| SkyLight.Enabled                       | true; bool               | Boolean                  | Image-based diffuse and specular lighting         |
| SkyLight.Source                        | CapturedScene; enum      | CapturedScene, Cubemap   | Radiance captured from the sky, or a cube texture |
| SkyLight.Cubemap                       | none; cube texture asset | Absolute asset URI       | Lighting radiance for the cubemap source          |
| SkyLight.CubemapIlluminanceLux         | 0; lux                   | Finite, >= 0             | Calibrates the cubemap; 0 keeps it as imported    |
| SkyLight.Intensity                     | 1; multiplier            | Finite, >= 0             | Scales diffuse and specular                       |
| SkyLight.TintRgb                       | (1,1,1); linear RGB      | Finite, >= 0             | Multiplies the sky radiance                       |
| SkyLight.DiffuseIntensity              | 1; multiplier            | Finite, >= 0             | Diffuse only                                      |
| SkyLight.SpecularIntensity             | 1; multiplier            | Finite, >= 0             | Specular only                                     |
| SkyLight.CubemapAngleRadians           | 0; radians               | Finite                   | Turns the cubemap source around up                |
| SkyLight.LowerHemisphereColor          | (0,0,0); linear RGB      | Finite, >= 0             | Ground color below the horizon                    |
| SkyLight.LowerHemisphereIsSolidColor   | true; bool               | Boolean                  | Replaces the lower hemisphere with that color     |
| SkyLight.LowerHemisphereBlendAlpha     | 1                        | [0,1]                    | How much of the lower hemisphere it replaces      |
| SkyLight.VolumetricScatteringIntensity | 1; multiplier            | Finite, >= 0             | Sky light scattered by volumetric fog             |
| SkyLight.AffectReflections             | true; bool               | Boolean                  | Contributes to specular reflections               |
| Background.Enabled                     | false; bool              | Boolean                  | May show behind the scene                         |
| Background.ColorRgb                    | (0,0,0); linear SDR RGB  | Finite, Clamp each [0,1] | Display-only color                                |

Defaults are the native ones, except that the Sky Sphere and the Background
start disabled so that scenes without them show neither.

### One backdrop choice

The renderer shows one backdrop: an enabled atmosphere, else an enabled
background, else an enabled sky sphere. The inspector therefore presents one
choice, Show: Atmosphere, Cubemap or Solid color, as Godot's Background Mode
and Unity HDRP's sky type do. Each choice sets the enable flags so that exactly
it shows:

- Atmosphere enables the atmosphere and disables the sky sphere and background.
- Cubemap disables the atmosphere and background and enables the sky sphere
  with its cubemap source.
- Solid color disables the atmosphere. Its "Light the scene with this color"
  toggle selects the record: on, the sky sphere's solid color, which sky light
  captures see; off, the background, which only paints the view. Toggling
  carries the color across, clamped to [0,1] for the background.

Choosing Cubemap or Solid color turns the atmosphere off, including aerial
perspective and the atmospheric tint of the sun; the inspector says so beside
the choice. The atmosphere's own section shows only while it is the backdrop,
and its settings are kept while it is off. A hand-edited scene enabling several
systems shows the backdrop the renderer would show.

Fields that apply only to some choices follow the inspector's applicability
rule, as the exposure modes do: browsing shows the atmosphere fields for the
Atmosphere backdrop, the cubemap and its rotation for Cubemap, the color and
its lighting toggle for Solid color, the intensity and tint for Cubemap or a
color that lights the scene, and the Sky Light cubemap and its rotation for the
cubemap source. A search also finds the stored values of fields that do not
apply, each with a note saying when it applies.

### Sky Light

The Sky Light section mirrors the engine's fields: Enabled; Source, "Capture
sky" or "Cubemap"; the cubemap and its rotation, shown for the cubemap source;
intensity, tint, diffuse and specular multipliers; a Lower hemisphere
disclosure; and an Advanced disclosure with volumetric scattering and affect
reflections. Capture sky already follows the backdrop, rotation included, so
the cubemap rotation stays separate from the backdrop rotation, as in Unreal.

Capture sky lights the scene with the backdrop's calibrated radiance. Display
and lighting are decoupled, as in three.js and Filament: the Sky Sphere's
intensity and tint scale only what the view shows, and only the Sky Light's own
multipliers scale lighting. Editing them never rebuilds the capture. Without an
atmosphere the capture reads the Sky Sphere, its cubemap or its solid color, so
"Light the scene with this color" lights it; the display-only background never
does. Lighting accepts only float radiance, so a cubemap that lights the scene,
directly or through a captured backdrop, must be cooked as rgba16f or rgba32f.
An LDR cube can still be shown.

### Calibration

Imported HDR images rarely carry physical units: a typical one delivers a few
lux on an upward-facing surface, while a daylight sun delivers tens of
thousands. Left raw, auto exposure meters the dim sky and burns sunlit surfaces
to white, and the sky lights shadows with almost nothing. Illuminance, in lux,
calibrates a source once, as Unity HDRP's lux intensity mode does. The
renderer measures each float cube when it loads, by integrating its luminance
against the cosine to world up over the upper hemisphere, and scales its
radiance to deliver the authored illuminance. A solid color of luminance L
delivers pi times L. The backdrop's Illuminance applies to the view and to
Capture sky, so the two always agree; the Sky Light's cubemap Illuminance
applies to the specified cubemap. Zero keeps the radiance as imported, which is
what older scenes contain. Only float cubes can be measured: an LDR backdrop
cube with an Illuminance stays raw, and the renderer reports it as
`sky.uncalibrated` in the diagnostics ledger and the log. Intensity in EV then adjusts relative to the calibrated
radiance: the backdrop's for the view only, the Sky Light's for lighting only.
Tooltips give typical values: a clear-day sky delivers 10,000 to 25,000 lux, an
overcast one 1,000 to 10,000.

### Units and pickers

Oxygen renders with physical exposure, so both radiance multipliers,
SkySphere.Intensity and SkyLight.Intensity, are presented as Intensity in EV:
the stored multiplier is 2^EV, so EV 0 keeps the authored radiance and each
stop doubles it. The lowest presented stop, EV -16, stands for a zero
multiplier. Rotations are presented in degrees. Diffuse, specular and
volumetric multipliers stay linear because they balance shares of one light.

Cubemap fields list the project's cube textures: texture descriptors whose cube
settings produce a cube texture. Texture import offers a Cube shape. Its layout
is detected from the image (2:1 panorama, 6:1 or 1:6 strip, 4:3 or 3:4 cross)
or chosen, and a panorama takes a face size that is a multiple of 256. Cube
textures default to the hdr_env intent, linear color space and rgba16f output.
Choosing an LDR format for a cube warns that it can only be displayed, and the
cubemap pickers mark such cubes "display only". Half floats clamp radiance
above 65504; the cook reports how much was clamped, and rgba32f keeps it.

### Runtime, cooking and the display-only background

One environment command applies the atmosphere, sky sphere, sky light,
background, fog and post process in one scene mutation. It loads the exposure
mask and both cubemaps together and applies them only when all have loaded; a
failed texture rejects the edit and keeps the previous revision. A texture
whose source is unchanged keeps its accepted load, so edits to other settings
apply at once; a refresh after cooking reloads every texture. A cubemap is
loaded, and cooked as a scene reference, only while an enabled system's source
uses it, so a stale reference behind another source or a disabled system
blocks nothing. Scenes saved with the former BackgroundColor clamp it to the
background's [0,1] range on load. Cooked cubemaps are
texture bindings in the scene's resource table, as the metering mask is, and
must be cooked into the scene's own content mount.

The background paints only the presented view: sky light captures and
reflections skip it, it does not become emissive or an ambient light, and an
enabled atmosphere takes precedence. Convert picker sRGB to stored linear SDR
once and keep the picked display result independent of scene exposure and tone
mapping. Translucent foreground uses correct coverage composition; changing the
backdrop must not replace or flatten that material's lighting.

Capture produces diffuse irradiance and roughness-dependent specular radiance
with the required BRDF integration. With no active lighting sky, sky
contribution is zero; do not retain a stale capture or invent ambient energy
from the background. The engine
[captured-sky IBL contract](../../../projects/Oxygen.Engine/design/vortex/lld/captured-sky-ibl.md)
defines the scene-global anchor, processing products, filtering, publication
and Stage 13 ownership. Changes to either contributing light, effective
visibility or participation, assignment, atmosphere parameters or the
applicable sky source invalidate affected products. Explicit disks are excluded
from reflection captures to avoid double-counting direct source energy, while
both sources' atmospheric scattering remains.

## 5. Exposure fields

ExposureEnabled is independent of tone-curve selection. Manual, ManualCamera and Auto
retain native enum values 0, 1 and 2; ManualCamera uses the camera EV from the
authored aperture, shutter rate and ISO. Preserve existing API
names; no enum renumbering or cosmetic aliases are introduced.

| PostProcess path              | Default / unit                      | Bounds                                                | Conditional UI and active effect                                                              |
| ----------------------------- | ----------------------------------- | ----------------------------------------------------- | --------------------------------------------------------------------------------------------- |
| ExposureEnabled               | true; bool                          | Boolean                                               | Primary; Off uses unit exposure without erasing settings                                      |
| ExposureMode                  | Auto                                | Manual / ManualCamera / Auto                          | Primary; selects fixed EV, camera EV or metered exposure                                      |
| ManualExposureEv              | 9.7 EV100                           | Finite; validate resulting gain with key/compensation | Enabled + Manual; fixed exposure; [-24,24] is a typical control range, not a conversion clamp |
| ExposureCompensationEv        | 0 EV                                | Finite and representable conversion                   | Enabled; +1 doubles exposure, -1 halves it; no light-only [-10,10] restriction                |
| ExposureKey                   | 10; dimensionless calibration scale | Finite >0; validate coupled gain                      | Advanced, Enabled; same bias scale for Manual and Auto                                        |
| AutoExposureMinEv             | -6 EV100                            | Finite, Min <= Max                                    | Auto; minimum metered EV                                                                      |
| AutoExposureMaxEv             | 16 EV100                            | Finite, Max >= Min                                    | Auto; maximum metered EV                                                                      |
| AutoExposureSpeedUp           | 3 EV/s                              | Finite >=0                                            | Auto; adaptation toward brighter luminance                                                    |
| AutoExposureSpeedDown         | 1 EV/s                              | Finite >=0                                            | Auto; adaptation toward darker luminance                                                      |
| AutoExposureMeteringMode      | Average                             | Average / CenterWeighted / Spot                       | Auto; image weighting; native ordinals 0/1/2                                                  |
| AutoExposureLowPercentile     | 0.1                                 | [0,1], Low < High                                     | Advanced Auto; lower histogram percentile                                                     |
| AutoExposureHighPercentile    | 0.9                                 | [0,1], High > Low                                     | Advanced Auto; upper histogram percentile                                                     |
| AutoExposureMinLogLuminance   | -12; log2 luminance                 | Finite, window within [-24,32]                        | Advanced Auto; histogram lower range                                                          |
| AutoExposureLogLuminanceRange | 25; log2 span                       | Finite >0, upper window endpoint <=32                 | Advanced Auto; histogram span                                                                 |
| AutoExposureTargetLuminance   | 0.18; linear target                 | Finite >=0; validate coupled gain                     | Advanced Auto; exposure target                                                                |
| AutoExposureSpotMeterRadius   | 0.2; normalized image radius        | Finite >=0                                            | Advanced Auto + Spot; active metering radius                                                  |

Validate coupled ranges atomically. Exposure Min/Max can be ordered together on
commit; invalid histogram intervals reject without partial mutation. Numeric
source loaded for rendering must already satisfy the canonical constraints.

The native calibration constant 12.5 is not the creation ExposureKey default 10.
Using Core/Types/PostProcess.h, bias = 2^Compensation * (ExposureKey/12.5);
Manual exposure = bias / 2^EV100. Auto applies the same bias to its target.
Do not duplicate this authority with a second editor exposure implementation.
Expected conversion examples are independently authored for qualification.

### Exposure extension and persistence layout

The [exposure implementation package](../../../projects/Oxygen.Engine/design/vortex/milestones/exposure/README.md)
extends the existing adapters; it does not introduce another editor exposure
model. Native ManualCamera remains available to games; the existing editor
Manual/Auto presentation remains unchanged. Use one canonical native validator
for coupled settings and propagate structured failures without partially
publishing converted fields. Previous valid settings remain active on error.

| Field                          | Default   | Validation / behavior                                                                                |
| ------------------------------ | --------- | ---------------------------------------------------------------------------------------------------- |
| AutoExposureBlackInfluence     | 0         | Finite [0,1]; dark-bin weight                                                                        |
| AutoExposureTransitionDistance | 1.5 stops | Finite positive; advanced Auto control                                                               |
| AutoExposureMeteringMask       | Absent    | Existing resource descriptor reference; linear R, bilinear clamp; multiply selected analytic profile |
| AutoExposureCompensationCurve  | Empty     | At most 64 finite EV/compensation pairs with strictly increasing EV                                  |

All active existing fields and these additions round-trip source, schema,
cooker, native record, loader hydration, scripting, Interop and editor save.
Keep enum ordinals: Manual=0, ManualCamera=1, Auto=2 and Average=0,
CenterWeighted=1, Spot=2. New-scene defaults remain the table above: absent mask, empty curve, black
influence 0 and D=1.5. The 2026-09-21 user direction requires migration to v6
throughout the repository, with no backward-compatibility or legacy read paths.
Pending mask residency retains the complete old settings/resource revision;
failed authored resource loading reports failure, never substitutes no mask.
Transient transitions, GPU state and adaptation history are never serialized.

Live editor mask requests retain the authored asset URI in the document. The
editor maps that URI through its existing cooked-mount layout to a source root
and a relative texture descriptor path. Content resolves that explicit locator
only within the matching mounted loose source, validates the current sidecar and
its texture-table entry, and returns a source-qualified ResourceKey. The editor
does not parse sidecars, invent texture asset UUIDs, or persist ResourceKeys.
The scene-session async request owner supersedes stale completions, retries after
cooked-root refresh, and applies a complete exposure revision only when the
requested mask is available. Failures preserve the previous accepted revision.

The current `PostProcessVolumeEnvironmentRecord` has a 144-byte fixed prefix,
including its eight-byte type/record-size header. Its first 104 bytes retain
the established scalar field positions. The scene asset version is 6; readers
accept only v6 with the complete record below. Reject v5 and 104-byte records;
do not retain compatibility decoders or synthesize missing fields. Update `SceneAsset` exact-size validation,
`PakFormatSerioLoaders`, cooker and serializer together.

| Byte offset | Type                   | Extended record field                                                  |
| ----------- | ---------------------- | ---------------------------------------------------------------------- |
| 0..103      | Existing packed fields | Established field offsets; header record_size reflects full new length |
| 104         | uint32                 | exposure_extension_version=1                                           |
| 108         | float32                | black_influence                                                        |
| 112         | float32                | transition_distance_ev                                                 |
| 116         | uint32 ResourceIndexT  | source-local mask texture index; zero means absent                     |
| 120..131    | 3 uint32               | reserved=0                                                             |
| 132         | uint32                 | curve_key_count, 0..64                                                 |
| 136         | uint32                 | reserved=0                                                             |
| 140         | uint32                 | reserved=0                                                             |
| 144         | count pairs of float32 | EV100, compensation EV, eight bytes per key                            |

Total record_size is exactly `144+8*curve_key_count`, at most 656 bytes.
Check count/size/available bytes before reading; reject unsupported extension
version, malformed keys and nonzero reserved fields. Author the mask using a
texture descriptor path; the cooker resolves its source-local texture index,
and PAK packing remaps that dependency into the output texture table. Content
hydrates the scene-relative index into a runtime ResourceKey, following the
existing texture-resource model. The 2026-09-16 user-approved correction
replaces the initially proposed descriptor UUID: no runtime UUID lookup exists
for those cooker-only names. This preserves the 144-byte prefix and curve
offsets without introducing a new asset type. There are no native pointers or GPU
descriptor slots in saved records. The single current reader requires the
complete explicitly versioned record; obsolete representations are rejected.

Native physical camera persistence is included by the 2026-09-16 scope decision.
Scene-v6 perspective camera records append aperture_f/shutter_rate/iso at
20/24/28 (32 bytes total); orthographic records append them at 28/32/36
(40 bytes total). Reject version-5 20/28-byte records. All scene producers and
consumers migrate together; 11/125/100 are new-authoring defaults, not fallback
values for old assets. Source schemas, cooker, loader, scripting and existing
editor adapters preserve these fields. The camera inspector authors these
fields (ED-M08.10); the engine renders no depth of field or motion blur.

The engine mathematical authority is now the
[PBR specification](../../../projects/Oxygen.Engine/design/renderer-core/physically-based-rendering.md);
Core provides its runtime implementation. The 12.5 normalization and creation
key 10 remain distinct. Target zero is legal and displays black while retaining
positive latent adaptation. Speeds retain their saved numeric values and now
implement maximum EV/s, not exponential rate constants.

## 6. Tone mapping, bloom and grading

| PostProcess path  | Default / unit      | Bounds                                                | Conditional UI and active effect                                            |
| ----------------- | ------------------- | ----------------------------------------------------- | --------------------------------------------------------------------------- |
| ToneMapper        | AcesFitted          | None / AcesFitted / Filmic / Reinhard, native 0/1/2/3 | Selects tone curve; None is SDR clipping without a filmic curve             |
| BloomIntensity    | 0; multiplier       | Clamp >=0                                             | Bloom contribution; 0 disables bloom                                        |
| BloomThreshold    | 1; linear HDR units | Clamp >=0                                             | BloomIntensity >0; extraction threshold                                     |
| Saturation        | 1; multiplier       | Clamp >=0                                             | Foreground colour grading; 0 removes chroma                                 |
| Contrast          | 1; multiplier       | Clamp >=0                                             | Foreground contrast around the defined middle-grey reference                |
| VignetteIntensity | 0                   | Clamp [0,1]                                           | Foreground edge attenuation within the camera content rectangle             |
| DisplayGamma      | 2.2                 | Clamp >=0.001                                         | Foreground output power 1/gamma after tone mapping; remains active for None |

Every numeric input is finite. None bypasses the tone curve only; it does not
turn off exposure, bloom, grading, vignette or output mapping. Do not hide these
active controls when None is selected. Debug render modes that force unrelated
settings are outside authored ToneMapper.None behavior.

The native foreground pipeline has this fixed order:

1. Combine scene-linear foreground and bloom, then apply the existing effective
   exposure exactly once.
2. Compute Rec.709 luminance `Y = dot(rgb, (0.2126, 0.7152, 0.0722))` and
   saturation `rgb = Y + Saturation * (rgb - Y)`.
3. Apply contrast in linear light: `rgb = max(0, 0.18 + Contrast * (rgb - 0.18))`.
4. Apply the selected tone curve; None clips to SDR `[0,1]`.
5. Apply vignette to this foreground display-linear signal using content-rectangle
   coordinates: `p = 2 * contentUV - 1`, `r2 = dot(p,p)`,
   `factor = 1 - VignetteIntensity * smoothstep(0.25, 1, r2)`. This centred ellipse
   has no hidden artist-facing radius parameter.
6. Apply the existing DisplayGamma power conversion, then the
   display-background/foreground-coverage composition.

Saturation=1, Contrast=1 and VignetteIntensity=0 are identity settings. Background
and Fixed-camera bars remain outside foreground grading and metering. Preserve
correct transparent coverage; no additional exposure or output conversion is
introduced. Grading needs real Vortex shader/configuration consumers, not only
stored DTO/buffer fields. This defines Oxygen's bounded implementation; it does
not claim an identical look to another engine.

Independent golden cases include linear `(0.2,0.4,0.6)` becoming
`(0.37192,0.37192,0.37192)` at Saturation=0; the same input becoming
`(0.19,0.29,0.39)` at Contrast=0.5; and vignette factor 1 at the centre versus 0.5
at `contentUV=(1,0.5)` for intensity 0.5. Use unit exposure, None and gamma 1 for
these isolated checks. Separately, exposure 2 maps `(0.2,0.4,0.6)` to
`(0.4,0.8,1.2)`, which None clips to `(0.4,0.8,1)`. Expected values must not be generated by the adapter
or shader helper under test. Retain full rendered background/transparent cases.

## 7. Per-light atmosphere assignment

Each directional source has one AtmosphereLightSlot: None, Primary or Secondary.
Use existing enum/GetAtmosphereLightSlot/SetAtmosphereLightSlot names. Primary and
Secondary map to integration slots 0 and 1; None maps to neither. Neither slot
means priority, brightness or an intrinsic celestial body type.

<a id="84-per-light-atmosphere-assignment"></a>

### Assignment rules

- At most one stored occupant per non-None slot, even while hidden/off. Conflicting
  direct Light assignment/import/cook reports the occupant and changes neither light.
  The Scene source picker explicitly replaces a role's source: clearing the old
  occupant and assigning the selected light are one atomic command change set and
  one undo entry. This changes only role ownership, not either light's illumination
  settings or existence. Undo/redo and saved reopen preserve both role assignments.
- Assignment/clear/copy uses the ordinary light command/history path. Hiding or
  disabling retains assignment; Secondary-only never promotes it.
- None retains ordinary directional illumination/shadow controls. Each light's
  Affects Scene and effective node visibility gate all contribution independently
  of assignment. Clearing a role is not turning the light off.
- Both sources provide independent direction, colour, lux, full source angle,
  requested forward/deferred surface/applicable-fog shadows, atmospheric scattering
  and captured-sky diffuse/specular contribution.
- A Moon use case is directional moonlight and an analytic disk. Lunar phases,
  textures/orbits, more than two atmospheric sources and sky-only authoring are
  outside V0.1. ResolveMoon's existing alias is not a lunar-body implementation.

The source angle is presented as Atmosphere Disk Diameter for assigned sources.
It controls the analytic disk only; V0.1 retains conventional 3x3 PCF and does
not promise finite-source GGX highlights or angle-driven penumbra widening.
The control is absent for role None; that light's direct illumination and
independent contact/conventional shadow controls remain functional.

A scene may show a read-only assignment summary. No competing Sun pointer or
independent IsSun/Contributes source remains. Ordinary light and caster/receiver
controls are defined in [property-inspector.md](property-inspector.md).

## 8. Commands, Save, cooking and results

Scene settings use typed Scene.Environment.Edit operations; assignment uses light
edits. Commands validate complete values, mutate atomically, advance revision,
record one history step and request current-lifetime sync. Field hiding changes
presentation only. Preserve inactive values and unrelated errors. Reuse shared
property controls, sessions and diagnostics; no direct UI-to-Interop calls.

Canonical source, descriptor schemas, packed native records, loader and Interop
carry every field at its stated units/precision. Finite validation exists before
native use as well as at the editor boundary. Required mappings missing from a
schema/API are production implementation work, not an Unsupported success mode.
Transient runtime unavailability preserves valid source and Save with truthful
preview status; queue acceptance alone never proves rendered application.

Save captures a coherent revision and atomically replaces source; later edits
remain dirty. Cooking follows its owning workflow. Scene templates emit explicit
per-light roles/defaults; native startup never selects the first/brightest source
or restores unrelated demo lighting preferences.

## 9. Migration and implementation boundaries

Migrate useful source once and regenerate cooked output. Preserve unambiguous
explicit slots and sun intent; report conflicts instead of guessing by name,
brightness/order or collapsing two-source content. Remove SunNodeId, independently
authored IsSun/Contributes state and implicit fallback readers. Consolidate
exposure mirrors into PostProcess and remove editor ManualCamera authoring while
preserving useful resolved composition/exposure through an explicit migration.
Native physical exposure remains available to its engine consumers.

Keep valid existing values when creation defaults change. Native creation defaults
are Auto/EV 9.7/Key 10 and atmosphere 100 km/Aerial Start 100 m; current conflicting
managed defaults must converge. This does not reset useful existing authored
Manual EV 13 scenes or silently recook them as Auto.

Required engine work includes complete two-directional surface/shadow processing,
captured-sky diffuse/specular processing, receiver and grading effects, and cache
invalidation. They are not deferred because source storage already exists.
Deferred boundaries are recorded in the owning
[engine capability record](../../../projects/Oxygen.Engine/design/vortex/milestones/ED-M08/deferred-capabilities.md).
When relevant engine code is modified, add its linked TODO(post-v0.1, ID) beside
the deferred boundary; do not annotate unrelated untouched files wholesale.

## 10. Qualification and rejected alternatives

For every table field, verify a meaningful non-default value through edit,
Undo/Redo, Save/reopen, cook/load, native observation and rendered effect. Exercise
finite/enum/cross-field failures without partial writes and verify metre/radian/
colour conversions independently. Native rendered tests precede editor tests.

Include Primary-only, Secondary-only, both sources with distinct colours/directions
and requested shadows, role-None fill, either source hidden/off/re-enabled,
assignment conflicts, atmosphere-off, captured-product invalidation, diffuse and
roughness-dependent specular response, all tone mappers, Manual and Auto, and
non-default grading/bloom/background with transparent foreground. Auto tests
observe actual GPU exposure at controlled checkpoints, not just the saved enum.

A single scene Sun pointer was rejected because it loses explicit two-source
ownership; automatic promotion hides source mistakes; A/B renaming offers no
capability benefit. Removing ineffective fields was rejected as a substitute
for implementing promised behavior. Raw DTO exposure, duplicate exposure mirrors
and using display background as ambient lighting are not alternate contracts.
