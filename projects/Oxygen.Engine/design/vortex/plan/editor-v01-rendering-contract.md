# Editor V0.1 Rendering Contract

Status: **Ready for implementation**. This is the engine production contract
for the final editor domains, not a claim that current rendering implements it.
Existing VTX milestone evidence remains scoped to the behavior it validated.

## 1. Scope and authority

Implement ordinary engine scene/loading/rendering behavior here. Development
qualification consumes those capabilities through separately opted-in targets;
protocol readers, deep observations and test-only capture controls do not become
normal engine/editor startup requirements.

Domain field/default/validation authorities:

- [Scene authoring model](../../../../../design/editor/lld/scene-authoring-model.md)
- [Property inspector](../../../../../design/editor/lld/property-inspector.md)
- [Environment and exact post-process domains](../../../../../design/editor/lld/environment-authoring.md)
- [Captured-sky IBL algorithm](editor-v01-captured-sky-ibl.md)
- [Deferred capability boundaries](editor-v01-deferred-capabilities.md)

This extension replaces conflicting single-directional, implicit atmosphere-role,
boolean-only flag, viewport-aspect overwrite and minimal post-process assumptions
in earlier service interface sketches. It does not erase historical VTX closure
results or expand editor scope to every native capability.

## 2. Scene flags, light contribution and mutation

Scene flags retain explicit Local/Inherit semantics. New authored roots start
Shown/Cast On/Receive On; new children start Inherit. A root using Inherit resolves
the scene defaults Shown/On/On, never stale stored bits or implicit false. Creation
policy and root resolution are separate from the low-level Flags constructor.

Resolve modes before render consumers use effective flags. A locally Shown child
can remain visible/illuminating under a Hidden parent. Geometry and light gather
must reject a node without pruning independently overridden children: use the
existing `FilterResult::kReject` semantics where appropriate. Do not globally
change `VisibleFilter`'s documented subtree-pruning behavior for other callers.
`IgnoreParentTransform` affects transform composition only.

Light eligibility is effective node visibility AND `Common().affects_world`.
Changing either preserves the other and stored intensity, colour and assignment.
Light Cast Shadows is independent of geometry casting/receiving. New authored
light Cast Shadows is explicitly On; native CommonLightProperties currently
starts Off and must not be mistaken for the authoring default. Authored mobility
is Realtime-only; existing other engine capabilities are not silently removed.

Effective flag, hierarchy, component/role/participation and relevant parameter
changes invalidate the owning directional resolver/frame selection and affected
lighting products. Cover visibility-only edits after cache population and role
changes while hidden/off. `Scene::Update` processing flags without resolver
invalidation is insufficient. Mutation APIs must make invalidation part of the
same operation, not require accidental later transform edits to repair state.

Targets: `Scene/SceneFlags.h`, `Scene/SceneTraversal.h`, node creation/update and
mutation dispatch; `Scene/Light/DirectionalLightResolver.*`; Vortex ScenePrep
and `SceneRenderer::BuildFrameLightSelection`; native/editor flag adapters.

## 3. Independent directional array and atmosphere assignments

`FrameLightSelection` owns an ordered collection of all eligible directional
lights, independently of atmosphere membership. Each record carries native light
identity, direction-to-source, colour/lux, effective light compensation, shadow
settings/flags and explicit atmosphere assignment. LightingService's forward
publication and deferred packets consume this same collection; neither elects
another light. Do not retain an optional single primary record as a parallel
fallback authority.

Publish an explicitly counted directional GPU array; forward consumers iterate
it and deferred lighting emits a fullscreen contribution per eligible directional.
Retain existing bounded-volume point/spot paths. Validate counts/allocation limits;
resource failure cannot silently discard a second source or promote another.
V0.1 qualification requires two simultaneous shadowed atmospheric sources and
ordinary role-None directional illumination, not unlimited-light performance.

