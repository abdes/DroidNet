# ED-M09 - V0.1 Gaps And Blocking Issues

Status: `planned`

## Completion Ledger

One row per item that needs work, in the order of work. The issues keep the
owner's order from section 3; the LLD gaps of section 2 start once their
dependencies are closed. An item is `done` only when the done gate of section 6
holds; its row then names the commit.

**Next: item 14**, #77 slice 77.8, planned and tracked in
[ED-M09.14](ED-M09.14-scene-instances.md): its dependencies (items 10, 11 and 12) are closed.

| Order | Item                                                                        | Needs        | State | Commit    |
| ----- | --------------------------------------------------------------------------- | ------------ | ----- | --------- |
| 1     | #77 decision in the scene model LLD and inspector                           | -            | done  | 0fd1e1a58 |
| 2     | #77 slice 77.1: surface rename                                              | 1            | done  | 9b1ba890f |
| 3     | #77 slice 77.2: scene component schema and generator                        | 2            | done  | 4c4e10dec |
| 4     | #77 slice 77.3: node components in the editor                               | 3            | done  | 5069bfa45 |
| 5     | #77 slice 77.4: slot model removed                                          | 4            | done  | 2dba3ac84 |
| 6     | #94 session services own their project scope                                | -            | done  | b5f0cccb2 |
| 7     | #95 Scene Explorer history without `UndoRedo`                               | -            | done  | ee79d3463 |
| 8     | #74 one view per view model per host                                        | -            | done  | 85ee1b76c |
| 9     | #76 LivePreview leaves Ready on engine fault                                | -            | done  | 7f4392514 |
| 10    | Inherit source modes (section 2)                                            | 4, #94, #95  | done  | 26124f382 |
| 11    | #77 slice 77.5: format and runtime                                          | 5            | done  | adce0424e |
| 12    | #77 slice 77.6: surface values in the editor                                | 11, #95, #76 | done  | e70244363 |
| 13    | #77 slice 77.7: LOD policy in the editor                                    | 11, #95, #76 | done  | 859eaa824 |
| 14    | #77 slice 77.8: scene instances ([ED-M09.14](ED-M09.14-scene-instances.md)) | 11, 12, 10   | open  | -         |
| 15    | Light collection by resolved visibility                                     | 10, 11       | open  | -         |
| 16    | Hide mask step 1: engine hidden-root snapshot                               | 15           | open  | -         |
| 17    | Hide mask step 2: Interop editing-view mask                                 | 16           | open  | -         |
| 18    | Hide mask step 3: Runtime port                                              | 17           | open  | -         |
| 19    | Hide mask step 4: editor dispatch and checks                                | #76, 18      | open  | -         |
| 20    | Material emission editing                                                   | 3, #95, #76  | open  | -         |
| 21    | #81 producer identity of same-named sources                                 | -            | open  | -         |
| 22    | #97 moved output group retires old cooked outputs                           | -            | open  | -         |
| 23    | #86 consistent editing commands across parts                                | -            | open  | -         |
| 24    | #96 one file access model                                                   | -            | open  | -         |
| 25    | #93 badge text in `AssetStatusText`                                         | -            | open  | -         |
| 26    | #73 Content Browser keyboard focus                                          | #86          | open  | -         |
| 27    | #72 Content Explorer tree context menu                                      | #86          | open  | -         |
| 28    | #87 texture cooker formats and settings                                     | -            | open  | -         |

Ordering decisions:

- #77 follows its slices (section 2.2). Items 2 to 5 change no behavior:
  materials keep working through the new model. Items 11 to 14 add the new
  capabilities. The commit of the last #77 item carries `Fixes #77`.
- All cooked-format changes land in item 11, so the format changes once and the
  owner recooks once for it.
- The schema (item 3) precedes every item that adds or changes a property, so
  no property is hand-mirrored and then migrated: the Inherit modes, the
  surface values, the LOD policy and the emission edits are schema entries.
- The node components (item 4) precede the Inherit modes, which store the
  flags in `NodeFlagsComponent`.
- Model instances (item 14) come after the Inherit modes and the surface
  values, because instanced-node overrides carry both.
- Items 11, 15 and 16 change engine scene preparation in order: surface
  resolution first, then light collection reads the node visibility that item
  10 resolves, then the Hide mask skips subtrees, so each engine change is
  tested against the final resolution rules.
- The LLD gaps and the #77 editor capabilities start after #94, as section 3
  requires, and after #76, because their preview checks need a LivePreview that
  reports engine faults.

## 1. Purpose

Close what V0.1 still lacks before input authoring and release qualification:
the LLD behaviors that are designed but not built, and the abdes/DroidNet issues
that must be fixed before V0.1.

## 2. LLD Gaps

The design pack audit found three LLD behaviors that are not built.

