# ED-M08: Node visibility, light contribution and shadows

Status: **source review complete; individual decisions pending**.
Baseline: `e27baf1ae`, reviewed on 2026-09-15. No visibility implementation or
new visibility policy was approved or changed during this review.

## 1. Correction and scope

The earlier proposed “Visible in render” switch combined several different
responsibilities. It also assumed ancestor-AND visibility when Oxygen actually
supports explicit local flag overrides. The resulting broad claim that Oxygen
“handles visibility inconsistently” was not precise enough. That proposal is
withdrawn.

The corrected findings distinguish:

1. Intended engine contracts: per-flag Local/Inherit values, geometry versus
   light shadow controls, and direct versus atmospheric light participation.
2. Consumer policies: which traversal/filter a rendering subsystem chooses.
3. Specific implementation gaps: a missing shadow-receiver consumer, a narrow
   directional-light selection limitation, and a visibility invalidation path
   requiring a regression test.

These are not grounds to replace the engine's node model with an unreviewed
group-disable flag. All previously approved camera, material, primitive and
development-build decisions remain unchanged.

## 2. What Oxygen does today

### 2.1 The knobs have different owners

| Control | Owner and meaning | Actual current behavior |
| --- | --- | --- |
| `kVisible` | Scene node: rendering eligibility | Geometry uses the node's resolved flag. Light collectors additionally use a subtree-pruning filter. It is not a general activation flag. |
| `kCastsShadows` | Scene node: its geometry is a potential occluder | Feeds geometry shadow-caster routing. It does not control whether a light casts shadowed illumination. |
| `kReceivesShadows` | Scene node: intended receiver control | Copied into CPU render items, but not consumed by active Vortex GPU metadata/shading. Stored value is not proof of a rendered effect. |
| `Common().affects_world` | Light: participates in world lighting | Checked separately by light collection. Native Directional/Point/Spot lights have no additional generic `Enabled` field. |
| `Common().casts_shadows` | Light: illumination uses shadows | Requests shadowing from that light; illumination can remain when shadowing is off. Independent of geometry caster eligibility. |
| `EnvironmentContribution` | Directional light: eligibility for environment systems | Separates atmospheric participation from the general directional-light list. Vortex currently couples direct illumination to atmosphere slot 0, as described below. |
| `IsSunLight` | Directional light: preferred authored primary-sun candidate | Requires environment contribution. An explicit primary atmosphere-slot binding can take precedence. |
| `AtmosphereLightSlot` | Directional light: explicit primary/secondary atmosphere role | Role selection, not visibility or a general on/off control. |
| `kIgnoreParentTransform` | Scene node: transform composition | Does not bypass visibility filtering or determine light participation. |
| Managed `IsActive` | Editor runtime-presence state | Means loaded/projected into the engine; synchronization rewrites it. It is not an authored activation control. |

The inspector's exact labels matter: **Contributes** appears inside the
**Environment** card and binds `EnvironmentContribution`; **Affects World** is
a different control. Calling both “contributes to scene” would hide an important
distinction. Source: [light inspector](../../../projects/Oxygen.Editor.WorldEditor/src/Inspector/DirectionalLightView.xaml),
[common light properties](../../../projects/Oxygen.Engine/src/Oxygen/Scene/Light/LightCommon.h),
[directional-light roles](../../../projects/Oxygen.Engine/src/Oxygen/Scene/Light/DirectionalLight.h).

### 2.2 Local and inherited flags are deliberate

Every native node flag has its own effective value, inheritance bit and deferred
update state. `SetLocalValue` disables inheritance; `SetInherited(true)` copies
the parent's effective value during the scene update. It is **not** an AND
between a stored local boolean and all ancestors.

After pending changes have been processed:

| Parent effective visibility | Child setting | Child effective visibility |
| --- | --- | --- |
| Hidden | Local Visible | Visible |
| Visible | Local Hidden | Hidden |
| Hidden | Inherit | Hidden |
| Visible | Inherit | Visible |

Native default visibility is local true. Cast/receive-shadow and ray-selection
flags start inherited; Static and IgnoreParentTransform start local false.
Editor source has boolean flags; cooked node records preserve those values as
packed bits, and DemoShell reconstructs local flags. Neither expresses the full native
Local/Inherit state. This is an authoring/cooking representational boundary to
decide, not permission to silently invent another inheritance rule.