`AtmosphereLightSlot` remains None/Primary/Secondary using existing native enum
and setter/getter names. None maps to neither atmospheric integration slot;
Primary/Secondary map to slots 0/1. Validate stored assignment uniqueness across
all directional components, even hidden/off ones, before computing active slots.
Conflicts fail with the occupying identity, never first-wins or brightest-wins.
Remove duplicate IsSun/Contributes/Sun-pointer authoring authority and implicit
fallback promotion through canonical migration.

Disabling a source retains its assignment. Secondary-only remains Secondary;
packing it into the first active GPU-array element is not permission to change
its atmospheric slot. Keep per-light shadow indices explicitly associated with
light identity, not inferred from direct-array order or atmosphere slot 0.

Both atmosphere assignments use existing two-source scattering and analytic disks.
None retains direct illumination without atmospheric membership. AngularSizeRadians
is the full analytic atmosphere disk diameter only in this V0.1 contract; preserve
its API names. It does not widen a BRDF highlight or shadow penumbra. Keep current
3x3 PCF, not an implicit PCSS/finite-source GGX expansion.

Targets: `Scene/Light/DirectionalLight.*` and resolver; `Vortex/Types/FrameLightSelection.h`;
`Lighting/Internal/LightGridBuilder`, `ForwardLightPublisher`,
`DeferredLightPacketBuilder`; `Lighting/Passes/DeferredLightPass`;
`Lighting/Types/DirectionalLightForwardData`; HLSL LightingFrameBindings and
`ForwardDirectLighting.hlsli` / `DeferredLightDirectional.hlsl`;
`Environment/Internal/AtmosphereLightState` and atmosphere/fog consumers.

## 4. Conventional shadows and receiver control

ShadowService consumes the complete directional selection and publishes one
per-view shadow family with explicitly indexed per-light CSM records/resources.
Only participating lights with Light Cast Shadows On request maps. Each has its
own cascade count/splits/distance/bias/resolution and identity association. Both
surface paths and applicable fog select the matching light's shadow data; no
slot-0-only shadow authority remains.

Retain established conventional depth format, reversed-Z comparisons, stable
cascade setup and 3x3 PCF. Apply authored depth bias once in the existing owner;
receiver normal/texel offsets remain separate. Existing advanced CSM fields,
contact shadows and per-light compensation gain complete source/cook/runtime
behavior; their current missing consumers are implementation work.

Geometry effective Cast Shadows determines occluder eligibility independently of
Light Cast Shadows. Authored Hidden excludes casting; visible off-screen casters
may remain. Blended casters and authored hidden-shadow/shadows-only modes are
outside V0.1. Existing opaque/masked material/raster behavior remains canonical.

Carry resolved Receive Shadows per instance through GPU draw data and the
GBuffer/forward surface contract. Deferred receivers read that GPU bit; forward
receivers use the corresponding instance value. Off bypasses conventional and
contact direct-shadow attenuation only, retaining direct illumination, AO, IBL
and casting. Do not turn the material Unlit or mutate a shared material. Instanced
merging must preserve differing receiver values, using per-instance data or
correct partitioning rather than applying one instance's value to the group.

Targets: `Vortex/Shadows/ShadowService`, `Passes/CascadeShadowPass`,
`Internal/CascadeShadowSetup`, `ConventionalShadowTargetAllocator`,
`ShadowCasterCulling`, `Types/ShadowFrameBindings.h`; ScenePrep `RenderItemData`,
`Extractors.h`, `Resources/DrawMetadataEmitter`; HLSL GBufferMaterialOutput,
BasePassGBuffer, directional/local direct-light and shadow receiver helpers.

## 5. ContactShadowCasterDepth and contact attenuation

