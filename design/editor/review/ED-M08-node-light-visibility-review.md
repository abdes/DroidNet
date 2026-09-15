# Node visibility, light participation and shadows

Status: **final implementation contract**

## 1. Separate responsibilities

Node rendering eligibility, light contribution, atmospheric assignment, object
casting, light shadowing, receiving and editor-only hiding are independent
concepts. General activation is a separate lifecycle capability and is not a
meaning of visibility or runtime-loaded state.

| Control | Canonical behavior |
| --- | --- |
| Editor Hide eye | Masks geometry/gizmo representations and descendants in the editing main view; retains illumination and caster eligibility; local workspace state only |
| Scene Visibility | Inherit / Shown / Hidden source mode; geometry and light eligibility honor the same resolved value |
| Light Affects Scene | Independent runtime contribution gate; Off stops that light without hiding its fixture or erasing intensity/assignment |
| Geometry Cast Shadows | Independent Inherit / On / Off; Off removes geometry as an occluder while it remains visible/lit |
| Light Cast Shadows | Off retains illumination without shadowing from that light; other lights and geometry settings remain unchanged |
| Geometry Receive Shadows | Independent Inherit / On / Off; Off skips direct-light shadow attenuation, not ambient occlusion or ordinary lighting |
| Atmosphere assignment | Per-light None / Primary / Secondary, separate from ordinary illumination; defined in the [celestial contract](ED-M08-celestial-light-authoring.md) |

## 2. Native flag and authoring semantics

Each authored visual/shadow flag preserves a source mode, not only a resolved
boolean. Local values override the parent; Inherit takes the parent's effective
value. At a root, Inherit resolves against scene defaults: Shown, casting On and
receiving On. New roots use these explicit local defaults; children start Inherit.

| Parent visibility | Child mode | Child effective visibility |
| --- | --- | --- |
| Hidden | Shown | Shown |
| Shown | Hidden | Hidden |
| Hidden | Inherit | Hidden |
| Shown | Inherit | Shown |

Changing a parent never rewrites child source modes. Reparenting re-resolves
inherited flags and invalidates affected render/light/capture products. A local
Shown descendant remains eligible under a Hidden parent in both geometry and
light collection; light traversal adds no extra ancestor-pruning rule.

An effectively Hidden node contributes no geometry, shadows or illumination.
Its hierarchy/transforms remain, and a camera on it remains usable. Its light
contributes only when effectively Shown and Affects Scene is on. The inspector
shows the resolved value/origin or a hidden-by-visibility explanation. Node
handle liveness and editor runtime-loaded state are derived, not saved activation.

Sources and implementation seams:
[SceneFlags](../../../projects/Oxygen.Engine/src/Oxygen/Scene/SceneFlags.h),
[node defaults](../../../projects/Oxygen.Engine/src/Oxygen/Scene/SceneNodeImpl.h),
[flag update](../../../projects/Oxygen.Engine/src/Oxygen/Scene/SceneTraversal.h),
[light resolver](../../../projects/Oxygen.Engine/src/Oxygen/Scene/Light/DirectionalLightResolver.cpp),
[geometry extraction](../../../projects/Oxygen.Engine/src/Oxygen/Vortex/ScenePrep/Extractors.h).

## 3. Editor-only Hide

