# V0.1 captured-sky image-based lighting

Status: final implementation contract; implementation and rendered qualification
remain pending.

## 1. Ownership and scope

Activate Vortex Stage 13's `IndirectLightingService` for diffuse and specular
environment lighting. `EnvironmentLightingService` remains the owner of sky
radiance generation, cubemap processing, persistent product resources and
`EnvironmentFrameBindings`. Stage 13 consumes published bindings; it does not
reach into Environment internals. Put the service under the current Vortex
family layout `src/Oxygen/Vortex/IndirectLighting/`, matching the actual
`Lighting/` and `Environment/` families rather than reviving the reserved LLD's
obsolete `Services/` directory convention.

Remove the Stage 12 static-SkyLight draw kind, ambient-bridge bindings/flags,
configuration switches and shader branch in the same activation. Stage 12
becomes direct lighting only. Stage 13 adds indirect lighting to deferred
opaque/masked SceneColor once. Forward/translucent surfaces evaluate the same
Stage-13-owned shader helper in their existing surface pass: do not run the
deferred full-screen apply over their already shaded colour. Product publication
must precede both consumers. Existing fog consumers can read the published SH
product without acquiring surface-indirect ownership.

The V0.1 editor exposes CapturedScene, Enabled and Intensity only; native tint,
diffuse/specular multipliers and reflection participation keep their established
meaning and defaults. Existing native specified-cubemap support remains a valid
source adapter feeding the same product/evaluation contract. It is not a fallback
when captured sky fails. No HDRI picker, local probes, geometry reflections, SSR,
GI, clouds, new AO algorithm, reflection occlusion, temporal denoiser or continuous
time-sliced capture feature is introduced by this work.

### 1.1 One capture policy and canonical migration

CapturedScene has one automatic, change-driven policy: capture on first use and
whenever its source key changes. An unchanged source reuses its products. There
is no separate authored `real_time_capture_enabled` mode, no every-frame switch
and no time-slicing selector. Remove that obsolete bool from the canonical
SkyLight source schema, packed record, native Scene/model API, importer/loader,
Inspector reports, Interop, DemoShell settings/VM and example startup parameters.
Remove its contribution to source hashes and its unavailable reason/branch.

The old bool had no functioning capture effect: `IblProbePass` rejected captured
sources and reported a separate deferred state for true on other sources. The
other references store, transport or inspect it. One-time migration drops either
old value while preserving useful Source, Enabled, intensity, tint, hemisphere
and other effective SkyLight values, then recooks through the normal native
producer. It does not reinterpret old true as continuous capture or false as
manual capture. A previously unavailable source becomes the implemented canonical
source behavior; it is not retained as a hidden compatibility mode.

Version source/packed layouts and update all serializers/size checks together.
Migrate maintained scene recipes and local demo settings before using them for
the new native gate. Remove the current Interop forced-true write and native
examples' setters/startup fields; canonical readers reject retired fields after
migration. Replace bool round-trip-only tests with first-use, change-driven,
unchanged-key reuse and rendered diffuse/specular tests. Fog's independently
named `visible_in_real_time_sky_captures` flag is not this SkyLight mode and is
not removed by a substring-based migration.

Concrete source seams: `Scene/Environment/SkyLight.h:115–121,187`,
`Data/PakFormat_world.h:466`, `Data/PakFormatSerioLoaders.h:563`,
`Cooker/Import/Schemas/oxygen.scene-descriptor.schema.json:657–670`,
`Cooker/Import/Internal/Jobs/SceneDescriptorImportJob.cpp:547–548`,
`Vortex/Environment/Internal/AtmosphereState.cpp:186,337`,
`Vortex/Environment/Passes/IblProbePass.cpp:152–157`,
`Oxygen.Editor.Interop/src/Commands/SetEnvironmentCommand.cpp:157`,
`Examples/DemoShell/Services/EnvironmentSettingsService`, `SkyboxService`,
`Examples/RenderScene/main_impl.cpp:402–403` and its startup plumbing.
`Examples/Async/MainModule.cpp:1123` and `Examples/VortexBasic/MainModule.cpp:750`
also set the obsolete bool. Preserve historical evidence separately from the
current source/schema contract.

