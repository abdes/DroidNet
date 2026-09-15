# Property Inspector LLD

Status: **Final V0.1 contract**. [Implementation status](../IMPLEMENTATION_STATUS.md)
records delivered behavior and remaining qualification.

## 1. Purpose and scope

The inspector edits Transform, Geometry/all existing material slots, basic
PerspectiveCamera, DirectionalLight and scene environment/post-processing.
Every editable field in this contract must work through command, Undo/Redo,
Save/reopen, cooking/loading and live native rendering. Advanced means supported
with secondary disclosure; it does not mean an unimplemented property.

Material values are edited in [Material Editor](material-editor.md); Geometry
only assigns identities. [Scene authoring](scene-authoring-model.md) owns source
state, [environment authoring](environment-authoring.md) owns scene settings,
[property pipeline](property-pipeline.md) owns typed edits/sessions, and
[live sync](live-engine-sync.md) owns revision-aware runtime convergence.

This covers REQ-005 through REQ-009, REQ-022/024/026/037 and SUCCESS-002/003.
Import UI, topology editing, general simulation activation, physical-camera
controls, orthographic authoring and transform gizmos are outside this inspector
slice. Native capabilities outside this surface remain with their engine owners.

## 2. Ownership and mutation flow

```text
control/edit buffer -> field VM -> shared edit session
  -> document command -> validate whole edit -> atomic domain mutation
  -> one history entry + scene revision/dirty state -> live sync request
  -> correlated field/section result
```

| Owner | Responsibility |
| --- | --- |
| WorldEditor inspector | Controls, edit buffers, mixed values, conditional presentation and diagnostics |
| Document commands | Target validation, mutation, history, revision, dirty state and sync request |
| World | Canonical node/component/source DTOs and invariants |
| Schemas | Property identities, scalar/range/enum validation and annotations |
| Runtime/Interop/engine | Current-revision projection and actual rendering |
| Asset/material owners | Discovery, picker, slot inventory, material create/open and publication |

VMs do not call Runtime, Interop or SceneEngineSync directly. They do not write
JSON, mutate catalog state or dehydrate scenes. A valid source edit survives
transient runtime failure; unavailable preview is reported, not mistaken for
failed Save. A required field lacking native behavior fails qualification.

## 3. Field descriptors and validation

A descriptor has a stable component/property ID, value type, reader/writer,
validator, label, unit, disclosure group, conditional visibility and engine
mapping. Existing IDs/API names remain unless a real contract change requires
replacement; cosmetic renaming creates no feature value.

- Reject NaN/Infinity in every numeric scalar and vector/quaternion component
  before clamping, conversion or mutation. Check converted values for overflow.
- Undefined enum values, invalid identities and impossible cross-field values
  fail with a field-scoped diagnostic; no partial edit remains.
- Tables specify stored bounds. Controls may use softer drag ranges. A finite
  value can clamp on commit where the table says Clamp; imported/saved invalid
  source is diagnosed rather than silently repaired during runtime load.
- Optional hidden-by-mode fields retain valid source values; when inactive they
  do not imply an effect. Missing implementation is not an inapplicability rule.
- Raw/Diagnostics shows useful identities, unresolved references and computed
  state only. There is no automatic Raw row for every DTO/native field and no
  editable control justified solely by a registry entry.

## 4. Node settings and editor-only Hide

| Control | Source type/default | Effect and presentation |
| --- | --- | --- |
| Scene Visibility | Inherit / Shown / Hidden; new root Shown, child Inherit | Node's resolved rendering eligibility; expose resolved value/origin separately |
| Geometry Cast Shadows | Inherit / On / Off; root/default On, child Inherit | Off removes geometry as an occluder, retaining visible surface and receiving |
| Geometry Receive Shadows | Inherit / On / Off; root/default On, child Inherit | Advanced; Off skips direct-light shadow attenuation while retaining direct lighting, ambient occlusion and casting |

Local overrides replace inheritance. A locally Shown child may render/illuminate
under a Hidden parent; no extra light-only ancestor gate is permitted. Reparenting
recomputes inherited values without overwriting stored modes. Multi-selection
shows mixed source modes independently of different effective values.
Root Inherit resolves the scene defaults Shown/Cast On/Receive On and displays
that origin. This resolution and the creation defaults are explicit engine/model
policies, not assumptions about the current low-level Flags constructor.

