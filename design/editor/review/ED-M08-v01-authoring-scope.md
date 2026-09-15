# ED-M08: Professional V0.1 authoring scope

Status: **interactive decisions in progress**. The user approved development-only
qualification, no backward compatibility, captured-sky diffuse/specular lighting,
overrides for all existing material slots, and emission colour/HDR intensity on
2026-09-15. Other property choices below are recommendations, not an
approved package or implemented features. Decide them individually with the user.

## 1. Recommendation

Make V0.1 a dependable static-scene and scalar-material authoring tool. Every
visible control must have a useful meaning and survive edit, Undo/Redo, Save,
reopen, cook, native load and rendering. A renderer structure or existing
inspector registration is not sufficient reason to expose a property.

Prioritize ordinary scene composition, material assignment, camera framing,
direct and ambient lighting, and a small useful appearance surface. Keep low-level
shadow algorithms and atmospheric simulation parameters out of routine authoring.
This is a product judgement informed by the sources below; mature engines do not
define one mandatory inspector inventory.

Three additions offer more practical value than more renderer knobs:

1. Override **each existing material slot** of an imported mesh; retain mesh
   defaults and clear individual overrides without changing topology.
2. Author **emissive colour and intensity** for scalar materials.
3. Author a minimal **sky light** for ambient diffuse and specular lighting.
   Capturing the scene sky needs actual native implementation; it is the largest
   proposed addition, not a small editor binding change.

Recommend Manual and Auto exposure, basic colour grading and bloom. Implement
the effects that are selected. Remove rejected physical-camera and renderer
options from the canonical V0.1 model. Migrate useful old content to the selected
model; do not retain legacy fields or runtime fallbacks to accommodate it.

### Decision record

| Decision | State |
| --- | --- |
| Entire qualification workflow excluded from normal Debug/Release builds | Approved |
| No pre-V0.1 backward compatibility; migrate useful legacy content instead | Approved |
| Captured-sky lighting with diffuse and roughness-dependent specular reflection | Approved; engine and editor implementation pending |
| Material overrides for every existing mesh slot | Approved; independent per-instance assignment/clearing, clear restores mesh material; no slot creation/topology editing; implementation pending |
| Scalar emission colour and HDR intensity | Approved; zero intensity disables emission, no promise of lighting surrounding objects; implementation pending |
| Primitive picker; camera; visibility/transform semantics; directional/sun roles; shadow controls; exposure; grading/output; atmosphere controls | Discuss individually; recommendations below are not approval |

For each open decision, present the current Oxygen behaviour, recommended
canonical design, viable alternatives, implementation cost, industry references
and implications after V0.1. Ask one decision at a time. Do not convert a reply
about one area into approval of the rest of this document.

### Decision 1: all existing material slots — approved

The user chose all slots. Each scene instance can assign or clear an override
for every material slot supplied by its mesh. Clearing restores that slot's
mesh-assigned material; it does not rewrite the shared mesh or material asset.
Slot creation/removal and topology editing are outside this decision.

ED-M08 must implement the complete route: resolved slot inventory, inspector and
commands, Undo/Redo, canonical scene serialization, cooking/native descriptors,
runtime binding and missing-material recovery. Migrate useful prior slot-0
assignments into the canonical per-slot representation, then remove the old
single-override path. Slot identity and reimport matching require an explicit
contract before implementation; approval does not authorize guessing by name or
silently assigning an override to a different surface.

Qualification must include a multi-material mesh with independent overrides on
nonzero slots, two instances sharing the mesh, individual clear-to-mesh-default,
Save/reopen/cook/native load, Undo/Redo and missing-material recovery. The user
has approved the scope; none of these implementation or validation gates is
claimed complete by this decision.

### Decision 2: emission colour and HDR intensity — approved

The user approved scalar material emission. Expose colour and HDR intensity,
with zero intensity meaning off. Emission contributes self-illumination while
the material retains its ordinary PBR and alpha-mode behaviour. Bloom may add a
halo when enabled. This does not approve emissive surfaces lighting surrounding
geometry through GI, texture authoring, or a new Unlit shading mode.

Native cooking and rendering already carry an RGB emissive factor. ED-M08 must
extend the canonical managed material model, property commands, copying/edit
state, serialization, cooking and preview, including Undo/Redo and Save/reopen.
Useful old materials migrate with no emission. There is no shipping old-format
fallback. Define colour conversion, intensity units and finite HDR limits in the
implementation contract; the approval does not assert physical luminance units
or authorize silently relaxing numerical comparison thresholds for half-floats.

