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

## 5. Smaller decisions and recommendations

These are recommendations, **not a replacement blanket approval request**.

| Separate decision | Options and recommendation | Consequence |
| --- | --- | --- |
| 7A. Editor-only Hide | Add a local editing hide control, or omit it. **Recommend adding it independently of authored state.** Decide its exact view/workspace behavior without touching light contribution. | Look inside a scene without changing what gets cooked or rendered standalone. |
| 7B. Node visibility/inheritance | Retain explicit Local/Inherit semantics, or deliberately replace them with ancestor-AND. **Recommend retaining the useful existing model until a concrete authoring need justifies replacement.** | A forced-visible child may override a hidden parent under the first model; it cannot under the second. Defaults and UI exposure need their own explicit agreement. |
| 7C. Light contribution control | A runtime light participation switch, separately labelled and preserving intensity, or no user-facing on/off control. **Recommend the independent switch.** | Turning a light off should not hide its fixture mesh, change hierarchy or erase its intensity. Do not copy Unreal's editor-time, bake-invalidating Affects World contract merely to match its name. |
| 7D. Does node visibility additionally gate light eligibility? | Use the node's effective flag (honoring Local/Inherit), retain the extra ancestor-pruning gate, or let light participation alone decide. **Recommend honoring the effective flag for visual eligibility plus the independent light contribution gate; discuss this after 7B/7C.** | The hidden-parent/forced-visible-light case differs. This is the precise consumer policy that needs a decision, not a rewrite of all flags. |
| 7E. Shadows | Separate object casting, light shadowing, receiver opt-out and shadow-only geometry. **Recommend independent casting/shadowing; default receivers on; decide whether an advanced receiver opt-out and shadow-only mode are worth V0.1 scope individually.** | Receiver opt-out requires actual GPU work. Existing stored receiver values alone must not establish intended appearance during migration. |
| 7F. Atmosphere roles | Separate surface illumination from atmosphere/sun membership. **Recommend explicit destination labels and one clear V0.1 sun-binding workflow.** | A fill light can illuminate geometry without adding a sun disk; detailed role UI is a later individual choice. |

General activation, selection locking and editor gizmo visibility remain separate
topics. Do not add simulation behavior to visual flags. Do not preserve legacy
paths for compatibility: after each chosen canonical contract, migrate useful
content, remove obsolete interpretations, and qualify the intended behavior.

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