Effective Hidden removes the node's geometry, shadows and light contribution.
Hidden cameras remain selectable/usable. Geometry Cast/Receive flags are distinct
from Light Cast Shadows. Opaque/masked casting is supported; blended casting and
authored Shadows Only/Hidden Shadow modes are excluded. Visible off-screen
casters may remain in shadow submissions.

The eye control, **Hide in editor**, instead masks geometry/gizmo representations
and descendants in the editing main view. Light contribution/caster eligibility
remain unchanged. Store its choices in per-user/project workspace state outside
authored content. Parent hide/show retains child choices; Show All clears view
overrides. It never changes scene dirty/history, saved source or cooking demand.
Derived runtime presence (`IsActive`) is not an authored activation field.

## 5. Transform

All axes use common-component multi-selection and per-axis mixed values. Transform
is required and locked against removal.

| Field | Source/unit | Default | Bounds and commit | Active effect |
| --- | --- | --- | --- | --- |
| Local Position X/Y/Z | float32 metres | 0/0/0 | Finite | Local translation; parented world pose follows hierarchy |
| Local Rotation X/Y/Z | UI degrees; normalized XYZW quaternion source | Identity | Finite; wrap displayed angles to [-180,180]; reject zero/invalid quaternion | Existing YXZ yaw/pitch/roll convention; no competing Euler source |
| Local Scale X/Y/Z | float32 multiplier | 1/1/1 | Finite, nonzero; negative allowed | Scale including mirrored winding/normal handling |

Preserve the manipulated Euler axis during a gesture; commit its canonical
quaternion. Inspecting a transform does not normalize/rewrite source. Raw
quaternion, rotation-order, pivot and generic activation controls are absent.

## 6. Geometry and primitive recipes

| Field | Source/default | Behavior |
| --- | --- | --- |
| Geometry | Geometry asset URI; explicit chosen native recipe/import | Shared catalog picker, loading/error state and correct native geometry |
| Existing material slots | Instance override by MaterialSlotId; no override initially | Assign/clear each slot independently; Clear restores mesh-assigned material |
| Geometry Cast/Receive Shadows | Node modes from section 4 | Full independent effects; no material-editing substitute |
| Resolved LOD/submesh/slot counts, bounds, identity | Read-only | Useful diagnostics, not topology/LOD generation controls |

### Primitive recipes

The native catalog owns generator identity, recipes and defaults. The creation
palette contains ten canonical choices; SubdividedCube is Advanced. No independent
managed list or corrective editor rotation/scale is permitted.

| Primitive | Metric default and orientation |
| --- | --- |
| Cube / SubdividedCube | 1 m edge lengths |
| Sphere / IcoSphere | 1 m diameter |
| Cylinder / Cone | 1 m height and diameter along Z; cone tip +Z |
| Capsule | 2 m total height, 1 m diameter, axis Z |
| Torus | XY plane; 1 m outer diameter and 0.2 m tube diameter |
| Plane | 1 x 1 m XY ground surface, facing +Z |
| Quad | 1 x 1 m upright XZ card, facing -Y |

All use centred pivots in Oxygen's metre/Z-up coordinate system. Native buffers,
bounds, normals, UVs and winding must satisfy these recipes. ArrowGizmo is a tool
resource, not a creation choice. IcoSphere has one canonical identity. Useful
old ArrowGizmo scene uses migrate to ordinary geometry. Recipe changes require
matching API prose and native/editor rendered evidence.

### Existing material slots

`MaterialSlotId` is engine-owned, opaque 128-bit identity, serialized as a UUID;
indices/names are presentation metadata. The native inventory provides slot IDs,
layout revision and per-LOD/submesh bindings. Assignment stores the geometry URI,
slot ID, witnessed layout revision and material identity on that instance.

Clear removes the override; engine Default is an explicit separate choice.
Every nonzero slot receives the same command/history/Save/cook/live-binding and
failure recovery as slot zero. No adding/removing slots or modifying shared mesh
or material data is offered.

Reimport retains only proven identity continuity. Unproven matching keeps the
unresolved override and old context and blocks affected scene cook/qualification
until explicit repair or Clear; it never silently redirects a material by name
or position. The engine owns continuity/provenance, and the
[content pipeline](content-pipeline.md) owns publication and repair. Use the
[material contract](material-editor.md) for assignment/precision behavior.

## 7. Basic perspective camera

