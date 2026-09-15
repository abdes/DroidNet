# Environment Authoring LLD

Status: **Final V0.1 contract**. Delivery and rendered evidence are recorded in
[IMPLEMENTATION_STATUS.md](../IMPLEMENTATION_STATUS.md).

## 1. Purpose and ownership

Define scene atmosphere, captured-sky lighting, background and post-process
source values and their live/cooked behavior. Directional-light atmospheric
assignment belongs to each light, not to a scene Sun pointer. Every editable
field below has a required runtime effect; storage or queue acceptance alone
cannot satisfy the contract.

| Owner | Responsibility |
| --- | --- |
| World | Canonical SceneEnvironmentData and per-light assignment source, defaults, DTOs and invariants |
| WorldEditor inspector/commands | Mode-aware controls, finite/cross-field validation, atomic edits/history/dirty state |
| ContentPipeline/native cooker | Complete source-to-native schema/record mapping and diagnostics |
| Runtime/Interop/engine | Current-state application, role resolution, lighting/capture invalidation and rendering |
| SceneEnvironment/PostProcessVolume/SkyLight | Native ordinary scene-system semantics and creation defaults |

Environment is scene scope, never editor startup preferences. Editor-only Hide
belongs to workspace/view state and does not alter these values. See
[property inspector](property-inspector.md), [scene model](scene-authoring-model.md),
[live sync](live-engine-sync.md), [content pipeline](content-pipeline.md) and
[standalone qualification](standalone-runtime-validation.md).

## 2. Canonical source and validation

SceneEnvironmentData contains AtmosphereEnabled, SkyAtmosphere, SkyLight,
PostProcess and BackgroundColor. PostProcess is the sole exposure/tonemapping
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

| Source path | Default / unit | Bounds after finite validation | Conditional UI and effect |
| --- | --- | --- | --- |
| AtmosphereEnabled | true; bool | Boolean | Primary; enables atmospheric sky/scattering, independent of light direct illumination |
| SkyAtmosphere.SunDiskEnabled | true; bool | Boolean | Atmosphere On; visible analytic disks for both assigned sources |
| SkyAtmosphere.PlanetRadiusMeters | 6360000 m | Clamp >=1 | Advanced, Atmosphere On; planetary geometry/scattering; km display divides by1000 |
| SkyAtmosphere.AtmosphereHeightMeters | 100000 m | Clamp >=1 | Advanced, Atmosphere On; atmosphere extent; km display |
| SkyAtmosphere.GroundAlbedoRgb | (0.4,0.4,0.4); linear RGB | Clamp each [0,1] | Advanced, Atmosphere On; ground scattering response |
| SkyAtmosphere.RayleighScaleHeightMeters | 8000 m | Clamp >=1 | Advanced, Atmosphere On; Rayleigh vertical density; km display |
| SkyAtmosphere.MieScaleHeightMeters | 1200 m | Clamp >=1 | Advanced, Atmosphere On; Mie vertical density; km display |
| SkyAtmosphere.MieAnisotropy | 0.8; dimensionless | Clamp [-0.999,0.999] | Advanced, Atmosphere On; scattering directionality |
| SkyAtmosphere.SkyLuminanceFactorRgb | (1,1,1); linear multipliers | Clamp components >=0 | Atmosphere On; sky and aerial luminance scaling through the canonical native mapping |
| SkyAtmosphere.AerialPerspectiveDistanceScale | 1; multiplier | Clamp >=0 | Advanced, Atmosphere On; aerial optical-distance scaling |
| SkyAtmosphere.AerialScatteringStrength | 1; multiplier | Clamp >=0 | Advanced, Atmosphere On; aerial scattering contribution |
| SkyAtmosphere.AerialPerspectiveStartDepthMeters | 100 m | Reject <0 | Advanced, Atmosphere On; aerial start distance; exact native minimum is0m |
| SkyAtmosphere.HeightFogContribution | 1; multiplier | Clamp >=0 | Advanced, atmosphere and height-fog contribution applicable; controls their coupling |

All metre values remain metres in source/native scene state; presentation in km
is not a second storage unit. Preserve existing native enum/API names. Fixed
engine coefficients and other fields outside this table do not automatically
become editor sliders. An inapplicable fog-coupling field explains why it is
inactive rather than implying that an unimplemented fog-authoring workflow exists.

Canonical native defaults are grounded in Core/Types/Atmosphere.h,
Scene/Environment/SkyAtmosphere.h and EnvironmentSystem.h. Height is 100 km and
Aerial Start is 100 m; 80 km and 0 m are not the native creation defaults.

## 4. Captured SkyLight and background

| Source path | Default / unit | Bounds | Effect |
| --- | --- | --- | --- |
| SkyLight.Enabled | true; bool | Boolean | Enables scene-sky diffuse and specular image-based lighting |
| SkyLight.IntensityMul | 1; dimensionless | Finite, Clamp >=0 | SkyLight On; multiplies both diffuse/specular contribution |
| BackgroundColor | (0,0,0); linear SDR RGB | Finite, Clamp each [0,1] | Solid display background when atmosphere is off; always retains source intent |

SkyLight uses the existing native CapturedScene source. V0.1 exposes no cubemap
picker or alternate HDRI/probe/GI authoring workflow. Native tint, diffuse and
specular multipliers remain 1; other internal defaults remain engine-owned.
Source radiance comes from the scene sky/atmosphere, including both assigned
lights. The display-only Background supplies no illumination/reflection radiance.
With no active lighting sky, sky contribution is zero; do not retain a stale
capture or invent ambient energy from the background picker.

