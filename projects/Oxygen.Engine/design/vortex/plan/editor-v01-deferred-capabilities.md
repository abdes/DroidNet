# Editor V0.1: deferred capabilities and source annotations

Status: **final V0.1 scope record**

This record defines post-V0.1 boundaries and their source-local tracking IDs.
Source annotations identify the actual capacity limit, omitted mode or authoring
gap. Missing implementation of a V0.1 feature remains current milestone work.

Authority: [authoring contract](../../../../../design/editor/review/ED-M08-v01-authoring-scope.md),
[visibility/shadow contract](../../../../../design/editor/review/ED-M08-node-light-visibility-review.md),
[Primary/Secondary celestial contract](../../../../../design/editor/review/ED-M08-celestial-light-authoring.md).

## Source annotation rule

When an implementation change touches a listed owner, place a short note beside
the actual capacity limit, omitted mode or policy boundary. Use a stable ID and
link to its section here. For example:

```cpp
// TODO(post-v0.1, EV01-SHADOW-BLEND): Add blended-light transmission semantics;
// the current opaque/masked depth path must not treat Blend as opaque.
// Scope: design/vortex/plan/editor-v01-deferred-capabilities.md#ev01-shadow-blend
```

- Describe exactly what is deferred and what already works.
- Keep one useful annotation at a decision boundary; do not repeat it at every
  caller or annotate unrelated untouched files merely to populate a checklist.
- Maintain annotations with the relevant implementation and remove them when
  the deferred feature is delivered.
- Keep Primary/Secondary identifiers and relevant existing API names. There is
  no naming cleanup task for neutral A/B terminology.
- Editor-only exposure belongs to editor work. If native behavior already
  exists, mark an authoring/persistence/qualification gap rather than inventing
  a missing runtime feature.
- Validation protocols, fixtures, telemetry and qualification runners remain
  in opt-in development targets.

The annotations are present at the role enum/direction boundary,
analytic-disk helper, light master gate, authored visibility rejection,
opaque/masked caster routing, node-state contract, light mobility/angle fields
and packed camera record.
These are comment-only tracking changes; they do not implement deferred or
current V0.1 functionality. Later implementation must keep them at the relevant
boundary and update/remove a note when its feature is delivered.

## Deferred capabilities

### EV01-ATM-COUNT

**More than two atmospheric directional contributors.** V0.1 supports Primary
and Secondary, including two suns or sun and moon. Expanding the count requires
coherent scene/schema, GPU payload, LUT/pass and shader changes; increasing one
constant is insufficient.

Source owners: [DirectionalLight.h](../../../src/Oxygen/Scene/Light/DirectionalLight.h),
[DirectionalLightResolver.cpp](../../../src/Oxygen/Scene/Light/DirectionalLightResolver.cpp),
[AtmosphereLightModel.h](../../../src/Oxygen/Vortex/Environment/Types/AtmosphereLightModel.h).

### EV01-CELESTIAL-SURFACE

**Lunar/body textures and illuminated phases.** Existing sky code draws analytic
disks with direction, angular size and luminance. V0.1 does not add a lunar
surface map, phase/orientation shading or a body-kind control without an effect.
Secondary is not intrinsically Moon; the existing convenience name does not
implement an astronomical body renderer.

Source owners: [DirectionalLight.h](../../../src/Oxygen/Scene/Light/DirectionalLight.h),
[Sky.hlsl](../../../src/Oxygen/Graphics/Direct3D12/Shaders/Vortex/Services/Environment/Sky.hlsl).

### EV01-CELESTIAL-MOTION

**Orbital/calendar-driven celestial motion.** Current directions follow scene
transforms. Future astronomy/controller authoring should drive those transforms;
the light resolver should not accumulate an orbital simulation responsibility.

Source boundaries: [DirectionalLight.h](../../../src/Oxygen/Scene/Light/DirectionalLight.h),
[DirectionalLightResolver.cpp](../../../src/Oxygen/Scene/Light/DirectionalLightResolver.cpp).

### EV01-LIGHT-SKY-ONLY

**Authored sky-only light contribution.** The V0.1 Affects Scene gate
controls all contribution; an atmosphere assignment additionally opts a light
into sky systems. A future sky-only mode needs explicit destination semantics
and consistent consumers. Secondary's current missing direct-light path is a
V0.1 defect to fix, not an implementation of sky-only authoring.

Source owners: [LightCommon.h](../../../src/Oxygen/Scene/Light/LightCommon.h),
[DirectionalLight.h](../../../src/Oxygen/Scene/Light/DirectionalLight.h),
[DirectionalLightResolver.cpp](../../../src/Oxygen/Scene/Light/DirectionalLightResolver.cpp),
[SceneRenderer.cpp](../../../src/Oxygen/Vortex/SceneRenderer/SceneRenderer.cpp).

### EV01-SHADOW-HIDDEN

**Authored Shadows Only / Hidden Shadow mode.** V0.1 authored Hidden suppresses
geometry and its casting. Internal shadow-only routing already exists for
off-camera visible casters; editor-only Hide must also retain caster eligibility
under the workspace-visibility contract. Neither is an authored hidden-shadow mode.