The full fixed algorithm is authoritative in
[property-inspector.md#contact-shadow-algorithm](../../../../../design/editor/lld/property-inspector.md#contact-shadow-algorithm).
It defines the 0.25 m/16-sample ray, geometric-normal bias, linear-depth thickness,
self-hit rejection, edge/end fades and one-time multiplication with map visibility.
No new inspector tuning parameters accompany the existing bool.

ShadowService owns a dedicated per-view `ContactShadowCasterDepth` product/pass
at Stage 8, after frame light selection and before direct-light consumers. Reuse
DepthPrepass mesh/material/raster helpers without reusing its main colour depth
as the authoritative product. Render with the current resolved camera/content
rectangle and matching projection/jitter/depth conventions. Include only eligible
opaque/masked casters with effective Cast On; include editor-view-hidden geometry
and exclude authored Hidden, Cast Off and Blend.

Allocate and execute only when an active contact-shadow light needs the product.
No-contact frames allocate/run none and publish no usable contact binding.
Publish dimensions/content rectangle, depth reconstruction inputs, generation and
SRV through ordinary per-view lighting/shadow bindings. Keep resources alive until
GPU consumers drain; invalidate/reallocate on view destruction, resize or target
replacement. Forward/blended receivers use their own world position against the
same caster-depth product.

Shared forward/deferred attenuation checks light participation, Light Cast
Shadows, ContactShadows and resolved receiver eligibility. It applies to ordinary
role-None lights as well as both atmosphere sources. Contact depth excludes bars
and editor masks; conventional shadows retain off-screen coverage. Missing
required depth is a reported rendering failure, not a silently ignored checkbox.

Targets: new `Vortex/Shadows/Passes/ContactShadowCasterDepthPass` and its per-view
product; `SceneRenderer` scheduling/bindings; existing DepthPrepass mesh processor
and depth shaders; shared Lighting/Shadows HLSL attenuation helpers. The product
is normal rendering state; qualification readback uses an opt-in development
adapter and existing Graphics readback ownership.

## 6. Editor representation mask

Editing Hide is a per-view geometry/gizmo representation mask, not a node flag.
Apply it to editing-main-view depth/colour/representation submissions while
retaining frame caster/light eligibility. Do not alter conventional shadow maps,
ContactShadowCasterDepth, sky capture or other scene outputs. The workspace owns
persistence/Show All/child choices; engine view state owns scoped mask delivery.
No authored dirty/history/cook mutation or generic activation is introduced.

Targets: per-view ScenePrep refinement / prepared draw classification, InitViews,
base/depth/velocity/translucency main-view dispatch, and Interop view transport.
An editor-hidden geometry item can remain a caster without becoming an authored
Shadows Only material mode. Hidden authored cameras still resolve by identity.

## 7. Basic camera projection and aspects

Hydrate every authored camera component before selecting a view. Preserve valid
FOV/near/far/source aspect policy; invalid authored values fail instead of being
abs-normalized, reordered, or replaced with a synthetic camera. A demo navigation
camera is application-owned view intent, not an authored-camera fallback.

Auto is the new authoring default: preserve vertical FOV and derive effective
aspect independently for each target. Fixed preserves its stored positive ratio
and fits its complete frame in a centred content rectangle. Use one native
projection/fit helper for ordinary engine, editor and development views; do not
mutate the camera object or saved data to render multiple targets. Pixel rounding
and content/scissor mapping are deterministic and shared by both capture paths.
Fixed 4:3 into 1920x1080 yields 1440x1080 content at x=240, with side bars.

Bars are composed after post-processing and remain outside metering/foreground
grading. Preserve camera identity, local/world pose, vertical FOV and clipping;
`kVisible` does not disable camera function. Existing native physical exposure
remains; basic editor camera/Manual-Auto exposure does not add physical controls.

Targets: `Scene/Camera/Perspective`, source schemas/packed camera records,
DemoShell `SceneLoaderService::SelectActiveCamera/EnsureCameraAndViewport`,
`Vortex/SceneCameraViewResolver`, `Core/Types/ResolvedView`, InitViews/SceneTextures,
composition and Interop EditorView projection.

## 8. Captured-sky IBL

[Captured-sky IBL](editor-v01-captured-sky-ibl.md#1-ownership-and-scope) defines
processing, products, readiness, invalidation and numerical/sample details.
EnvironmentLightingService owns environment radiance/products; Stage 13
IndirectLightingService owns opaque indirect surface evaluation. This activates
its first real IBL subset, not the entire future GI feature family. Remove the
Stage-12 ambient/sky diffuse bridge for that capability when Stage 13 is wired;
never add the same IBL twice. Forward/translucent consumers share the same products
and BRDF semantics.

Both atmospheric sources affect captured radiance. The display-only clear colour
and editor Hide never become IBL inputs. With no active lighting sky, publish
zero contribution rather than stale products or invented ambient colour. Explicit
sky disks follow the companion capture policy separately from atmospheric
scattering. Diffuse-only specified-cubemap processing is not captured-sky specular
closure. No HDRI/probe/GI editor scope is inferred from native storage fields.

## 9. Exposure, grading and output

The exact field/default/ordering/math authority is
[environment-authoring.md section 6](../../../../../design/editor/lld/environment-authoring.md#6-tone-mapping-bloom-and-grading),
with exposure in section 5. Scene-linear foreground+bloom receives effective
exposure exactly once, then Rec.709 saturation, contrast about linear 0.18,
tone curve, content-ellipse vignette, existing DisplayGamma conversion and
background/coverage composition. Retain the stated independent golden cases.

None bypasses a tone curve only, clipping to SDR; it does not disable exposure,
bloom, grading, vignette or gamma. Debug-mode forced overrides are separate from
authored None. Background and Fixed bars remain outside foreground grading and
metering. Add real Saturation/Contrast/Vignette consumers to ordinary native
configuration/shaders. Do not build an editor-only colour pipeline.

Use native scene-system creation defaults, not generic PostProcessConfig/demo
fallback defaults. Explicit source values survive cook/load and camera/view
resolution. Auto exposure reads actual scene luminance and owns per-view history;
qualification telemetry does not supply its values.

Targets: `SceneRenderer::ResolveAuthoredPostProcessConfig`,
`Vortex/PostProcess/Types/PostProcessConfig`, service/frame bindings, TonemapPass,
ExposurePass and HLSL `Services/PostProcess/Tonemap.hlsl` / `Exposure.hlsl`;
SceneEnvironment hydration and native/editor source mapping.

## 10. Schema, migration and implementation order

Version native scene records/schemas for source flag modes, camera aspect policy,
canonical light assignment and complete light/shadow values. Preserve local flag
value versus inheritance mode, not only an effective boolean. Update CPU/HLSL
wire declarations, resource bindings and layout tests together; remove obsolete
single-light/role/reader paths rather than maintain a compatibility execution
branch. Canonical material-slot/precision extensions remain with their material
and content owners.

Implement in dependency order:

1. Canonical Scene flag/root resolution, assignment validation/mutation and
   versioned load/source mapping, including all cameras.
2. Frame directional array, forward/deferred publication, per-light CSM/fog
   shadow association and complete retained settings.
3. GPU receiver eligibility, dedicated contact depth and shared attenuation.
4. Auto/Fixed view projection and representation masks with correct caster
   retention and resource lifetimes.
5. Companion captured-sky products and Stage-13/forward consumers.
6. Post-process field application/output order and final integration.

Keep new passes within their existing capability owner and frame-product seams.
Update the relevant deferred-source TODOs only when modifying that engine owner,
using IDs/links from [the deferred record](editor-v01-deferred-capabilities.md).
Do not mark contact, receiver, two-source shadows, IBL or finite validation deferred.

## 11. Qualification

Focused native tests precede editor integration and rendered checks. Required
cases include root Inherit/defaults/local override/reparenting; visibility-only
cache invalidation; independent node/light shadow flags; differing receiver values
on shared/instanced geometry; no-contact allocation/dispatch absence; contact
self/edge/end/threshold behavior and editor-Hide caster retention; Primary-only,
Secondary-only, both shadowed, None fill, hidden/off role occupancy and conflicts;
all camera components, Auto resize and Fixed fit; IBL companion tests; exact
post-process golden/neutral/None/background/transparency cases.

Use actual native state/products/images and current resource generations. Existing
storage tests, single-source VTX evidence or two disks with one direct-light packet
cannot close this extension. Reconcile service LLDs and source comments with
implemented behavior without rewriting earlier historical evidence.