Capture produces diffuse irradiance and roughness-dependent specular radiance
with the required BRDF integration. Both effects are mandatory. CapturedScene's
current unavailable path and specified-cubemap diffuse-only products are
implementation gaps, not acceptable substitutes. Neither a stored Enabled flag
nor two visible sky disks proves captured lighting.

The engine [captured-sky IBL contract](../../../projects/Oxygen.Engine/design/vortex/plan/editor-v01-captured-sky-ibl.md)
defines the scene-global anchor, processing products, filtering, publication and
Stage 13 ownership. Camera navigation leaves this shared lighting unchanged;
the authored atmosphere and contributing light state determine its radiance.

Changes to either contributing light, effective visibility/participation,
assignment, atmosphere parameters or applicable sky source invalidate affected
products. Publish one dependency-ordered current generation for rendering;
qualification readiness additionally requires completed processing/uploads and
valid GPU metadata, as defined by the IBL contract. Ordinary light edits produce
and consume their updated sky in the same frame without a CPU fence wait.
Explicit disks are excluded from reflection captures to avoid double-counting
direct source energy, while both sources' atmospheric scattering remains.

Background is a display backdrop. Convert picker sRGB to stored linear SDR once,
and retain the picked display result independent of scene exposure/tone mapping.
It does not become emissive or an ambient light. Atmosphere takes precedence over
its visual presentation. Translucent foreground uses correct coverage composition;
changing the backdrop must not replace or flatten that material's lighting.

## 5. Exposure fields

ExposureEnabled is independent of tone-curve selection. Manual and Auto retain
native enum values 0 and 2; native ManualCamera value 1 remains engine-owned and is
not an editor choice without physical-camera authoring. Preserve existing API
names; no enum renumbering or cosmetic aliases are introduced.

| PostProcess path | Default / unit | Bounds | Conditional UI and active effect |
| --- | --- | --- | --- |
| ExposureEnabled | true; bool | Boolean | Primary; Off uses unit exposure without erasing settings |
| ExposureMode | Auto | Manual / Auto | Primary; selects fixed EV or metered exposure |
| ManualExposureEv | 9.7 EV100 | Clamp [-24,24] | Enabled + Manual; fixed exposure |
| ExposureCompensationEv | 0 EV | Finite and representable conversion | Enabled; +1 doubles exposure, -1 halves it; no light-only [-10,10] restriction |
| ExposureKey | 10; dimensionless calibration scale | Clamp >=0.001 | Advanced, Enabled; same bias scale for Manual and Auto |
| AutoExposureMinEv | -6 EV100 | Finite, Min <= Max | Auto; minimum metered EV |
| AutoExposureMaxEv | 16 EV100 | Finite, Max >= Min | Auto; maximum metered EV |
| AutoExposureSpeedUp | 3 EV/s | Clamp >=0 | Auto; adaptation toward brighter luminance |
| AutoExposureSpeedDown | 1 EV/s | Clamp >=0 | Auto; adaptation toward darker luminance |
| AutoExposureMeteringMode | Average | Average / CenterWeighted / Spot | Auto; image weighting; native ordinals 0/1/2 |
| AutoExposureLowPercentile | 0.1 | [0,1], Low < High | Advanced Auto; lower histogram percentile |
| AutoExposureHighPercentile | 0.9 | [0,1], High > Low | Advanced Auto; upper histogram percentile |
| AutoExposureMinLogLuminance | -12; log2 luminance | Finite | Advanced Auto; histogram lower range |
| AutoExposureLogLuminanceRange | 25; log2 span | Clamp >=0.001 | Advanced Auto; histogram span |
| AutoExposureTargetLuminance | 0.18; linear target | Clamp >=0 | Advanced Auto; exposure target |
| AutoExposureSpotMeterRadius | 0.2; normalized image radius | Clamp >=0 | Advanced Auto + Spot; active metering radius |

Validate coupled ranges atomically. Exposure Min/Max can be ordered together on
commit; invalid histogram intervals reject without partial mutation. Numeric
source loaded for rendering must already satisfy the canonical constraints.

The native calibration constant 12.5 is not the creation ExposureKey default 10.
Using Core/Types/PostProcess.h, bias = 2^Compensation * (ExposureKey/12.5);
Manual exposure = bias / 2^EV100. Auto applies the same bias to its target.
Do not duplicate this authority with a second editor exposure implementation.
Expected conversion examples are independently authored for qualification.

## 6. Tone mapping, bloom and grading

| PostProcess path | Default / unit | Bounds | Conditional UI and active effect |
| --- | --- | --- | --- |
| ToneMapper | AcesFitted | None / AcesFitted / Filmic / Reinhard, native 0/1/2/3 | Selects tone curve; None is SDR clipping without a filmic curve |
| BloomIntensity | 0; multiplier | Clamp >=0 | Bloom contribution; 0 disables bloom |
| BloomThreshold | 1; linear HDR units | Clamp >=0 | BloomIntensity >0; extraction threshold |
| Saturation | 1; multiplier | Clamp >=0 | Foreground colour grading; 0 removes chroma |
| Contrast | 1; multiplier | Clamp >=0 | Foreground contrast around the defined middle-grey reference |
| VignetteIntensity | 0 | Clamp [0,1] | Foreground edge attenuation within the camera content rectangle |
| DisplayGamma | 2.2 | Clamp >=0.001 | Foreground output power 1/gamma after tone mapping; remains active for None |

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
  edit/import/cook reports the occupant and changes neither light.
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
[engine capability record](../../../projects/Oxygen.Engine/design/vortex/plan/editor-v01-deferred-capabilities.md).
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