Sources: [SceneFlags](../../../projects/Oxygen.Engine/src/Oxygen/Scene/SceneFlags.h),
[node defaults](../../../projects/Oxygen.Engine/src/Oxygen/Scene/SceneNodeImpl.h),
[flag propagation](../../../projects/Oxygen.Engine/src/Oxygen/Scene/SceneTraversal.h),
[cooked flag decoding](../../../projects/Oxygen.Engine/Examples/DemoShell/Services/SceneLoaderService.cpp).

### 2.3 Different consumers apply additional selection rules

**Geometry:** ScenePrep visits all nodes, then rejects a renderable whose own
effective `kVisible` is false. A locally visible child beneath a hidden parent
can render. This honors local override semantics.

**Lights:** `VisibleFilter` deliberately prunes an invisible node's entire
subtree. On a fresh collection, a light needs its node and traversed ancestors
to pass that filter, and must independently have `affects_world=true`. A local
visibility override cannot rescue a light that traversal never reaches.
This is an additional consumer policy; it is not what `SceneFlags` itself means.
Type-specific selection and shading conditions still apply. Zero intensity is
not equivalent to disabling participation: a light may remain selected with no
visible illumination.

**Cameras:** an explicitly selected camera needs a live node and camera
component. Geometry visibility is not checked. A camera does not become invalid
merely because its representation is hidden. Selecting the first camera instead
of the requested camera is a separate loading issue already assigned to M08.

**Explicitly hidden versus off-screen geometry:** hidden nodes/submeshes are
removed before shadow routing. A visible shadow caster outside the camera
frustum can remain in shadow-only submissions because its shadow may fall into
the camera's image. This is correct and is not evidence of failed visibility.

Sources: [ScenePrep traversal](../../../projects/Oxygen.Engine/src/Oxygen/Vortex/ScenePrep/ScenePrepPipeline.cpp),
[geometry and submesh filters](../../../projects/Oxygen.Engine/src/Oxygen/Vortex/ScenePrep/Extractors.h),
[VisibleFilter contract](../../../projects/Oxygen.Engine/src/Oxygen/Scene/Types/Traversal.h),
[light collection](../../../projects/Oxygen.Engine/src/Oxygen/Scene/Light/DirectionalLightResolver.cpp),
[camera view resolution](../../../projects/Oxygen.Engine/src/Oxygen/Vortex/SceneCameraViewResolver.cpp).

### 2.4 Light collection is not atmosphere selection

On a fresh directional-list rebuild, visibility/traversal eligibility and
`affects_world` are checked first. The resulting general list can contain a
directional light with environment contribution disabled. Atmosphere role
selection is a later operation:

- Explicit primary/secondary roles require environment contribution.
- The primary can fall back to a sun candidate, then an eligible environment
  contributor if no explicit primary was selected.
- A sun flag without environment contribution is rejected. Contract validation
  currently examines collected eligible lights, not every stored component.

Vortex then takes **only atmosphere slot 0** into its one direct-directional
record for forward and deferred surface lighting. Consequently a non-atmospheric
directional light can be in the general list but still produce no direct Vortex
illumination. This is a specific integration limitation. It should not redefine
the meaning of EnvironmentContribution or be patched by tagging every light as a
sun. [Resolver](../../../projects/Oxygen.Engine/src/Oxygen/Scene/Light/DirectionalLightResolver.cpp),
[Vortex selection](../../../projects/Oxygen.Engine/src/Oxygen/Vortex/SceneRenderer/SceneRenderer.cpp),
[forward direct-light evaluation](../../../projects/Oxygen.Engine/src/Oxygen/Graphics/Direct3D12/Shaders/Vortex/Services/Lighting/ForwardDirectLighting.hlsli).

### 2.5 Concrete gaps, with evidence limits

| Finding | Classification and next work |
| --- | --- |
| Geometry uses effective node visibility while lights prune invisible ancestors | Source-confirmed policy difference. Decide whether light eligibility should honor local overrides or retain the additional branch gate. Do not label all geometry inheritance broken. |
| `kReceivesShadows` stops in CPU state | Source-confirmed functional gap. If V0.1 offers this control, implement a real shader consumer; otherwise remove the ineffective authoring control. Do not equate it with light Cast Shadows. |
| Only atmosphere primary reaches direct directional shading | Source-confirmed integration limitation, separate from node visibility. Ordinary directional surface illumination needs its own path if that capability is selected. |
| Visibility-only mutation does not invalidate the directional resolver | Source-identified invalidation gap: resolver subscriptions cover light/transform/destruction, while the visibility command changes flags only. Test before fixing; cached membership can mask a fresh-collection truth table until another invalidation. |
| Editor `IsVisible` is called editor-local in prose but is serialized/cooked; no normal authoring command was found in the reviewed WorldEditor delivery paths | Model/UI/documentation gap. Establish separate local editor state and authored state before wiring controls. |

