# V0.1 captured-sky image-based lighting

Implementation and progress: [VX-IBL-01](../milestones/VX-IBL-01/README.md).

Capture atmosphere and exponential height fog into coherent diffuse/specular
products. Use immediate updates for first use and authoring, and budgeted
incremental updates for runtime changes. The editor keeps simple lighting controls.

Read: [source and fog](#2-capture-source-and-coordinates),
[products and math](#3-products-and-processing),
[scheduling and lifetime](#4-readiness-invalidation-and-lifetime),
[qualification](#5-required-qualification).

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
GI, clouds, local/volumetric fog capture, new AO algorithm, reflection occlusion
or temporal denoiser is introduced by this work. Immediate and budgeted
incremental scheduling are both part of VX-IBL-01.

### 1.1 Automatic scheduling and canonical migration

CapturedScene updates on first use and source-key changes. Unchanged sources
reuse their products. The renderer selects immediate or incremental execution
under section 4; there is no authored scheduling mode or time-slicing control.

Remove `real_time_capture_enabled` from the SkyLight schema, packed record,
native Scene/model API, importer/loader, Interop, DemoShell, Inspector reports
and example startup settings. Both former values migrate to automatic scheduling,
preserving Source, Enabled, intensity, tint, hemisphere and other effective values.
Advance source/packed versions and serializers together, migrate maintained
recipes/settings and recook before native qualification. Canonical readers
reject the retired field; the old capture-unavailable branches retire with it.

Scene source/packed version **8** removes the four-byte scheduling field;
`SkyLightEnvironmentRecord` is **88 bytes**. Use
[`MigrateSceneV8.py`](../../../tools/content/MigrateSceneV8.py) on v7 source
JSON, then recook with the current ImportTool. The migration changes the version, its versioned schema identifier when present,
and the retired field. The editor descriptor generator emits the same v8
schema. PakGen's outer PAK recipe version remains 7.

DemoShell environment settings **v6** omit the field. Loading v5 removes either
boolean value and persists the v6 marker without rewriting saved custom controls,
including when a built-in or scene profile is active. Earlier settings keep their
marker until existing migrations complete. A v6-or-newer settings file containing
the retired key is rejected.

Authoring intent is transient request metadata from the existing native/editor
command and dirty-domain path. It is coalesced with the scene snapshot and is
not serialized. Gameplay changes use the runtime scheduler once a usable product
exists. Neither intent adds an Inspector control.

`Scene::NotifyEnvironmentAuthoringChange()` advances a transient scene revision
after native/editor edits are applied. Environment carries the revision in its
stable snapshot, separately from radiance hashes. S4 compares it with the
revision last observed by that renderer's scene cache, including when the source
key is unchanged, so an old edit cannot mark a later runtime change as authoring.
Current processing remains immediate. An unchanged source key still reuses its
products. Editor transform, hierarchy and visibility commands carry intent
because they can move, replace, remove or hide an atmosphere light through its
ancestors; radiance keys determine whether lighting work is needed. Runtime
scene setters do not mark authoring.

Fog's separate `visible_in_real_time_sky_captures` setting remains meaningful:
it controls height-fog participation in the captured source. Keep its native,
source/packed, loader and authoring routes intact.

The cutover touches `Scene/Environment/SkyLight.h`, `Data/PakFormat_world.h`,
`PakFormatSerioLoaders.h`, the scene descriptor schema/importer,
`Environment/Internal/AtmosphereState.cpp`, `IblProbePass.cpp`, Interop's
`SetEnvironmentCommand`, DemoShell environment services/VM, RenderScene startup,
Async and VortexBasic. Replace bool-only round-trip tests with automatic-update,
source-key reuse and rendered diffuse/specular tests.

## 2. Capture source and coordinates

CapturedScene contains authored atmosphere and capture-visible exponential height
fog. It excludes ordinary geometry, emissive objects, gizmos, display-only
backgrounds, camera bars, exposure, bloom, grading and previous IBL. Local and
volumetric fog are separate view-dependent products and remain outside capture.

Atmosphere and height fog have independent enablement. Fog-only capture evaluates
over zero atmosphere radiance. With neither contributing, publish ready-zero.
Disabling the SkyLight itself publishes ready-zero immediately.

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

The capture source reuses the atmosphere LUT builders at unit exposure and the
shared native tangent frame. Its working LUTs feed a copy into the admitted IBL
slot: that slot owns the frozen sky-view and distant-light inputs through
processing. Subsequent source edits can reuse working storage without changing
the candidate. Capture submissions retain their LUT constant descriptors through
GPU completion, including source teardown. Include these copies in update costs.

Use the existing native cube face basis and `CubemapSamplingDirFromOxygenWS`
conversion for every source, integration and sampling pass. Preserve +Z world up;
do not invent a second face-order or yaw convention. Apply the existing lower
hemisphere policy once before convolution: default solid black, blend 1, below
world horizon. These black/solid/blend-1 defaults are already present in
`Scene/Environment/SkyLight.h:189–191`; they are not new editor selections.
Native specified-source yaw remains its independent existing
setting; captured atmosphere uses its native world directions without an extra
SkySphere rotation.

### 2.1 Height fog and visible sky

Extract the analytic height-fog integral and inscattering evaluation from
`Fog.hlsl` into one shared helper with explicit ray origin and direction.
Capture evaluates a distant ray from the global capture anchor; visible sky uses
the actual view origin. Reuse both height layers, directional/atmosphere
inscattering, start/end/cutoff distances and maximum opacity. Preserve the current
unsupported inscattering-cubemap boundary tracked by VX-FOG-01.

Both visible-sky and captured fog use the same virtual-cube distant-ray
convention: 90° faces, 0.05 m near plane, infinite reversed-Z projection and
far-depth epsilon 1e-10. For normalized direction d in the existing cube basis,
`D = 0.05 / (1e-10 * max(abs(d.x), abs(d.y), abs(d.z)))` metres. Pass origin,
direction and D separately to the shared helper, avoiding a large world-position
addition/subtraction. Only origin differs between visible sky and capture.
Preserve the existing observer-height, XY end-distance, height-compensation,
start-exclusion and cutoff evaluation order. Test axes, edges,
corners, both layers and authored distance limits. Consumer-camera projection
changes do not alter this convention or invalidate captured lighting.

Compose `L = T_fog * L_atmosphere + L_fog` in scene-linear radiance before
hemisphere replacement, HDR normalization and convolution. Fog ambient input
comes from atmosphere's distant-sky product, never the IBL being generated.
Publish effective Primary/Secondary fog-light inputs independently of analytic
disk visibility and capture disk suppression. Current view data gates
`atmosphere_light*_disk_luminance_rgb` on `sun_disk_enabled`; the new fog helper
must not use that display-gated payload as its light-eligibility signal. Preserve
fog calibration while separating the participating light values from disk masks.
`EnvironmentViewData` appends two fog illuminance/enable rows (304 bytes total).
The CPU preserves the established finite-disk calibration; a zero-angle
participating light supplies its authored illuminance. Sky-only fog writes
premultiplied coverage so the existing display-background composition remains
exposure-independent.

With atmosphere disabled, its ambient LUT contribution is zero. Authored fog
inscattering and enabled, participating Primary/Secondary directional fog inputs
remain available; role-None lights are not implicitly reassigned. Fog-only
capture and visible-sky composition must survive the current atmosphere-off
early-outs in `SkyPass.cpp` and `Sky.hlsl`. SkyLight enablement gates IBL, not
the independent visible height-fog effect.

Capture honors fog enablement and `visible_in_real_time_sky_captures`;
visible-sky composition honors fog enablement and `render_in_main_pass`.
`visible_in_reflection_captures` keeps its separate reflection-view meaning.
Apply distant height fog in the visible-sky path before exposure/post-processing.
Display-only background color remains absent from capture inputs.

The depth-based main fog pass continues skipping far-background pixels, so sky
receives height fog exactly once. Local/volumetric fog keeps its existing
far-background exclusion. At the capture anchor, identical source/fog settings
must agree between visible-sky and capture evaluations after excluding analytic
disks and display overrides. Other camera heights use their own view-ray origin.

## 3. Products and processing

These are renderer constants/internal product policies, not new editor controls.
Changing the processing revision invalidates products and qualification evidence.

| Product             | Concrete contract                                                                                        | Basis                                                                           |
| ------------------- | -------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------- |
| Captured source     | 128×128×6, scene-linear RGBA32Float scratch, alpha 1                                                     | 128 is UE's documented default; FP32 scratch prevents premature HDR clipping    |
| Processed cube      | 128×128×6 canonical RGBA32Float plus qualified RGBA16Float; all 8 mips                                   | Existing Oxygen processed format/scale contract; source mip chain for filtering |
| Diffuse SH          | Structured buffer of 8 float4; existing three-band packing in entries 0–6, average brightness in 7       | Current `StaticSkyLightProcessor` and shader evaluator                          |
| Specular cube       | 128×128×6 canonical RGBA32Float plus qualified RGBA16Float; all 8 GGX mips                               | Same orientation/range scale as processed cube                                  |
| BRDF lookup         | 128×32 RG16Unorm, one mip, 128 deterministic samples/texel, renderer-lifetime cache                      | UE 5.7 `SystemTextures.cpp` native baseline; no scene dependency                |
| Generation metadata | One generation record: source scale/brightness, processing flags/revision and half-precision certificate | GPU-produced scaling/validity without a CPU readback dependency                 |

Specified-cubemap sources retain existing resolved source size/power-of-two
selection and HDR source identity. Their specular product has the same face size
and complete mip count as their processed source. Record actual dimensions; do
not silently recook/rescale authored texture assets to the captured default.

### HDR precision

Preserve automatic FP32 promotion. Convolution and SH use canonical FP32
intermediates. Each immutable generation retains the canonical processed and
specular cubes alongside qualified FP16 representations. FP16 is a storage and
sampling choice, not a limit on source radiance or authored intensity.

A GPU certificate covers the actual stored processed and specular chains.
Preserve the half-storage limits: 0.25% relative RGB
error plus 1e-5 after the supported 2^32 display-gain envelope; positive luminance
must survive within 1/1024 stop unless below the amplified absolute budget.
Require finite values and the existing alpha bound. Make the certificate valid
under arbitrary allowed nonnegative channel weights, including later tint and
material/BRDF amplification; an untinted luminance check is insufficient.

Qualify native FP16 filtering against the canonical FP32 sampler on supported
GPU configurations over the specular LODs reachable through the production
roughness mapping: 0.25% relative RGB error plus the same amplified absolute
budget, and at most **2/1024 stop** for visible positive luminance. The **1/1024
stop** guard above applies to stored texels. Exercise ordinary atmosphere/fog,
directional and high-contrast sources, seams/corners, integer/fractional LODs,
roughness and amplified tiny channels. These native comparisons qualify sampler
behavior; they are not a universal sampler-error proof. Keep hardware filtering.
They compare the combined storage/filtering result; do not add the two budgets.
Processed-cube consumers use canonical FP32. Processed-half sampling and
unreachable specular LODs remain characterized but unqualified; new consumers or
roughness-mapping changes must qualify those domains before enabling FP16 there.
Strict storage certification still covers every texel of both complete chains.

The shared evaluator selects FP16 only when the certificate covers the current
intensity, tint, lobe and material gains. Unknown or failed half qualification
selects valid FP32 without making lighting unavailable. Gain edits change neither
the source key nor convolution: both representations remain available in the
same published generation. Never reconstruct FP32 from already-lossy FP16.

Reuse the existing HDR interval/rounding helpers and submission ownership. Keep
qualification and selection within Environment and the common evaluator; no
precision scheduler or blocking CPU readback is needed. Charge both complete
cube chains in both formats, scratch, frozen LUTs, reductions and retained slots
to the existing timing and memory gates.

Compare each stored half texel to its canonical texel. Subtract the guaranteed
`0.0025*reference` relative allowance from its absolute error before calculating
the absolute-budget gain limit. Require every channel to preserve its positive
value within 1/1024 stop; otherwise cap the whole texel's gain using its largest
canonical channel, keeping the amplified texel below the absolute budget.
Scale bounds to scene units once. Round error bounds and required consumer gain
upward, and permitted gain limits downward. Include possible FP32 subnormal
flushing and require finite nonnegative RGB with alpha exactly one.

Scan all faces and mips into disjoint tile partials in one batch and reduce the
minimum permitted gain once, reusing completed SH scratch. Normal varied skies
use FP16 when this storage certificate permits it. The shared evaluator includes
the actual BRDF response in the gain test and marks a material-dependent
descriptor index nonuniform. SH remains FP32 for both selections.

Processing order:

1. Freeze a coherent atmosphere/light/fog/global-anchor snapshot and its LUT
   dependencies. Capture all six atmosphere-plus-height-fog faces into private
   FP32 scratch, either immediately or in scheduled tiles from that snapshot.
2. Apply hemisphere policy; GPU-reduce maximum RGB. Reject non-finite source
   output visibly. Set `source_radiance_scale = max(1, maximumRGB / 65504)`;
   store radiance divided by this scale in the canonical FP32 processed product.
3. Generate all processed cube mips with the established face orientation and
   deterministic four-child box reduction. Filtering samples through TextureCube
   seamless direction lookup. Qualify cube edges, poles and energy; a failing
   seam is an implementation defect, not permission to change expected images.
4. Integrate diffuse SH from every canonical FP32 mip-zero texel with the current exact texel
   solid-angle weights and fixed reduction order. Preserve the native three-band
   normalization/packing: evaluator output is diffuse irradiance divided by pi,
   so multiply by diffuse albedo once without another pi division. Constant unit
   radiance with hemisphere replacement disabled evaluates to unit diffuse light.
5. Generate every canonical FP32 specular mip with the deterministic GGX policy below.
6. Produce the FP16 representations from the completed canonical chains and
   generate their stored-texel precision certificate. Failed half
   qualification leaves canonical FP32 lighting available.
7. Record final metadata validity after every producer, then publish one coherent
   generation for GPU-ordered consumption. Every required face/mip, SH, metadata
   and BRDF dependency must have guaranteed producer-before-consumer submission
   and transitions. Incremental work stays private until this final step; consumers
   keep the previous complete generation meanwhile. CPU fence completion remains
   a separate qualification fact.

All convolution operates in the scaled linear-radiance domain. Apply
`source_radiance_scale` exactly once during shading, together with SkyLight
intensity/tint and the relevant diffuse/specular multiplier. The stored SH and
brightness remain scaled; never expand both the buffer and its consumer.
The shared IBL helper reads source scale from generation metadata, not a CPU
copy of the GPU reduction. CPU-authored `radiance_scale` carries the existing
SkyLight intensity multiplier; shader evaluation multiplies it by metadata's
source scale once. Both source adapters produce scale in the same GPU metadata representation. All consumers, including existing fog SH users,
must use that single conversion contract.
The existing average-brightness metadata is solid-angle-weighted mean RGB, not
exposure histogram luminance; it does not alter exposure or normalize lighting.

The metadata element is 32 bytes: `{ float source_radiance_scale;
float average_brightness; uint processing_flags; uint product_revision;
float maximum_half_gain; uint precision_flags; uint processed_half_srv;
uint specular_half_srv; }`. The first 16 bytes retain the canonical-product ABI.
Precision flag bit 0 means the half certificate is complete; other precision
bits are zero. A missing/failed certificate selects the canonical FP32 views.
The gain bound uses scene units; evaluator selection must not apply source scale
again when comparing authored/material gains.
In `processing_flags`, bit 0 means finite source/convolution output; bit 1 means every required product
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
Use managed SRV views in `kGlobalSrvDomain` for SH/metadata and
`kTexturesDomain` for cubes/BRDF. Allocation domain participates in view-cache
identity; registration/use pins retain the descriptors through completion.

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

### 4.1 Source identity and automatic selection

Track desired, building and published source keys separately. Each candidate
freezes scene/device identity, source kind/content identity, atmosphere state and
LUT revisions, both participating light identities/directions/colors/illuminance,
capture-visible height-fog parameters and their radiance inputs, global anchor,
hemisphere/yaw policy, dimensions/formats and processing revision. Specified-cube
keys use asset/processing inputs rather than unrelated atmosphere/fog state.

Use the scene's process-unique lifetime ID and each active light's full node
handle, including its generation. Keep one cache per live scene so interleaved
views reuse their scene's products. Switching scenes clears active publication
before selecting or updating the target cache; an update failure cannot expose
another scene's products. Scene expiry invalidates active publication; frame and
source-update boundaries retire expired caches. Submitted readers retain their
existing ownership. The BRDF lookup
is shared by the renderer/device.

Intensity/tint/diffuse/specular multipliers and AffectReflections update evaluation
bindings without reconvolution. Exposure, grading, display background, geometry,
materials, workspace Hide and consumer camera movement/resize do not change the
captured source key. Fog capture visibility and effective radiance changes do.
Camera aerial-perspective distance, strength and start depth are view-only;
the inactive specified-cubemap selection does not affect CapturedScene identity.

| Event                                                                                            | Execution                                                  | Visible result                                                                                   |
| ------------------------------------------------------------------------------------------------ | ---------------------------------------------------------- | ------------------------------------------------------------------------------------------------ |
| First use, re-enable, scene/device/source-kind replacement, processing-layout change             | Immediate full update                                      | Current complete generation before its surface consumers                                         |
| Explicit native/editor authoring edit, including a continuous drag                               | Immediate full update; coalesce edits once per scene/frame | Current snapshot in that frame; edits after the snapshot enter the next frame                    |
| Runtime radiance changes with a usable published generation                                      | Budgeted incremental update                                | Previous complete IBL remains visible until atomic replacement                                   |
| Unchanged key or evaluation-only edit                                                            | Reuse                                                      | No capture/convolution or steady allocation                                                      |
| SkyLight disabled; or CapturedScene with neither atmosphere nor capture-visible fog contributing | Immediate ready-zero                                       | No stale illumination; specified-cubemap sources remain independent of atmosphere/fog enablement |

The current visible sky and direct lights follow the current scene snapshot.
Incremental IBL follows its own published snapshot with bounded age; diagnostics
expose both revisions. Immediate authoring requires matching current-source IBL.

### 4.2 Work scheduling and atomic publication

Use Environment's pre-light boundary, before forward/base shading and Stage 13:
LUTs → capture/fog → range reduction → source mips → SH/prefilter → metadata →
consumers. Use the existing graphics queue, recorder/submission owner and barriers;
input uploads from another queue use ordinary GPU dependencies. This is one
Environment-owned IBL job queue, not a general GPU scheduler or pool framework.

Immediate execution records the full chain in that frame. Incremental execution
partitions capture/convolution into bounded tiles and face/mip work batches.
Predict batch cost from work size and existing GPU timestamps; admit work within
the per-frame allowance. Large mips are tiled so one dispatch cannot monopolize
the allowance. The BRDF lookup is generated once per renderer/device revision.

Estimate each shader's cost from launch count and work units: texels, GGX samples
or reduction inputs. Completed GPU batch timings update the renderer-local model;
discarded, mismatched and overflowed samples do not train it. IBL requests the
existing collector independently of the diagnostic toggle and releases its
request when idle. The first work frame after idle uses the retained estimates.

Start with a 0.30 ms prediction allowance, reserving 0.10 ms for a new snapshot.
Balance remaining estimated work over the frames left before the four-frame
deadline; the fourth frame completes the remainder. This allowance is adaptive,
not a hard time ceiling. Both the measured GPU-cost gates and completion/source-age
gates below must pass together.

Batch independent tiles and output mips. Keep barriers between source-mip levels
and other producer/consumer dependencies. Native timestamps bracket batches;
Tracy retains individual dispatch scopes. Range reduction shares the existing
64-lane maximum/validity reduction, preserving extrema and validity exactly.
Failed IBL/source recordings invalidate their timestamp frame before resolve; its query
range still retires through the normal frame-tail fence.

The 96-byte dispatch work record carries a group origin. Spatial tiles keep
whole-cube texel and partial-reduction indices; appended tile records leave the
canonical mip/half table intact. Work constants are uploaded once before
submission and remain immutable while that generation has GPU readers.

`IblGpuProcessor` prepares one immutable dispatch plan shared by immediate and
incremental execution. `BeginSky` submits the LUT copy and invalid metadata before
returning; specified-cube jobs retain an immutable source/descriptor lease.
`Advance` commits its cursor only after accepted submission and returns products
only with the final receipt. Dropping a job retires its already-submitted work
through existing Graphics/Nexus ownership; recordings pin the internal generation,
without retaining the job's external registration leases.

Keep one building candidate and one latest desired snapshot per scene. Continuous
runtime edits replace the queued desired snapshot, not the candidate in progress.
Finish and publish that candidate, then start the latest snapshot by the next
submitted scene frame. This prevents starvation during continuous sun movement.
Every pass reads the candidate's frozen inputs, including retained LUT versions;
a reused atmosphere cache must not overwrite inputs needed by later slices.

Make one scheduling decision per scene/frame after edit coalescing. An immediate
edit cancels the candidate's eligibility to publish and starts at most one
replacement generation that frame, using the latest snapshot. Scene/device/source replacement does the same.
Already-submitted resources retire normally. Submission outcomes commit CPU-side
publication once; abandoned/failed recording cannot publish a generation.

Only the complete final binding set becomes public: processed cube, all specular
mips, SH, metadata and matching BRDF. Metadata starts invalid and its final
producer marks completion. Shaders require its validity bits and matching bound
revision. Publication can precede GPU completion when ordered submission guarantees
that consumers execute after all producers. Qualification capture/readback waits
asynchronously for actual completion before checking the required generation.

Use renderer frame/fence ownership with an IBL-specific admission limit. Reserve
normal update capacity for one published generation, one candidate, and at most
`frame::kFramesInFlight` superseded generations awaiting renderer retirement.
Count canceled submitted candidates in that allowance. A view in an ordinary
in-flight frame uses this renderer allowance.

Allow at most two additional distinct generations held exclusively by explicit
offscreen/qualification capture leases per scene; multiple leases of the same
generation count once. Reserve that allowance at lease admission so captures
cannot consume normal update capacity. If it is full, return a capture-busy/retry
result before accepting the capture. Never reclaim a live lease. Count resources,
descriptors and metadata together; bound bytes using the admitted dimensions and
formats. Reuse existing retirement primitives, adding admission at the IBL owner.
Generic texture-pool reuse alone does not establish this bound.

`Renderer::AcquireIblCapture(view_id)` admits the complete generation published
for that view. Public operations use Oxygen `Result` for C++20 SDK clients.
Copies share admission. `IblCaptureLease::Attach` retains the
generation through recording discard or GPU completion; dropping the CPU lease
does not free a pending GPU reader's allowance. The CPU lease holds external
registration leases; submitted batches hold internal generation/registration
pins, so queue ownership cannot keep Graphics alive through a cycle.

Normal occupancy ends when renderer ownership and ordinary GPU uses drain.
A captured generation then occupies only its reserved capture slot. With three
frames in flight, the per-scene bound is five normal plus two capture slots.
Scene expiry or renderer shutdown closes new admission; an already admitted
lease remains readable while the Graphics backend is active. Backend closure
or fault rejects attachment. Scene-expiry invalidation remains pending until
the environment consumes it or publishes a replacement state.

Failed lease allocation leaves admission unchanged. IBL generation retirement
allocates no memory during CPU release, recording discard or completed GPU-use
release. Discard closes the native command list without preparing submission
state snapshots, and submission callbacks resolve in their existing storage.

Reuse resources after all readers drain. Genuine allocation or normal-pool
exhaustion follows the failure rules below rather than adding a CPU wait or
growing the pool. Scene/device teardown invalidates publication before resource
release. Consumer-view recreation releases only that view's references.

CPU-known allocation, recording or submission failure in an incremental update
keeps the previous valid generation bound and records the update failure/source
age. Exceeding the age bound fails qualification. CPU-known first-use/immediate
failure is unavailable-zero with an explicit failure.

GPU validation can reject a complete recorded candidate after CPU publication.
Its consumers output zero for invalid metadata, and asynchronous diagnostics
record the failed generation; it never satisfies readiness. This is an exceptional
failure, not the normal incremental-update display path. No GPU fallback-selection
framework is introduced. Never publish diffuse-only success or let an older
completion replace a newer published generation.

### 4.3 Performance and latency gates

Qualification uses the existing RTX 3080 / Ryzen 9950X reference, Release, the
128-face captured-source policy, and a 60-Hz scene workload. These are engineering
acceptance targets for VX-IBL-01. Keep all formats and sample counts from section 3.

| Measure                                              | Gate                                                                               |
| ---------------------------------------------------- | ---------------------------------------------------------------------------------- |
| Warm immediate update, additional GPU work p95 / p99 | ≤2.0 / ≤4.0 ms                                                                     |
| Incremental update work per scene frame, p95 / p99   | ≤0.5 / ≤1.0 ms                                                                     |
| Incremental candidate completion                     | Within 4 submitted scene frames, including its snapshot frame                      |
| Published IBL source age during continuous changes   | At most 8 submitted scene frames                                                   |
| Immediate authoring latency                          | Current-snapshot IBL in the same submitted frame                                   |
| Stable source                                        | Zero capture/convolution dispatches and zero product-allocation churn after warmup |
| Sustained source changes                             | Bounded product/descriptor populations; no growth across repeated update cycles    |

Measure age from the candidate's immutable snapshot, not from its publication.
Under continuous changes, both an update in progress and the next queued update
contribute to age. When changes stop, publish the final desired key within the
same 8-frame bound. A non-rendered scene resumes with an immediate current update.

Charge capture-specific LUT work, fog, range reduction, mips, SH, prefilter and
metadata to the update. Report per-frame interval unions and whole-frame p95/p99
separately. Use matched static/animated scenes with timestamps enabled; collect
at least 30 seconds after warmup. Record first-use wall/GPU cost separately,
including uploads and BRDF initialization. Specified-cube sizes receive functional
coverage and reported scaling; the timed reference gate above uses 128-face capture.

Incremental results must match an immediate update of the same frozen source
within the existing image/product tolerances. Scheduling changes work placement,
not sample counts, radiance, filtering quality or the material model.

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
- Atmosphere-only, fog-only, combined and both-disabled sources; ready-zero for
  disabled lighting; background/exposure/grading changes without regeneration.
- Height-fog density/layers, altitude, inscattering, distance limits and capture
  visibility; visible-sky/capture agreement at the anchor; exactly one distant
  fog application on sky and unchanged opaque fog composition. Fog-only output
  survives atmosphere-off; disk hiding/suppression preserves directional fog
  scattering. No volumetric/local-fog capture or new translucent height-fog path.
- Current-key publication, rapid coalesced edits, late generation, pending
  uploads, source replacement, scene/device lifetimes and clean teardown.
- Authoring edits, including drags, consume matching current-generation IBL.
  Runtime animation respects update-cost/completion/source-age gates while reusing
  only complete generations. Exercise nonstop edits, edit cessation, immediate
  preemption, failure/retry and out-of-order completion without mixed faces/mips.
- Ordered producers, barriers, metadata, frozen LUT leases and capture leases;
  one scene shares products across forward/deferred/offscreen views. Exercise
  repeated immediate preemption, maximum frames in flight, two externally pinned
  generations, capture admission rejection, CPU-known failure and GPU-invalid
  output separately. Measure the
  section 4.3 gates with both atmosphere lights and height fog enabled.
- Camera navigation leaves scene-global product identity/radiance unchanged;
  atmosphere anchor/radius changes recompute the defined capture position.
- Stage 13 pass/product evidence plus absence of Stage 12 ambient draws/flags;
  production loads the normal renderer without qualification instrumentation.

Record dimensions, formats, sampling, source/build/published generations, source
age, range scale, readiness, GPU costs and stage use through existing development
diagnostics. Native visual review covers dielectric and metallic materials,
fog/horizon response, sun movement, roughness changes and editor edit latency.
The milestone plan owns execution state and evidence links.

## 6. Implementation map

| Existing file/section                                   | Current statement/path                                                          | Required reconciliation                                                                                            |
| ------------------------------------------------------- | ------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------ |
| cubemap-processing.md §§1,4.3,5.1,6.1                   | CapturedScene unavailable; diffuse-only readiness; specular optional            | Keep closed VTX-M08 proof historical; new extension requires full product set for canonical IBL                    |
| skybox-static-skylight.md §§2.5,6.4,11                  | Static diffuse routed through Lighting; captured/specular deferred              | Retain source/sky policy and evidence; supersede Stage 12 placement and captured/specular deferral                 |
| indirect-lighting-service.md §§1.3,2.1,5                | Stage 13 reserved; old Services path; optional AO                               | Activate bounded sky diffuse/specular subset; use current family path; no new AO requirement                       |
| environment-service.md §§2.3,10                         | Stage 13 future/deferred                                                        | Product ownership stays Environment; Stage 13 apply is now mandatory for V0.1                                      |
| IblProbePass.cpp:164–171                                | Diffuse 0 disables all; CapturedScene unavailable                               | Independent diffuse/specular gates; captured atmosphere/fog, automatic scheduling and explicit ready-zero          |
| IblProcessor.cpp:386–423                                | Specular/LUT slots invalid; CPU static-source processing/upload                 | Reuse valid static machinery; add GPU captured processing and complete common publication                          |
| SceneRenderer.cpp:2052,2187; DeferredLightPass.cpp:905  | Ambient bridge enabled; Stage 13 reserved; `Vortex.Stage12.StaticSkyLight` draw | Remove bridge and activate canonical indirect service                                                              |
| Sky.hlsl:329–339                                        | Irradiance/prefilter entry points are empty                                     | Implement actual owned processors/shared radiance helper; catalog registration alone is no evidence                |
| ForwardMesh_PS.hlsl:92–145                              | Linear mip mapping, approximate missing-LUT fallback                            | Shared canonical IBL helper and matching producer mapping, complete product gate                                   |
| Fog.hlsl / Sky.hlsl / atmosphere LUT cache              | Main-view fog skips far depth; LUTs follow live views                           | Share distant height-fog evaluation, visible-sky composition and capture-specific immutable LUT snapshots          |
| Existing authoring commands / environment dirty domains | No IBL scheduling intent or candidate queue                                     | Carry transient authoring intent; bound incremental work, coalesce runtime inputs and publish complete generations |
| SkyLight.h:25                                           | CapturedScene prose includes background                                         | Clarify lighting-sky radiance versus display background with implementation                                        |
| SkyLight.h / source schema / Interop / DemoShell        | Unsupported real-time-capture bool is stored and sometimes forced true          | Remove canonical field/API/branch; migrate useful source/settings once and recook under automatic scheduling       |

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
- Local UE 5.7 `ReflectionEnvironmentRealTimeCapture.cpp:415,913–949`:
  full/editor versus time-sliced capture, capture-visible distant height fog and
  capture-position origin. `ReflectionEnvironmentCapture.cpp:57,1237–1242`
  supplies the 5 cm near plane and infinite reversed-Z projection;
  `ReflectionEnvironmentShaders.usf:923–960` uses the explicit fog origin and
  1e-10 far-depth clamp.
- Oxygen `Services/Environment/Fog.hlsl:221–283,340–390` and
  `Scene/Environment/Fog.h`: existing analytic fog, visibility flags and the
  main-view far-background skip to preserve when moving sky fog into the sky path.
- [Epic Sky Lights, UE5.7](https://dev.epicgames.com/documentation/en-us/unreal-engine/sky-lights-in-unreal-engine?application_version=5.7):
  diffuse/specular capture, exponential height fog, GPU time slicing and the
  128-face default. Algorithm details above use the installed 5.7 source.

Oxygen uses a fixed global anchor and two automatic execution schedules to keep
authoring immediate and runtime frame cost bounded. Complete-generation publication
preserves the same filtering quality in both schedules. The 128-face profile and
section 4.3 gates define the first qualified operating point.
