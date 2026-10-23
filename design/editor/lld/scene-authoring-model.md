# Scene Authoring Model LLD

Status: **Final V0.1 contract**. Implementation evidence is tracked in
[IMPLEMENTATION_STATUS.md](../IMPLEMENTATION_STATUS.md).

Last Updated: 2026-10-23 12:00

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

A node is an identity and a container of components; it has no other fields.
This is the engine's model: every native node is created with its
`ObjectMetadata`, `NodeData`, `GraphData` and `TransformComponent` components,
and its name, flags, hierarchy and transform live there. The editor mirrors it
component for component.

```text
Scene: Id, Name, ordered RootNodes, Environment
  SceneNode: Id, components
    Required, exactly one each, created with the node and never removed:
      MetadataComponent     name                      (engine ObjectMetadata)
      HierarchyComponent    parent, ordered children  (engine GraphData)
      TransformComponent    local transform           (engine TransformComponent)
      NodeFlagsComponent    node flags (section 4)    (engine NodeData)
    Optional, zero or one each:
      GeometryComponent     asset URI, overrides      (engine RenderableComponent)
      CameraComponent, LightComponent, ScriptingComponent
      ModelInstanceComponent  model scene URI, overrides (engine scene instance)
  ExplorerLayout (editor layout, never native nodes)
```

`NodeFlagsComponent` stores every engine node flag: the source modes of Scene
Visibility, Cast and Receive Shadows (section 4), and the local values of
`Static`, `RayCastingSelectable` and `IgnoreParentTransform`. Each is a property
of the schema's `Node` component, so Save, cooking and Live Preview carry all
six; a source mode travels as its wire value (Inherit 0, the local on value 1,
the local off value 2) and the cook writes it as `inherit`, `shown`/`on` or
`hidden`/`off`. The component resolves a flag's value and the node it comes
from; resolved values are never stored. The
inspector edits `IgnoreParentTransform` in the Transform section, since it
decides how the transform composes with the parent's, and `Static` and
`RayCastingSelectable` under Rendering's Advanced. The serializer nests nodes by
their hierarchy component, written after the node's other components.

IDs are nonempty and unique within the document. Names are nonempty after
trimming; duplicate display names are allowed and never identify native nodes.
Root/child order is stable authoring state. Parent links and child membership
agree; cycles and cross-scene parenting are rejected before mutation. Deletion
removes a hierarchy. Reparenting preserves local transform; an existing explicit
preserve-world operation remains distinct.

Commands, deserialization and migration enforce component cardinality. Malformed
source does not gain silent duplicate-component selection or synthetic cameras/
lights. Required components cannot be added, removed or duplicated. V0.1
authored editors cover Transform, node flags (Rendering), Geometry, basic
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

Nine controls stay independent; each changes one effect only. General
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
| Surface Visibility       | Per surface of one geometry instance (section 5); Off removes that surface only, as a draw and as an occluder        |
| Surface Cast Shadows     | Per surface of one geometry instance (section 5); Off removes that surface as an occluder only                       |
| Atmosphere assignment    | Per directional light None / Primary / Secondary (section 6)                                                         |

The node flags are properties of the node's `NodeFlagsComponent` (section 3)
and the only carriers of node visibility and shadow participation; no surface
value or renderable record repeats them. Every node has them, including group
nodes without geometry or light, whose values their Inherit children take. A
template node's flags come from its model and may be overridden like any other
property (section 5.2). Each flag preserves its source mode independently of
its resolved value.

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
composition, not visibility or light participation.

## 5. Geometry, surfaces, overrides and primitives

### 5.1 Surfaces

Geometry references an asset URI. A geometry asset owns its surfaces, each
identified by an engine-owned `SurfaceId`, an opaque 128-bit ID serialized as a
canonical UUID. Import allocates one surface per distinct source material of
the geometry, and each surface binds one submesh per LOD; separate source
objects import as separate nodes. A surface is therefore the finest authored
part of one geometry. The engine's surface inventory supplies IDs, the layout
revision and per-LOD/submesh bindings; the editor does not derive IDs from
names/indices. Each surface carries its asset defaults: Material, Visibility
and Cast Shadows. The geometry asset also carries a default LOD policy.
[content-pipeline.md](content-pipeline.md) owns the inventory, its provenance
and the import settings that set these defaults.

### 5.2 Parts, sources and overrides

