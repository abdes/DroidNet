# Scene Authoring Model LLD

Status: **Final V0.1 contract**. Implementation evidence is tracked in
[IMPLEMENTATION_STATUS.md](../IMPLEMENTATION_STATUS.md).

Last Updated: 2026-10-20 01:32

## 1. Purpose and related contracts

The editor-owned model is the source of truth for hierarchy, components,
authored values and stable identities. Commands, Save, cooking and live runtime
projection consume the same state. The model owns no WinUI controls, GPU
resources or runtime handles.

This covers REQ-004 through REQ-009, REQ-036/037 and SUCCESS-002. Related
owners: [property inspector](property-inspector.md),
[environment authoring](environment-authoring.md),
[material editor](material-editor.md), [content pipeline](content-pipeline.md),
[documents and commands](documents-and-commands.md), and
[live sync](live-engine-sync.md).

## 2. Ownership

| Owner                         | Responsibility                                                     |
| ----------------------------- | ------------------------------------------------------------------ |
| `Oxygen.Editor.World`         | Scene/node/component values, DTOs, serializer and invariants       |
| WorldEditor document commands | Atomic validated mutations, history, dirty state and sync requests |
| Scene explorer/workspace      | Selection, layout, editor-only hiding and view preferences         |
| ContentPipeline/native cooker | Canonical source-to-native mapping and asset/slot identity         |
| Runtime/Interop/engine scene  | Lifetime-bound projection of authoring state                       |

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
lights. V0.1 authored editors cover Transform, Geometry, basic
PerspectiveCamera, OrthographicCamera, DirectionalLight, PointLight and
SpotLight. Existing other component data does not imply a new editor or release
capability.

## 4. Transform and camera state

Transform stores local position in metres, a finite nonzero normalized XYZW
quaternion and finite nonzero scale axes. Negative scale is valid. Rotation UI
uses degrees and the existing YXZ convention; no competing Euler source or raw
quaternion editor is added. World pose derives from hierarchy/native rules.

PerspectiveCamera stores vertical FOV in degrees, positive near/far distances
with near < far, and Auto/Fixed aspect policy. Auto is the creation default.
Fixed retains a positive width/height ratio, initially 16:9. Per-view Auto
aspect is derived, not saved. Fixed fits its full image without stretching or
cropping. The mode and retained ratio survive save/load and runtime
synchronization together. The one-time project upgrade marks existing explicit
ratios Fixed; newly created cameras use Auto. Resizing never overwrites the
retained ratio. OrthographicCamera stores OrthographicSize (half-height in
metres) with the same Auto/Fixed aspect policy and retained ratio; horizontal
extent is OrthographicSize × aspect. Both projections store aperture (f-number),
shutter rate and ISO, which set the camera EV that ManualCamera exposure uses.
Select cameras by authored ID; hidden camera nodes remain usable.

### Visibility, participation and shadow source state

Seven controls stay independent; each changes one effect only. General
activation is a separate lifecycle capability, not a meaning of visibility or of
runtime-loaded state.

| Control                  | Behavior                                                                                                             |
| ------------------------ | -------------------------------------------------------------------------------------------------------------------- |
| Editor Hide eye          | Editing-view mask (section 7); local workspace state, never authored                                                 |
| Scene Visibility         | Inherit / Shown / Hidden; geometry and light eligibility honor the same resolved value                               |
| Light Affects World      | Runtime contribution gate (native `affects_world`); Off stops the light without hiding its fixture or erasing values |
| Geometry Cast Shadows    | Inherit / On / Off; Off removes the geometry as an occluder while it stays visible and lit                           |
| Light Cast Shadows       | Off keeps the light's illumination without shadowing from it                                                         |
| Geometry Receive Shadows | Inherit / On / Off; Off skips direct-light shadow attenuation, not ambient occlusion or ordinary lighting            |
| Atmosphere assignment    | Per directional light None / Primary / Secondary (section 6)                                                         |

Each flag preserves its source mode independently of its resolved value.

