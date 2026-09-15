# Scene Authoring Model LLD

Status: **Final V0.1 contract**. Implementation evidence is tracked in
[IMPLEMENTATION_STATUS.md](../IMPLEMENTATION_STATUS.md).

## 1. Purpose and related contracts

The editor-owned model is the source of truth for hierarchy, components,
authored values and stable identities. Commands, Save, cooking and live runtime
projection consume the same state. The model owns no WinUI controls, GPU
resources or runtime handles.

This covers REQ-004 through REQ-009, REQ-036/037 and SUCCESS-002. Related owners:
[property inspector](property-inspector.md), [environment authoring](environment-authoring.md),
[material editor](material-editor.md), [content pipeline](content-pipeline.md),
[documents and commands](documents-and-commands.md), and [live sync](live-engine-sync.md).

## 2. Ownership

| Owner | Responsibility |
| --- | --- |
| `Oxygen.Editor.World` | Scene/node/component values, DTOs, serializer and invariants |
| WorldEditor document commands | Atomic validated mutations, history, dirty state and sync requests |
| Scene explorer/workspace | Selection, layout, editor-only hiding and view preferences |
| ContentPipeline/native cooker | Canonical source-to-native mapping and asset/slot identity |
| Runtime/Interop/engine scene | Lifetime-bound projection of authoring state |

World has no WorldEditor, WinUI, Runtime or Interop dependency. DTOs contain no
native pointers, handles, cache/view indices or current loading status.

## 3. Scene and node structure

```text
Scene: Id, Name, ordered RootNodes, Environment
  SceneNode: Id, Name, node flags, ordered Children
    TransformComponent (exactly one)
    GeometryComponent (zero or one)
    CameraComponent (zero or one)
    LightComponent (zero or one)
  ExplorerLayout (editor layout, never native nodes)
```

IDs are nonempty and unique within the document. Names are nonempty after
trimming; duplicate display names are allowed and never identify native nodes.
Root/child order is stable authoring state. Parent links and child membership
agree; cycles and cross-scene parenting are rejected before mutation. Deletion
removes a hierarchy. Reparenting preserves local transform; an existing explicit
preserve-world operation remains distinct.

Commands, deserialization and migration enforce component cardinality. Malformed
source does not gain silent duplicate-component selection or synthetic cameras/
lights. V0.1 authored editors cover Transform, Geometry, basic PerspectiveCamera
and DirectionalLight. Existing other component data does not imply a new editor
or release capability.

## 4. Transform and camera state

Transform stores local position in metres, a finite nonzero normalized XYZW
quaternion and finite nonzero scale axes. Negative scale is valid. Rotation UI
uses degrees and the existing YXZ convention; no competing Euler source or raw
quaternion editor is added. World pose derives from hierarchy/native rules.

PerspectiveCamera stores vertical FOV in degrees, positive near/far distances
with near < far, and Auto/Fixed aspect policy. Auto is the creation default.
Fixed retains a positive width/height ratio, initially 16:9. Per-view Auto aspect
is derived, not saved. Fixed fits its full image without stretching or cropping.
Select cameras by authored ID; hidden camera nodes remain usable. Physical-camera
authoring is outside V0.1; existing native physical exposure remains engine-owned.

### ED-M08 visibility and shadow source state

Each flag preserves its source mode independently of its resolved value.

| Node flag | Source choices | New root | New child | Root Inherit fallback |
| --- | --- | --- | --- | --- |
| Scene Visibility | Inherit / Shown / Hidden | Shown | Inherit | Shown |
| Geometry Cast Shadows | Inherit / On / Off | On | Inherit | On |
| Geometry Receive Shadows | Inherit / On / Off | On | Inherit | On |

Local overrides replace inheritance for that flag. Inherit copies the parent's
resolved value; this is not ancestor-AND activation. Reparenting recomputes an
inherited flag and preserves a local override. Parent edits never overwrite
child choices. Undo restores source modes/hierarchy before recomputation.
For a root, Inherit resolves the scene defaults Shown/On/On, never stale effective
bits or an implicit false. Root/child creation policies and this root resolution
are distinct from the low-level native Flags constructor and must be implemented
explicitly; existing constructor defaults are not evidence of compliance.

Effective Hidden removes that node's geometry, caster and light contribution.
A locally Shown child can remain visible and illuminating under a hidden parent.
Geometry/light consumers use the same resolved flag without a light-only
ancestor-pruning rule. Light Affects Scene remains independent; neither setting
rewrites the other.

Light Cast Shadows controls shadowed illumination from that light; node Cast
Shadows controls geometry as an occluder. Receiver Off removes direct-light
shadow attenuation without disabling direct light, ambient occlusion or casting.
Opaque/masked casting is supported; blended casting and authored hidden-shadow/
shadows-only modes are excluded. Visible off-screen casters remain eligible.
`IgnoreParentTransform` concerns transform composition, not visibility or light
participation. Other optimization/selection metadata does not automatically gain
an inspector control because a DTO/native flag exists.

## 5. Geometry, slots and primitives

Geometry references an asset URI. Instance material overrides use engine-owned
`MaterialSlotId`, an opaque 128-bit ID serialized as a canonical UUID. The engine
inventory supplies IDs, layout revision and per-LOD/submesh bindings; the editor
does not derive IDs from names/indices. Source retains geometry identity, slot
ID and the witnessed layout revision.