| Gap                       | LLD                                                                                                          | Today                                                                                                                                                                                     | Work                                                                                                                                                                                                                                     |
| ------------------------- | ------------------------------------------------------------------------------------------------------------ | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Inherit source modes      | [Scene model](../lld/scene-authoring-model.md#visibility-participation-and-shadow-source-state), inspector 4 | World stores `IsVisible`, `CastsShadows` and `ReceivesShadows` as booleans, so the editor authors local values only; the Rendering section offers no Inherit and shows no resolved origin | Inherit / Shown / Hidden and Inherit / On / Off in World source and `SceneNodeData`, Scenes descriptors, the cook's node flags, an Inherit path in Interop `NodePropertyApplier` and the inspector; migrate the booleans as local values |
| Light collection pruning  | [Scene model](../lld/scene-authoring-model.md#visibility-participation-and-shadow-source-state)              | `DirectionalLightResolver` and `SceneRenderer::BuildFrameLightSelection` traverse with `VisibleFilter`, so a locally Shown light under a Hidden parent contributes nothing                | Collect lights by each node's resolved visibility, as geometry extraction does; keep `VisibleFilter` for callers that prune subtrees deliberately                                                                                        |
| Material emission editing | [Material editor](../lld/material-editor.md)                                                                 | `MaterialSource` carries and writes `EmissiveColor` and `EmissiveIntensity`; `MaterialEdits` has no emission edit and the Material Editor shows emission only as a texture slot           | Emission colour and HDR intensity edits in `Oxygen.Editor.Materials` and their Material Editor controls, with history, Save, cook and preview                                                                                            |

**Inherit source modes** means this. A node has three visual-state choices:
Scene Visibility, Geometry Cast Shadows and Geometry Receive Shadows. Today each
is one boolean, so the user can only say "this node is shown" or "hidden". The
scene model says each is a source mode: **Inherit** (take the parent's resolved
value) or an explicit local value (Shown / Hidden, On / Off). A new root starts
Shown / On / On, a new child starts Inherit, and an Inherit root resolves to
Shown / On / On. Hiding a parent then hides every child that inherits, while a
child set explicitly to Shown stays active. The inspector's Rendering section
shows the mode and the resolved value with its origin. The engine already
resolves Inherit per flag, and the cooked node record already carries local and
inherited bits (`PakFormat_world.h` `NodeRecord`), so the format does not
change. The work changes the stored value in World source and `SceneNodeData`,
the Scenes descriptors, the cook's node flags, the Interop `NodePropertyApplier`
(which sets local values only today) and the inspector, and migrates the
existing booleans to local values by editing the data. `SceneNodeData`'s
shadow defaults (off) are replaced by the source-mode creation defaults. Under
#77 the flags live in the node's `NodeFlagsComponent`, a schema component
(section 2.1), and the Rendering section applies to every node.

### 2.1 #77 decision: parts, sources and one property schema

The full contract is in the
[scene model](../lld/scene-authoring-model.md#5-geometry-surfaces-overrides-and-primitives)
sections 3 to 5; the inspector design is in
[instance overrides inspector](../ui/instance-overrides-inspector.md). Facts it
rests on:

- Import allocates one surface per distinct source material of a
  geometry and one submesh per surface per LOD; separate source objects become
  separate nodes.
- The editor's scene descriptor omits a renderable's `visible`, so the cooker
  writes `RenderableRecord.visible` as 1; `SceneInstantiation` skips the
  renderable when it is 0.
- The engine has model scenes (`kModelScene`) and `SceneInstanceRecord`; the
  editor does not instance them yet.
- No engine property declaration or generator exists; `NodeField` and the other
  wire field enums are mirrored by hand in `PropertyKeys.h` and
  `WireVocabulary.h`. The editor's `PropertyDescriptor`s are hand-written per
  component.
- The engine has no skeleton asset or stable bone IDs (joints are indices).
- Every engine node is created with `ObjectMetadata` (name), `NodeData`
  (flags), `GraphData` (hierarchy) and `TransformComponent`
  (`SceneNodeImpl.cpp`); the editor's `SceneNode` instead holds the name,
  flags and children as fields.
- The inspector appends Rendering only in All components mode and only when
  every selected node has geometry or a light
  (`NodeRenderingViewModel.AppliesTo`).

The decision:

- **Surface rename.** `MaterialSlot`, `MaterialSlotId`,
  `MaterialSlotInventory`, the slot provenance schema and their records become
  `Surface`, `SurfaceId`, `SurfaceInventory` and surface provenance in the
  engine, Interop, the content-pipeline LLD and every document that names them.
- **A node is a container of components.** `SceneNode` keeps only its ID.
  Name, hierarchy, transform and flags move into required
  `MetadataComponent`, `HierarchyComponent`, `TransformComponent` and
  `NodeFlagsComponent`, mirroring the engine; Geometry, Camera, Light,
  Scripting and the new `ModelInstanceComponent` are optional. Rendering
  becomes an inspector list entry after Transform for every node.
- **One scene property schema.** Every authored scene component is declared
  once in an engine-owned schema with its cardinality, part kinds and
  properties (key, type, creation default, sources, resolution rule, wire and
  cooked field). One generator emits the
  engine field enums and codecs, the Interop and managed wire vocabulary, and
  the editor's typed property ids. The hand-written mirrors are deleted.
- **Sources and overrides.** Properties without a source store values;
  properties a source supplies (geometry asset, instanced scene) store sparse
  overrides addressed by node, component, part and key. `GeometryComponent`
  and `ModelInstanceComponent` hold them. Inherit stays a value. Reset removes an override.
- **Removed.** `RenderingSlot`, `LightingSlot`, `LevelOfDetailSlot`,
  `LodPolicy` and its editor subclasses, node-scope slots,
  `TargetedOverrides`, `GeometryOverrideTarget`, `OverrideSlot`,
  `OverridableProperty`, `MaterialsSlot`, `SurfaceTarget` as stored data
  (it remains the surface an edit picks from an inventory), their records,
  `SceneJsonContext` entries, reference-field paths and tests, and the
  never-called `LogLodPolicySyncUnsupported`. In the engine:
  `RenderableRecord.visible`, the index-keyed `SetSubmeshVisible` and
  `SetMaterialOverride(lod, submesh)` (scripting moves to surface IDs) and
  `MaterialOverrideRecord`, replaced by the cooked override table.
- **Engine additions.** Per-surface Visibility and Cast Shadows defaults and a
  default LOD policy in the geometry asset; the cooked override table with
  typed values, applied by `SceneInstantiation` to owned and instanced parts;
  one surface-keyed override path in the renderable that survives geometry
  replacement; scene preparation resolves surfaces as the LLD states;
  provenance-backed stable IDs for model-scene nodes.
- **Interop.** One override command carries a part reference and the part's
  whole override state, each override an override id and a typed value; it
  replaces `SetMaterialOverrideCommand`. `PropertyEntry`'s float-only
  payload becomes a typed value.
- **Data.** The World, Scenes and ContentPipeline fixtures and the
  content-pipeline LLD are edited; the owner recooks.

### 2.2 #77 implementation slices

Layers: E is the engine (runtime, cooker, pak format), I is Interop (C++/CLI),
R is the Runtime port and Ed is the managed editor including the cook
generator. Each slice is one thematic commit or a short series that builds and
passes on its own.

| Slice                     | Layers         | Content                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                             | Needs               | Recook           |
| ------------------------- | -------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ------------------- | ---------------- |
| 77.1 Surface rename       | E, I, Ed, docs | `MaterialSlot`, `MaterialSlotId`, `MaterialSlotInventory` and the slot provenance schema become `Surface`, `SurfaceId`, `SurfaceInventory` and surface provenance in data types, cooker, renderable, scripting bindings, Interop, editor names and documents. Binary layout unchanged                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                               | Decision approved   | Yes              |
| 77.2 Schema and generator | E, I, Ed       | The scene component schema, `Data/Schema/SceneComponents.yaml`, with its JSON Schema. Each platform generates its own code: the engine build runs a Python generator in the uv workspace, with unit tests, emitting the engine field enums; Interop includes that header; the editor's Roslyn generator, with Verify tests, emits Runtime's wire ids, the scene `PropertyId`s and LivePreview's wire table. Its first content is exactly today's components and properties, so generated code replaces `PropertyKeys.h`'s enums, `WireVocabulary.h`, `RuntimeComponentId` and the descriptor ids with no behavior change                                                                                                                                                                                                                                                                                                                                                                                                                                            | 77.1                | No               |
| 77.3 Node components      | Ed, I, E       | `MetadataComponent`, `HierarchyComponent` and `NodeFlagsComponent`; `SceneNode` keeps only its ID. Serializer, data migration, cook generator and Live Preview read the components; Rendering becomes a component-list entry for every node. The add-component kinds are the schema kinds. `NodeFlagsComponent` carries all six engine node flags, and Static, Ray-cast selectable and Ignore parent transform become authored: schema fields of the `Node` component (the engine header regenerates), the Interop applier and observer, Live Preview, Ignore parent transform in the Transform section and the other two under Rendering's Advanced, with tests. The engine node is already built from components                                                                                                                                                                                                                                                                                                                                                  | 77.2                | No               |
| 77.4 Slot model removed   | E, Ed          | The slot machinery is replaced by the override store of [scene model section 5.7](../lld/scene-authoring-model.md#57-editor-representation-of-parts-and-overrides): the schema declares parts and sources (Geometry's `Surface` part with `Material`), validated by the engine's generator with no change to its output; `PropertyId` moves to `Oxygen.Managed.Core` and the generated `SceneSchema` to World; `SourceOverrides` on `GeometryComponent`, one `SetOverrides` edit, and the data migrated. Materials keep working; the cook keeps writing `MaterialOverrideRecord` until 77.5 replaces it in the same commit that changes the format                                                                                                                                                                                                                                                                                                                                                                                                                  | 77.3                | No               |
| Inherit modes (section 2) | Ed, I          | Node flags become source-mode values; `NodePropertyApplier` gains the Inherit path; the cook writes the inherited bits the format already has                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                       | 77.3, #94, #95      | No               |
| 77.5 Format and runtime   | E, I, R, Ed    | All format changes in one commit. Schema ([scene model 5.3](../lld/scene-authoring-model.md#53-the-scene-property-schema)): cardinality, `Metadata` (`Name`, type `string`) and `Hierarchy`, `enums`, the `lod-policy` type, `override_id`s, the surface `Visibility` and `CastsShadows` and the geometry `LodPolicy`, and a `default`, `range` and `commit` for every property; both generators emit them and every consumer reads them (section 2.3). Geometry asset v6: `SurfaceDesc` flags and `default_lod_policy` ([content pipeline 20.5](../lld/content-pipeline.md#205-surface-defaults-and-the-default-lod-policy)). Scene asset v15: the `kOverride` table replaces `MaterialOverrideRecord`; `RenderableRecord.visible` removed. The renderable's surface-keyed `Set…Override`/`Clear…Override` survive geometry replacement, scene preparation resolves surfaces, the index-keyed APIs go. `SetOverridesCommand`, `RuntimeSetOverrides` and `FieldValue`; LivePreview and the cook carry every override; World gains the `LodPolicy` record (no edits) | 77.4                | Yes              |
| 77.6 Surface values       | Ed, R          | Visibility and Cast Shadows edits with history, the Surfaces list in the inspector, Live Preview of both                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                            | 77.5, #95, #76      | No               |
| 77.7 LOD policy           | Ed, R          | Policy edits with history, the inspector's LOD rows, Live Preview; the engine side is in 77.5                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                       | 77.5, #95, #76      | No               |
| 77.8 Scene instances      | E, I, R, Ed    | Specified by [scene instances](../lld/scene-instances.md) and [model editor](../lld/model-editor.md); planned in [ED-M09.14](ED-M09.14-scene-instances.md) as engine slices E1 to E10, then editor slices D1 to D11                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                 | 77.5, 77.6, Inherit | Yes, once, at E2 |

Verification per layer: engine changes run explicit test executables with
`--gtest_filter` (section 4.6); Interop runs its native tests; the generator
runs its unit tests and a check that the committed generated files match its
output; editor changes run filtered test runs of the touched projects. 77.3
regenerates the engine schema header; 77.4 runs the engine generator's unit
tests and checks its output is unchanged, with no engine build. The owner
recooks after 77.1, 77.5 and 77.8 (at its slice E2).

### 2.3 77.5 defaults and ranges

Today a property's default and range are decided in up to seven places (World
component and DTO initializers, creation paths, descriptor validators,
inspector views, the cooker's descriptor schema and `SceneSource.cpp`, pak
record initializers, Interop appliers), and they disagree. In 77.5 each
consumer reads the generated values instead:

| Layer | Consumer                                                                                    | Change                                                                                                                                       |
| ----- | ------------------------------------------------------------------------------------------- | -------------------------------------------------------------------------------------------------------------------------------------------- |
| Ed    | World components and `*Data` DTOs                                                           | Initializers read `SceneSchema.<Component>.Defaults`; constants such as `DefaultFieldOfViewDegrees` and `DefaultIntensityLux` go             |
| Ed    | `NodeFlagsData.WithCreationModes`, `SceneComponents.Create`, `SceneNodeTemplates`           | Take `default` and `root_default`; the creation paths set no property values of their own                                                    |
| Ed    | `*Descriptors.cs`, `CameraComponent` setters, `LightValidation` single-field checks         | One generated check per property; the rules keep only cross-property rules                                                                   |
| Ed    | Inspector views (`TransformView`, `DirectionalLightViewModel` cascade clamp), number fields | Bounds and clamping from the generated range; the views' own checks go                                                                       |
| Ed    | `ScenePreview.Helpers.cs` field-of-view fallback                                            | Removed: invalid values are rejected, never repaired                                                                                         |
| E     | `SceneSource.cpp`                                                                           | Omitted descriptor values take the generated defaults; values outside the generated range fail with `scene.descriptor.property_out_of_range` |
| E     | `oxygen.scene-descriptor.schema.json` component records                                     | Keep types and structure; their `default`, `minimum` and `maximum` go                                                                        |
| E     | `PakFormat_world.h` component records                                                       | Authored fields are value-initialized; the cooker writes every field                                                                         |
| I     | Property appliers, `AttachCameraCommand`, local fog attach                                  | Check the generated field ranges                                                                                                             |

Engine runtime component constructors keep their low-level defaults (scene
model 5.3).

Where code and the LLD field tables disagree, the schema takes the LLD value:

| Property                                         | Schema (LLD)                      | Code today                                                                  |
| ------------------------------------------------ | --------------------------------- | --------------------------------------------------------------------------- |
| DirectionalLight `CastsShadows`                  | default On                        | Off from the toolbar (`SceneNodeTemplates.cs:51`) and the class             |
| Point/Spot `LuminousFluxLumens`                  | default 1600, >0                  | class, DTO, pak and engine 800; validators ≥0                               |
| Point/Spot `Range`                               | default 20 m, >0                  | point 10, spot created at 15; `LightValidation` and the cooker ≥0           |
| Spot inner/outer cone                            | 22.5° / 45° (0.3927 / 0.7854 rad) | 0.4 / 0.6 rad                                                               |
| Light `Color`                                    | Clamp each channel [0,1]          | ≥0; cooker ±float max                                                       |
| Light `ExposureCompensation`                     | Clamp [-10,10]                    | finite only                                                                 |
| Light `ShadowBias`                               | Clamp [0,10]                      | ≥0                                                                          |
| Directional `IntensityLux`, `ShadowNormalBias`   | Clamp ≥0                          | reject <0                                                                   |
| Directional `CascadeCount`                       | [1,4], reject                     | the inspector clamps                                                        |
| Directional `AngularSizeRadians`                 | default 0.00935                   | pak record 0                                                                |
| PerspectiveCamera `FieldOfViewDegrees`           | default 60, Clamp [1,179]         | rejected with `TransformFieldNotFinite`; cooker (0, π); applier finite only |
| Camera `NearPlane` / `FarPlane`                  | defaults 0.1 / 1000, near >0      | `CameraComponentData` 0 / 0                                                 |
| OrthographicCamera `AspectRatio`, near/far, mode | 16/9, 0.1 / 1000, Auto            | cooker 1.0; pak record -100 / 100 and Fixed                                 |
| LocalFogVolume `FogPhaseG`                       | [0, 0.999]                        | Interop applier [0, 1)                                                      |
| Transform `Scale`                                | finite, nonzero                   | the inspector box needs magnitude ≥0.001                                    |
| Transform `Rotation`                             | finite; the display wraps to ±180 | the inspector box rejects values outside [-180, 180]                        |

The per-property range codes (`PerspectiveCameraNearPlaneNonPositive`,
`PerspectiveCameraAspectRatioNonPositive`, the three
`OrthographicCamera*NonPositive`, `CameraExposureNonPositive`,
`DirectionalLightFieldNotFinite`, `NodeFlagSourceInvalid`, the range uses of
`LocalLightInvalid`, `LocalFogVolumeInvalid` and `TransformFieldNotFinite`,
and the literals `PROPERTY_NONFINITE`, `PROPERTY_DEGENERATE_SCALE` and
`LIGHT_DISK_SCALE_INVALID`) give way to `PropertyValueNotFinite`,
`PropertyValueOutOfRange` and `PropertyValueUndefined`. A code is deleted
once no use remains, and so are the never-used
`PerspectiveCameraNearFarInvalid` and `OrthographicCameraNearFarInvalid`
(cross-property failures keep `PropertiesInconsistent`).

Units (scene model 5.3) are declared in 77.5:

| `id`             | `abbreviation` | `name`         | Properties                                                                                                                                  |
| ---------------- | -------------- | -------------- | ------------------------------------------------------------------------------------------------------------------------------------------- |
| `metre`          | m              | metres         | Transform position; camera near/far, orthographic size; shadow normal bias, CSM distances; light range and source radius; fog height offset |
| `degree`         | °              | degrees        | Transform rotation; perspective field of view                                                                                               |
| `radian`         | rad            | radians        | Directional angular size; spot cone angles (`display_unit: degree`)                                                                         |
| `lux`            | lx             | lux            | Directional intensity                                                                                                                       |
| `lumen`          | lm             | lumens         | Point and spot luminous flux                                                                                                                |
| `exposure-value` | EV             | exposure value | Light exposure compensation                                                                                                                 |
| `per-second`     | 1/s            | per second     | Camera shutter rate                                                                                                                         |
| `per-metre`      | 1/m            | per metre      | Fog radial and height extinction, height falloff                                                                                            |
| `f-number`       | f/             | f-number       | Camera aperture                                                                                                                             |
| `multiplier`     | x              | multiplier     | Transform scale; directional atmosphere disk luminance scale                                                                                |

Today the inspector's unit strings are literals in each view's XAML, and the
overlay's `x-editor-unit` entries are read by no code. 77.5 removes those
overlay entries for scene component properties; the editor generator emits
`SceneSchema.<Component>.Units.<Property>`. Switching the views' literals to
the generated units, which changes `lux`, `s` and `f` to `lx`, `1/s` and
`f/`, is [abdes/DroidNet#99](https://github.com/abdes/DroidNet/issues/99).

Model import settings for the surface defaults and the default LOD policy, with
their editor UI and wiring, are
[abdes/DroidNet#98](https://github.com/abdes/DroidNet/issues/98); until then
the importers write the schema defaults.

The editor Hide engine mask
([settings](../lld/settings-architecture.md#51-workspace-visibility-storage-and-lifetime))
is not built: the eye keeps state, persistence, undo and picking exclusion, and
logs that viewport suppression is pending. The work, in order:

1. Engine: a scene-scoped hidden-root snapshot with revision in
   `ScenePrepContext`; `ScenePrepPipeline::PrepareView` skips the descendant
   closure, emitting no render items and no shadow casters for editing views;
   Vortex design note. Test: a hidden mesh has no draws and no casters in the
   masked view and renders in an unmasked one.
2. Interop: `EditorModule::SetEditingViewHiddenNodes(revision, ids)` queued with
   world commands, IDs resolved through `NodeRegistry` into the snapshot of the
   scene's editing views in `OnPublishViews`, unresolved IDs retained;
   `OxygenWorld::SetEditingViewHiddenNodes`.
3. Runtime: `RuntimeSetEditingViewHiddenNodes` and
   `IRuntimeWorldCommands.SupportsEditingViewMask`.
4. Editor: dispatch the mask on change; check hide of a mesh and of a parent
   (geometry and shadows gone in every editing pane, kept in game and capture
   views), show of a parent keeping the child hidden, and the gizmo on a
   selected hidden node.

## 3. Blocking Issues

The owner selected these abdes/DroidNet issues; each must be closed before
V0.1. They are listed in the order of work.

| Order | Issue | Why it blocks V0.1                                                                                                                                                                |
| ----- | ----- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| 1     | #77   | Decides where a node's visibility and shadow choices live and which overrides an instance carries (section 2.1); the Inherit source modes of section 2 are built on that decision |
| 2     | #94   | Session services take a `ProjectContext` per call; settle project and scope ownership before the new work                                                                         |
| 3     | #95   | Scene Explorer history falls back to the static `UndoRedo.Default`; the Inherit edits need correct history                                                                        |
| 4     | #74   | Views rebuilt over long-lived view models make the editor unstable                                                                                                                |
| 5     | #76   | LivePreview stays Ready after the engine faults or stops; the Hide mask and emission preview need it reliable                                                                     |
| 6     | #81   | Sources with the same file name in different folders look like one producer, corrupting cook data                                                                                 |
| 7     | #97   | Moving a model's output group must retire its old cooked outputs, through save, reopen and cook                                                                                   |
| 8     | #86   | The editing commands must behave the same across parts, as Windows does; adds the context service that #91, #90 and #92 read                                                      |
| 9     | #96   | One file access model (`IFileSystem`, `DroidNet.Storage`, `System.IO` statics)                                                                                                    |
| 10    | #93   | Move `AssetEntry` badge text into `AssetStatusText`                                                                                                                               |
| 11    | #73   | Content Browser keyboard focus across the tree and the asset list                                                                                                                 |
| 12    | #72   | Content Explorer tree context menu that adapts to the node type                                                                                                                   |
| 13    | #87   | Cooker BC4, BC5 and BC6H, maximum size, alpha handling and channel packing for textures                                                                                           |

Dependencies between the issues:

- The Inherit source modes (section 2) need #77's schema and part model first.
- #73 (focus) and #72 (tree context menu) build on #86: #86 delivers the
  context service and the `Edit.*` commands that the context menu entries and
  the focus rules run through.
- #86 does not need #91 (activity, typed clipboard and drag, context-menu
  contributions, per-window dialogs), which stays after V0.1.

## 4. Rules For Every Change In This Milestone

These are the lessons of the asset identity and scope topology work. Each one
names a way earlier changes created the debt they were meant to remove. Read
the ones that match a change before its first edit; the per-issue notes in
section 5 point back here.

### 4.1 Scope and decisions

- A problem found while working an issue is fixed in the same change. Nothing is
  deferred, parked behind a pending or allowlist entry, or recorded as "waits
  for" without the owner's approval; an approved deferral is a GitHub issue on
  abdes/DroidNet with **Observed** (each fact marked confirmed or unconfirmed,
  with files and lines), **Design**, **Recommended course of action** and
  **Done when**.
- V0.1 exposes what the engine can do now. Code or docs that exclude or force
  off an engine capability are defects to raise, not scope.
- A missing seam or a bad API is never worked around (casting to concrete
  types, reading telemetry as control, time-based guesses, interim paths). Stop,
  describe the gap, propose the refactor (owner, API shape, cost) and wait.
- A design question is answered from user scenarios and the engine's actual
  behavior (one rendered scene, one scene document open at a time, AssetLoader
  caches within budget), never from today's code shape. The new contract drives
  the code, not the reverse.

### 4.2 Reuse before building

Check the library first, in designs and plans as well as in code, and name the
reused piece in the change.

| Need                                                 | Read and extend                                                                                             |
| ---------------------------------------------------- | ----------------------------------------------------------------------------------------------------------- |
| Windows, documents, tabs, dialogs, menus, decoration | `projects/Aura/src`, its READMEs and `Aura/samples/MultiWindow`; add a missing hook to Aura                 |
| Inspector rows, numbers, vectors, colors, trees      | `projects/Controls/*`, `Oxygen.Editor.Controls`, Inspector controls; add the missing feature with its tests |
| Coordinate spaces and DPI                            | `projects/Coordinates`; never multiply by `RasterizationScale` by hand                                      |
| Uploading, staging, versioning, recycling GPU data   | `src/Oxygen/Vortex/Upload/` and `src/Oxygen/Nexus/`; reusable additions go in the owning module             |
| Test fakes of a module's ports                       | the module's `testing/<Module>.Testing` kit                                                                 |

Inspector rows use icon buttons with descriptive tooltips, not text hyperlinks.

### 4.3 No new debt

- One owner per identity string. A mount name, extension, owner prefix, group
  name or schema key gets one constant or API before its first call site; a
  caller never spells it twice.
- A value that gets a better type changes the existing property's type; never
  add a second property that holds the same value.
- Migrate data by editing the data (descriptors, retained records, project
  files). Never write backward-compatibility code for an old format. A database
  schema rename edits `InitialCreate`, its designer and the snapshot as if the
  new name had always existed, with no rename migration; name the table and
  column in the report so the owner migrates their database by hand.
- The owner clears and recooks their own projects (F1 Validation, Test Scene
  Rename, Vortex); never edit them. Example cooks go directly into the in-tree
  `Examples/Content/.cooked`.
- No silent existence checks standing in for a contract, no "kept until later"
  leftovers, no test-only public API, no dead code, no undisclosed deviation
  from the agreed design.
- Visibility errors after a move are ownership questions. Never add
  `InternalsVisibleTo` or widen `internal` to `public` to compile; audit the
  member and its outside consumers first. `Oxygen.Managed.Core` is shared with
  the engine: no editor concepts, and ask before adding types to a shared
  assembly.
- Scene edits are serializable id/value records with typed ids and explicit
  targets, callable from any thread ([scene edits](../lld/scene-edits.md)), so
  remote control stays a thin adapter.
- Show in Editor (eye), Lock and runtime visibility are three independent
  states ([settings](../lld/settings-architecture.md)); state the distinction in
  anything that touches them.
- Window lifecycle: closing the Project Explorer exits; closing the workspace
  window closes the project and all its windows and returns to the Project
  Explorer, never exits; one scene editor tab at a time.

### 4.4 Code conventions

- C#: write `.editorconfig`-compliant code from the first edit (`this.`
  qualification; statics before instance, nested types last). Split methods
  that trip MA0051 into real private methods; local functions do not count. Fix
  every warning in files you touch, whatever its origin; fix none in untouched
  files.
- Logging, in every C# file new or touched (read one `*.Logs.cs` in
  `projects/Aura/src` first):
  - every `[LoggerMessage]` is in `<Type>.Logs.cs` as a multi-line attribute
    with `SkipEnabledCheck = true`, paired with a wrapper that takes raw domain
    data, checks `IsEnabled` first and builds locals after the guard;
  - never pass an invocation, `Join`, `ToString` or LINQ into the generated
    call, and never build a log argument at a call site;
  - Debug and Trace wrappers carry `[Conditional("DEBUG")]`; types take
    `ILoggerFactory? loggerFactory = null`.
- Anything that moves, rewrites or deletes user files, or closes or reopens
  documents, logs from its first implementation: Information per committed
  action with paths, Warning for rejections and skipped work, Error for failures
  and rollbacks, Debug for plan details. Never swallow a background failure
  unlogged.
- C++ (engine): trailing return types, checked optionals, trailing commas,
  `observer_ptr`, `_NDAPI`, C++20 headers; follow the
  [Oxygen engineering rules](../../oxygen/RULES.md). `NodeHandle` is used by the
  C++/CLI interop, so search `.h` and `.cpp` before calling a managed type
  unused.
- Diagnostics: reuse `Oxygen.Managed.Core.Diagnostics` (`DiagnosticCodes`,
  `OXE.*` and per-area codes) and the
  [operation results](../lld/diagnostics-operation-results.md) contract. A
  refusal without a code is a contract gap to raise, not to paper over.

### 4.5 Tests

- Derive every expected value from the written contract and quote the rule. A
  test that pins wrong output is part of the bug: rewrite it and fix the
  producer, never add a converter at the consumer.
- Assert contract codes and ids, never message text.
- Fixtures live in the module's own `tests/TestSupport/`. Other modules fake its
  public ports or use the kit fake; never link or copy another module's
  fixtures. A test of another module's logic moves to that module.
- Moq only for a port with no kit fake, for a few canned answers, a thrown
  failure or a call check; a port mocked repeatedly with the same setups gets a
  kit fake in its owner. Never mock models or the code under test.
- Projects come from `TestProject` on Testably `MockFileSystem`. Test project
  names end in `.Tests` (WinUI `.UI.Tests`, integration `.Integration.UI.Tests`)
  and assemblies are named `DroidNet.<ProjectName>`.
- UI tests never drive the system mouse or keyboard or need window focus; they
  use control APIs, view-model state, raised routed events and bindings. No
  flaky timing or input approximations.
- Integration tests share one native engine per process (`NativeSceneFixture`);
  batch shapes into one scene, no stress loops.
- A bug fix starts with a deterministic reproduction of the real mechanism where
  one exists.
- Before handing a change over, sweep the whole repo for every remaining
  instance of what it fixes (every mock of a port that now has a kit fake,
  every other `UndoRedo.Default` use) and fix them in the same change.

### 4.6 Build, run and verify

- Managed builds use the Insiders x64 developer shell and the `amd64` MSBuild
  through PowerShell, Debug configuration; the Integration.UI projects of
  WorldEditor and LivePreview build and run in Release against the installed
  `out/install/Release` SDK. Build only the owning test project, never
  `Oxygen.Editor.App`. Batch all edits of a pass, then one build; never edit
  while a build runs.
- Run only the test cases the change touches, with a filter. A filtered run that
  did not start is a failure. WinUI lanes get at most one targeted pass; on
  failure, stop and hand the owner the exact build and `traverse Invoke-Tests`
  commands. Never import Sponza-class scenes.
- Engine changes (#81, #87 and the light pruning and Hide mask work) follow the
  [engine instructions](../../../projects/Oxygen.Engine/AGENTS.md): explicit test
  executables with `--gtest_filter`, no whole-suite reruns, no GPU
  device-removal tests (the owner runs those).
- Never run clang-tidy, oxytidy or analyzer builds unasked. Format with
  `oxyformat` on explicit files, never directories.
- A failing test or editor bug is fixed whoever caused it. Reproduce from the
  live evidence (dump, stack, saved state) with at most one instrumented run
  that carries every useful datum; probes are announced, kept out of commits and
  removed.

### 4.7 Documents and commits

- LLDs state the design in present tense, including unbuilt parts; done and
  not-done live only in plans. A change that alters behavior updates its LLD in
  the same commit; a replaced document is deleted or rewritten, with every
  reference fixed. Never cite run artifacts or write evidence narration.
- Markdown is reflowed to 80 columns; run `pre-commit run --files <file>` until
  it reports Passed. Render-check any new Mermaid block.
- Nothing that changes what the editor shows (XAML, menus, focus visuals,
  notices) is committed before the owner has seen it in the running editor. A
  new design or plan text stays uncommitted until the owner approves it.
- Commit every modified file in the tree, including small edits you did not
  make. Run `git diff --cached --stat` first; the message describes the code
  outcome with a code-area scope, carries `Fixes #<n>`, and never cites
  milestones, slices or batches.

## 5. Notes Per Issue

Each note lists what to read first and the traps specific to the issue.

| Issue | Read first                                                                                                                                                                                                    | Traps and requirements                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                          |
| ----- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| #77   | [Scene model](../lld/scene-authoring-model.md) sections 3 to 5, section 2.1, [instance overrides inspector](../ui/instance-overrides-inspector.md), [content pipeline](../lld/content-pipeline.md) section 20 | Exactly one carrier per value and one declaration per property: the schema declares it, the generator emits every mirror, parts from a source store only overrides. Remove what is dropped and edit existing descriptors and projects; no reader of the old shape. Switch `SceneAssetReferences`, `SceneReferenceRetargeting`, `SceneReferenceFields`, the toolbar, Live Preview and the cook to part overrides. The generator is a tool with its own tests; generated files are not edited by hand. Every reader of `SceneNode.Name`, `IsVisible`, `CastsShadows`, `ReceivesShadows`, `Flags`, `Parent` and `Children` moves to the node's components; the Rendering section becomes a component-list entry for every node, and its All-mode append and `AppliesTo` geometry-or-light check are removed. Inspector control changes, with tests, follow the inspector design section 5: `PropertyCard` override state and reset (Scripting moves to it), a source-mode field over the toolkit `Segmented`, a toggle field with a mixed state, shared item-card and caution-box styles, and the model-origin selection header. The rename covers about 70 engine files, 4 Interop files and 6 design documents. Engine work follows the engine instructions; update the content-pipeline LLD, the World README, the asset-family-modules diagram note and ED-M08's stale "node-flag slots" line. |
| #94   | [Scope topology](../lld/scope-topology.md) decisions 5, 7 and 8, the topology test                                                                                                                            | The services take `IProjectSession` and read `Context` when they act; a mount change keeps the project id and root, so settings keys do not change. Keep the per-scene generation checks. Sweep every other session-scoped service for a `ProjectContext` parameter or an `activeProject` field, not only the three named. A test that switched projects on one instance becomes a test with two sessions.                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                      |
| #95   | [Editor documents](../lld/editor-documents.md), scope topology 5.10                                                                                                                                           | Verify first (the issue lists two unconfirmed effects) and report each as confirmed or not. Remove every `UndoRedo.Default` and `UndoRedo.GetHistory` use in the editor, not only the one in `SceneExplorerViewModel`. Without a document the explorer records nothing and Undo is disabled. Record changes as scene-edit records. Verified: no explorer action recorded into `UndoRedo.Default`, because every edit records through the document's `SceneAuthoring`; each explorer constructor did create an entry there that `UndoRedo` never removes, keeping every explorer reachable. The explorer exists only while a scene document is open, so its own stacks and the fallback were removed.                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                            |
| #74   | Docking, Mvvm and Routing.WinUI READMEs, commit 5d6ea4532                                                                                                                                                     | Rule: one view per view-model instance per host. Confirm the trigger first with logged constructors before changing the hosts. Part 2 (the `DynamicTreeViewModel` multi-view contract) is a design decision to settle and record, then remove the workarounds from `DynamicTree`. Never change a XAML property from `Unloaded`: a closing window raises it during teardown. These are shared libraries; no editor concepts in them. UI tests use control APIs. Done: every host keeps one view per view-model instance through `DroidNet.Mvvm.HostedViews` (the dock panel, the router outlet, the document area and the Scene Inspector's sections); the trigger was confirmed (seven views for two dockables after three tab switches). Decided: option B, one live tree per view model, recorded on `DynamicTreeViewModel` and enforced. The unloaded-tree test passed, so `DisplayedTreeItems` was removed.                                                                                                                                                                                                                                                                                                                                                                                                                                                                                 |
| #76   | [Live preview](../lld/live-preview.md) section 5, [operation results](../lld/diagnostics-operation-results.md)                                                                                                | A fault after `Ready` becomes `EngineUnavailable` with a diagnostic code and a published operation result, and `RetryAsync` restarts and remounts. Tests drive `IEngineService.StateChanged` through a fake engine service; no real device-removal test. Update the LLD, which does not cover this case. Log Warning or Error on the transition. Done: the session follows the run it mounted on; a fault keeps the engine service's published result and uses its code (`LOOP_FAULTED`, `LOOP_EXITED`), a shutdown publishes `RuntimeDiagnosticCodes.Stopped`, and a run that ends while the start mounts is reported when the mount finishes. No automatic restart. Editor exit disposes the session before the engine shuts down, so it reports nothing.                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                     |
| #81   | [Content pipeline](../lld/content-pipeline.md), `ImportManifest.cpp`, `LooseCookedIndexFormat.h`                                                                                                              | Remove `ImportRequest::SourceVirtualPath()` entirely and compute the producer where the manifest loads each job. The index version stays 6 but the field's content changes, so update every doc comment that describes it; the owner recooks. Tests assert `import.identity_collision` by code. Example manifests cook into the in-tree `Examples/Content/.cooked`.                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                             |
| #97   | Content pipeline LLD (retired products), `CookRootRepair`, `IAssetRelocationService`                                                                                                                          | Write the integration test first and let it decide whether the old entries remain. The group a model's outputs live in has one owning API; do not rebuild the path by hand. Removal goes through `remove-cooked` and logs each removed output at Information with its path. Restore the clause in the LLD once verified or implemented. Use a small model, never a Sponza-class scene.                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                          |
| #86   | [Editor shell](../lld/editor-shell.md) 5.5 and 5.6, the issue's list of what is done, `CommandKeyBindings`, `CommandService`, the parts' `ICommandHandler` implementations                                    | The routing, `DynamicTree`'s release of the clipboard keys and the Scene Explorer's and asset list's handlers exist; do not rebuild them. The issue's scope is: the context service (replacing the private `WorkTracker`), `Edit.Rename`, `Edit.Duplicate` and the Delete gesture, the sources tree's and the scene viewport's handling, removal of the view-local accelerators and the active part's visual. Typed clipboard and drag payloads belong to #91; focus after a content change belongs to #73. Routing tests for the asset list, the tree, the Scene Explorer and the scene editor use no system input.                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                            |
| #96   | `PROJECT-LAYOUT.md` section 13.3, Storage and Projects READMEs, `IAtomicFileStore`                                                                                                                            | Audit and classify before changing anything: `rg` also matches `fileSystem.File.*`, so the static count is unestablished. Real-disk boundaries shared with native tools are classified and documented, not wrapped. The decision is recorded in `PROJECT-LAYOUT.md`. Module tests move to `MockFileSystem` through `TestProject`. Moves and rewrites log from the first implementation.                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                         |
| #93   | [Assets module](../lld/assets-module.md) section 4, [Content Explorer identity](../lld/content-explorer-asset-identity.md) 7.2                                                                                | Move the members, switch every reader the issue lists (including the explorer's UI test support) and delete the members from `AssetEntry`; no forwarding. Status wording keeps one owner (`AssetStatusText`). Tests assert through `AssetStatusText`. Update both LLDs.                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                         |
| #73   | Issue #86's routing, `DynamicTree` focus support, asset layout code                                                                                                                                           | Lands with or after #86 and must not duplicate its routing. Reuse `DynamicTree` focus support; no key handling in views; the tab order is fixed in one place. Tests use view models and control hooks only.                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                     |
| #72   | `DroidNet.Controls.Menus`, Scene Explorer's tree menu, `IAssetRelocationWorkflow`, the cook commands                                                                                                          | Build it the way Scene Explorer does: a menu source the view model builds per requested row. Node kinds come from one mount-kind descriptor, never from literal mount names ("Content", "Cooked", "Examples"). Actions call the asset list's workflows. A disabled entry says why. It opens from the keyboard too.                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                              |
| #87   | [Texture editor](../lld/texture-editor.md), `TextureCooker.cpp`, the descriptor schema, [Oxygen rules](../../oxygen/RULES.md)                                                                                 | Each setting needs its schema key (one owner), request-builder mapping, cooker implementation and native tests. BC4, BC5 and BC6H first. The Texture Editor shows new settings through `inspect-texture` with no editor change; do not add editor code for them. Native tests by explicit executable and filter.                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                |

### 5.1 Notes for the LLD gaps in section 2

- **Inherit source modes:** one enum per concept with one owner, shared by World,
  Scenes, Interop and the cook. Migrate the stored booleans as local values by
  editing the data. The inspector shows the resolved origin using the existing
  inspector controls; add what is missing to them. The flags are schema
  properties, so this starts after #77's schema and part model.
- **Light collection pruning:** engine change with explicit native tests; keep
  `VisibleFilter` for callers that prune on purpose.
- **Material emission:** reuse `InspectorColorField` for the colour and
  `InspectorNumberField` for HDR intensity, adding any missing capability to
  those controls with their own tests; edits are scene-edit style records with
  history, and Save, cook and preview all carry them.
- **Hide mask:** it affects editing views only and is independent of Lock and of
  runtime visibility. Before adding the snapshot publication in step 1, read
  `Vortex/Upload` and `Nexus` for an existing versioned-publication mechanism.
  Gaps in `IRuntimeWorldCommands` are closed at the port, not by casting to a
  concrete runtime type.

## 6. Done Gate For Each Change

A change in this milestone is ready only when all of these hold, and the report
says which it checked:

1. The contract (LLD section or issue "Done when") is met, and the LLD states the
   behavior in present tense in the same commit.
2. Debt review of the diff: no duplicated helper or idiom, no second owner of an
   identity string, no second property for one value, no dead code, no
   compatibility reader, no invented convention, no coupling to another
   module's internals, no new build-time dependency, no stale document.
3. A repo-wide sweep found no remaining instance of what the change fixes.
4. Logging inventory over every `.Logs.cs` and every `[LoggerMessage]` file in
   the touched modules, not only the new lines.
5. No warnings left in touched files; no failing test left behind.
6. Tests assert the contract by code, use the module's own fixtures and kits,
   and the filtered run passed (or the owner's lane was handed the exact
   commands).
7. The staged diff was reviewed, hooks passed, and the commit message follows
   section 4.7.

Each blocking issue is closed by a commit that carries its `Fixes #<n>` footer.