Every authored property belongs to a component, or to a **part** of one: a
piece of a component defined by its asset, with a stable ID that asset owns. A
property's address is node ID, component type, part ID when it has one, and
property key.

| Owner of the value             | Part ID     | ID owner                       | Value when not changed       |
| ------------------------------ | ----------- | ------------------------------ | ---------------------------- |
| A component of an owned node   | -           | The scene that authors it      | Its stored value             |
| A surface of a geometry        | `SurfaceId` | The geometry asset             | The geometry asset's default |
| A component of a template node | -           | The model scene (model import) | The model scene's value      |

The schema states, per property, which **sources** may supply it. A property
with no source stores its value. A property with a source stores only what this
scene changes: a sparse override, where an absent key means the source's value.
There are two sources:

- **Asset**: a geometry asset supplies the defaults of its surfaces and of the
  geometry component's LOD policy to every node that references it.
- **Model template**: a node with a `ModelInstanceComponent` instances a model
  scene (engine `kModelScene`, `SceneInstanceRecord`) and brings in its
  hierarchy as template nodes. They are nodes with components like any other;
  they follow the model on reimport, the instancing scene stores only its
  overrides of the properties the schema marks overridable, and a template
  node's geometry keeps its asset as the next source.

Overrides are stored by the component that brings in the source: a
`GeometryComponent` stores the overrides of its asset-sourced properties, and a
`ModelInstanceComponent` stores the overrides of its template nodes. An
override's address is relative to that component: a surface ID, or a template
node's source ID followed by component type and, when there is one, part ID.
Surface addresses carry the geometry's witnessed layout revision.

The authored value of a property is the first present of: this scene's
override, the model template's value, the asset default, the schema default.
Node flags keep **Inherit** as a value of their own: an override can set Inherit
over a template's Hidden, so Inherit is never represented by an absent key.
After this authored resolution the engine applies hierarchy inheritance
(section 4). Reset on a property removes its override and shows the source's
value; it never writes the source's value as an override.

Overrides are retained across geometry replacement and reimport for the same
part IDs. A part whose ID is no longer proven (a surface or source node lost on
reimport) keeps its override as unresolved; it blocks only the affected cook
until the author repairs or removes it, as material assignments do today.
Model import therefore gives template nodes provenance-backed stable IDs, under
the same rules as surface IDs ([content-pipeline.md](content-pipeline.md)).

### 5.3 The scene property schema

One engine-owned schema declares every authored scene component once, with
its cardinality (`required` or `optional`), its part kinds, and each property:
name, key, value type, creation default, valid range, unit, the sources that
may supply it, its wire fields, for a sourced property its override ID and, for
an asset property, the `asset_type` it references. It is a YAML file of the Data
module, `Oxygen/Data/Schema/SceneComponents.yaml`, since it defines the cooked
scene format Data reads, validated by its JSON Schema
(`SceneComponents.schema.json`). Each platform
generates its own code from it, with its own tools, and none knows the others:

- the engine build runs `SceneSchemaCodeGen` (Python, in the uv workspace)
  before compiling Data; it validates the file and generates
  `Generated.SceneComponents.h`: the component and field enums, the override
  IDs, the override property table (ID, component, part kind, key, value
  type, asset type) with its lookups by ID and by address, and each
  property's default and range in field units. Data's scene validation, the
  content loader, Interop's appliers, the cooker and `SceneInstantiation`
  include it. It validates the
  units and emits none, since no engine UI reads them;
- the editor's Roslyn generator `SceneComponentsGenerator`
  (`Oxygen.Editor.Schemas/Generators`) reads the same file and generates
  Runtime's component, field and override enums, the scene component kinds
  with their cardinality, typed `PropertyId`s with their descriptor
  skeletons, each property's default, range, unit and display unit, and
  LivePreview's
  property-to-field table. It reports a problem of the file as a compiler
  error at its line. Readers, writers and validators attach to the generated
  ids; inspector labels, grouping, steps and soft drag ranges stay in the
  `.editor.schema.json` overlays.

The engine never refers to an editor project; the engine SDK installs the file
(`share/oxygen/schemas`), and the editor reads that installed copy, never the
engine source tree. Values are encoded once per value type (`bool`, `int`, `float`, `vec3`,
`enum`, `asset`, `string`, `lod-policy`), never per property.