## 2. Capture source and coordinates

CapturedScene here means the authored distant sky/atmosphere. It captures no
ordinary scene geometry, emissive objects, editor gizmos, display background,
camera bars, exposure, bloom, tone curve, grading, fog composition or previous
IBL contribution. This avoids a feedback loop and preserves the display-only
background contract. Atmosphere-off produces an explicitly ready zero-radiance
result, not indefinite unavailable state and not a stale previous sky.

Both participating Primary/Secondary lights feed the existing shared atmosphere
integration. Hidden/off sources are absent without reassignment; role-None direct
lights contribute no atmosphere radiance. Exclude both analytic light disks using
the reflection-capture policy, while preserving both sources' atmospheric
scattering. The full direct surface lights remain separate from IBL.

The capture anchor is scene-global: `C + (R + h) * worldUp`, where C is the
existing resolved planet centre, R the authored planet radius, worldUp is +Z,
and h is the existing `kPlanetRadiusOffsetKm` converted to metres (1 m). Reuse
`ResolvePlanetCenterWs` and the native planet/tangent-frame conversion through a
shared helper. In the default planet-top-at-world-origin mode the anchor is
(0,0,1 m); the other modes follow their authored atmosphere anchor/centre.
Use a stable native tangent frame, independent of the current camera rotation.
The same scene/source key shares one complete product set across views.

Camera translation, rotation, FOV, aspect and resolution never invalidate this
scene-global sky light. This is a global approximation of radiance at its defined
location, so it does not assert that every distant/high-altitude camera location
has identical local atmospheric radiance. It does not restrict atmospheric camera
rendering. Positioned/local probes would need separately defined authored
semantics; they are not an implicit part of this implementation.

UE 5.7 likewise evaluates its sky-light capture from the SkyLight component's
CapturePosition, rather than the main camera. Oxygen's SkyLight is a scene-global
environment system without an authored component transform, so the existing
planet surface reference supplies its deterministic anchor. Camera-following
capture is excluded because navigation would change authored lighting and force
recapture. The distant-sky LUT's 6 km sample altitude is a fog ambient input,
not this surface-reflection origin.

Reuse the atmosphere radiance integrator and LUT dependencies, extracting a shared
shader helper where necessary. The visible Sky shader's final pre-exposure/range
clamp/output steps are not capture radiance. The capture helper evaluates finite
scene-linear HDR radiance before those steps; do not render the screen and undo
its tone mapping or copy the atmosphere equations into an independent shader.
Reuse the existing atmosphere internal quality policy: transmittance 256×64,
multiple scattering 32×32, sky-view 192×104, sky-view integration 4–32 samples with
the existing 150 km distance scale. Evaluate the sky-view product at the stable
capture anchor/referential; do not reuse a camera-position-dependent LUT as if
its source key were global. Changes to these internal settings invalidate the
corresponding products.

Use the existing native cube face basis and `CubemapSamplingDirFromOxygenWS`
conversion for every source, integration and sampling pass. Preserve +Z world up;
do not invent a second face-order or yaw convention. Apply the existing lower
hemisphere policy once before convolution: default solid black, blend 1, below
world horizon. These black/solid/blend-1 defaults are already present in
`Scene/Environment/SkyLight.h:189–191`; they are not new editor selections.
Native specified-source yaw remains its independent existing
setting; captured atmosphere uses its native world directions without an extra
SkySphere rotation.

## 3. Products and processing

These are renderer constants/internal product policies, not new editor controls.
Changing the processing revision invalidates products and qualification evidence.