| Field | Source/unit/default | Bounds | UI/effect |
| --- | --- | --- | --- |
| Vertical FOV | float32 degrees, 60 | Finite; Clamp [1,179] | Convert once to native radians; controls vertical angle |
| Near Plane | float32 metres, 0.1 | >0 and <Far | Reject invalid cross-field edit |
| Far Plane | float32 metres, 1000 | >Near | Reject invalid cross-field edit |
| Aspect Mode | Auto / Fixed, Auto | Defined values | Auto derives aspect per target, retaining vertical FOV |
| Fixed Aspect Ratio | float32 width/height, initially 16/9 | Finite >0 | Shown in Fixed mode; retain ratio when toggling modes |

Fixed fits the complete authored frame into a centred content rectangle; no
stretch/crop. Compose bars after scene exposure/post-processing so they do not
influence metering. Auto fills its target and changes horizontal framing.
Resizing never rewrites saved data. Record effective per-view projection and
content rectangle separately. The fixed qualification target is not the editor
navigation camera. Select the exact authored camera, including second/parented
cameras; no synthetic fallback. A hidden camera node remains usable.

Physical aperture/shutter/ISO, sensor/lens/DOF and ManualCamera authoring are
excluded; existing native physical exposure is not removed or renamed.

## 8. Directional light

All numeric rows are float32 unless stated otherwise. Light colour is linear RGB;
colour-picker display conversion occurs at the UI boundary only.

| Field | Default/unit | Bounds | Disclosure/active effect |
| --- | --- | --- | --- |
| Affects Scene (`AffectsWorld`) | On; bool | Boolean | Primary; controls all contribution, separately gated by effective node visibility |
| Color | (1,1,1); linear RGB | Clamp each channel [0,1] | Primary; illumination and atmospheric colour |
| IntensityLux | 100000 lux | Clamp >=0 | Primary; stored illuminance |
| Cast Shadows | On; bool | Boolean | Primary; shadowing from this light, independent of geometry flags |
| AtmosphereLightSlot | None | None / Primary / Secondary | Primary; stable atmosphere assignment, independent of direct light |
| Atmosphere Disk Diameter (`AngularSizeRadians`) | 0.00935 rad full diameter; optional degree display | Finite, Clamp >=0 | Advanced, role other than None; analytic atmospheric disk size only |
| ExposureCompensation | 0 EV | Clamp [-10,10] | Advanced; effective light intensity multiplier 2^EV, without rewriting lux |
| Shadow.Bias | 0; dimensionless user bias | Clamp [0,10] | Advanced, Cast Shadows On; depth-bias effect |
| Shadow.NormalBias | 0.02 m receiver normal offset | Clamp >=0 | Advanced, Cast Shadows On; normal offset effect |
| Shadow.ContactShadows | Off; bool | Boolean | Advanced, Cast Shadows On; real contact-shadow contribution required |
| Shadow.ResolutionHint | Medium | Low / Medium / High / Ultra | Advanced, Cast Shadows On; resolution request bounded by renderer quality/capability |
| CascadeCount | 4; int | [1,4] | Advanced conventional shadows; active cascade count |
| SplitMode | Generated | Generated / ManualDistances | Advanced conventional shadows |
| MaxShadowDistance | 160 m | Finite >0 | Advanced conventional shadows; coverage/fade extent |
| CascadeDistances[0..3] | [8,24,64,160] m | Active distances positive and strictly increasing | ManualDistances only; inactive stored entries remain valid and retained |
| DistributionExponent | 3 | >=1 | Generated only; distribution of cascade coverage |
| TransitionFraction | 0.1 | [0,1] | Advanced conventional shadows; cascade transition |
| DistanceFadeoutFraction | 0.1 | [0,1] | Advanced conventional shadows; far-distance fade |

Light Cast Shadows On is an explicit creation default. The writer emits it;
the low-level native CommonLightProperties constructor currently defaults Off.
Realtime is the canonical authored mobility. Remove Mixed/Baked choices and the
redundant authored mobility field; do not advertise a nonexistent bake workflow.
Existing shadow/CSM/contact and compensation fields require complete cooker,
runtime and rendered effects. A current no-op is an implementation defect, not
permission to silently defer the field.