| Node flag                | Source choices           | New root | New child | Root Inherit fallback |
| ------------------------ | ------------------------ | -------- | --------- | --------------------- |
| Scene Visibility         | Inherit / Shown / Hidden | Shown    | Inherit   | Shown                 |
| Geometry Cast Shadows    | Inherit / On / Off       | On       | Inherit   | On                    |
| Geometry Receive Shadows | Inherit / On / Off       | On       | Inherit   | On                    |

| Parent visibility | Child mode | Child effective visibility |
| ----------------- | ---------- | -------------------------- |
| Hidden            | Shown      | Shown                      |
| Shown             | Hidden     | Hidden                     |
| Hidden            | Inherit    | Hidden                     |
| Shown             | Inherit    | Shown                      |

Local overrides replace inheritance for that flag. Inherit copies the parent's
resolved value; this is not ancestor-AND activation. Reparenting recomputes an
inherited flag and preserves a local override. Parent edits never overwrite
child choices. Undo restores source modes/hierarchy before recomputation. For a
root, Inherit resolves the scene defaults Shown/On/On, never stale effective
bits or an implicit false. Root/child creation policies and this root resolution
are distinct from the low-level native Flags constructor and must be implemented
explicitly; existing constructor defaults are not evidence of compliance.

Effective Hidden removes that node's geometry, caster and light contribution. A
locally Shown child can remain visible and illuminating under a hidden parent.
Geometry/light consumers use the same resolved flag without a light-only
ancestor-pruning rule: light collection does not use `VisibleFilter`, which
stays for callers that deliberately prune hidden subtrees. Light Affects World
remains independent of stored intensity, visibility and atmosphere assignment;
neither setting rewrites the other. Zero intensity is not an activation
mechanism. A visibility-only change invalidates directional membership and the
captured lighting products that depend on it.

A lamp shows the separation: a fixture mesh and a light are siblings under a
visible lamp node. Hiding only the mesh leaves the illumination; turning Affects
World off extinguishes the light and keeps the fixture; turning off the light's
shadows keeps unshadowed illumination. Hiding the lamp node suppresses its
Inherit children, while an explicitly Shown child stays active.

Light Cast Shadows controls shadowed illumination from that light; node Cast
Shadows controls geometry as an occluder. Receiver Off removes direct-light
shadow attenuation without disabling direct light, ambient occlusion or casting.
Receive Shadows has a GPU consumer in both the deferred and the forward
surface-lighting paths. Opaque/masked casting is supported; blended casting and
authored hidden-shadow/shadows-only modes are excluded. Visible off-screen
casters remain eligible; authored Hidden removes a caster; editor Hide removes
it from the editing views only. `IgnoreParentTransform` concerns transform
composition, not visibility or light participation. Other optimization/selection
metadata does not automatically gain an inspector control because a DTO/native
flag exists.

## 5. Geometry, slots and primitives

Geometry references an asset URI. Instance material overrides use engine-owned
`MaterialSlotId`, an opaque 128-bit ID serialized as a canonical UUID. The
engine inventory supplies IDs, layout revision and per-LOD/submesh bindings; the
editor does not derive IDs from names/indices. Source retains geometry identity,
slot ID and the witnessed layout revision.

Assignment affects one instance/slot only. Clear removes its override and uses
the mesh-assigned material. Engine Default is a separate explicit assignment. No
slot/topology creation or deletion is offered. Reimport preserves proven
continuity; an unproven mapping retains the unresolved override/old context and
blocks only affected scene cook/qualification until explicit repair or clear.
The detailed contract is owned by [content-pipeline.md](content-pipeline.md).