Verify zero/nonzero/HDR emission and changes under Manual/Auto exposure, with
ordinary lighting, opacity/masking/blending and bloom. Native-engine evidence
precedes editor workflow validation; implementation and evidence remain pending.

## 2. Approved: development qualification does not ship

The whole standalone validation workflow is development-only, including its
command, protocol, schemas, fixtures, expected-value generation, comparisons,
checkpoint scheduling, telemetry and qualification-specific capture ownership.

| Location | Responsibility |
| --- | --- |
| Normal editor modules | Real authoring, commands, persistence, cooking, ordinary runtime integration and user diagnostics. No M08 qualification dependency. |
| Normal engine modules | Correct production scene/loading/rendering behaviour. A new capability must have an independent runtime responsibility to ship. |
| Native development tools/tests | Opt-in qualification driver, protocol/schema ownership and instrumentation; reuse actual loading/rendering. No installed SDK protocol and no normal RenderScene validation CLI. |
| Managed development tools/tests | Opt-in orchestration, fixtures, semantic/image comparisons and test-host adapters. May reference production modules; production modules cannot reference them. |
| Separate development output tree | Tool/test binaries and packages, instrumented variants when necessary, captures and reports. Never replace normal editor or SDK artifacts. |

Do not embed engine-example schemas in core editor projects. Do not use `DEBUG`
as the isolation boundary: ordinary Debug must also remain clean. A feature flag
that merely hides UI while shipping its implementation does not satisfy this
decision. Qualification runs may use Release optimization in an explicit opt-in
development target.

The provisional four managed protocol files were moved out of production source
to ignored `artifacts/ed-m08/review-drafts/ContentPipeline/`. The project resource
changes were removed. No native schema, fixture or field manifest was created,
and no M08 build or test ran before this correction.

## 3. Recommended property surface

“Advanced” means supported and tested, with secondary disclosure. “Deferred”
means absent from the V0.1 authoring contract. Old representations migrate when
their content remains useful; they do not remain hidden compatibility state.

| Area | Primary V0.1 | Supported advanced controls | Deferred or removed from editable UI |
| --- | --- | --- | --- |
| Hierarchy and transform | Name, parent, local position, rotation in degrees, scale; create/delete/duplicate/reparent, reset and multi-selection | Deliberate world/local transform operations where already supported | Raw quaternion editing, rotation-order selection, pivots, generic Static/mobility and Ignore Parent Transform switches without a complete authoring contract |
| Visibility and editor state | Explicit **Visible in render**; separate editor selection lock/hide state where supported | Cast/receive shadows when the engine actually honours them | General Enabled semantics across future scripts/physics; do not repurpose `IsActive`, which means loaded in the engine |
| Geometry | Mesh identity, loading/missing state, Cube/Sphere/Capsule/IcoSphere/Plane/Quad/Cylinder/Cone/Torus, qualified static imports; Capsule is a proposed addition | SubdividedCube; approved existing material slots, assign/clear override, explicit mesh default and engine default | ArrowGizmo as an ordinary asset-creation choice; duplicate GeodesicSphere alias row; adding/removing slots, topology editing, LOD/collision generation |
| Perspective camera | Pose, **Vertical FOV**, near/far in metres, aspect/frame ratio, explicit authored-camera selection | Preserve non-default aspect and parented cameras | Physical-camera exposure, lens/sensor/DOF controls, orthographic authoring in this slice |
| Directional lighting | Enabled/affects scene, orientation, colour, illuminance in lux, Cast Shadows | Angular size, labelled according to its actual supported sun-disk/shadow effect | Contact Shadows without an implementation; mobility promises without matching runtime behaviour; per-light exposure compensation as a routine control |
| Sun selection | One clear scene-level sun reference; clearing it does not disable ordinary directional illumination | Sun-disk visibility | Competing independent sun toggles, additional atmosphere slots and moon authoring |
| Shadow quality | Dependable conventional-shadow defaults | Any artist-facing override needs an approved purpose and complete implementation | Cascade count, manual splits, distribution/fades, raw bias and resolution algorithms in the normal light inspector; no new generic project-settings panel |
| Sky and ambient light | Atmosphere enabled, sun reference, sky brightness, sky-light enabled/intensity | Aerial perspective strength/distance/start depth; captured-sky lighting | Planet radius, atmosphere height, individual scattering scale heights, ozone/planet workflows; HDRI/probe/GI authoring |
| Exposure | Manual EV, Auto mode, compensation; mode-relevant controls | Auto EV limits, adaptation speeds and metering | Physical/ManualCamera with no physical inputs; histogram implementation ranges, calibration/target-grey internals and routine arbitrary display-gamma editing |
| Appearance | Dependable ACES default, background colour with existing display-colour semantics | Existing Filmic/Reinhard choices, bloom intensity/threshold, saturation, contrast, vignette | LUT/volume authoring and shader-debug “None” as a normal tone-mapping choice; no colour-management redesign by hiding a slider |
| Scalar material | Base colour/opacity, metallic, roughness, Opaque/Mask/Blend, Double Sided, proposed emissive colour/intensity | Cutoff only for Mask; normal scale/occlusion strength only when a supported texture input exists and is effective | Texture parameter editing, material graphs, extended BRDF lobes; no inert texture controls for scalar-only materials |