The required components are `Metadata` (kind `metadata`, with `Name` of type
`string`), `Hierarchy` (kind `hierarchy`, with no properties: parent and
children are structure), `Transform` and `Node` (kind `node-flags`); every
other component is optional (section 3).

A property key is valid only on the component or part kind the schema lists,
so a scope the engine cannot apply cannot be written. Overrides travel on the
wire and in the cooked scene as their address (section 5.2), the property's
override ID and a typed value, through one override command and one cooked
override table; a new property adds a schema entry, not a command, record type
or hand-written mirror. An override ID is stable and unique across the schema,
and is never reused. Adding a property keeps existing source valid: an absent
key means the source's value, so no data migration is needed.

**Defaults and ranges.** The schema is the only place that decides a
property's creation default and its valid range. No component class, record
initializer, serializer, validator, inspector control, cooker or applier
states its own:

- `default` is the creation value, in the property's authored units: a number,
  three numbers for a `vec3`, an enumeration member name or a `lod-policy`
  object. Every property declares one except `asset` and `string` properties,
  whose value the creating command supplies. A node flag also declares
  `root_default`: a new child takes `default` (Inherit), a new root takes
  `root_default`, and an Inherit root resolves to `root_default` (section 4).
- `range` bounds a numeric property, each component of a `vec3` alike: `min`
  and `max`, each optional (absent means unlimited), `min_exclusive` and
  `max_exclusive` (inclusive when absent), and `nonzero`. Every `float` and
  `vec3` value is finite whether or not it declares a range, and every `enum`
  value is a declared member.
- `commit: clamp` lets a commit of outside values clamp a finite value into the
  range; the default, `commit: reject`, rejects it. A commit of outside values
  is an inspector edit, or a model import that maps imported values onto scene
  properties (V0.1 imports none). The inspector clamps once, through
  `PropertyChecks.Commit`, before it builds the edit, and the edit then refuses
  any value still out of range. Loading, hydrating the scene model, model and
  edit validation, cooking and the runtime always reject an out-of-range
  value: saved source is diagnosed, never repaired. The scene model's setters
  and `Hydrate` throw on such a value, so a scene in memory holds only values
  in range.
- Enumerations are declared once in the schema's `enums`, each member with its
  wire value; the editor generator reports a mapped CLR enumeration whose
  members differ.
- Units are declared once in the schema's `units`, each with an `id`, an
  `abbreviation` and a `name` (`metre`, `m`, `metres`). A property with a
  physical unit names it in `unit`: the unit of its stored value, the one its
  `default` and `range` use (`/inner_cone_angle_radians` is in `radian`,
  `/field_of_view_degrees` in `degree`). A property that UIs show in another
  unit declares `display_unit`; the only conversion is between `radian` and
  `degree`, the pair `degrees-to-radians` already defines. Dimensionless
  values, `bool`, `enum`, `asset` and `string` properties have no unit. UIs
  take every unit string from the generated schema, never from a literal of
  their own.

Defaults and ranges are written in authored units; the engine generator
converts them through the property's `encoding` (such as
`degrees-to-radians`) into field units, so engine consumers compare field
values. A rule that relates two properties (near below far, the spot cone
pair, increasing active cascade distances, representable light intensity, one
owner per atmosphere slot) is not a range: it stays in the component's rules,
in the editor and the engine. Engine constructor defaults of runtime
components are low-level API defaults for code that builds scenes, not
authored creation defaults (section 4).

Node templates place what they create, and that placement is not a property
default: a new point light's node stands at (0, 2, 0), a new spot light's at
(0, 3, 3) pitched down 45 degrees, and a new directional light's node, like an
unrotated node a directional light is added to, takes
`DirectionalLightComponent.DefaultLocalRotation`. The template writes the
node's transform; the transform's schema defaults stay the origin, no rotation
and unit scale.

Every property reports the same range failures:
`SceneDiagnosticCodes.PropertyValueNotFinite`, `PropertyValueOutOfRange` and
`PropertyValueUndefined`. An edit wraps them in
`SceneEditCodes.PropertyOutOfRange`, and the cooker reports
`scene.descriptor.property_out_of_range` with the property key. The field
tables of the [property inspector](property-inspector.md) and
[environment authoring](environment-authoring.md) LLDs are the contract that
the schema's defaults and ranges implement.

V0.1 properties with an asset source:

| Property     | Key              | Part               | Override ID | Asset default                | Resolution in scene preparation                                              |
| ------------ | ---------------- | ------------------ | ----------- | ---------------------------- | ---------------------------------------------------------------------------- |
| Material     | `/material`      | Surface            | 1           | The mesh-assigned material   | Binds to every submesh of the surface                                        |
| Visibility   | `/visible`       | Surface            | 2           | On unless the asset says Off | Draws when the node resolves Shown and this is On                            |
| CastsShadows | `/casts_shadows` | Surface            | 3           | On unless the asset says Off | Casts when the node resolves casting On, this is On and its Visibility is On |
| LodPolicy    | `/lod_policy`    | Geometry component | 4           | Fixed finest LOD unless set  | Selects the LOD per view                                                     |

The surface properties take the node flags' names and keys (`Visibility`,
`/visible`; `CastsShadows`, `/casts_shadows`): a surface stores a `bool`,
since it has no hierarchy, where a node stores a source mode. Receive Shadows
stays a `NodeFlagsComponent` property; no surface value changes receiving. A
hidden node therefore hides every surface, and a hidden surface never shows
under a Shown node. A surface whose Visibility is Off casts no shadow either,
because authored shadows-only modes are excluded (section 4). The LOD policy
is the engine's: Fixed (LOD index, clamped to the LOD count), Distance
(per-LOD thresholds and a hysteresis ratio) or Screen-space error (per-LOD
enter-finer and exit-coarser thresholds).

The inspector shows each value with its origin (this scene, model, asset or,
for Inherit, the parent it resolves from) and a reset for overridden values
([instance overrides inspector](../ui/instance-overrides-inspector.md)).

### 5.4 Extending the model

| Change                         | Extension                                                             |
| ------------------------------ | --------------------------------------------------------------------- |
| A new property                 | A schema entry, its overlay annotation and its engine resolution rule |
| A new component                | A schema component with its cardinality; its engine counterpart       |
| A new mesh-rendering component | Reuses the surface part and inventory unchanged                       |
| A new part kind                | A part kind in the schema, keyed by an ID its defining asset owns     |
| A new source                   | A link in the source chain; overrides and reset are unchanged         |

Skeletal meshes are the first new part kinds. A skinned-mesh component
references its skinned geometry and its skeleton; its surfaces are ordinary
surface parts; a bone is a part keyed by a skeleton-owned bone ID and a morph
target a part keyed by a geometry-owned morph target ID, never a name or index.
The engine has neither a skeleton asset nor stable bone IDs yet (joints are
indices), so these part kinds arrive with them. The animation a node plays is a
component property, not an override.

### 5.5 Material assignment

Material assignment affects one instance/surface only. Reset removes the
Material override and uses the source's material. Engine Default is a separate
explicit assignment. No surface/topology creation or deletion is offered.
Reimport preserves proven continuity; an unproven mapping retains the
unresolved override/old context and blocks only affected scene
cook/qualification until explicit repair or clear. The detailed contract is
owned by [content-pipeline.md](content-pipeline.md).

### 5.6 Primitives