Assignment affects one instance/slot only. Clear removes its override and uses
the mesh-assigned material. Engine Default is a separate explicit assignment.
No slot/topology creation or deletion is offered. Reimport preserves proven
continuity; an unproven mapping retains the unresolved override/old context and
blocks only affected scene cook/qualification until explicit repair or clear.
The detailed contract is owned by [content-pipeline.md](content-pipeline.md).

Creation uses ten native-owned canonical recipes: Cube, Sphere, Capsule, Cylinder,
Cone, Plane, Quad, IcoSphere, Torus, and SubdividedCube under Advanced. Exact
metric defaults/orientation are in [primitive recipes](property-inspector.md#primitive-recipes).
All pivots are centred in Z-up space. No managed generator/default list or
editor-only corrective rotation/scale exists.

## 6. Directional lights and atmosphere

A directional light owns colour, lux, source angle, Affects Scene, shadow
settings and `AtmosphereLightSlot`: None, Primary or Secondary. Preserve existing
native enum/setter/getter names. None retains ordinary directional illumination
without atmospheric membership. New light Cast Shadows is On: this is an
explicit authoring default, not the low-level native common-structure default.

Each non-None slot has one stored occupant at most, including hidden/off lights.
Conflicting edits/imports/cooks fail without partial reassignment. Hiding or
disabling retains assignment; Secondary-only operation never promotes it.
Primary/Secondary mean neither priority nor a celestial body type. No editable
scene Sun pointer or independent IsSun/Contributes source remains; a scene
summary is read-only.

Both sources support direct illumination, requested surface/fog shadows,
atmosphere and captured-sky diffuse/specular lighting. A Moon use case is
moonlight plus an analytic disk; lunar textures/phases/orbits and sky-only
creation remain outside V0.1. Detailed fields/defaults are owned by the inspector
and environment LLDs. Authored lights are Realtime-only; no Mixed/Baked authoring
field advertises an absent baking workflow.

## 7. Editor state and runtime presence

Editor Hide is per-user/project workspace state outside authored content. It
filters geometry/gizmo representations and descendants in the editing main view
while retaining light contribution and caster eligibility. Parent hide/show
retains child choices; Show All clears view overrides. It produces no scene
dirty state, authoring history, cooking request or runtime-role edit. Other
outputs and controlled qualification targets do not consume the mask.

Explorer layout can remain scene-associated UI metadata, but never generates
runtime nodes or changes actual parenting. Historical `IsActive` means runtime
projection/loading status: derive it for the current runtime, do not serialize
it as activation. Visibility does not disable simulation/scripts.

## 8. Commands, Save and projection

Commands validate the whole edit, mutate atomically, advance revision, record
one undo entry and request convergence. Gestures use shared property sessions.
Runtime failure preserves valid authoring and Save; show a scoped error or
unavailable-preview state. Missing required capability is a qualification failure,
not successful fallback.

Save captures a coherent revision and writes atomically; newer edits remain
dirty. Cooking and sync consume canonical source. Observations come independently
from actual native state. Every editable field must survive commands, Undo/Redo,
Save/reopen, cooking/loading and projection. The domain calls no runtime, picker,
catalog-mutation or cooking API.

## 9. Canonical migration

Migrate useful content once to the canonical form and regenerate derived output.
Normal execution keeps no obsolete aliases, fallback readers or dual meanings.

- Convert old visibility/caster booleans to explicit modes preserving useful
  intent; never apply new-child defaults to old local choices.
- Old receiver flags had no rendered opt-out. Preserve prior receiving-On
  appearance rather than infer visual intent from an ineffective value.
- Preserve explicit atmosphere slots and unambiguous old sun intent. Report
  ambiguous/conflicting combinations; do not guess by name/brightness/order or
  collapse useful two-source content.
- Remove saved runtime presence, duplicate sun authority and authored Mixed/Baked
  mobility. Migrate ineffective mobility to its actual Realtime behavior.
- Convert single-slot overrides to stable IDs without remapping unresolved
  assignments or substituting engine Default.
- Map GeodesicSphere to IcoSphere and useful ArrowGizmo scene uses to ordinary
  geometry; retain tool-internal ArrowGizmo for its own purpose.
- Preserve useful camera composition under Auto/Fixed. Consolidate exposure
  mirrors into PostProcess; native physical exposure is not removed.

## 10. Alternatives and rationale

| Alternative | Reason not used |
| --- | --- |
| Ancestor-AND visibility | Discards intentional local overrides |
| One scene Sun selector | Cannot express two simultaneous sources without competing state |
| Slot mapping by name/index | Can silently redirect an override to another surface |
| Activation through IsActive | Confuses runtime presence with authored processing |
| Mixed/Baked choices without baking | Advertises nonfunctional lighting modes |
| Editor hide stored as render visibility | Alters cooked output during a view-only editing action |

## 11. Qualification

Test identity/cardinality/finite constraints, duplicate names, hierarchy order,
inherited reparenting, local overrides, all slots and reimport conflicts, camera
Auto/Fixed projection, and new-object defaults against native recipes/system
defaults. The qualification fixture's overrides do not redefine creation defaults.

Verify independent mesh/light shadows and receiver effect, hidden usable cameras,
two atmosphere sources and ordinary fill, cache invalidation, workspace Hide
without authoring changes and useful-content migration. Native rendered cases
precede editor cases. Stored values and unit/source checks do not substitute for
required rendered evidence.