Receiver evidence: [CPU extraction](../../../projects/Oxygen.Engine/src/Oxygen/Vortex/ScenePrep/Extractors.h),
[GPU metadata emission](../../../projects/Oxygen.Engine/src/Oxygen/Vortex/Resources/DrawMetadataEmitter.cpp),
[directional shadow attenuation](../../../projects/Oxygen.Engine/src/Oxygen/Graphics/Direct3D12/Shaders/Vortex/Services/Lighting/DeferredLightDirectional.hlsl).
Invalidation evidence: [resolver subscription/cache](../../../projects/Oxygen.Engine/src/Oxygen/Scene/Light/DirectionalLightResolver.cpp),
[visibility command](../../../projects/Oxygen.Editor.Interop/src/Commands/SetVisibilityCommand.h),
[light-property invalidation](../../../projects/Oxygen.Editor.Interop/src/Commands/SetPropertiesCommand.h).

## 3. Practical ramifications

Consider a lamp composed of sibling BulbMesh and Light nodes under a visible
Lamp parent:

| Intended action | Appropriate control | What must stay independent |
| --- | --- | --- |
| See behind the bulb while editing | Editor-only hide for its representation | Saved lighting/visibility, cooking and standalone output |
| Turn off illumination but keep the bulb visible | Light Affects World/scene participation | Mesh visibility and stored intensity |
| Hide the bulb surface but keep illumination | Geometry visibility on BulbMesh; Light remains visible and participating | Do not hide the shared parent and assume its light traversal is unaffected |
| Keep illumination but remove shadows from this light | Light Cast Shadows off | Geometry can still cast shadows for other shadow-enabled lights |
| Stop the bulb geometry occluding light | Geometry Cast Shadows off | Light illumination and other geometry's caster eligibility |
| Hide a roof while retaining its shadow | Explicit shadow-only/hidden-shadow capability | Ordinary authored hiding currently removes Oxygen geometry from shadow submission |
| Add a directional fill without a second sun disk | Surface-light participation on, atmosphere role off | The current Vortex primary-only limitation must be addressed; visibility is not the solution |

General activation would additionally control component lifetimes/processing.
That is a different future decision for simulation and scripts, not a meaning to
attach to `IsActive` or a visibility checkbox now.

## 4. Industry comparison

### Editor state and hierarchy

| Engine | Relevant distinction |
| --- | --- |
| Unity 6 | Scene visibility is local editing state, separate from in-game visibility and picking. GameObject activation is broader: it affects components and descendants; component enablement is another level. |
| Unreal 5 | Editor hiding, Actor Hidden In Game and component visibility are distinct. Component visibility exposes explicit child propagation; actor hiding does not itself mean disabling collision/ticking. |
| Godot 4 | Node3D visibility requires visible ancestors; Light3D uses inherited visual visibility. This is a different model from Oxygen's explicit per-flag local override. |