Creation uses ten native-owned canonical recipes: Cube, Sphere, Capsule,
Cylinder, Cone, Plane, Quad, IcoSphere, Torus, and SubdividedCube under
Advanced. Exact metric defaults/orientation are in
[primitive recipes](property-inspector.md#primitive-recipes). All pivots are
centred in Z-up space. No managed generator/default list or editor-only
corrective rotation/scale exists.

### 5.7 Editor representation of parts and overrides

The editor mirrors the engine: a geometry component is the engine's renderable
component, whose state per surface (`SurfaceId`) is applied to every LOD and
submesh binding of that surface. The schema is the only declaration of what a
component, a part and an override can hold.

**Schema.** A component may declare `parts`, each with a PascalCase name, a
kind and its properties. A property may declare `sources` (`asset`, `model`):
the property is then stored as a sparse override and declares an
`override_id`. A part property declares at least one source and has no
`fields`; overrides reach the engine through the override table and command,
never field by field. The Geometry component declares the `Surface` part (kind
`surface`) with `Material`, `Visibility` and `CastsShadows`, and its own
`LodPolicy` (section 5.3). Both generators emit the override IDs: the engine's
for the cooked table, `SceneInstantiation` and Interop, the editor's for
`RuntimeOverrideId` and the typed ids.

**Identity.** `PropertyId` (component kind, optional part kind, JSON pointer)
and `PropertyId<T>` are vocabulary of `Oxygen.Managed.Core`, so the scene model,
the cook and Live Preview address properties without a dependency on the
editor's schema services. The generated `SceneSchema` belongs to
`Oxygen.Editor.World`, next to the enumerations its ids are typed with. A part
property is `SceneSchema.Geometry.Surface.Material`, typed
`PropertyId<AssetPath>`. `SceneSchema.Overridable` maps every property with a
source to its value type; nothing else may be overridden.

**Values.** An override holds a value of its property's schema type, the CLR
type of its typed id: `bool`, `int`, `float`, `Vector3`, a mapped enumeration,
`AssetPath`, `string` or `LodPolicy`. `lod-policy` is a schema value type like
`vec3`, not a general structure: the engine policy is a variant with
variable-length thresholds, so each platform implements its value once (the
engine's `LodPolicyRecord`, the editor's `LodPolicy` record in World). Its
JSON, the same in the saved form, the descriptors and the inventory, is
`{ "type": "fixed", "lod_index": 0 }`,
`{ "type": "distance", "thresholds": […], "hysteresis_ratio": 0.1 }` or
`{ "type": "screen_space_error", "enter_finer_sse": […], "exit_coarser_sse": […] }`,
with at most 7 thresholds, since a geometry has at most 8 LODs. Script parameter values stay a separate,
tagged representation: a script declares its parameters at run time and a
value may outlive its declaration, whereas a schema property's type is fixed
at compile time.

**Store.** A component with sourced properties implements
`IOverridableComponent` and holds one immutable `SourceOverrides`:

- `PartId(string Kind, Guid Id)`, an asset-owned part identity;
- component-level values by `PropertyId`, for properties without a part;
- part entries by `PartId`, each with the layout revision the part was
  witnessed in and its values by `PropertyId`;
- `TryGet`, `With` and `Without`; an entry left with no values is removed, so
  an absent key always means the source's value.

The component replaces the whole value on each change, so one property change
notification covers every override and an edit's inverse is the previous
value. It also names the asset that owns each part kind's IDs: a geometry
component's surfaces are its geometry's.

An override's address carries no asset: a part ID is the asset's, and the
override applies to whatever asset of the component has that part. It stays
across geometry replacement and applies to a new geometry with the same part
ID; otherwise it is kept, unresolved.

**Saved form.** Values are written as their property's JSON and read back
through `SceneSchema.Overridable`. An unknown key, a wrong part kind or a value
of the wrong type rejects the scene, as does a member the node or geometry
records do not declare, so a stale file fails to open instead of being saved
without what it held:

```json
"Overrides": {
  "Parts": [ { "Kind": "surface", "Id": "…", "LayoutRevision": "…",
               "Values": { "/material": "asset:///Content/Materials/Red.omat" } } ]
}
```

**Edits.** `SetOverrides` writes or resets (a null value) overrides of nodes,
each addressed by property, part and the witnessed layout revision. It
validates that the property is overridable, that the part matches the
property's part kind, and the value through the property's override rule (one
per overridable property), and records the previous values as its inverse. A
write built from an asset's part inventory names that asset, and the edit
refuses a node whose component no longer uses it; the inverse names none, so
undo always restores. Clearing unresolved overrides is the same edit. The inspector, the cook and Live Preview read the
store through typed ids; asset-typed values are found by the store, so asset
references, retargeting and relocation need no rule per property.

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
- Convert each surface material assignment into a Material override of its
  surface part. Remove rendering, lighting and level-of-detail slots,
  node-scope slots and LOD/submesh-targeted overrides from source; none had an
  authored or rendered effect.
- Rename material slots to surfaces in source, descriptors, provenance and
  cooked data; the owner recooks.
- Cooked renderable records carry no visibility; the node flags own it.
- Move each node's name, hierarchy and flags from `SceneNode` fields into its
  metadata, hierarchy and node-flags components in source.
- Use the single canonical IcoSphere identity. Migrate useful ArrowGizmo scene
  uses to ordinary geometry; retain tool-internal ArrowGizmo for its own
  purpose.
- Preserve useful camera composition under Auto/Fixed. Consolidate exposure
  mirrors into PostProcess.

## 10. Alternatives and rationale

| Alternative                                            | Reason not used                                                                    |
| ------------------------------------------------------ | ---------------------------------------------------------------------------------- |
| Ancestor-AND visibility                                | Discards intentional local overrides                                               |
| One scene Sun selector                                 | Cannot express two simultaneous sources without competing state                    |
| Slot mapping by name/index                             | Can silently redirect an override to another surface                               |
| Generic override slots at node, geometry, LOD scopes   | Second carrier of node flags; stores defaults; holds states the engine ignores     |
| Node fields outside components (name, flags, parent)   | Diverges from the engine node; needs special cases in schema, inspector, overrides |
| Overrides addressed by LOD and submesh index           | Indices move on reimport; a surface already spans every LOD                        |
| Visibility in the renderable record and node flags     | Second carrier; a hidden node's geometry never loads and cannot be shown           |
| A typed override record per part kind                  | Each new property is hand-written in every layer; drifts from the engine           |
| A separate system for model instance overrides         | Two override models with two reset and origin rules                                |
| Inherit stored as an absent override                   | Cannot override a template's local value back to Inherit                           |
| "Material slot" for a part carrying more than material | The name hides visibility and shadow overrides of the same part                    |
| Activation through IsActive                            | Confuses runtime presence with authored processing                                 |
| Mixed/Baked choices without baking                     | Advertises nonfunctional lighting modes                                            |
| Editor hide stored as render visibility                | Alters cooked output during a view-only editing action                             |
| One visibility/activation/contribution/shadow switch   | Changes unrelated rendering and lifecycle responsibilities together                |
| Light-only hidden-ancestor pruning                     | Resolves the same flag differently from geometry collection                        |
| Receiver flag stored without a GPU consumer            | No rendered effect                                                                 |
| Zero intensity or hidden gizmo as light enablement     | Conflates magnitude, editor representation and participation                       |
| Binary16 emission with relaxed tolerances              | Changes the precision contract; float32 factors meet it                            |
| Clear background colour as ambient light               | Mixes display composition with environment radiance                                |
| Every public engine field as product scope             | A field needs a useful workflow, precise meaning and a complete consumer path      |
| Old formats and aliases kept as compatibility paths    | Multiplies runtime contracts; one migration produces canonical content             |

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
[Unreal's static mesh component](https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Runtime/Engine/Components/UStaticMeshComponent)
keeps per-slot `OverrideMaterials` over the asset's materials, with an empty
entry meaning the asset default, and keeps per-section shadow casting on the
asset; Oxygen uses the same asset-default/instance-override split, keyed by
`SurfaceId` instead of slot index, and lets the instance override surface
visibility and casting as well. Unreal's archetype delta (an instance saves only
properties that differ from its template, with reset to default) is the model
for template overrides; Oxygen declares which properties a source may supply in
one schema instead of per class.

## 11. Qualification

Test identity/cardinality/finite constraints, duplicate names, hierarchy order,
inherited reparenting, local overrides, all slots and reimport conflicts,
surface Visibility/Cast Shadows against node flags, asset-default and instance LOD
policies, retention across geometry replacement, template overrides through
model reimport, Inherit over a template's local value, reset to each source,
camera
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
| Mesh surfaces                 | Per-surface instance override of Material, Visibility and Cast Shadows over asset defaults; Reset restores the source value; stable SurfaceId and explicit repair of lost continuity      | Section 5, [content pipeline](content-pipeline.md), [material editor](material-editor.md)     |
| Model instances               | A node instances a model scene; template nodes follow reimport; the scene stores only its overrides, with origin and reset per property                                                   | Section 5.2, [content pipeline](content-pipeline.md)                                          |
| Level of detail               | Fixed, Distance or Screen-space error policy per geometry asset, optionally replaced per instance                                                                                         | Section 5, [content pipeline](content-pipeline.md)                                            |
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
them. Mesh topology and surface creation are modeling tools with no engine
counterpart. Physics authoring is post-V0.1. The
[engine deferred-capability record](../../../projects/Oxygen.Engine/design/vortex/milestones/ED-M08/deferred-capabilities.md)
owns the source-local TODO IDs of engine features not yet authored.

There is one current source/cooked/runtime contract. Maintained development
tools migrate useful prior content and recook through the native producers; no
legacy reader, alias or duplicate behavior path remains (section 9).