| Product             | Concrete contract                                                                                  | Basis                                                                           |
| ------------------- | -------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------- |
| Captured source     | 128×128×6, scene-linear RGBA32Float scratch, alpha 1                                               | 128 is UE's documented default; FP32 scratch prevents premature HDR clipping    |
| Processed cube      | 128×128×6 RGBA16Float, all 8 mips down to 1×1                                                      | Existing Oxygen processed format/scale contract; source mip chain for filtering |
| Diffuse SH          | Structured buffer of 8 float4; existing three-band packing in entries 0–6, average brightness in 7 | Current `StaticSkyLightProcessor` and shader evaluator                          |
| Specular cube       | 128×128×6 RGBA16Float, all 8 mips, GGX prefiltered                                                 | Same orientation/range scale as processed cube                                  |
| BRDF lookup         | 128×32 RG16Unorm, one mip, 128 deterministic samples/texel, renderer-lifetime cache                | UE 5.7 `SystemTextures.cpp` native baseline; no scene dependency                |
| Generation metadata | One 16-byte structured element: float32 source scale/brightness, uint32 processing flags/revision  | GPU-produced scaling/validity without a CPU readback dependency                 |

Specified-cubemap sources retain existing resolved source size/power-of-two
selection and HDR source identity. Their specular product has the same face size
and complete mip count as their processed source. Record actual dimensions; do
not silently recook/rescale authored texture assets to the captured default.

Processing order:

1. Resolve a coherent current atmosphere/light/global-anchor snapshot and required LUT
   generation. Record six-face sky-only capture into private FP32 scratch.
2. Apply hemisphere policy; GPU-reduce maximum RGB. Reject non-finite source
   output visibly. Set `source_radiance_scale = max(1, maximumRGB / 65504)`;
   store radiance divided by this scale in the processed FP16 product.
3. Generate all processed cube mips with the established face orientation and
   deterministic four-child box reduction. Filtering samples through TextureCube
   seamless direction lookup. Qualify cube edges, poles and energy; a failing
   seam is an implementation defect, not permission to change expected images.
4. Integrate diffuse SH from every mip-zero texel with the current exact texel
   solid-angle weights and fixed reduction order. Preserve the native three-band
   normalization/packing: evaluator output is diffuse irradiance divided by pi,
   so multiply by diffuse albedo once without another pi division. Constant unit
   radiance with hemisphere replacement disabled evaluates to unit diffuse light.
5. Generate every specular mip with the deterministic GGX policy below.
6. Record final metadata validity after every producer, then publish one coherent
   generation for GPU-ordered consumption. Source, all mips, SH, metadata and
   matching BRDF must have guaranteed producer-before-consumer submission and
   resource transitions. CPU fence completion is a separate qualification fact;
   it is not required to shade that frame. No diffuse-only success state satisfies
   this V0.1 contract.

All convolution operates in the scaled linear-radiance domain. Apply
`source_radiance_scale` exactly once during shading, together with SkyLight
intensity/tint and the relevant diffuse/specular multiplier. The stored SH and
brightness remain scaled; never expand both the buffer and its consumer.
The shared IBL helper reads source scale from generation metadata, not a CPU
copy of the GPU reduction. CPU-authored `radiance_scale` carries the existing
SkyLight intensity multiplier; shader evaluation multiplies it by metadata's
source scale once. Static cubemap processing uploads its known scale into the
same metadata representation. All consumers, including existing fog SH users,
must use that single conversion contract.
The existing average-brightness metadata is solid-angle-weighted mean RGB, not
exposure histogram luminance; it does not alter exposure or normalize lighting.

The metadata element is exactly `{ float source_radiance_scale;
float average_brightness; uint processing_flags; uint product_revision; }`.
Bit 0 means finite source/convolution output; bit 1 means every required product
was written; all other bits are zero. The final producer writes bit 1 only after
the complete dependency chain. Shaders require both bits and equality with the
bound nonzero revision before sampling. Initialize candidate flags to zero;
non-finite output leaves the candidate invalid. Generation wrap cannot alias a
live product: reset the owner namespace only after its readers drain.