Sources: [Unity editor visibility](https://docs.unity3d.com/6000.0/Documentation/Manual/SceneVisibility.html),
[Unity activation](https://docs.unity3d.com/6000.0/Documentation/ScriptReference/GameObject.SetActive.html),
[Unreal actor controls](https://dev.epicgames.com/documentation/unreal-engine/API/Runtime/Engine/AActor),
[Unreal component visibility](https://dev.epicgames.com/documentation/unreal-engine/API/Runtime/Engine/USceneComponent/SetVisibility),
[Godot Node3D](https://docs.godotengine.org/en/4.6/classes/class_node3d.html),
[Godot Light3D implementation](https://raw.githubusercontent.com/godotengine/godot/4.6-stable/scene/3d/light_3d.cpp).

### Light contribution and shadow controls

| Engine/pipeline | Participation | Shadows and receivers |
| --- | --- | --- |
| Unity URP | Light component enablement is independent of an object's mesh renderer | Light shadow type controls shadowing; Lit material Receive Shadows controls the receiver. |
| Unity HDRP | Adds independent diffuse, specular and volumetric contribution controls | Shadow-map enablement is separate. Do not assume URP's exact receiver UI is the HDRP contract. |
| Unreal 5 | Affects World is a bake-affecting property that cannot change at runtime; runtime visibility is another gate | Primitive Cast Shadow and light shadow settings differ. The named Receive CSM Shadows primitive flag is mobile-specific, not a universal desktop counterpart. |
| Godot 4 | Light3D has visual visibility plus light-energy/contribution properties, not a separate general `enabled` field | Light `shadow_enabled`, geometry caster mode and material `disable_receive_shadows` are distinct. |

Oxygen's runtime-mutable `affects_world` therefore does not acquire Unreal's
baking restrictions merely because the name matches. Select a clear Oxygen
label and lifecycle contract instead of copying names without their semantics.

Sources: [Unity Light API](https://docs.unity3d.com/6000.0/Documentation/ScriptReference/Light.html),
[URP lights](https://docs.unity3d.com/6000.0/Documentation/Manual/urp/light-component.html),
[URP Lit](https://docs.unity3d.com/6000.0/Documentation/Manual/urp/lit-shader.html),
[HDRP lights](https://docs.unity3d.com/Packages/com.unity.render-pipelines.high-definition@17.0/manual/reference-light-component.html),
[UE light properties](https://dev.epicgames.com/documentation/en-us/unreal-engine/python-api/class/DirectionalLightComponent?application_version=5.7),
[Godot lights](https://docs.godotengine.org/en/stable/classes/class_light3d.html),
[Godot material receiver control](https://docs.godotengine.org/en/4.6/classes/class_basematerial3d.html).

### Hidden shadows and atmosphere

All three engines have an explicit way to retain a hidden mesh's shadow:
Unity ShadowsOnly, Unreal Hidden Shadow (with casting enabled), and Godot
Shadows Only. Ordinary object hiding is not a substitute for that capability.
[Unity](https://docs.unity3d.com/6000.0/Documentation/ScriptReference/Rendering.ShadowCastingMode.ShadowsOnly.html),
[Unreal](https://dev.epicgames.com/documentation/unreal-engine/API/Runtime/Engine/Components/UPrimitiveComponent/bCastHiddenShadow?application_version=5.5),
[Godot](https://docs.godotengine.org/en/stable/classes/class_geometryinstance3d.html).

Atmospheric membership is separately meaningful. Unreal's Atmosphere Sun Light
does not define ordinary direct-light eligibility. Godot explicitly offers Light
and Sky, Light Only and Sky Only. HDRP also separates celestial/sky participation
from light contributions. URP selects its main light from Sun Source or the
brightest directional light; handling additional directional lights differs
between Forward and Forward+. This is not a universal sun-only rule.
[Unreal directional light](https://dev.epicgames.com/documentation/unreal-engine/directional-lights-in-unreal-engine),
[Godot sky modes](https://docs.godotengine.org/en/stable/classes/class_directionallight3d.html),
[URP main-light selection](https://docs.unity3d.com/6000.0/Documentation/Manual/urp/universalrp-asset.html),
[URP additional lights](https://docs.unity3d.com/6000.0/Documentation/Manual/urp/use-built-in-shader-methods-additional-lights-fplus.html).

Baked/captured products introduce another boundary. Hiding a light need not
remove already baked lighting; Godot explicitly separates visibility from
LightmapGI baking. Oxygen's approved captured-sky work must invalidate the
appropriate products when their contributing inputs change. A stale capture
must not be mistaken for a current direct-light contribution.

## 5. Complete V0.1 recommendation — awaiting one approval

The user rejected another list of unresolved subdecisions. The following is the
complete recommended behavior for this area. It is a proposal, not an approved
contract; there is no pending implementation choice about what these controls
are meant to do. The user can approve it or name the behavior to change.

| Control/action | Exact proposed behavior |
| --- | --- |
| **Hide in editor** (eye icon) | Hides selected geometry/gizmo representations and their descendants only in the editing view. Light contribution and shadow-caster eligibility remain unchanged. Store this per-user/project workspace state outside authored content; it never dirties a scene or triggers a cook. Show All clears these view overrides. Individual child hide choices survive hiding/showing a parent. |
| **Scene Visibility: Inherit / Shown / Hidden** | The saved runtime rendering policy. Root default Shown; child default Inherit. Shown/Hidden are local overrides of the parent. Effective Hidden suppresses that node's geometry, its shadow casting and its light contribution; a locally Shown child is evaluated independently and may remain visible/illuminating under a hidden parent. Both geometry and light consumers honor the resolved flag without an extra light-only ancestor-pruning rule. |
| **Light: Affects Scene** | The existing runtime participation meaning of `affects_world`, clearly labelled. Off stops illumination and atmospheric contribution from that light; it does not hide geometry on the node or change its intensity, colour, visibility or stored sun assignment. On participates only if the node's effective Scene Visibility permits it. Show a derived hidden-by-visibility explanation instead of implying that On guarantees illumination. |
| **Geometry: Cast Shadows** | Independent of illumination and receiving. Off leaves the surface visible/lit but removes it as an occluder. Root/default resolved value On; children may inherit or explicitly override. Applies to supported opaque/masked casters; blended shadow casting is excluded from V0.1. |
| **Light: Cast Shadows** | Off retains illumination but disables shadowing produced by that light. It does not change any geometry's casting/receiving settings or shadowing from other lights. Implement the control for every light offered by the V0.1 authoring contract; do not offer an ignored checkbox. |
| **Geometry: Receive Shadows** (Advanced) | Implement the GPU effect. Off skips direct-light shadow attenuation for that surface while it remains visible and lit; its own shadow casting is unchanged. Root/default resolved value On; children may inherit or override. This does not disable ambient occlusion or turn the material into Unlit. |
| **Scene: Sun = directional light / None** | One explicit atmospheric sun source. Other participating directional lights still illuminate geometry independently. Replace the duplicate Sun/Contributes authoring controls with this one source reference; derive native atmospheric role data through normal cooking/sync. A disabled/hidden selected sun produces no active sun contribution, retains the reference for re-enable, and does not silently promote another light. Sky-only and secondary/moon authoring are excluded from V0.1. |
| **Camera on a hidden node** | Remains selectable and usable; visibility hides its representation, not its camera function. No generic node-activation or simulation-disable control is added. Runtime-loaded state is derived, not a saved activation flag. |
| **Hidden versus off-screen geometry** | Authored Hidden does not cast shadows. Camera-frustum exclusion can retain a visible caster for shadows. Editor-only Hide retains caster eligibility. No authored Shadows Only/Hidden Shadow mode in V0.1; this is a decided exclusion, not an unresolved checkbox. |

Examples: hide a roof with the editor eye to edit the room while retaining its
shading; set its Scene Visibility to Hidden to remove it from runtime rendering
and shadows. Turn off a lamp's Affects Scene to extinguish illumination while its
mesh remains. Hide the whole lamp node through Scene Visibility to suppress both
geometry and illumination, subject to explicit child overrides. Set a child's
visibility to Shown to deliberately exempt it from an inherited hidden state.

The required implementation is concrete: a separate editor view mask; canonical
Local/Inherit serialization and mutation; matching geometry/light eligibility;
resolver invalidation on effective flag/hierarchy changes; functional receiver
shading; direct directional illumination independent of atmosphere selection;
one sun source and correct capture invalidation. Normal Undo/Redo/Save/cook/load
and engine-first/editor-second rendered tests cover every authored control.
The developer-only qualification tools remain outside normal shipping builds.

Migrate useful old content once. Used visibility/caster intent moves to explicit
canonical values. The previously ineffective receiver flag cannot establish a
former visual opt-out: migrate its prior rendered behavior as receiving shadows.
Resolve old sun-role combinations to a single explicit source, reporting conflicts
instead of guessing. Remove obsolete aliases/fields/fallback paths. There is no
backward-compatible execution branch.

This selects a complete policy rather than copying an engine wholesale: local
editor state and separate contribution/shadow responsibilities follow common
practice; explicit overrides retain Oxygen's useful native model. The exact
eligibility and V0.1 feature boundaries above are the proposed Oxygen decisions.

## 6. Evidence and limits

Source review covered native flags/defaults/update propagation, geometry and
light consumers, shadow metadata/shaders, camera resolution, cooked flag
decoding, and managed flags/inspector/interop delivery.

Ran **38 existing Debug native tests, all passed**: 18 flag inheritance/local
tests, 8 node-default/local-flag tests, 2 visibility traversal tests, 8 directional
role tests, one invisible-geometry filter test and one caster-routing test.
Evidence and executable hashes: `artifacts/ed-m08/visibility-research/evidence.json`;
per-suite XML/logs are beside it. Binaries were not rebuilt for this research.

These tests support their existing contracts. They do not provide a new
end-to-end rendered matrix of hidden parents, local overrides, light visibility
changes, receiver flags or cached atmosphere products. The invalidation gap
still needs a dedicated regression. No desktop UI was controlled, no scene or
material files were changed, and no new production behavior was implemented.