Creation uses ten native-owned canonical recipes: Cube, Sphere, Capsule,
Cylinder, Cone, Plane, Quad, IcoSphere, Torus, and SubdividedCube under
Advanced. Exact metric defaults/orientation are in
[primitive recipes](property-inspector.md#primitive-recipes). All pivots are
centred in Z-up space. No managed generator/default list or editor-only
corrective rotation/scale exists.

## 6. Light components

A directional light owns colour, lux, source angle, Affects World, shadow
settings and `AtmosphereLightSlot`: None, Primary or Secondary. Preserve
existing native enum/setter/getter names. None retains ordinary directional
illumination without atmospheric membership. New light Cast Shadows is On: this
is an explicit authoring default, not the low-level native common-structure
default.

Each non-None slot has one stored occupant at most, including hidden/off lights.
Conflicting edits/imports/cooks fail without partial reassignment. Hiding or
disabling retains assignment; Secondary-only operation never promotes it.
Primary/Secondary mean neither priority nor a celestial body type. No editable
scene Sun pointer or independent IsSun/Contributes source remains; a scene
summary is read-only. Duplication never carries atmosphere membership: a copied
directional light starts at None, including when the source slot would be free
after a cut. Automatic promotion or re-acquisition hides the authoring mistake,
so the author reassigns the moved light explicitly.

Both sources support direct illumination, requested surface/fog shadows,
atmosphere and captured-sky diffuse/specular lighting. A Moon use case is
moonlight plus an analytic disk; lunar textures/phases/orbits and sky-only
creation remain outside V0.1.

Point and spot lights own colour, lumens, range, Affects World and shadow
settings. Point lights additionally own source radius (visual only in V0.1).
Spot lights additionally own inner and outer cone angles. All light types
support contact shadows, shadow bias/normal-bias/resolution, and exposure
compensation. Authored lights are Realtime-only; no Mixed/Baked authoring field
advertises an absent baking workflow.

Detailed fields/defaults are owned by the inspector and environment LLDs.

## 7. Editor state and runtime presence

Editor Hide is per-user/project workspace state outside authored content. It
filters geometry, shadows and viewport picking (plus descendants) in the editing
main view; the transform gizmo still renders when the node is selected. It drops
the node's geometry, depth and cast shadows from the editing views only
([settings](settings-architecture.md#51-workspace-visibility-storage-and-lifetime)).
Parent hide/show retains child choices; Show All clears view overrides. It
produces no scene dirty state, no cooking request and no runtime-role edit; the
eye toggle records an undo step. Other outputs and controlled qualification
targets do not consume the mask.

Explorer layout can remain scene-associated UI metadata, but never generates
runtime nodes or changes actual parenting. Historical `IsActive` means runtime
projection/loading status: derive it for the current runtime, do not serialize
it as activation. Visibility does not disable simulation/scripts.

## 8. Commands, Save and projection

Commands validate the whole edit, mutate atomically, advance revision, record
one undo entry and request convergence. Gestures use shared property sessions.
Runtime failure preserves valid authoring and Save; show a scoped error or
unavailable-preview state. Missing required capability is a qualification
failure, not successful fallback.

Save captures a coherent revision and writes atomically; newer edits remain
dirty. Cooking and sync consume canonical source. Observations come
independently from actual native state. Every editable field must survive
commands, Undo/Redo, Save/reopen, cooking/loading and projection. The domain
calls no runtime, picker, catalog-mutation or cooking API.

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
- Remove saved runtime presence, duplicate sun authority and authored
  Mixed/Baked mobility. Migrate ineffective mobility to its actual Realtime
  behavior.
- Convert single-slot overrides to stable IDs without remapping unresolved
  assignments or substituting engine Default.
- Use the single canonical IcoSphere identity. Migrate useful ArrowGizmo scene
  uses to ordinary geometry; retain tool-internal ArrowGizmo for its own
  purpose.
- Preserve useful camera composition under Auto/Fixed. Consolidate exposure
  mirrors into PostProcess.

## 10. Alternatives and rationale

| Alternative                                          | Reason not used                                                               |
| ---------------------------------------------------- | ----------------------------------------------------------------------------- |
| Ancestor-AND visibility                              | Discards intentional local overrides                                          |
| One scene Sun selector                               | Cannot express two simultaneous sources without competing state               |
| Slot mapping by name/index                           | Can silently redirect an override to another surface                          |
| Activation through IsActive                          | Confuses runtime presence with authored processing                            |
| Mixed/Baked choices without baking                   | Advertises nonfunctional lighting modes                                       |
| Editor hide stored as render visibility              | Alters cooked output during a view-only editing action                        |
| One visibility/activation/contribution/shadow switch | Changes unrelated rendering and lifecycle responsibilities together           |
| Light-only hidden-ancestor pruning                   | Resolves the same flag differently from geometry collection                   |
| Receiver flag stored without a GPU consumer          | No rendered effect                                                            |
| Zero intensity or hidden gizmo as light enablement   | Conflates magnitude, editor representation and participation                  |
| Binary16 emission with relaxed tolerances            | Changes the precision contract; float32 factors meet it                       |
| Clear background colour as ambient light             | Mixes display composition with environment radiance                           |
| Every public engine field as product scope           | A field needs a useful workflow, precise meaning and a complete consumer path |
| Old formats and aliases kept as compatibility paths  | Multiplies runtime contracts; one migration produces canonical content        |

Industry systems separate these concerns with different inheritance and UI
models.
[Unity Scene visibility](https://docs.unity3d.com/6000.0/Documentation/Manual/SceneVisibility.html)
is local editing state, while
[GameObject activation](https://docs.unity3d.com/6000.0/Documentation/ScriptReference/GameObject.SetActive.html)
drives a broader component lifecycle.
[Unreal component visibility](https://dev.epicgames.com/documentation/unreal-engine/API/Runtime/Engine/USceneComponent/SetVisibility)
exposes propagation explicitly, and
[Godot Node3D](https://docs.godotengine.org/en/4.6/classes/class_node3d.html)
uses ancestor-dependent visual visibility; none makes ancestor-AND a requirement
for Oxygen's explicit override model. Unreal's
[Affects World](https://dev.epicgames.com/documentation/en-us/unreal-engine/python-api/class/DirectionalLightComponent?application_version=5.7)
is editor-time and invalidates baked lighting; Oxygen's runtime gate shares the
name, not those restrictions.
[URP Lit](https://docs.unity3d.com/6000.0/Documentation/Manual/urp/lit-shader.html)
and
[Godot materials](https://docs.godotengine.org/en/4.6/classes/class_basematerial3d.html)
show receiver controls separately from light shadowing.

## 11. Qualification

Test identity/cardinality/finite constraints, duplicate names, hierarchy order,
inherited reparenting, local overrides, all slots and reimport conflicts, camera
Auto/Fixed projection, and new-object defaults against native recipes/system
defaults. The qualification fixture's overrides do not redefine creation
defaults.

Verify independent mesh/light shadows and receiver effect, hidden usable
cameras, two atmosphere sources and ordinary fill, cache invalidation, workspace
Hide without authoring changes and useful-content migration. Native rendered
cases precede editor cases. Stored values and unit/source checks do not
substitute for required rendered evidence.

## 12. V0.1 capability boundary

V0.1 is a static-scene and scalar-material authoring release with a complete
edit → history → Save → cook → native load → render path. Every exposed control
has an implemented effect. Runtime availability and diagnostic observations are
separate from authored state. [PRD sections 8–10](../PRD.md) define the release
envelope; the LLDs below own exact fields, defaults and conversions.

| Capability                    | V0.1 contract                                                                                                                                                                             | Owner                                                                                         |
| ----------------------------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | --------------------------------------------------------------------------------------------- |
| Hierarchy and transforms      | Stable IDs; create/delete/duplicate/rename/reparent; local position/rotation/scale; mixed selection; explicit handling of unrepresentable transforms                                      | This LLD, [property pipeline](property-pipeline.md)                                           |
| Primitive creation            | Ten native recipes (section 5); SubdividedCube under Advanced                                                                                                                             | [Inspector](property-inspector.md#primitive-recipes), [content pipeline](content-pipeline.md) |
| Mesh materials                | Independent instance override per existing slot; Clear restores the mesh default; stable SlotId and explicit repair of lost continuity                                                    | [Content pipeline](content-pipeline.md), [material editor](material-editor.md)                |
| Scalar materials              | Base colour/opacity, metallic, roughness, alpha mode/cutoff, sidedness, emission colour and HDR intensity; texture-dependent controls only when meaningful                                | [Material editor](material-editor.md)                                                         |
| Node visual state             | Inherit / local source modes; roots resolve Shown and casting/receiving On; children Inherit; explicit child overrides honored                                                            | Section 4, [inspector](property-inspector.md)                                                 |
| Editor Hide                   | Per-user/project editing-view mask; drops geometry and shadows there only; undoable; no dirty or cook effect                                                                              | Section 7, [settings](settings-architecture.md)                                               |
| Cameras                       | Perspective (vertical FOV) and orthographic (OrthographicSize), Auto/Fixed framing, pose, near/far, aperture/shutter/ISO; authored selection independent of navigation                    | [Inspector](property-inspector.md), [runtime](runtime-integration.md)                         |
| Directional lighting          | Realtime contribution, colour/lux, atmosphere disk diameter, independent shadowing, advanced shadow/CSM/contact controls                                                                  | [Inspector](property-inspector.md), [environment](environment-authoring.md)                   |
| Point and spot lighting       | Realtime contribution, colour/lumens, range, source radius (point), cone angles (spot), Affects World, shadow/CSM/contact/bias/compensation                                               | [Inspector](property-inspector.md)                                                            |
| Atmospheric lights            | Per-light None/Primary/Secondary on directional lights; two contributing, shadowed sources; role-None fill; no implicit promotion                                                         | Section 6, [environment](environment-authoring.md#7-per-light-atmosphere-assignment)          |
| Sky and environment           | Atmospheric sky, captured-sky diffuse/specular lighting, fog and local fog volumes, Sky Sphere and Sky Light, Manual/ManualCamera/Auto exposure, tone mapping, grading, bloom, background | [Environment](environment-authoring.md)                                                       |
| Scripts and Play              | Node scripts with declared parameters; Play in the standalone player                                                                                                                      | [Script authoring](script-authoring.md)                                                       |
| Viewports                     | One- to four-pane layouts with independent panes and persisted per-pane state                                                                                                             | [Viewport](viewport-and-tools.md)                                                             |
| Publication and live delivery | Explicit Save, incremental cook/publication, typed references, visible failure/pending state, coherent convergence                                                                        | [Cooking workflows](content-cooking-workflows.md), [live sync](live-engine-sync.md)           |

Native-supported advanced shadow/atmosphere/exposure fields stay in the field
tables; a missing cook or GPU consumer is implementation work, not a reason to
drop a field. Mixed/Baked light authoring is absent because V0.1 has no baking
workflow. Creation defaults are explicit in the owning tables.

Camera framing: Auto is the creation default and derives the aspect per target
with the vertical FOV unchanged and no saved-data change. Fixed fits the
authored ratio with centred letterbox/pillarbox bars, never cropping or
stretching; the bars are outside metering and post-processing. Imported explicit
glTF ratios map to Fixed, omitted ones to Auto.

Emission is linear RGB colour times a nonnegative HDR intensity stored as
float32; zero disables emission without erasing the colour. It is
self-illumination only: no emissive global illumination and no Unlit mode.

Captured sky lighting consumes the radiance of the sky and atmosphere, including
both assigned directional sources. The display-only clear background is not a
light source; with no lighting sky, captured sky illumination is zero and stale
products are invalidated. Exposure None bypasses the tone curve only.

The editor exposes what the engine renders. The engine has no generic node
activation, authored hidden-shadow or Shadows Only mode, blended-material shadow
casting, more than two atmospheric sources, sky-only light contribution, lunar
surface/phase/orbit features or material graph, so the editor offers none of
them. Mesh topology and material-slot creation are modeling tools with no engine
counterpart. Physics authoring is post-V0.1. The
[engine deferred-capability record](../../../projects/Oxygen.Engine/design/vortex/milestones/ED-M08/deferred-capabilities.md)
owns the source-local TODO IDs of engine features not yet authored.

There is one current source/cooked/runtime contract. Maintained development
tools migrate useful prior content and recook through the native producers; no
legacy reader, alias or duplicate behavior path remains (section 9).