Publish a typed `product_metadata_srv` in native/HLSL `EnvironmentProbeBindings`
and `GpuSkyLightParams`. It belongs to the structured-buffer descriptor domain,
not a TextureCube slot. Append one 16-byte row to `GpuSkyLightParams` containing
the SRV and three zero padding words, making that record 80 bytes; its current
672-byte aggregate becomes 688 bytes. Update C++/HLSL mirrors, affected offsets,
shader catalog/build identities and ABI assertions atomically. The metadata
buffer, textures and descriptors share the same generation lifetime. Development
diagnostics can read back metadata asynchronously; ordinary shading needs no
CPU readback of scale, brightness or validity.
Any native/embedded qualification capture lease retains this metadata buffer and
its descriptor with the product textures until its GPU/readback consumers drain.

### 3.1 GGX prefilter

Use the desktop policy from UE 5.7 `ReflectionEnvironmentShaders.usf::FilterPS`
and its inverse mapping in `ReflectionEnvironmentShared.ush`, expressed through
shared Vortex-native helpers. Do not mix the producer mapping with the current
forward shader's linear `roughness * maxMip` lookup.

For maximum mip M, the lookup is
`clamp(M - 2 + 1.2 * log2(max(roughness,0.001)), 0, M)`.
For output mip m, filter roughness is
`exp2((m + 2 - M) / 1.2)`; roughness above 0.99 takes the cosine branch.
For roughness below 0.01 copy source mip 0. Otherwise use 32 Hammersley samples
below 0.1 and 64 samples at/above 0.1, with sequence seed 0. For the intermediate
GGX branch use alpha² = roughness⁴, the reference's E.y×0.995 endpoint handling,
N=V, reflected sample direction and NoL weighting. The rough branch uses cosine
hemisphere samples. Source mip selection uses the reference sample-solid-angle
to cube-texel-solid-angle ratio, including its ×2 texel footprint factor.
Clamp source LOD to its actual mip range and guard zero weight/PDF numerically;
do not turn invalid radiance into a silently valid product.

The 1024-sample branch in the same UE shader supplies independent high-quality
reference measurements, not the production sample count. Preserve both mapping
directions and golden roughness/mip values in tests. At 128 faces, M=7, roughness 1
looks up mip 5; the coarser levels remain complete but are not a reason to replace
the mapping with a linear one.

### 3.2 BRDF integration and surface evaluation

Generate A/B using the UE 5.7 `SystemTextures.cpp` preintegrated-GF algorithm:
texel-centre NoV/roughness, 128 Hammersley samples, GGX half-vectors,
the documented Smith-joint approximation and Schlick Fresnel split. Accumulate
in float32; quantize final A/B to RG16Unorm. This is a Vortex-owned generator,
not a shipped copy of an external engine asset or per-frame CPU rebuild.
Upload once per renderer/device and algorithm revision; retain upload completion
ownership. Sample directly at `(saturate(NoV), saturate(roughness))` with linear
clamp: do not apply the old forward path's second texel-centre remapping when the
generator already defines texel-centre samples.

Use existing material F0: dielectric default 0.04 and metallic interpolation with
linear base colour. The specular term is filteredRadiance × `(F0*A + F90*B)`.
Use the UE native default-material F90 policy `saturate(50*F0.g)` consistently in
the shared helper; qualify near-black metals explicitly. Diffuse is packedSH(N)
× baseColour × (1-metallic). Preserve effective material occlusion on the diffuse
term; do not introduce SSAO or speculative specular occlusion. Receiver-shadow
opt-out does not remove ambient/material occlusion.