The [settings contract](../lld/settings-architecture.md#51-workspace-visibility-storage-and-lifetime)
defines the per-user/project storage, scene/node IDs and lifecycle. Hiding a
parent masks its descendants in the editing main view regardless of their
authored visibility mode. Showing that parent removes its local Hide entry but
retains individual child entries; Show All clears the scene's view overrides.

Hide never changes authored flags, light properties, source hashes, dirty/history
state or cooking demand. It retains light contribution and shadow-caster
eligibility; a roof can be hidden for editing while still shading the room.
Hidden geometry is omitted from editing-main-view rendering/picking, while
hierarchy selection remains available. Controlled qualification views omit local
Hide masks and preserve the current editing mask when their session ends.

## 4. Shadows and light controls

The geometry caster flag says whether an object can obstruct illumination. The
light shadow flag says whether that light uses shadow attenuation. Both are
required for a shadow interaction; neither is an illumination on/off switch.
Opaque/masked geometry casts in V0.1. Blended casting and authored Shadows Only /
Hidden Shadow modes are excluded. A visible caster outside the camera frustum
can remain in shadow submissions; authored Hidden removes it; editor-only Hide
retains eligibility.

Receive Shadows has a real GPU consumer in both surface-lighting paths. Off
preserves surface lighting, ambient occlusion and its own casting while skipping
direct-light shadow attenuation. Default resolved casting/receiving is On.

Affects Scene retains the native `affects_world` contribution meaning and is
labelled accordingly. It is independent of stored intensity, node visibility
and atmospheric assignment. Zero intensity is not an activation mechanism and
need not remove a light from resource selection. Light Cast Shadows defaults On
for newly authored lights and is distinct from low-level native constructor defaults.

A fixture mesh and light can be siblings under a visible lamp node: hiding only
the mesh leaves illumination; disabling Affects Scene extinguishes the light
while keeping the fixture; disabling the light's shadows retains unshadowed
illumination. Hiding the parent suppresses Inherit descendants, while explicit
Shown descendants remain active.

The exact light/shadow tuning fields and effects are owned by the
[property inspector](../lld/property-inspector.md) and
[environment contract](../lld/environment-authoring.md). Realtime lighting is the
V0.1 authoring workflow. Unsupported Mixed/Baked labels do not substitute for a
baking implementation.

## 5. Required engine/editor changes

The native flag container already provides Local/Inherit semantics. Geometry
collection honors effective values; current light traversal additionally prunes
hidden ancestors. Remove that extra selection policy for these consumers while
retaining VisibleFilter for callers that deliberately request subtree pruning.
Canonical source/cooked formats must carry inheritance modes, not flatten them
into the current boolean-only records.

Receiver state currently reaches CPU items without an active GPU consumer;
implement metadata/G-buffer/forward propagation and shadow attenuation behavior.
Visibility-only mutation must invalidate directional membership and dependent
captured products. Current light/transform/destruction notifications alone do not
cover that change. Slot-independent direct lighting follows the celestial contract.

The editor adds normal authored flag commands and a separate workspace-view mask;
it does not repurpose runtime-loaded `IsActive`. Migration retains used visibility/
caster intent as explicit canonical values. The previously ineffective receiver
flag cannot prove an old visual opt-out; migrate the prior rendered behavior as
receiving shadows. No compatibility reader or hidden fallback remains.

Source seams:
[caster metadata](../../../projects/Oxygen.Engine/src/Oxygen/Vortex/Resources/DrawMetadataEmitter.cpp),
[shadow attenuation](../../../projects/Oxygen.Engine/src/Oxygen/Graphics/Direct3D12/Shaders/Vortex/Services/Lighting/DeferredLightDirectional.hlsl),
[interop visibility mutation](../../../projects/Oxygen.Editor.Interop/src/Commands/SetVisibilityCommand.h),
[scene hydration](../../../projects/Oxygen.Engine/Examples/DemoShell/Services/SceneLoaderService.cpp),
[camera resolution](../../../projects/Oxygen.Engine/src/Oxygen/Vortex/SceneCameraViewResolver.cpp).

## 6. Eliminated alternatives and industry basis

| Alternative | Reason |
| --- | --- |
| One visibility/activation/contribution/shadow switch | Changes unrelated rendering and lifecycle responsibilities together |
| Unconditional ancestor-AND for native flags | Removes useful explicit local overrides |
| Light-only hidden-ancestor pruning | Overrides the same resolved flag differently from geometry collection |
| Editor Hide implemented by authored flag writes | Changes saved/runtime content and lighting instead of editing-view presentation |
| Treat receiver CPU storage as implementation | No rendered effect exists without the shader consumer |
| Treat intensity0 or hidden gizmos as light enablement | Conflates magnitude, editor representation and scene participation |

Industry systems separate these concerns but use different inheritance and UI
models. [Unity Scene visibility](https://docs.unity3d.com/6000.0/Documentation/Manual/SceneVisibility.html)
is local editing state; [GameObject activation](https://docs.unity3d.com/6000.0/Documentation/ScriptReference/GameObject.SetActive.html)
affects a broader component lifecycle.
[Unreal component visibility](https://dev.epicgames.com/documentation/unreal-engine/API/Runtime/Engine/USceneComponent/SetVisibility)
exposes propagation explicitly, while
[Godot Node3D](https://docs.godotengine.org/en/4.6/classes/class_node3d.html)
uses ancestor-dependent visual visibility. None makes ancestor-AND a requirement
for Oxygen's explicit override model.

Unreal's [Affects World](https://dev.epicgames.com/documentation/en-us/unreal-engine/python-api/class/DirectionalLightComponent?application_version=5.7)
disables contribution but is editor-time and invalidates baked lighting; Oxygen's
runtime gate does not inherit those restrictions because of its name.
[URP Lit](https://docs.unity3d.com/6000.0/Documentation/Manual/urp/lit-shader.html)
and [Godot materials](https://docs.godotengine.org/en/4.6/classes/class_basematerial3d.html)
show receiver controls separately from light shadowing. Explicit hidden-shadow
modes in those engines remain a separate capability from ordinary hiding.

## 7. Qualification

Verify native flag/consumer contracts and rendered effects before editor cases:
root/child defaults, root Inherit, inherited Hidden, local Shown overrides,
reparenting, independent contribution/casting/receiving, visibility cache changes,
opaque/masked/Blend behavior, off-screen versus Hidden casters, hidden usable
cameras and both atmospheric sources.

Then verify editor commands/history/Save/cook/load and local Hide restoration,
child choices, Show All, picking, project/scene/view lifetimes and zero source/
dirty/cooking side effects. Development captures observe canonical authored
visibility rather than the editing mask. Detailed ownership and image gates are
in the [standalone qualification LLD](../lld/standalone-runtime-validation.md).