Source owners: [node flags](../../../src/Oxygen/Scene/Types/Flags.h),
[RenderableComponent.h](../../../src/Oxygen/Scene/Detail/RenderableComponent.h),
[visibility and shadow extraction](../../../src/Oxygen/Vortex/ScenePrep/Extractors.h).

### EV01-SHADOW-BLEND

**Shadow casting by alpha-blended materials.** Opaque/masked shadow routing and
masked alpha clipping already exist. Blend needs an explicit opacity/transmission
model; routing it through ordinary opaque depth would give incorrect shadows.
This does not defer rendering blended surfaces or their receiver effect.

Source owners: [DrawMetadataEmitter.cpp](../../../src/Oxygen/Vortex/Resources/DrawMetadataEmitter.cpp),
[ShadowDepthPass.cpp](../../../src/Oxygen/Vortex/Shadows/Passes/ShadowDepthPass.cpp),
[DirectionalShadowDepth.hlsl](../../../src/Oxygen/Graphics/Direct3D12/Shaders/Vortex/Services/Shadows/DirectionalShadowDepth.hlsl).

### EV01-NODE-ACTIVATION

**Generic node activation across systems.** V0.1 visual flags do not disable
camera use or promise simulation/script/physics lifecycle changes. Native handle
liveness and existing script-slot activation already have their own meanings.
Future generic activation needs a deliberate cross-system contract; it must not
be implemented by reinterpreting visibility or editor runtime-loaded state.

Source owners: [node flags](../../../src/Oxygen/Scene/Types/Flags.h),
[SceneFlags.h](../../../src/Oxygen/Scene/SceneFlags.h),
[SceneNode.h](../../../src/Oxygen/Scene/SceneNode.h),
[Scene.cpp](../../../src/Oxygen/Scene/Scene.cpp).

### EV01-CAMERA-PHYSICAL-AUTHORING

**Physical-camera authoring, persistence and qualification.** Native physical
exposure already exists: aperture, shutter rate and ISO produce camera EV, and
view initialization can consume it. The deferred work is editor controls and the
complete canonical saved/cooked/load path. Do not add a TODO claiming native
physical exposure is absent. Aperture does not by itself prove depth-of-field,
and shutter input does not prove motion blur.

Source owners/boundaries: [CameraExposure.h](../../../src/Oxygen/Scene/Camera/CameraExposure.h),
[Perspective.h](../../../src/Oxygen/Scene/Camera/Perspective.h),
[camera view resolution](../../../src/Oxygen/Vortex/SceneCameraViewResolver.cpp),
[view initialization](../../../src/Oxygen/Vortex/SceneRenderer/Stages/InitViews/InitViewsModule.cpp),
[packed camera records](../../../src/Oxygen/Data/PakFormat_world.h).

### EV01-LIGHT-BAKING

**Mixed/Baked light authoring and production.** The native mobility enum names
these workflows but does not supply a V0.1 light-baking pipeline. V0.1 authors
realtime lights; canonical source does not expose an ineffective mobility choice.
Future baking needs bake products, invalidation, runtime composition and a
complete authoring/publication workflow before those choices become available.

Source boundary: [LightCommon.h](../../../src/Oxygen/Scene/Light/LightCommon.h).

### EV01-LIGHT-FINITE-SOURCE

**Finite-source directional shading and variable shadow softness.** Current
angular diameter controls the analytic atmosphere disk. Conventional shadows
retain the existing fixed 3×3 PCF filter; directional surface shading does not
derive finite-source specular broadening from this angle. V0.1 labels the control
Atmosphere Disk Diameter and shows it for assigned atmosphere contributors.
Future finite-source shading requires a separate renderer contract and reference
images; the stored angular size alone does not implement those effects.

Source boundary: [DirectionalLight.h](../../../src/Oxygen/Scene/Light/DirectionalLight.h).
Existing shadow scope: [conventional shadow parity](VTX-M05D-conventional-shadow-parity.md).

## Other authoring exclusions: do not mislabel existing engine capabilities

Orthographic camera authoring, texture parameter/material-graph authoring, mesh
topology/material-slot creation and general physics/script authoring are outside
the V0.1 editor scope. Their corresponding native capabilities are not
all absent. When touching a related engine boundary, track an actual remaining
native/persistence requirement if one exists and link the owning editor LLD.
Do not create generic engine TODOs to implement already working orthographic
cameras, textures, parameterized geometry or scripting.

The final inspector and environment field tables define retained advanced
controls. Their required producer and renderer consumers belong to ED-M08;
they are not implicitly deferred by this record.

## Must remain V0.1 work

Do **not** defer either atmosphere slot's real direct lighting/shadowing, ordinary
directional fill lighting, captured-sky diffuse/specular lighting, functional
Receive Shadows, correct visibility invalidation, all material slots, scalar
emission, Capsule and canonical primitive defaults, finite input validation, or
Auto/Fixed camera framing. These are current obligations. A post-V0.1 TODO cannot
replace an implementation or validation gate for a retained V0.1 control.