Both terms use the shading normal after normal mapping/sidedness correction,
the same reflection vector/cube conversion and the same contribution multipliers.
IBL consumes the material's perceptual roughness in [0,1]; do not substitute a
direct-light BRDF's internal alpha/roughness numerical floor for that source.
Transparent surfaces preserve their ordinary coverage composition. Remove the
missing-LUT `EnvBrdfApprox` production fallback and any `SKIP_BRDF_LUT` route that
could report canonical specular readiness without its required product. Dedicated
debug isolation may omit a term but cannot satisfy qualification for that term.

## 4. Readiness, invalidation and lifetime

Use one current key/generation for each scene/source product set. Include:

- Scene activation identity and applicable source kind/resource content identity.
- Canonical atmospheric scalar/planet state and shared LUT algorithm revision.
- Both stored slot identities and effective participating direction, colour and
  compensated illuminance; resolved visibility/participation changes invalidate.
- Resolved global anchor, hemisphere/yaw policy, selected dimensions/formats and processor
  revision. Source radiance includes no exposure/post-process/background state.

Intensity/tint/diffuse/specular multipliers and AffectReflections are evaluation
state: update bindings without reconvolving. Display background, grading, material
edits, geometry membership, workspace Hide, camera orientation/FOV/aspect/resize
do not invalidate a sky-only radiance cube. Camera translation likewise leaves
the product unchanged. Shadow-map tuning changes
direct illumination, not the unoccluded atmosphere source.

Schedule the complete capture/convolution chain after current atmosphere LUT
dependencies in Environment's existing pre-light publication boundary, before
Stage 9 forward/base shading and Stage 13 indirect apply. Do not wait until
Stage 15 visible-sky composition to discover an input dependency. Production
uses the existing graphics queue: LUTs → capture → range reduction → processed
mips → SH/prefilter → final metadata → surface consumers. Record ordered
commands with required UAV ordering and final SRV transitions. Command-list
boundaries are not substitutes for barriers. If an input upload uses another
queue, insert its ordinary GPU queue dependency before this chain, not a CPU wait.
Record the whole update for the current frame; no face/mip time slicing is used.

At the frame snapshot boundary, coalesce authoring changes into one desired key
per scene. Direct lights, atmosphere and IBL for that frame use that coherent
snapshot. Once the full chain is recorded with guaranteed ordered submission,
publish its binding set for that frame's consumers, even while the GPU has not
completed it. They execute after its producers and therefore see the new sky.
Changes arriving after this snapshot belong to the next frame. This is normal
frame snapshotting, not silently retaining an old product for a new source key.

Distinguish **GPU-ordered availability** from **GPU-completed readiness**.
Ordinary interactive light edits consume the newest generation in the same
frame; they do not deliberately render zero or wait for a CPU fence poll.
Qualification starts frame 0 only after its required generation, uploads and
metadata validity have completed and been observed. A queued/recorded generation
alone cannot satisfy that gate. Asynchronous diagnostics/qualification inspect
the metadata flags; an invalid generation never passes readiness. Disabled or
no-lighting-sky is explicit ready-zero. Genuinely missing resources, failed
allocation/recording/submission or invalid processing yields unavailable zero
with a scoped failure, not an ordinary update phase. Do not substitute an older
key or a diffuse-only product to conceal that failure.

Permit consecutive frames' generations to be GPU-in-flight. Use renderer
frame-slot ownership and a bounded resource pool; do not block later updates
until the CPU observes the preceding generation complete. Resource reuse follows
the normal frame/fence lifecycle. Unchanged keys reuse their product with no
per-frame allocation/convolution. A private candidate becomes visible only as
a complete binding set with its guaranteed producer chain. Late completion
checks scene/device/private-capture-view lifetime and revision; it cannot replace
a newer published binding. Allocation/submission failure invalidates the affected
frame rather than certifying an unproduced generation.