The scope remains a static-scene/scalar-material release. Point/spot authoring,
animation, physics, scripting and general textured-material authoring do not enter
M08 merely because a mature engine supports them. Source/import limitations must
remain explicit. Expanding material overrides does not make imported native scenes
editable authored scene documents.

Proposed primitive picker: ten canonical generator choices, including a new
Capsule and keeping SubdividedCube under advanced creation. The smaller alternative
uses the nine existing authoring generators and defers Capsule. If accepted,
migrate GeodesicSphere references to IcoSphere
and remove the obsolete alias from authoring resolution. ArrowGizmo can remain
an internal tool resource for its current purpose; useful old scene uses should
migrate to ordinary geometry. No legacy picker/resolver path remains. Qualify
the approved visible choices and the migration's canonical outputs.

The native catalog currently has eleven names for ten generators; one is the
GeodesicSphere alias and one is the tool-oriented ArrowGizmo. Capsule has no
current generator/catalog entry and would need native implementation, cooking
and rendered qualification. It is useful for character/rounded-object blockouts
without adding physics or animation scope. Both [Unity's primitive menu](https://docs.unity3d.com/6000.0/Documentation/Manual/PrimitiveObjects.html)
and [Godot's primitive meshes](https://docs.godotengine.org/en/stable/classes/class_primitivemesh.html)
include Capsule; that supports the addition beyond copying Oxygen's current list.

These are generator choices, not a claim of distinct default silhouettes.
Oxygen's current Plane and Quad defaults are both four-vertex XY surfaces; their
generator parameters differ. Orienting one as a ground grid and the other as an
upright card requires a separate explicit decision and implementation. The menu
proposal does not silently change primitive topology, normals or orientation.

### Why these choices

Transforms and basic perspective framing are established authoring controls;
physical cameras are an additional system. Unity also separates editor visibility
from object activation, supporting an explicit distinction rather than reusing
Oxygen's runtime-presence flag. [Unity Transform](https://docs.unity3d.com/6000.0/Documentation/Manual/class-Transform.html),
[Camera](https://docs.unity3d.com/6000.0/Documentation/Manual/class-Camera.html),
[Scene visibility](https://docs.unity3d.com/6000.0/Documentation/Manual/SceneVisibility.html).

Mesh defaults and per-instance material overrides are established concepts.
Editing existing slots is valuable for imported meshes without requiring a mesh
editor. [Unreal mesh materials](https://dev.epicgames.com/documentation/unreal-engine/using-materials-with-static-meshes-in-unreal-engine),
[Godot surface overrides](https://docs.godotengine.org/en/stable/classes/class_meshinstance3d.html).

Lit materials expose surface colour, metallic/roughness behaviour, transparency,
sidedness and emission. Oxygen can remain scalar-focused while covering those
basic material intentions. Emission need not imply that surfaces illuminate nearby
objects through GI. [Unity Lit material](https://docs.unity3d.com/6000.0/Documentation/Manual/urp/lit-shader.html),
[Godot material emission](https://docs.godotengine.org/en/stable/tutorials/3d/standard_material_3d.html#emission),
[glTF material semantics](https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html#materials).

Shadow-control ownership varies: Unity uses render-pipeline/volume settings,
while Godot exposes directional-light splits. Oxygen does not need to make every
CSM parameter a per-light authoring obligation. Lux, colour, direction and shadow
participation have much clearer everyday purpose. [Unity HDRP light](https://docs.unity3d.com/Packages/com.unity.render-pipelines.high-definition@17.0/manual/reference-light-component.html),
[Unity URP shadow resolution](https://docs.unity3d.com/6000.0/Documentation/Manual/urp/shadow-resolution-urp.html),
[Godot directional light](https://docs.godotengine.org/en/stable/classes/class_directionallight3d.html).

Visible sky and ambient/reflection lighting are distinct. A sunlit scene needs
deliberate ambient illumination; adding a background alone is insufficient.
This motivates sky lighting ahead of specialist atmosphere parameters.
[Unreal atmosphere and Sky Light](https://dev.epicgames.com/documentation/en-us/unreal-engine/sky-atmosphere-component-in-unreal-engine),
[Godot environment](https://docs.godotengine.org/en/stable/tutorials/3d/environment_and_post_processing.html).

Auto-exposure limits/adaptation and simple grading are useful artistic controls.
They justify effective runtime support; their absence from current rendering is
not a reason to certify stored values as working effects.
[Unity exposure](https://docs.unity3d.com/Packages/com.unity.render-pipelines.high-definition@17.0/manual/reference-override-exposure.html),
[Unity colour adjustments](https://docs.unity3d.com/6000.0/Documentation/Manual/urp/Post-Processing-Color-Adjustments.html),
[Unreal colour grading](https://dev.epicgames.com/documentation/en-us/unreal-engine/color-grading-and-the-filmic-tonemapper-in-unreal-engine).

## 4. Production work identified by the source audit

These are implementation tasks if the recommendation is accepted. They are not
claims that a new renderer feature has passed visual validation.

| Priority | Change | Current evidence and ownership |
| --- | --- | --- |
| P0 | Make directional illumination independent of atmospheric sun membership; retain one explicit atmospheric sun | Vortex `SceneRenderer.cpp` builds a single directional selection from atmosphere slot 0 and skips other directionals. Fix selection and forward/deferred consumption; do not patch the editor by tagging every light as a sun. |
| P0 | Preserve selected authored-camera identity and projection | DemoShell `SceneLoaderService.cpp` currently chooses the first camera and rewrites aspect/clipping. Exact camera selection/preservation is real loading behaviour; the development driver must consume it without synthetic camera/sun injection. |
| P0 | Preserve every approved authored field across cooking and native load | `SceneDescriptorGenerator.cs` emits only part of registered directional data. Correct mappings for the chosen canonical surface; migrate useful former authoring intent instead of carrying deprecated expert fields. |
| P1 | Complete material-slot overrides end to end | `GeometryDescriptors.cs` and `SceneDescriptorGenerator.cs` use slot 0/one material reference. Changes need native descriptor/loader support, stable submesh-slot identity, commands, live sync, persistence, missing-asset recovery and per-slot clear semantics. |
| P1 | Add approved scalar emissive colour/intensity | `MaterialSource`/material editing lack emission. Native schema exposes `emissive_factor`; binder and forward/G-buffer code evaluate it. Extend authoring/read/write/cook and prove opaque/masked/blended effects. Native half-float storage also requires explicit finite-range/precision handling; do not silently relax comparison thresholds. |
| P1 | Implement minimal captured-sky ambient lighting, then expose it | `IblProbePass.cpp` rejects captured-scene as deferred. Existing specified-cubemap processing provides diffuse SH only: `IblProcessor.cpp` leaves prefiltered-specular/BRDF-LUT bindings invalid. This needs real sky capture, diffuse and roughness-dependent specular products, forward/deferred consumers, invalidation and readiness. Then add managed authoring, persistence, cook and sync. |
| P1 | Apply basic colour grading and vignette in Vortex | `ResolveAuthoredPostProcessConfig` in `SceneRenderer.cpp` and `Tonemap.hlsl` apply exposure/bloom/tone mapping/gamma, but no saturation/contrast/vignette. Add the actual GPU effect with defined ordering and defaults. Preserve the accepted clear-background/translucency composition. |
| P1 | Remove rejected or inert options from the canonical model | `ManualCamera` falls back to Manual EV without camera EV. Normal/occlusion edits lack an effect without texture refs. Contact Shadows/mobility need an active consumer before being offered. Migrate useful old values; remove compatibility branches and deprecated fields. |
| P2 | Define and implement authored visibility, then verify shadow participation, mirrored transforms and reparenting | `IsActive` is runtime presence and is overwritten by sync; it is not the proposed visibility control. Decide hierarchy/shadow semantics before adding the control. Keep editor state separate. Report transforms that introduce unrepresentable shear through the normal command rather than silently changing the pose. |

Key source locations:

- [Directional field registrations](../../../projects/Oxygen.Editor.WorldEditor/src/Documents/Commands/DirectionalLightDescriptors.cs)
- [Scene descriptor generation](../../../projects/Oxygen.Editor.ContentPipeline/src/SceneDescriptorGenerator.cs)
- [Runtime presence updates](../../../projects/Oxygen.Editor.WorldEditor/src/Services/SceneEngineSync.Documents.cs)
- [Material authoring model](../../../projects/Oxygen.Managed.Assets/src/Import/Materials/MaterialSource.cs)
- [Material fields](../../../projects/Oxygen.Editor.MaterialEditor/src/PropertyPipeline/MaterialDescriptors.cs)
- [Native scene selection and post-processing](../../../projects/Oxygen.Engine/src/Oxygen/Vortex/SceneRenderer/SceneRenderer.cpp)
- [Native SkyLight contract](../../../projects/Oxygen.Engine/src/Oxygen/Scene/Environment/SkyLight.h)
- [Native sky-light support checks](../../../projects/Oxygen.Engine/src/Oxygen/Vortex/Environment/Passes/IblProbePass.cpp)
- [Current diffuse-only IBL products](../../../projects/Oxygen.Engine/src/Oxygen/Vortex/Environment/Internal/IblProcessor.cpp)
- [Native camera loading](../../../projects/Oxygen.Engine/Examples/DemoShell/Services/SceneLoaderService.cpp)

## 5. Canonical model, migration and evidence

Backward compatibility is explicitly out of scope before V0.1. Do not ship
obsolete fields hidden in the model, old schema readers, alias resolvers,
fallbacks or dual rendering behaviour solely to keep legacy content working.
Use one canonical authoring/runtime contract after each approved change.

Migrate useful existing content once through maintained development tooling and
the normal cook pipeline. Update authoring identities/references together and
recook derived output; do not patch cooked binary descriptors. Keep migration
code and legacy input knowledge outside the shipping editor/runtime. Report
unrepresentable intent explicitly rather than inventing a silent approximation.
For example, non-default old gamma or unavailable physical-camera semantics
may require a deliberate conversion decision; they do not justify a permanent
runtime fallback. Remove obsolete compatibility code and its tests when the
canonical path replaces it.

Backups and atomic migration/recovery protect the user's work; they are not a
backward-compatibility feature. Current canonical data still needs stable
identities, normal Save/reopen, valid references and robust failure handling.
Do not discard useful content or alter test inputs just to obtain passing images.

Update PRD section 8 and the owning field tables only after approval. Keep earlier
milestone evidence at its recorded scope; M08 owns the newly identified production
fixes and new evidence. The development field manifest follows the approved table,
then checks that registrations neither omit required controls nor expose unapproved
ones. It does not copy all registrations into requirements.

For each retained/new feature, validate the native engine first, then the real
editor Save/cook/live workflow. Use the refreshed native examples where useful
and opt-in focused tools for deterministic evidence. Keep the exact 100-node /
1,000-logical-entry workload, unchanged numerical/image thresholds, ownership and
cancel/fault tests, and joint review. Add a normal Debug/Release build/package
inspection proving no development qualification dependency, embedded resource,
command registration, module initializer or instrumentation ships.

## 6. Interactive decision sequence

Captured-sky diffuse/specular lighting, all existing material-slot overrides,
scalar emission colour/HDR intensity, and the build/migration policies are
approved; do not ask for them again. Next discuss primitive choices, camera framing, scene participation
and transforms, light/sun roles, shadow controls, exposure, grading/output, and
atmosphere controls. Split a topic when it contains independent consequential
choices; do not request a blanket package approval.

After each choice, record the accepted canonical behaviour, required engine and
editor changes, migration work and validation cases. Update the owning PRD/LLD
table for that choice. Other proposals remain open until explicitly decided.