Atmosphere Disk Diameter retains the existing native AngularSizeRadians field
and setter/getter names. Show it only for Primary/Secondary assignment; explain
inactivity when atmosphere/disks or light participation is disabled. It does not
promise area-light specular highlights or penumbra widening. V0.1 retains the
conventional 3x3 PCF filter; PCSS/finite-source GGX is outside this contract, not
an implicit implementation behind the disk control. See the exact
[contact-shadow algorithm](#contact-shadow-algorithm) for the separate bool.

Each non-None atmosphere slot has at most one stored occupant, including hidden/
off lights. Reject occupied-slot edits with the occupant's identity and no
partial reassignment. Disabling/hiding retains assignment; Secondary-only never
promotes to Primary. The names imply no brightness priority or Moon type.
Keep native AtmosphereLightSlot and its setter/getter names; no A/B aliases.
Remove duplicate SunNodeId/IsSunLight/Contributes authoring through migration.
An optional scene summary is read-only.

Both atmospheric sources must illuminate and cast requested shadows in surface
paths/applicable fog, and drive atmosphere plus captured-sky diffuse/specular
lighting. None retains ordinary directional fill. A Moon use case is directional
moonlight and an analytic disk; lunar textures/phases/orbits, >2 atmosphere
sources and sky-only authoring are outside V0.1.

### Contact-shadow algorithm

`Shadow.ContactShadows` adds short-range screen-space occlusion to this light's
direct-shadow attenuation. It applies to every authored directional light,
including role None, independently of atmospheric membership. Evaluate it only
when Affects Scene and effective visibility permit contribution, Light Cast
Shadows is On, and the receiving surface's resolved Receive Shadows is On.
Receiver Off bypasses both conventional-map and contact attenuation, leaving
other lighting/caster behavior unchanged.

The fixed V0.1 engine profile is a 0.25 m ray, 16 point-depth samples, 0.001 m
origin normal bias and 0.002 m view-depth hit thickness. These are engine constants,
not new inspector sliders. For receiver world position P, use the normalized
world geometric normal after sidedness/orientation correction, excluding
normal-map perturbations: origin `P + 0.001 * N_geometric`. Trace toward the
light along normalized L. Sample distances are `0.25 * (i + 1) / 16` metres for
`i = 0..15`; no temporal random phase or accumulated history is introduced.

Project samples with the current camera into its content rectangle and point
sample caster depth at mip 0. Exclude the exact point-sampled start-depth value
to prevent origin self-intersection. Decode both depths with the same projection
to positive linear view-space metres. A hit requires
`0 < ray_view_depth - caster_view_depth <= 0.002`. Stop at the first valid hit.
Invalid depth is a miss for that sample; leaving the content rectangle or
crossing the camera near plane ends the remaining ray without a hit. Camera
bars never supply occluder depth.

Vortex owns a dedicated `ContactShadowCasterDepth` product/pass, using the current
camera/content-rectangle projection and existing depth-format/reversed-Z
conventions. Reuse normal DepthPrepass mesh/material/raster helpers, but keep the
product separate from main colour depth. Include opaque/masked geometry with
effective Cast Shadows On, including geometry hidden only by the editor mask;
exclude authored Hidden, Cast Shadows Off and blended casters. Allocate and run
this pass only when an active contact-shadow light needs it; ordinary frames
without contact shadows allocate/run none. A forward/blended receiver uses its
own world position with this same caster-depth product. No new inspector control
or alternative depth producer is introduced.

For hit distance d, fade occlusion over the last 20% of the ray with
`end_weight = 1 - smoothstep(0.20, 0.25, d)`. At hit UV, let e be the minimum
pixel distance to a content-rectangle edge and use `edge_weight = saturate(e/8)`.
Contact visibility is `1 - end_weight * edge_weight` for a hit and 1 for a miss.
Multiply conventional-map visibility by contact visibility once in the shared
forward/deferred direct-light path. This screen-space supplement cannot find
off-screen occluders or geometry behind the frontmost stored depth layer; it
does not replace conventional shadows.

The design basis is [Epic's contact-shadow depth-ray model](https://dev.epicgames.com/documentation/en-us/unreal-engine/contact-shadows-in-unreal-engine)
and the UE5.7 `DeferredLightingCommon.ush::ShadowRayCast` /
`ScreenSpaceShadowRayCast.ush::CastScreenSpaceShadowRay` reference. The explicit
metric profile and fades above are Oxygen's bounded policy, not a claim of
identical UE output. Preserve the established
[conventional CSM contract](../../../projects/Oxygen.Engine/design/vortex/lld/shadow-service.md#15-directional-csm-audit-conclusions)
and its separate caster/receiver bias handling.

Implement under Vortex Shadows/Lighting with shared receiver eligibility and
depth conventions. Qualify thin contact occluders, flat-plane self-shadow
rejection, edge/end fades, perspective/Auto/Fixed views, Primary/Secondary/None,
both light and receiver toggles, editor Hide, and native/editor parity. Independent
golden tests cover depths/bias/thresholds and fade endpoints; a stored bool or
code-presence assertion cannot establish the effect.

## 9. Scene settings and conditional UI

The scene/empty selection shows atmosphere, captured SkyLight, exposure,
tone-mapping/grading, bloom and background sections from
[environment-authoring.md](environment-authoring.md). No fake component is added
to a node. Optional Primary/Secondary summary is read-only. Mode-specific rows
are shown only when applicable; inactive values remain valid/persisted.

Use compact shared property sections, NumberBox/VectorBox/colour controls and
catalog pickers. Keep side-by-side labels/values, inline diagnostics and tooltips
for clipped content; no bespoke numeric controls or blanket Raw-field view.

<a id="91-ed-m07b-compact-layout-and-component-filtering"></a>

### Component layout and filtering

Component filters select types, not arbitrary first-node instances. All or no
filter shows all applicable sections; selecting a type shows that section and
selecting it again clears the filter. New node/scene/document selection resets
to All. Add/remove recomputes availability; removal of the filtered type resets
All. A filter never changes targets, implicitly adds components or edits source.

The header/component list uses content height, with up to four compact rows and
bounded overflow. Remaining height belongs to properties. Multi-selection gets
a compact count/type summary. Preserve field errors across filtering. Commit
valid pending text before changing sections; cancel unfinished drags. Late colour
picker completion cannot affect a new edit lifetime. Validate constrained docks
and 100%/150%/200% scale with real controls.

## 10. Commands, history and results

Use existing typed property/document command entry points. Extend their payloads
for node source modes, stable MaterialSlotId and per-light AtmosphereLightSlot;
old slotIndex is not canonical assignment identity. Operation kinds remain
owned string constants for transform, geometry, slot, camera, light, component
add/remove and environment edits.

One committed drag, text edit or menu pick produces one history entry. Intermediate
preview samples are coalesced; Escape restores the pre-gesture state. Wheel
samples use the existing 250 ms idle boundary. Mixed vector values are per axis;
bool/enum/source-mode values are indeterminate; untouched targets retain their
values. Validate the whole multi-edit, including slot occupancy, before mutation.
Transform cannot be removed; other cardinality rules are enforced consistently.

Runtime queue acceptance does not prove application/presentation. Report target/
revision-correlated results. Not running is a truthful skipped/unavailable preview
while Save remains valid. Missing/rejected required behavior is a field-scoped
implementation/qualification failure; no invisible default fallback can pass.
Unresolved asset identity is preserved with actionable loading/repair feedback.

## 11. Persistence, migration and dependencies

Canonical DTOs preserve node source modes, local transforms, geometry/all-slot
assignments, camera Auto/Fixed state, per-light assignment/settings and scene
systems. Save is coherent/atomic; later edits remain dirty. Migrate useful prior
intent once, regenerate derived content and remove obsolete readers/aliases.
Old ineffective receiver/mobility values migrate to prior receiving-On/Realtime
behavior; runtime IsActive and workspace Hide never become authored activation.

Inspector depends on document commands, World, shared controls/schemas and asset
identity interfaces. Commands own sync. Geometry does not embed scalar material
editing; Runtime/Interop remain behind their owning boundary.

## 12. Qualification and rationale

Every field has a meaningful non-default source case, native observation and
rendered effect where applicable. Test complete Undo/Redo/Save/cook/reopen,
finite/cross-field rejection, mixed edits, late callbacks, all slot identities,
primitive recipes, Auto/Fixed/hidden cameras, local visibility overrides,
independent light/mesh shadows and receiver effect, both atmosphere sources,
ordinary fill, and workspace Hide without source/cook changes. Engine rendered
cases precede editor cases; existing unit or storage checks are not presentation
proof. Invalidate resolver/captured products on all contributing changes.

Rejected alternatives: one scene Sun pointer loses two-source assignment;
ancestor-AND loses local overrides; name/index slot mapping can redirect materials;
Mixed/Baked without baking advertises no-ops; automatic Raw rows turn internal
storage into accidental product scope. Retained Advanced fields are implemented
and qualified rather than removed merely because their current path is missing.
Finite-source GGX/PCSS was not selected: the useful existing angle control is
atmospheric disk size, and established V0.1 conventional filtering remains 3x3 PCF.