Use renderer queue dependency/fence mechanisms, never a new engine-thread flush,
CPU wait or per-frame CPU readback. Old resources remain retained until all frames
using their descriptors drain. Scene/device teardown invalidates product publication
before releasing resources. Destroying the private capture-job view invalidates
that job's candidate/completion, not an already published valid product. Ordinary
consumer-view recreation, resize or destruction releases only its bindings/read
ownership; it does not invalidate the scene-global product or its source key.
The global BRDF product has its own renderer/device
lifetime and is unaffected by source changes.

## 5. Required qualification

Native unit/schema/CPU-reference and GPU product checks precede native visual
scenes, then the same controls/scenes run in the editor. Keep M08 semantic/image
tolerances unchanged; physical float fields and authored state are not rounded
to make FP16 render intermediates pass. Independent product tests cover:

- Constant white/coloured cube, one bright direction, six labelled faces,
  horizon and HDR range; SH normalization, scale applied once and cube seams.
- Roughness 0/0.1/0.5/1 and grazing/normal NoV; independent 1024-sample reference,
  matching producer/consumer mip mapping and valid BRDF dimensions/format.
- Diffuse dielectric and glossy/rough metals; separate diffuse/specular toggles
  and amplitude; forward/deferred agreement without double application.
- Primary-only, Secondary-only, both, hidden/off/re-enabled and role-None fill.
  Disk exclusion removes neither source's scattering.
- Atmosphere-off ready-zero, background-colour changes without IBL changes,
  exposure/grading without product regeneration, and transparent coverage.
- Current-key publication, rapid coalesced edits, late generation, pending
  uploads, source replacement, scene/device lifetimes and clean teardown.
- Continuous sun edits consume the matching current generation in each rendered
  frame without deliberate black/stale intervals. Verify ordered producers,
  barriers, metadata flags, consecutive in-flight generations and capture leases;
  CPU-observed completion remains a separate qualification gate.
- Camera navigation leaves scene-global product identity/radiance unchanged;
  atmosphere anchor/radius changes recompute the defined capture position.
- Stage 13 pass/product evidence plus absence of Stage 12 ambient draws/flags;
  production loads the normal renderer without qualification instrumentation.

Record actual dimensions, formats, sample policy, product/source/LUT generations,
range scale, resource readiness and stage use in development evidence. Missing
specular products or a second atmosphere source cannot be excused by diffuse-only
images. Fixed native profile visual review remains mandatory.

## 6. Implementation map

| Existing file/section                                  | Current statement/path                                                          | Required reconciliation                                                                                                 |
| ------------------------------------------------------ | ------------------------------------------------------------------------------- | ----------------------------------------------------------------------------------------------------------------------- |
| cubemap-processing.md §§1,4.3,5.1,6.1                  | CapturedScene unavailable; diffuse-only readiness; specular optional            | Keep closed VTX-M08 proof historical; new extension requires full product set for canonical IBL                         |
| skybox-static-skylight.md §§2.5,6.4,11                 | Static diffuse routed through Lighting; captured/specular deferred              | Retain source/sky policy and evidence; supersede Stage 12 placement and captured/specular deferral                      |
| indirect-lighting-service.md §§1.3,2.1,5               | Stage 13 reserved; old Services path; optional AO                               | Activate bounded sky diffuse/specular subset; use current family path; no new AO requirement                            |
| environment-service.md §§2.3,10                        | Stage 13 future/deferred                                                        | Product ownership stays Environment; Stage 13 apply is now mandatory for V0.1                                           |
| IblProbePass.cpp:164–171                               | Diffuse 0 disables all; CapturedScene unavailable                               | Independent diffuse/specular gates; supported captured source and explicit ready-zero                                   |
| IblProcessor.cpp:386–423                               | Specular/LUT slots invalid; CPU static-source processing/upload                 | Reuse valid static machinery; add GPU captured processing and complete common publication                               |
| SceneRenderer.cpp:2052,2187; DeferredLightPass.cpp:905 | Ambient bridge enabled; Stage 13 reserved; `Vortex.Stage12.StaticSkyLight` draw | Remove bridge and activate canonical indirect service                                                                   |
| Sky.hlsl:329–339                                       | Irradiance/prefilter entry points are empty                                     | Implement actual owned processors/shared radiance helper; catalog registration alone is no evidence                     |
| ForwardMesh_PS.hlsl:92–145                             | Linear mip mapping, approximate missing-LUT fallback                            | Shared canonical IBL helper and matching producer mapping, complete product gate                                        |
| SkyLight.h:25                                          | CapturedScene prose includes background                                         | Clarify lighting-sky radiance versus display background with implementation                                             |
| SkyLight.h / source schema / Interop / DemoShell       | Unsupported real-time-capture bool is stored and sometimes forced true          | Remove canonical field/API/branch; migrate useful source/settings once and recook under the single change-driven policy |

## 7. Primary sources and bounded choices

- Local UE 5.7 `Engine/Shaders/Private/ReflectionEnvironmentShared.ush:16–64`:
  matching roughness/mip mapping; `:80–120`: diffuse SH evaluation.
- Local UE 5.7 `Engine/Shaders/Private/ReflectionEnvironmentShaders.usf:544–654`:
  desktop 32/64-sample GGX/cosine filtering, 1024-sample reference and footprint LOD.
- Local UE 5.7 `Engine/Source/Runtime/Renderer/Private/SystemTextures.cpp:532–668`:
  128×32, 128-sample preintegrated GF and 16-bit normalized storage.
- Local UE 5.7 `Engine/Shaders/Private/BRDF.ush:612–627`: split-sum evaluation.
- Local UE 5.7 `Engine/Source/Runtime/Engine/Private/Components/SkyLightComponent.cpp:257`:
  capture position comes from the SkyLight component transform.
- Local UE 5.7 `Engine/Source/Runtime/Renderer/Private/ReflectionEnvironmentRealTimeCapture.cpp:555`
  and `SkyAtmosphereRendering.cpp:1546–1548`: cube-view origin and capture-specific
  sky-LUT data use that position; main-view translation is coordinate plumbing.
- Local Oxygen `Environment/EnvironmentLightingService.cpp:53–67,538–556`
  and `Core/Types/Atmosphere.h:28`: planet-centre modes, shared atmosphere
  coordinate conversion and the existing 1 m surface offset.
- Local Oxygen `Environment/Internal/StaticSkyLightProcessor.cpp:155–247,372–427`:
  exact solid angles, SH packing, HDR scale and brightness; `IblProcessor.cpp`
  owns existing upload tickets and processed products.
- Local Oxygen `SceneRenderer/SceneRenderer.cpp:2052–2096`,
  `Environment/EnvironmentLightingService.cpp:1028–1061` and atmosphere LUT
  `Record` methods: pre-base publication and graphics-queue UAV-to-SRV producers.
- Local Oxygen `Graphics/Common/Graphics.h:185–188` and
  `Graphics/Common/Internal/Commander.cpp:48–77,117–143`: recorder destruction
  submits immediately by default; deferred same-queue lists retain order.
  `Graphics/Direct3D12/Test/SubmitOrderedQueueActions_test.cpp` covers submission
  versus completion and ordered queue actions. These existing mechanisms support
  same-frame consumption; their presence is not evidence that IBL is implemented.
- [Epic Sky Lights](https://dev.epicgames.com/documentation/en-us/unreal-engine/sky-lights-in-unreal-engine):
  captured diffuse/specular lighting, GPU processing and 128-face default. The
  current page resolves to 5.8; algorithm details above use the installed 5.7 source.

128-face capture is a bounded new Oxygen default grounded in the reference, not
an industry-wide requirement. Qualify this defined default with the listed
native visual/product checks. No performance number from another engine/hardware
is claimed for Oxygen. Capture anchor, sky-only scope and atomic update policy are
Oxygen product decisions, distinct from UE's wider scene-capture capabilities.
