# Scene Instances LLD

Status: **design for owner review** (ED-M09 item 14, #77 slice 77.8).

Last Updated: 2026-10-28

Summary: a node of a scene can **instance** an imported model (`Car.glb`) or
another authored scene (`Watchtower.oscene`). The instanced hierarchy appears
under that node, stays linked to its source and follows it on reimport. The
instance root stores only its **changes** of the instanced nodes, in its
`SceneInstance` component. Opening a model shows it in the
[Model Editor](model-editor.md). Section 7 lists what the engine must add or
change (NGN1 to NGN13), section 8 what the editor must build (ED1 to ED12); neither section repeats the
other. The engine contract is owned by
[scene composition](../../../projects/Oxygen.Engine/design/runtime-core/scene-composition.md);
the override model this extends is the
[scene model](scene-authoring-model.md) section 5.

## 1. Why

A model import keeps the model's arrangement in one product, its model scene:
which mesh is where, under which parent, with which transform, lights and
cameras. `Car.glb` gives a body, four doors, four wheels and two headlights; a
city district such as `BurgerPiz.glb` gives 685 geometries, 73 materials and 68
textures. Without instances the editor cannot use that scene: parking five cars
on a quay means creating every body, door and wheel node by hand and picking a
geometry for each, and placing the district means 685 nodes. When the artist
exports again, nothing updates.

Authored scenes have the same need: a watchtower built once from geometry, two
torches and a guard script, placed four times along the harbor wall, and changed
once.

## 2. Scenarios

| ID  | Scenario                       | What the user sees                                                                                                                                                                                                                                                                                                               |
| --- | ------------------------------ | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| S1  | Place a model                  | _Add > Model…_ in the scene toolbar, _Add to scene_ on a model in the Content Explorer, or a Scene Instance component added in the Properties pane: a node `Car` appears with its body, doors, wheels and headlights under it                                                                                                    |
| S2  | Place a scene as a prefab      | The same with _Add > Scene…_: `Watchtower` appears with its geometry, its two torches and its script                                                                                                                                                                                                                             |
| S3  | Move it as a whole             | Moving, rotating, scaling, hiding or deleting the instance root acts on the whole hierarchy                                                                                                                                                                                                                                      |
| S4  | Place it many times            | Four watchtowers, five cars: each is its own instance with its own changes                                                                                                                                                                                                                                                       |
| S5  | Look inside                    | The Scene Explorer expands `Car3` into its nodes, dimmed as instanced; clicking `Door_FL` in the tree, or double-clicking it in the viewport, selects it, and the Properties pane says "in Car3 · Car.glb (model)"                                                                                                               |
| S6  | Change inside                  | On a node inside: open a door (rotate `Door_FL`), hide the glass, paint one car red (swap the Paint surface's material), turn off a node's shadows, pin its LOD, dim one torch's light. The changed property shows the override bar; Reset returns it to the source value                                                        |
| S7  | Reimport                       | The artist exports `Car.glb` again: every car shows the new version; changes on nodes that still exist stay; changes on nodes that are gone become kept changes, listed in the instance root's caution box, and apply again if the node returns or is reassigned (S15). Nothing blocks the cook                                  |
| S8  | Swap the source                | Choosing `Truck.glb` in the Source field of a car with changes asks _Keep changes by name_ or _Discard changes_; an opened `Door_FL` stays open if the truck has a `Door_FL`                                                                                                                                                     |
| S9  | Nest                           | `Watchtower.oscene` instances `Torch.oscene` twice; Harbor places four watchtowers, and a change can reach the flame of one torch on one tower, two nesting levels down                                                                                                                                                          |
| S10 | Apply to source                | A change made on one torch of one tower is pushed into `Watchtower.oscene` or `Torch.oscene`: every tower, or every torch, gets it and the change leaves this instance                                                                                                                                                           |
| S11 | Unpack                         | _Unpack_ turns one instance into ordinary nodes of this scene with its changes applied, nested instances staying instances; _Unpack completely_ does every level. The link is gone                                                                                                                                               |
| S12 | Play and standalone runtime    | The cooked scene carries the instances and their changes; the runtime builds the same hierarchy the editor shows                                                                                                                                                                                                                 |
| S13 | Open what was imported         | Double-clicking `Car.glb` opens the Model Editor, a tab beside Harbor: the car framed in the viewport, its nodes in the Scene Explorer, its import settings beside the viewport and its import status on the toolbar, with no pane moved; _Add to scene_ places it in Harbor ([model editor](model-editor.md))                   |
| S14 | From a placed car to its model | _Open model_ on `Car3`, or the "in Car3 · Car.glb (model)" link in the Properties pane, opens the Model Editor on the same node; fixing the import there (units, LOD policy, surface defaults) updates every placed car on Save                                                                                                  |
| S15 | Repair a renamed node          | The artist renamed `Door_FL` to `Door_Front_L`, so the door's change is a kept change on every car. _Reassign_ in the caution box lists the nodes new in this import first; choosing `Door_Front_L` records in the model's import settings that it continues `Door_FL`, and the change applies again on every car in every scene |

## 3. Terms

The terms follow the example used throughout: the Harbor scene places the
model `Car.glb` five times (renamed `Car1` to `Car5`) and the authored scene
`Watchtower.oscene` four times (`Tower1` to `Tower4`), and `Watchtower` places
the authored scene `Torch.oscene` twice (`TorchNorth`, `TorchSouth`).

```text
Harbor.oscene        the scene being edited
  Car3               level 0  instance root, source Car.glb
    Body             level 1  a node of the model
      Door_FL        level 1  a node of the model
  Tower2             level 0  instance root, source Watchtower.oscene
    Ladder           level 1  a node of Watchtower
    TorchNorth       level 1  a node of Watchtower; instance root, source Torch.oscene
      Flame          level 2  a node of Torch
```

- **Instance root.** The node that carries the `SceneInstance` component and
  stands for the whole instance: `Car3` and `Tower2` in Harbor, `TorchNorth` in
  Watchtower. It is an ordinary node of its scene; acting on it acts on the
  whole instance.
- **Source.** The model or the authored scene an instance root brings in.
- **Model instance.** An instance of an imported model, `Car.glb`. The model
  is the import's output: read-only, and updated only by reimporting it.
- **Prefab instance.** An instance of an authored scene, `Watchtower.oscene`,
  used as a prefab. Any scene can be placed this way; nothing marks it as a
  prefab. Instancing takes the scene's nodes with all their components and
  leaves what belongs to the scene as a whole (its environment, starting
  camera and starting input contexts), which applies only when the scene runs
  as a level.
- **Scene instance.** Either kind: what the `SceneInstance` component makes,
  since a model's arrangement is a scene too (SI-2).
- **Instanced node.** A node an instance brings in from its source: `Body`,
  `Door_FL`, `Ladder`, `TorchNorth` and `Flame` seen from Harbor. It belongs to
  its source, not to the scene that instances it, so Harbor cannot rename or
  delete it; Godot speaks of the nodes of an instanced scene, Unity of the
  objects in a prefab instance.
- **Node ID.** A 128-bit identifier every node has, unique within the scene or
  model that defines it, and stored as that node's ID in the cooked scene.
  Changes, node paths and reimport rely on it alone; no index or name is used
  to find a node. Every node ends up with one, and only how it gets it
  differs:

  | Node                               | Its node ID                                                                                                       |
  | ---------------------------------- | ----------------------------------------------------------------------------------------------------------------- |
  | A node of a scene (`Ladder`)       | The editor's `NodeId`, a UUID given when the user creates the node and saved in the scene file                    |
  | A node of a model (`Body/Door_FL`) | Derived by the importer from the node's name path and the model's source identity, which the import settings keep |

  A model's file cannot carry our IDs: a glTF node has none, and an FBX object
  ID is not guaranteed to survive another export. So the importer recognises
  "the same node" by the only thing the file states, its name path, and derives
  the same UUID from it on every import (section 7.2). A scene needs no such guessing, and
  must not use it:

  - The editor creates every node of a scene and already identifies it by its
    `NodeId` for undo, selection, Live Preview and the starting camera.
    Deriving an ID from names would discard what the editor knows.
  - Authors rename, reorder and reparent nodes all the time. Renaming `Ladder`
    to `Ladder_West` in `Watchtower.oscene` keeps its `NodeId`, so
    Harbor's change of that ladder still applies. With name paths, that
    rename would turn the change into a kept change in every scene that places
    a watchtower.
  - Names in a scene are not unique (two `Torch` nodes are normal) and their
    order is an ordinary edit. A name path with ordinals would move a change to
    the other torch when the author drags one above the other; the model case
    accepts that risk only because nothing better exists.

- **Nesting level.** How many instances deep a node is, seen from the scene
  being edited: Harbor's own nodes are level 0, the nodes of the car and of
  Watchtower level 1, the nodes of Torch level 2. Unity calls this multi-level
  nesting.
- **Node path.** How the scene names one of its instanced nodes, for a change,
  the starting camera or a lock: the outermost instance root above the node,
  then one node ID per nesting level below it, written `Tower2 › [TorchNorth,
Flame]` for the flame of the north torch on the second tower. A
  node ID alone is not enough: the four towers are the same
  `Watchtower.oscene`, each with two torches from the same `Torch.oscene`, so
  eight flames have the same ID, and only the path says which one.
- **Change.** An override of one property of an instanced node, stored in the
  `SceneInstance` component of the node path's instance root (always a node of
  the scene being edited), with the node path, the component, the surface when
  it is a surface property, and the value. A change is how Harbor puts out the north torch of the second
  tower (hides its flame) without touching `Torch.oscene`, or opens the
  driver's door of the third car.
- **Kept change.** A change whose node or surface no longer exists in the
  current source, after a reimport or a swap. It is stored, listed, removable and
  reassignable, has no effect, and applies again if its node comes back or it is
  reassigned to another node (section 6.4).
- **Source value.** The value a property has without this scene's change: the
  change a deeper nesting level makes, if any (Watchtower's own change of its
  torches' flames), else the value stored in the scene that defines the node
  (`Torch.oscene`).

The two kinds share the mechanism (one component, node paths, changes, kept
changes) and differ where their sources differ:

|                       | Model instance (`Car.glb`)               | Prefab instance (`Watchtower.oscene`)                                |
| --------------------- | ---------------------------------------- | -------------------------------------------------------------------- |
| Source owned by       | Its import; rewritten on reimport        | The author; edited in the scene editor                               |
| Node IDs              | Derived by the importer (section 7.2)    | The editor's `NodeId`s                                               |
| Brings                | Nodes with geometry, lights and cameras  | Nodes with all their components, scripts included                    |
| Leaves behind         | Nothing: a model has no scene settings   | The scene's environment, starting camera and starting input contexts |
| May contain instances | No                                       | Yes, of models and scenes                                            |
| _Apply to source_     | No                                       | Yes                                                                  |
| Updates when          | The model is reimported                  | The scene is saved and cooked                                        |
| Placed with           | _Add > Model…_, a model's _Add to scene_ | _Add > Scene…_, a scene's _Add to scene_                             |

## 4. Instancing is one more component

An instance root is an ordinary node with one more component. `Car3` in the
Harbor scene:

```text
Car3                            an owned node (NodeId 7f3e...)
  Metadata        name Car3
  Hierarchy       under the scene root
  Transform       (12, 0, -4), rotated 90 degrees
  NodeFlags       Visibility Inherit, Cast Shadows Inherit, ...
  Scripting       car_alarm.lua (any other component may sit beside it)
  SceneInstance   Source Car.glb, 3 changes
```

At runtime the body, doors, wheels and headlights under it are ordinary engine
nodes with ordinary components: `ObjectMetadata`, `NodeData`, `GraphData`,
`TransformComponent` and, where the model has them, a `RenderableComponent`, a
light or a camera. A change is written into the instanced node's own component,
where the same value of an owned node lives: the opened door is a rotation in
`Door_FL`'s `TransformComponent`.

So everything that works on nodes and components works on instances, with no
code that knows about instancing:

| What                                                        | Why it works                                                                                                          |
| ----------------------------------------------------------- | --------------------------------------------------------------------------------------------------------------------- |
| Moving, hiding or turning off shadows for the whole car     | The instance root's Transform and node flags are the parent of the source's root nodes; hierarchy inheritance does it |
| A script, a light or a geometry on the instance root itself | Any component can sit beside `SceneInstance`                                                                          |
| Duplicate, copy and paste, delete, undo                     | They copy or remove components; the changes travel inside the component                                               |
| Save, load, asset retargeting, cooking                      | The component's `Hydrate`/`Dehydrate`, its asset reference and its cooked component table                             |
| Rendering, shadows, LOD, culling, picking, framing, scripts | Instanced nodes are ordinary nodes; no system asks where a node came from                                             |
| A change taking effect                                      | It is applied to the instanced node's own Transform, `NodeData` or renderable surface override                        |
| Unpack                                                      | Remove one component and add owned copies of the instanced nodes                                                      |
| Nesting                                                     | An instanced node may itself carry `SceneInstance`; the same rules apply one nesting level down                       |

Only three places know about instancing: the `SceneInstance` component (editor
and engine, with its saved and cooked forms), instantiation, which creates the
instanced nodes and applies the changes, and the editor views that show
instanced nodes and refuse structural edits on them (section 6.2). A field
on every node, or a side table of instances, would make each row above a
special case.

## 5. Decisions

| ID    | Decision                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                        | Reason                                                                                                                                                                                                                                                                                                                                                                                                                                             |
| ----- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| SI-1  | The cooked Harbor scene holds, for `Car3`, only a reference to the model scene `Car.glb/Car.oscene` and its changes, never copies of the car's nodes. The game builds the car's nodes from that scene when it loads Harbor                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                      | Fixing the car and reimporting it updates every scene that places it, with no recook of those scenes; copied nodes would go stale until each scene is cooked again, and five cars would be five copies. The editor builds its view from the same `Car.oscene` the game loads, so both show the same car (engine SC-1)                                                                                                                              |
| SI-2  | The user picks a model by its source file, `Car.glb`. Importing it also writes the model's scene, `Car.glb/Car.oscene`: the nodes, transforms, lights and cameras that arrange its geometries. The editor instances that scene for the user and never offers it as a pick: the user sees it as the model itself, in the Model Editor (S13). The editor always imports the scene (the importer's `content_flags.scene`, on by default, is never turned off), since a model without it cannot be placed                                                                                                                                                                                                                           | One item per model ([#106](https://github.com/abdes/DroidNet/issues/106)): the user thinks of the car, not of the files its import produced                                                                                                                                                                                                                                                                                                        |
| SI-3  | Any authored scene can be placed as a prefab; no asset kind marks a scene as one                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                | The engine instances any scene; a scene's environment and start state are settings of the scene, not of its nodes, so they stay behind (SI-14)                                                                                                                                                                                                                                                                                                     |
| SI-4  | A change can set any property of any component of an instanced node: every scene schema property (transform, node flags, geometry with its surfaces and LOD policy, cameras, lights, local fog volumes) and every script parameter. Only structure is excluded (SI-5)                                                                                                                                                                                                                                                                                                                                                                                                                                                           | A change is written into the instanced node's own component, where an owned node holds the same value (section 4), so whatever value an owned node can hold, an instanced node can be changed to; a closed list would make the editor refuse what the engine applies, such as one torch flickering slower or one camera with another field of view                                                                                                 |
| SI-5  | No structural edits under an instance root: no add, delete, rename, reparent, reorder, add or remove component on instanced nodes                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                               | The source owns structure, so a reimport never meets local structure to merge; restructuring is Unpack                                                                                                                                                                                                                                                                                                                                             |
| SI-6  | A change names its node by node ID (section 3), never by name or index. Every node has one kind of node ID, a UUID: a scene node's comes from the editor when the node is created, a model node's from the importer, which derives the same one on every reimport                                                                                                                                                                                                                                                                                                                                                                                                                                                               | A name or an index changes with an ordinary rename, reorder or reimport; a node ID changes only when the node is gone, so a change follows its node or becomes a kept change, and never lands on another node, except when a model's same-named siblings are reordered in the source file (section 7.2)                                                                                                                                            |
| SI-7  | Reimport keeps changes by node ID. A change whose node or surface is gone is a kept change: inert, listed, removable, never blocking a cook                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                     | A change is work the author did. Dropping it on reimport loses that work, and refusing the cook lets one renamed node stop the whole scene; kept inert, it costs nothing at runtime (the cooker leaves it out, NGN9) and comes back when the node does                                                                                                                                                                                             |
| SI-8  | An override of a surface whose SurfaceId the current geometry no longer declares is a kept change, whether a geometry component of an owned node or an instance root's change stores it: (1) the reimported geometry publishes; (2) the scene opens, saves and cooks, and the cook reports the override with `scene.descriptor.surface_invalid` as a Warning and leaves it out of the cooked override table; (3) Live Preview and the runtime draw that surface with the geometry's own defaults; (4) the scene file keeps the override unchanged until the user reassigns it to a surface the geometry declares or removes it; (5) the override applies again, with no edit, when a later import declares that SurfaceId again | Today the cooker refuses the scene: `scene.descriptor.surface_invalid` is an Error (`SceneDescriptorImportJob.cpp:553`), so one surface lost on reimport stops every scene that uses the geometry. With this rule it costs exactly that surface's override, as a lost node costs exactly its own changes (SI-7)                                                                                                                                    |
| SI-9  | Swap keeps the instance root itself (name, transform, flags, components). With changes, it asks _Keep changes by name_ or _Discard changes_ (section 6.5)                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                       | The new source has its own node IDs, so a change can follow only by name path, a guess the user accepts or declines; a change whose name path is not found becomes a kept change                                                                                                                                                                                                                                                                   |
| SI-10 | An instance root of the scene being edited stores every change below it, however deep, addressed by node path. Nothing is written into the instanced scenes                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                     | The `SceneInstance` component is the scene's only foothold in the instance (section 4): every node below the instance root, nested instance roots included, belongs to its source and holds nothing of this scene. A node ID is unique only in its own scene (section 3), hence the path                                                                                                                                                           |
| SI-11 | A model never contains instances; an authored scene may instance models and scenes. A source that would instance the scene being edited, directly or through another scene, is refused when it is set and by the cooker                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                         | A model's scene holds only what its file describes, and glTF and FBX describe no Oxygen scenes. A cycle would make instantiation recurse forever                                                                                                                                                                                                                                                                                                   |
| SI-12 | _Apply to source_ moves changes into the file of an authored scene that the node path passes through, the scene chosen per nesting level; it is not offered for a model                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                         | An authored scene's file owns its nodes' values; a model's scene is rewritten by every import, so its import settings are where a change for every placement goes                                                                                                                                                                                                                                                                                  |
| SI-13 | Viewport: a click on an instanced node selects its outermost instance root; a double-click selects the node that was double-clicked, however deep                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                               | The instance root is the node of this scene that stands for the whole instance (section 4); the nodes below it can only be changed, not moved out or deleted (SI-5)                                                                                                                                                                                                                                                                                |
| SI-14 | A prefab instance brings the scene's nodes with all their components; the scene's environment, starting camera and input contexts stay with the scene                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                           | They are settings of the scene as a whole, not of its nodes, and apply when it runs as a level (engine SC-4); so a prefab needs no asset kind of its own                                                                                                                                                                                                                                                                                           |
| SI-15 | The starting camera can be an instanced node with a camera, such as the camera of a `CameraRig.oscene` placed in Harbor                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                         | The starting camera is a node reference, and the scene names an instanced node by its node path (section 3)                                                                                                                                                                                                                                                                                                                                        |
| SI-16 | A kept change is repaired where it was lost. One lost by a reimport of the model it was made against is repaired in that model: its import settings record that the chosen node's anchor continues the lost node's, and the chosen node takes the lost node's ID. Any other, a change a swap carried over or a change on a scene instance, is repaired in its instance by rewriting its node path. The importer reports which nodes vanished and which are new; it never proposes a match                                                                                                                                                                                                                                       | The rename happened in the model, so repairing it there fixes every placement in every scene and survives the next import; a repair per scene would leave the other scenes broken. A match proposed by similarity fails where names, meshes and empty nodes repeat, which they do in most models, and a wrong one moves a change onto another node without a word; the vanished and new lists are facts, and they narrow the choice to a few nodes |

## 6. Behaviour

### 6.1 Placement

_Add > Model…_ and _Add > Scene…_ in the toolbar `QuickAddMenu` open a picker of
models (one row per source file) or authored scenes. It never lists model scene
products, the current scene, or a scene that instances it. The new instance
root is named after its source (`Car`), placed like every _Add_ entry, at the
identity transform, and selected. _Add to scene_ in the Content Explorer and in
the Model Editor does the same for the chosen model or scene. To make an
existing node an instance root, the user adds a Scene Instance component to it
in the Properties pane and chooses its Source.

### 6.2 Instanced nodes: what they allow and how they show

Every node below an instance root is **instanced**: its structure belongs to
its source (SI-5), its values can be changed (SI-4). The instance root itself
is an owned node: every operation applies to it, and acts on the whole
instance. Instanced is a state of
its own, beside Lock, and the two combine:

|                  | Instanced                                              | Locked                                         |
| ---------------- | ------------------------------------------------------ | ---------------------------------------------- |
| Comes from       | Being below an instance root; nobody sets or clears it | The user's padlock, on the node or an ancestor |
| Stored           | Nowhere: derived from the hierarchy                    | Workspace state, per user (settings LLD 5.1)   |
| Values (changes) | Allowed                                                | Refused                                        |
| Structure        | Refused                                                | Refused                                        |
| Ends             | Only by Unpack                                         | Unlock                                         |
| Refusal code     | `SceneEditCodes.InstancedStructure`                    | `SceneEditCodes.TargetLocked`                  |

A locked instanced node refuses both. Lock keeps working on instanced nodes,
keyed by instance root and node path in the workspace state, as the user may
want to protect a car's changed doors from accidental edits as much as any
node.

**Operations on an instanced node:**

| Operation                                               | Result                                                                                                |
| ------------------------------------------------------- | ----------------------------------------------------------------------------------------------------- |
| Select, frame, Editor Hide (eye), Lock                  | Allowed                                                                                               |
| Change a value: inspector, transform gizmo              | Allowed; written as a change of the instance root                                                     |
| Reset, Apply to source                                  | Allowed                                                                                               |
| Copy                                                    | Allowed; pasting outside an instance creates owned copies with the resolved values, linked to nothing |
| Rename, delete, cut, duplicate, drag, reparent, reorder | Refused                                                                                               |
| Paste into, _New child node_, add a node under it       | Refused                                                                                               |
| Add component, remove component                         | Refused                                                                                               |

A refused operation names the node and its source and what to do instead:
"'Door_FL' comes from Car.glb. Unpack Car3 to change its structure." A
command over a selection that holds an instanced node is refused as a whole,
as a locked node refuses it today, so no batch applies to part of the
selection.

**How it shows.** One visual per state, so both can show at once:

- **Scene Explorer:** an instanced row draws its name and glyph in
  `TextFillColorSecondaryBrush`, the dimming the editor uses for content the
  scene does not own. The Eye and Lock slots keep their meaning and position:
  Lock is the user's padlock, not the instanced state, so a second lock icon
  would make a locked instanced node unreadable. The instance root keeps
  primary text and carries the link badge on its glyph; with kept changes, the
  caution glyph. An instanced row with changes carries the override bar.
  Refused commands stay in the context menu, disabled, with the refusal as
  their tooltip; the Explorer's foot buttons follow the selection the same way.
- **Properties:** the selection header names the source on its second line
  ("Door_FL" over "in Car3 · Car.glb (model)"), with a link that opens the
  Model Editor on that node and a button that selects the instance root. The
  name field is read-only; _Add component_ and _Remove component_ are disabled
  with the refusal as tooltip. Values are editable with override bars and reset
  (SI-4). When the node is also locked, the existing "Locked for editing" bar
  shows as for any locked node.
- **Viewport:** an instanced node looks and behaves like an owned one: the
  same selection outline and the same transform gizmo. Dragging the gizmo on it
  writes a Transform change of its instance root. The viewport adds no mark of
  its own; the Scene Explorer and the Properties header show that a node is
  instanced.

On the instance root, the Scene Instance section shows the Source field, the
change count, the kept changes in the caution box with _Remove_ and _Remove
all_, and, on a prefab instance, the notice that the scene's environment and
start state apply only when it runs as a level. The inspector's override
presentation follows the
[instance overrides inspector](../ui/instance-overrides-inspector.md). The
mockup is [scene-instances-explorer.svg](mockups/scene-instances-explorer.svg).

### 6.3 Value resolution

An instanced node's property resolves to the first present of: this scene's
change; the changes of the deeper nesting levels, from the shallowest down; the
value in the scene that defines the node; then, for a property with an asset
source, the asset default and the schema default. Node flags keep Inherit as a value: a change can set Inherit over
a source's Hidden. The engine's hierarchy inheritance then applies.

A model's nodes are cooked with Scene Visibility, Casts Shadows and Receives
Shadows set to Inherit, except a node the source file hides, which keeps a local
Hidden. Setting those flags on the instance root, an owned node, therefore
governs every instanced node that inherits them, at any depth; a change on an
instanced node sets them locally there and below. Static and Ray-cast
selectable have no source mode: they are values, set on an instanced node by a
change like any other value. Reset
removes this scene's change and shows the source value; it never writes the
source value as a change.

Owned nodes are unaffected: they store their values as today, and only their
asset-sourced properties (surfaces, LOD policy) carry overrides.

### 6.4 Reimport

When a source is republished (a model reimport, or a cook of a saved authored
scene), every open scene that instances it reads the new structure and
Live Preview rebuilds those instances. Changes resolve by node ID and surface
ID. A change that no longer resolves is a kept change: listed under the
instance root with its node's last known name path (`Body/Door_FL`) and
property, removable one by one or all at once, inert in the cook and the
runtime, and active again if a later version brings the node back.

_Reassign_ on a kept change opens a picker of the source's nodes: first the
nodes the import that lost the change reported as new, then all of them.
Choosing one repairs the change where it was lost (SI-16):

- **In the model**, for a change made against the current model. The edit
  writes into the model's import settings that the chosen node's anchor
  continues the lost one (`Body/Door_Front_L` continues `Body/Door_FL`), and
  reimports. The chosen node then has the lost node's ID, so every change of
  the lost node, in every scene, applies to it. Changes made on the chosen node
  under its own former ID become kept changes; the picker shows how many this
  scene has before the user confirms. As for _Apply to source_, the edit
  records the import settings' previous bytes: Undo restores them and
  reimports, and refuses when the settings changed since.
- **In the instance**, for a change a swap carried over from another source or
  a change on a prefab instance. The edit rewrites the change's node path to the
  chosen node, an ordinary undoable edit of this scene.

### 6.5 Swap

Setting another source on an instance keeps the instance root's own
components. Without changes it simply switches. With changes it asks:

- **Keep changes by name.** Each change's node path is turned into the
  name path it had in the old source and looked up in the new one; a match is
  rewritten to the new node IDs. Sibling nodes with the same name match in
  order. A change that finds no node becomes a kept change, marked as carried
  from the old source.
- **Discard changes.** Every change of the instance is removed.

Either way the swap is one undoable edit.

### 6.6 Nesting

A node path has one ID per nesting level below this scene. Putting out the
north torch of the second tower is a change stored by the `Tower2` instance
root, with the node path `[TorchNorth, Flame]`; nothing is written into
`Watchtower.oscene` or `Torch.oscene`. Instantiation applies the
deepest level's changes first, so Harbor's change wins over Watchtower's own
change of the same flame.

### 6.7 Apply to source

On an instance of an authored scene, _Apply to source_ on a changed row, a
component section, an instanced node or the whole instance offers each authored
scene the node path passes through, below the scene being edited (`Apply to
Watchtower`, `Apply to Torch`).
Applying moves the changes into that scene's file: a change of a node the
scene owns becomes its stored value; a change deeper down becomes a change of
that scene's instance root with the remaining path. The changes leave this scene.
The source scene is not open (one scene document is open at a time), so the
edit loads, changes and saves its file and records its previous bytes: Undo
restores both scenes, and refuses with a message when the source file changed
since. Models on the path are not offered.

Where the user asks for it:

- **A changed row** of the Properties pane: its context menu holds _Reset_
  and one _Apply to …_ per authored scene on the node's path, for the values
  the row shows.
- **A component**: the context menu of its entry in the pane's component
  list, for the node's changes of that component.
- **An instanced node**: the _Apply to…_ menu in the Properties header,
  beside _Select instance_, and the Scene Explorer row's menu, for all its
  changes.
- **The whole instance**: the Scene Explorer menu of the instance root and the
  _Apply to Watchtower_ button of the Scene Instance section, beside the
  change count; for a model instance the button is disabled and its tooltip
  gives the reason: the import makes the model's nodes and a reimport rewrites
  them, so changes stay on the instance.

A command is offered only while the node has changes it would move. A change
whose node the chosen scene does not hold stays on the instance and is named
in a warning. Writing the file is saving the scene: the automatic cook cooks
it while Cook on change is on, and every instance of it, Live Preview and the
Scene Explorer included, follows once it is mounted.

### 6.8 Unpack

_Unpack_ replaces the instance root's `SceneInstance` component with owned
copies of the source's nodes (the next nesting level) under it, with their resolved
values: Transform, node flags, cameras, lights, fog volumes and scripts as
stored values; geometry referencing the same geometry asset, with surface and
LOD changes as that geometry component's overrides. An instanced node that is
itself an instance root stays one, with the changes below it re-rooted.
_Unpack completely_ repeats this down to the deepest nesting level. Copies receive new
`NodeId`s. A starting camera or a lock that names an unpacked node is retargeted
to its copy. Kept changes are dropped and listed in the edit's result. One undo
step restores the instance.

### 6.9 Viewport selection

A click on an instanced node selects its outermost instance root (`Car3`). A
double-click selects the node under the cursor, however deep (`Door_FL`): the
pick returns that node with its node path (section 8.7). _Select instance_ in
the Properties header and the instanced row's context menu select the
outermost instance root again. In the Scene Explorer, a click selects the row
clicked. Frame Selected frames instanced nodes like owned nodes.

### 6.10 Starting camera

The Play section's Starting camera picker also lists instanced nodes with a
camera, under their instance root. If a reimport or swap removes that node,
the choice is kept and shown with the caution glyph, and Play starts through
the first camera, as for a scene with none.

## 7. Engine capabilities

The engine owns node identity, the cooked format and **one composition**: the
code that follows a scene's instances and resolves their changes. The cooker,
the runtime, the live API and the Inspector all call it, so no two tools can
disagree on what a scene contains, and the editor never decodes cooked bytes
or resolves nesting levels itself. This section is the engine's work list, each
capability as what, why and how;
[scene composition](../../../projects/Oxygen.Engine/design/runtime-core/scene-composition.md)
states the contract it implements. Engine paths are relative to
`projects/Oxygen.Engine/src/Oxygen/`.

**Already in place.** The engine instances scenes today, without changes. This
design extends that mechanism; it adds no second one:

- The descriptor node's `instance.scene_ref`
  (`Cooker/Import/Schemas/oxygen.scene-descriptor.schema.json`, definition
  `scene_instance`), read by `Cooker/Import/Internal/SceneSource.cpp:925` and
  cooked as `SceneInstanceRecord { node_index, scene_key }`
  (`Data/PakFormat_world.h:247`).
- The cooker refuses instance cycles with `scene.descriptor.instance_cycle`
  (`Cooker/Import/Internal/Jobs/SceneDescriptorImportJob.cpp:478-532`, :640).
- `SceneInstantiation` builds instanced scenes under their instance roots,
  recursively, to a depth of 16
  (`SceneInstantiation/SceneInstantiation.cpp:64`, :497-520). An instanced
  scene brings its nodes and scripts; its environment, starting camera and
  input contexts are not applied (SI-14).
- A model import marks its scene with `SceneAssetFlags::kModelScene`.
- The asset-sourced properties (a surface's Material, Visibility and Cast
  Shadows, the LOD policy) are cooked as `OverrideRecord`s and applied by
  `ApplyOverride` (`SceneInstantiation.cpp:387-440`).

**Missing:** node and slot identity that survives ordinary edits, changes that
reach instanced nodes, building an instance under a live node, and a report of
the composed tree.

| ID    | Capability                         | Needed by                   | Builds on |
| ----- | ---------------------------------- | --------------------------- | --------- |
| NGN1  | Authored node identity             | SI-6, S7, S9                | -         |
| NGN2  | Model node identity                | SI-6, S7                    | -         |
| NGN3  | Script slot identity               | SI-4 (script parameters)    | -         |
| NGN4  | Every property changeable          | SI-4, S6                    | -         |
| NGN5  | Changes in the cooked format       | SI-1, SI-10, SI-15          | NGN1-NGN4 |
| NGN6  | One composition                    | SI-10, sections 6.3 and 6.6 | NGN5      |
| NGN7  | Instantiation with changes         | S12                         | NGN6      |
| NGN8  | Live instance API                  | S5 to S8 in Live Preview    | NGN7      |
| NGN9  | Cooking instances and kept changes | SI-7, SI-8, S7              | NGN6      |
| NGN10 | Instanced starting camera          | SI-15, section 6.10         | NGN6      |
| NGN11 | Composed scene report              | S5, section 6.4             | NGN6      |
| NGN12 | glTF scene selection               | SI-2, S13                   | NGN2      |
| NGN13 | Retained source rebinding          | NGN2 through the ImportTool | -         |
| NGN14 | One model import record            | NGN2, NGN13, ED11           | -         |

NGN1 to NGN4 are independent of each other; NGN7 to NGN11 are independent of each
other once NGN6 exists; NGN12 needs only NGN2's anchors; NGN13 and NGN14 go
together. Section 7.15 groups the
format and cooker changes that must ship together, for one recook.

### 7.1 Authored node identity (NGN1)

**What.** Every node of an authored scene is cooked with the editor's `NodeId`
as its `NodeRecord.node_id`, and no two nodes of one scene share an ID.

**Why.** A change names its node by node ID (SI-6). Today `BuildNodeAssetKey`
(`SceneDescriptorImportJob.cpp:152-160`) derives the ID from the scene's
virtual path and the node's index: reordering Watchtower's nodes gives `Ladder`
another ID, so Harbor's change of it lands on another node or on none, and
moving the scene changes every ID. Nothing checks uniqueness when a scene
loads.

**How.**

- The descriptor's node definition (`oxygen.scene-descriptor.schema.json:181`,
  and its editor variant) gains a required `id`, a UUID string.
- `SceneSource` reads it into `NodeRecord.node_id`. `BuildNodeAssetKey` is
  removed.
- A nil or malformed `id` (`scene.descriptor.node_id_invalid`) and a repeated
  one (`scene.descriptor.node_id_duplicate`) are errors: malformed data, not
  kept changes.
- Loading a scene (`Data/SceneAsset.cpp`) refuses repeated node IDs, since
  composition finds nodes by ID (NGN6).
- Every scene descriptor in the examples and test fixtures gains IDs; the
  editor writes them (ED9).

**Done when.** A cooked scene's node IDs equal its descriptor's; reordering,
renaming and moving the scene keep them; a repeated ID fails the cook and the
load.

### 7.2 Model node identity (NGN2)

**What.** A model node's ID is derived from its anchor and the model's source
identity. It is the same on every import of the model and does not depend on
where the model lives in the project.

**Why.** A model file carries no Oxygen IDs (section 3). Today
`Cooker/Import/Internal/gltf/GltfAdapter.cpp:2860` and
`Cooker/Import/Internal/fbx/FbxAdapter.cpp:3382` derive the ID from the model
scene's virtual path and the node's de-duplicated name: moving or renaming
`Car.glb` changes every ID. The model already has a stable source identity, the
UUID created at first import and kept in its model import record (NGN14),
which no relocation moves; surface
allocation derives surface IDs from it
(`Cooker/Import/Internal/SurfaceAllocation.cpp:28-80`). Node IDs follow the
same rule, so no table of node IDs is stored: a table would only restate the
function.

**How.**

1. **Anchor.** The path of names in the source file's own hierarchy, from the
   model's root to the node, before node pruning (`NodePruningPolicy`):
   `Body/Door_FL`. Same-named siblings are numbered in file order from the
   second: `Body/Mirror`, `Body/Mirror[2]`; unnamed siblings are numbered
   from the first: `Body/[1]`, `Body/[2]`. A node the importer adds appends
   its role: `Body/Headlight_L#light` for a light's attachment node, `#root`
   for a synthesized root. Within a name, `/`, `[`, `#` and `\` are escaped
   with `\`.
2. **ID.** `node_id` is derived from `"oxygen.model-node/v1"`, the source
   identity and the anchor, by `NodeId::FromStableIdentity`, as
   `SurfaceId::FromStableIdentity` derives a surface ID
   (`Data/SurfaceId.cpp:29`). The model's virtual path is not an
   input.
3. **Source identity.** The import request's `surface_source_identity`
   (`SceneImportRequestBuilder.cpp:146`) is renamed `source_identity` and comes
   from the model import record (NGN14): it now identifies the model's nodes as
   well as its surfaces. An import without a record (the ImportTool's
   `--output` mode)
   generates one (`Uuid::Generate()`) and reports
   `import.source_identity_missing` as a warning, since its IDs then change on
   every import.
4. Both adapters compute the anchor while they traverse the source hierarchy
   and apply the rule to every node they emit, attachment nodes included
   (`GltfAdapter.cpp:2812`, `FbxAdapter.cpp:3348`).
5. **Continuity.** The model import record may hold `node_continuity`, a list
   of `{ anchor, continues }` entries, written by _Reassign_ (section 6.4) or
   by hand in the record. A node whose
   anchor is listed takes the ID of the anchor it continues, following chains
   to the first: if `C` continues `B` and `B` continues `A`, `C` has `A`'s ID.
   A cycle is an error. An entry whose continued anchor is in the file again,
   or a second entry continuing one anchor, is dropped with
   `import.node_continuity_conflict` (a warning), since two nodes cannot share
   an ID.
6. **Vanished and new.** The publication records the anchor and node ID of
   each node it emitted in the model import record, beside the surface
   provenance (NGN14). The import reports, against
   the previous publication's anchors, those that vanished and those that are
   new (`import.nodes_changed`, info), and records them: `new_nodes`, the
   nodes new against the previous publication, and `vanished_nodes`, every
   node an earlier publication emitted whose ID this one does not, kept from
   import to import until its ID is emitted again. These are facts for the
   user's choice (section 6.4); the importer proposes no match.

`Car.glb` reimported after the artist edited it in Blender:

| Edit in Blender                         | Anchor before → after                   | ID                   | The scene's change on it                                |
| --------------------------------------- | --------------------------------------- | -------------------- | ------------------------------------------------------- |
| Door_FL's mesh, material or position    | `Body/Door_FL` → `Body/Door_FL`         | Same                 | Applies                                                 |
| A new node `Body/Spoiler`               | → `Body/Spoiler`                        | New                  | -                                                       |
| `Headlight_R` deleted                   | `Body/Headlight_R` → gone               | Gone                 | Kept change                                             |
| `Door_FL` renamed `Door_Front_L`        | `Body/Door_FL` → `Body/Door_Front_L`    | New, until continued | Kept change until _Reassign_ continues it; then applies |
| `Door_FL` moved under a new `Chassis`   | `Body/Door_FL` → `Chassis/Door_FL`      | New, until continued | Kept change until _Reassign_ continues it; then applies |
| The two `Mirror`s swapped in file order | `Body/Mirror` and `Body/Mirror[2]` swap | Same, swapped        | Follows the position, not the mirror                    |
| The model renamed, or its folder moved  | Unchanged                               | Same                 | Applies                                                 |

An anchor that disappears and comes back has its old ID again, so its changes
apply again (SI-7). Renaming or moving a node in the DCC tool makes a new
anchor: the import reports the old anchor as vanished and the new one as new,
the node's changes become kept, and _Reassign_ continues the old anchor in the
new one for every placement (section 6.4). The importer does not match them by
similarity: names, meshes and empty nodes repeat too often for a match to be
evidence, and a wrong match would move a change without a word. Reordering same-named siblings is the one case
where a change silently moves to another node. FBX object IDs are not used:
some exporters regenerate them, which would make identity depend on the
exporter.

**Relocation.** Renaming `Car.glb` or moving its folder changes its virtual
path. The editor rewrites the `source` of the model's import record (NGN14)
and leaves its identity alone; it refuses to move the
model file alone out of its folder, since the model references its other files
by relative path. Node and surface IDs are derived from the source identity
and the anchor only, so they and every change addressed by them are unchanged.
What changes is the asset keys of the model's products, which are hashes of
their virtual paths (`Core/Types/AssetKey.cpp:48-59`): the model scene's key
in each `SceneInstanceRecord`, and the geometry keys in its records. The editor
retargets each instance root's Source as it retargets any asset reference
(section 8.1), and an instancing scene is cooked again through its dependency
on the model scene (ED9), as a scene is for a moved geometry today. A source
moved outside the editor is recognised by NGN13, in either tool.

**Done when.** Node IDs are equal across reimport, moving and renaming the
model; a renamed node gets a new ID, and its old one once a continuity entry
names it; a removed node that comes back has its old ID; a cycle is an error
and a conflicting entry a warning; the vanished and new anchors are reported.

### 7.3 Script slot identity (NGN3)

**What.** Every script slot of a cooked scene has a UUID, unique among its
node's slots.

**Why.** A change of a script parameter names its slot (NGN4), and a slot index
changes when the author reorders Watchtower's slots, the failure NGN1 removes for
nodes. The editor already gives each slot a stable `Guid`
(`Oxygen.Editor.World/src/Components/ScriptSlot.cs:19`) and writes it as the
scripting sidecar's `slot_id`, but the cooker uses it only to key sidecar rows
(`Cooker/Import/Internal/Pipelines/ScriptingSidecarImportPipeline.cpp:174-176`):
the cooked `ScriptSlotRecord` (`Data/PakFormat_scripting.h:117`) has no ID.

**How.** `ScriptSlotRecord` gains `slot_id` (16 bytes). The sidecar pipeline
parses a row's `slot_id` as a UUID, reports a malformed one as an error, and
cooks it, so the sidecar, which re-reads the cooked scene to patch it, gets
slots back with their IDs (today without, `ScriptingSidecarImportPipeline.cpp`
near :1034). Loading refuses a node with two slots of one ID.

**Done when.** Reordering a node's slots keeps each slot's ID.

### 7.4 Every property changeable (NGN4)

**What.** A change can set any property of any component of an instanced node
(SI-4). The engine applies it through the code that builds an owned node with
the same value, under the same validation. A property added to the schema is
changeable with no further code.

**Why.** `Data/Schema/SceneComponents.yaml` gives override IDs to 4 of its
properties, all asset-sourced (`Data/Schema/Generated.SceneComponents.h:375-381`),
and a hand-written switch applies them. Extending that switch would repeat, for
each of about a hundred properties, what the builders already do with cooked
records (`MakePerspectiveCamera`, `MakeSpotLight`, `ApplyLocalFogVolume` in
`SceneInstantiation.cpp`), and every new property would need a second edit that
is easy to forget. An owned node is built from its cooked records (`NodeRecord`
and the renderable, camera, light and fog records of `Data/PakFormat_world.h`),
so a change is applied by patching the instanced node's record before the node
is built: one path for owned and instanced values.

**How.**

- **Override IDs.** Every property with a `wire` declaration has an
  `override_id`; `SceneComponents.schema.json` requires it there (today only
  with `sources`, :323-325). IDs 1 to 4 keep their values, new ones count from
  5, none is reused. `Metadata`'s name and `Hierarchy` have no wire and stay
  structure (SI-5). The unused `model` value of `sources` (:269-275) is
  removed; `sources: [asset]` keeps marking the properties a geometry gives a
  default.
- **Rotation is one property.** `RotationX`, `RotationY` and `RotationZ`
  (`SceneComponents.yaml:246-263`) become one `vec3` property `Rotation`, key
  `/local_rotation_euler_degrees`, fields
  `[RotationXDegrees, RotationYDegrees, RotationZDegrees]`, encoding
  `euler-yxz-degrees-to-quat`. A cooked source holds only a quaternion
  (`NodeRecord.rotation`), and one angle of it exists only through a
  decomposition, which is ambiguous near ±90° pitch: an axis change could
  compose to a rotation the user never saw. A rotation change carries the three
  angles the user sees. Position and scale stay per axis: an axis of them is
  exact, and a change of one axis still lets a reimport move the others.
- **Field writers.** Each cooked record gets one hand-written function in
  `Data/`, beside the record, that writes one field of its generated field set
  (`TransformField`, `NodeField`, `PerspectiveCameraField`, …;
  `Generated.SceneComponents.h:41-144`) into the record, as a `switch` with no
  `default`, so a field added to the schema fails to compile until it is
  mapped. The tri-state flags rule (`inherited_flags` bit for Inherit, else the
  `node_flags` bit), now in `SceneSource.cpp:795-820`, moves into the
  `NodeRecord` writer. The descriptor reader writes records through the same
  functions.
- **Generated appliers.** The generator (`Data/Tools/SceneSchemaCodeGen`)
  emits, per property, its override ID, value type, part kind, encoding and
  `OverrideValue` member in `kOverrideProperties`, and two functions:
  `EncodeChange`, from a value in schema units to an `OverrideValue` (degrees
  to radians, Euler angles to a quaternion), and
  `ApplyChange(record, OverrideId, OverrideValue)`, which decodes the fields
  and calls the record's field writer. The cooker, the live API and Interop
  encode through `EncodeChange` alone.
- **Asset-sourced properties** stay `OverrideRecord`s applied by
  `ApplyOverride`: a change of a surface or of the LOD policy joins the
  instanced node's override list and replaces the entry with the same override
  ID and part. A change of a node's geometry (`GeometryUri`) discards the
  deeper levels' surface and LOD overrides of that node, which describe the
  geometry it replaces.
- **Validation.** Composition checks a patched record with the rules an owned
  record is checked with (`HasValidPerspectiveCameraValues`,
  `HasValidLocalFogVolumeValues`, the lights' rules), so the spot cone pair and
  near below far hold on the composed value. A component whose patched record
  fails keeps its source values, and its changes are reported as an issue.
- **Script parameters** are not schema properties: a script declares them when
  it is analysed. A change of one names the node path, the slot's `slot_id`
  (NGN3) and the parameter key, and holds a value of the parameter's type
  (`ScriptParamRecord`, `PakFormat_scripting.h:98`). A node-typed value is a
  node reference (section 7.5), so it can name an instanced node; that holds
  for every node-typed parameter, a scene's own slots included.

**Done when.** The generated tables cover every wired property; a test fails
when a field has no writer; for each property kind, a change gives the value
the same property has when authored on an owned node.

### 7.5 Changes in the cooked format (NGN5)

**What.** A cooked scene stores on each instance root every change it makes
below it, addressed by node path, and its starting camera can name an instanced
node.

**Why.** An `OverrideRecord` (`PakFormat_world.h:309-316`) addresses a node of
its own scene by index, which is all an owned geometry needs. A change of an
instanced node needs the instance root and the node IDs below it (SI-10), a
rotation change needs a quaternion, which `OverrideValue` has no member for,
and script parameters have no override table at all.

**How.**

```text
NodeRef                    node_index, node_path
OverrideRecord             target (NodeRef), override_id, part_id,
                           layout_revision, value
NodePathRecord             first, count                kNodePath table
NodePathElement            node_id (NodeId)          kNodePathElement table
ScriptParamOverrideRecord  target (NodeRef), slot_id, param
                                                       kScriptParamOverride table
ScriptParamRecord          ..., value; a node value is a NodeRef
SceneAssetDesc             ..., starting_camera (NodeRef)
```

A node's identity is a `data::NodeId` (16 bytes): `NodeRecord.node_id` and
`NodePathElement` hold one instead of an `AssetKey`, which names assets. Every
place the cooked scene names a node uses one type, `NodeRef`: an index
into the scene's own nodes and, when that node is an instance root, the path
below it.

- `NodeRef.node_path` (uint32) is `kNoNodePath` for the scene's own node,
  else the index of a `NodePathRecord`. With a path, `node_index` is the
  instance root and the path's elements name one node per nesting level,
  shallowest first: the first is a node of the root's source. Identical paths
  share one record.
- An `OverrideRecord` whose target has `kNoNodePath` must be of an
  asset-sourced property: an owned node stores every other value in its own
  records.
- `OverrideValue` gains `as_quat` (four floats; its size stays 16 bytes).
- `ScriptParamOverrideRecord` holds script parameter changes, one per address,
  sorted by target, slot and key; `param` is a `ScriptParamRecord`.
- `ScriptParamRecord`'s node value becomes a `NodeRef`, in a scene's own slots
  and in changes alike. The record is shared with script assets
  (`Data/PakFormat_scripting.h:85-97`), whose node-typed declarations have no
  value, so `kScriptAssetVersion` goes from 4 to 5 with no other change.
- The three tables get new `ComponentType` FourCCs in
  `Core/Meta/Data/ComponentType.inc`, and the loader a branch and
  `ComponentTraits` for each (`SceneLoader.h:472-581`).
- `SceneAssetDesc.starting_camera` becomes a `NodeRef` (NGN10); none is
  `kNoStartingCamera` in `node_index`.
- The override table is sorted strictly by
  `(target, override_id, part_id)`: the cooker's sort
  (`Cooker/Import/Internal/Pipelines/ScenePipeline.cpp:124-130`) and the
  loader's check (`SceneAsset.cpp:705-711`) compare the whole target. The loader also
  checks every path index and element range and the `kNoNodePath` rule.
- `kSceneAssetVersion` goes from 15 to 16 (`Data/PakFormatVersions.inc:14`).
  Maintained content is recooked; no reader of version 15 is kept. Section 7.15
  lists every format change that shares this increase.

**Done when.** Each table round-trips through the cooker and the loader; the
loader refuses each malformed case above.

### 7.6 One composition (NGN6)

**What.** One engine function composes a cooked scene. It follows the scene's
instances and gives, for every node of the composed tree, its node path, its
defining scene and its records with every change applied, plus the changes
that do not resolve.

**Why.** Resolution (sections 6.3 and 6.6) is needed in four places: the cooker
reports unresolved changes (NGN9), the runtime builds the nodes (NGN7), the live API
recomputes one node (NGN8) and the Inspector reports the tree (NGN11). Four
implementations would be four chances to disagree on which value wins, the
divergence SI-1 exists to prevent. Today the only one is inside
`Instantiator::Build` (`SceneInstantiation.cpp:243`), bound to a live scene and
private to that file.

**How.**

- `Data/SceneComposition.h`:
  `ComposeScene(const SceneAsset& scene, SceneResolver resolve) -> ComposedScene`.
  It is synchronous and builds no scene graph. `SceneResolver` returns the
  loaded scene for an asset key, or none: the runtime passes the scenes it has
  loaded, the cooker and the Inspector scenes read from the cooked root, as
  `CheckInstanceCycles` reads them.
- `ComposedScene` lists **occurrences**: the scene itself, then one per
  instance, depth first. An occurrence holds its defining `SceneAsset`, its
  instance root and its nodes; a composed node holds its node path, its index
  in the defining scene, its patched records, its effective overrides, and, per
  changed property, the scene whose change supplied the value. An occurrence
  also holds its scripts' effective parameters. The result lists the unresolved
  changes (the scene that made each, its instance root, node path and
  property) and the issues: an instanced scene missing or unreadable, a cycle
  (a scene reached again on its own path), which ends that branch and replaces
  today's depth cap (`kMaxInstanceDepth`), and a record that fails validation
  (NGN4).
- **Resolution.** Each occurrence indexes its scene's nodes by `node_id` (unique
  by NGN1). The changes that reach it are those its scene makes, by instance
  root, and those forwarded from above. A change whose remaining path is one
  element applies to that node; a longer one is forwarded, without its first
  element, to the instance whose root that element names. A change whose
  element names no node of the scene, or that must pass through a node that is
  not an instance root, is unresolved. A node's records are patched with the
  deepest level's changes first and the outermost's last, so the outermost
  scene's change wins (SI-10).
- `ComposeNode` runs the same steps for one node of one occurrence; the live API
  uses it to recompute a node after one change (NGN8).

**Done when.** Tests over hand-built assets: the outer change wins two nesting
levels down; an element that names no node, and a path through a node that is
not an instance root, are unresolved; a missing instanced scene is an issue;
`ComposeNode` equals `ComposeScene` for the same node.

### 7.7 Instantiation with changes (NGN7)

**What.** Instantiating a scene builds its composed tree. Every instanced node
is an ordinary node with its effective values, and each instance root carries
an engine `SceneInstanceComponent` that knows its source and its changes.

**Why.** The runtime must build what the editor shows (S12). Today
`Instantiate` applies a scene's overrides only to its own nodes
(`AttachRenderables`, `SceneInstantiation.cpp:360-382`) and keeps no trace of
which nodes an instance brought: its result holds a flat node list
(`SceneInstantiation.h:58`). Finding `Car3`'s `Door_FL` at runtime, and changing
it live (NGN8), needs that trace. It is a component for the reasons of section 4:
only instance roots have it, and it lives exactly as long as the instance, with
no registry to keep in step with an edited scene.

**How.**

- `Instantiate` loads the scene and, recursively, its instanced scenes (the
  existing loads, :497-520), calls `ComposeScene`, and builds the nodes from the
  composed records. The existing builders (`CreateNodes`, `AttachCameras`,
  `AttachLights`, `ApplyLocalFogVolume`, `AttachRenderables`) become
  `Apply*(SceneNode&, const Record&)` functions that attach or replace, so NGN8
  can apply them again. Scripts attach per occurrence with its effective
  parameters (`SceneScriptAttachment::Attach`), so node parameters keep binding
  to their own scene's nodes.
- `SceneInstanceComponent` (`Scene/`, declared with `OXYGEN_COMPONENT`,
  requiring `TransformComponent`) holds the source `SceneAsset`, kept loaded
  because clearing a change reads it again (NGN8), the changes its scene makes
  below the root, and the source's nodes by node ID, one level deep: a nested
  instance root has its own component. `SceneNode` gains `GetInstance()`, a
  facade like `GetRenderable()` and `GetScripting()`, with `Source()`,
  `Changes()` and `Find(path)`, which follows the path one component per level.
- `PlayerModule` refuses a scene flagged `kModelScene`
  (`player.model_scene`): a model is placed, never played.
- An unresolved change becomes a `SceneInstantiationIssue`, which gains a
  `code` (`scene.instance.change_unresolved`, …) and the node path. The result
  type `SceneInstance` is renamed `InstantiationResult`: "scene instance" names
  the instance a `SceneInstanceComponent` makes (section 3).

**Done when.** Engine tests: a scene that places a model twice, and one nesting
two levels, build the expected nodes and values; `Find` returns each instanced
node; an unresolved change is an issue and the rest of the scene builds.

### 7.8 Live instance API (NGN8)

**What.** A host can instance a scene under an existing node of a live scene
with a set of changes, set or clear one change, and remove the instance.
Clearing a change restores the source value.

**Why.** Live Preview shows what the editor edits (S5 to S8) without reloading
the scene. Only whole-scene `Instantiate` is public (`SceneInstantiation.h:87`);
`Instantiator::Build` accepts a parent but is private. An editor
implementation would resolve changes apart from the runtime.

**How.**

- `instantiation::InstantiateUnder(InstanceRequest, IAssetLoader&)` returns
  `co::Co<InstantiationResult>`. The request names the target scene, the
  existing node that becomes the instance root, the source scene's asset key
  and the changes. It composes with `ComposeInstance` and builds through the
  same builders as whole-scene `Instantiate`, so both run one path.
- `instantiation::SetChange(root, change, loader, scripts)` and
  `ClearChange(root, address, loader, scripts)` update the changes of the
  component that holds them, recompute the addressed node with `ComposeNode`
  (a script parameter with `ComposeSlots`) and apply only that property's
  records again. They live in `SceneInstantiation`, not on
  `SceneNode::Instance`, because applying needs the asset loader, the builders
  and the script attachment, which the Scene module does not depend on. An
  address (`ChangeAddress`) is a node path with an override ID and part, or
  with a slot ID and parameter key. The recomputation reads the changes every
  live instance root above the node holds, so an outer scene's change still
  wins; clearing yields the next level's change or the defining scene's value,
  read from the scenes the components keep loaded. An address that does not
  resolve (`CheckChange`) returns an issue and changes nothing.
- A component records who made each change: the requester, the scene that
  instances the source, whose tables its values use and whose live nodes,
  shared by its instance roots, its node values name; or a host, whose values
  name no scene's table: its node values name live nodes the host gave
  (`NodeParameterValue`), and its LOD policies (`LodPolicyValue`) a policy
  table the component keeps for the host, where a new value of one change
  replaces the old. Every change of an instance a host made is a host's.
- `instantiation::Detach(scene, root)` destroys the source's root nodes with
  `Scene::DestroyNodeHierarchies` (`Scene/Scene.h:326`) and removes the
  component. Owned children of the instance root stay. It takes the scene
  because a node facade holds a read-only view of its scene.
- Values arrive in schema units and are encoded by `EncodeChange` (NGN4); a
  host's surface change names the geometry as loaded, so the layout revision
  check applies only to a scene's changes.

**Done when.** Engine tests: an instance under a live node; setting then
clearing a change restores the source value, at both nesting levels; a host's
node value binds its node, and an outer scene's node value binds again after a
live change; `Detach` leaves the root and its owned children.

### 7.9 Cooking instances and kept changes (NGN9)

**What.** The cooker reads an instance root's changes from the descriptor,
cooks the ones that resolve, and reports the others as warnings without
failing the cook (SI-7, SI-8).

**Why.** The descriptor's `scene_instance` holds only `scene_ref` (schema
:203-212), and a surface override the geometry does not declare fails the cook
(`scene.descriptor.surface_invalid`, an error, `SceneDescriptorImportJob.cpp:553`).
An unresolved change is left out of the cooked table rather than cooked inert,
so an unresolved change met at runtime always means cooked content out of step
with its sources, worth an issue. The editor cooks a scene again when a scene
it instances changes (ED9), which is when its kept changes are checked again.

**How.**

- `scene_instance` gains `overrides`: per node path (UUID strings), the changed
  values by component, each property by its schema key and in schema units,
  surface parts as in geometry overrides:

  ```json
  "instance": {
    "scene_ref": "/Content/Vehicles/Car.glb/Car.oscene",
    "overrides": [
      { "path": ["5d0c…"],
        "components": {
          "transform": { "values": { "/local_rotation_euler_degrees": [0, 70, 0] } } } },
      { "path": ["91aa…"],
        "components": {
          "geometry": { "parts": [ { "kind": "surface", "id": "…",
            "layout_revision": "…", "values": { "/material": "/Content/Paint/Red.omat" } } ] } } }
    ]
  }
  ```

- The cooker finds each property in the generated tables by component and key
  and encodes it with `EncodeChange`. An unknown component, key or part kind, a
  malformed UUID or a value of the wrong type is an error: malformed data.
- It composes the scene with the cooked instanced scenes (NGN6), which are cooked
  first, and reports each unresolved change as
  `scene.descriptor.instance_override_unresolved`, a warning, leaving it out.
  `scene.descriptor.surface_invalid` becomes a warning, for owned geometry and
  changes alike, and leaves the override out instead of failing the link.
- The scripting sidecar gains instance rows (the instance root's `node_index`,
  `node_path`, `slot_id`, `params`), cooked into `ScriptParamOverrideRecord`s;
  one that does not resolve is the warning
  `script.sidecar.instance_override_unresolved`. A change goes to the input
  that holds the same value of an owned node: component properties to the
  descriptor, script parameters to the scripting sidecar.

**Done when.** Cooker tests: each malformed case is an error; an unresolved
node, surface and script parameter change are each a warning and absent from
the cooked tables, and the cook succeeds.

### 7.10 Instanced starting camera (NGN10)

**What.** The starting camera can be an instanced node with a camera (SI-15).

**Why.** `SceneAssetDesc.starting_camera` is an index into the scene's own
nodes (`PakFormat_world.h:143`), and loading requires that node to have a
camera (`SceneAsset.cpp:488`).

**How.** The descriptor's `starting_camera` gains an optional node path. The
cooker writes `starting_camera` as a `NodeRef` (NGN5). A path that does not resolve, or
whose node has no camera, is the warning `scene.starting_camera.unresolved`,
and no starting camera is written. Loading checks the camera only when the
reference names the scene's own node. `ApplyStartingCamera`
(`SceneInstantiation.cpp:538-562`) finds the node with `GetInstance().Find`;
without a starting camera its existing fallback applies: the first camera, in
composed order.

**Done when.** A scene whose starting camera is a camera of a placed
`CameraRig.oscene` starts through it; with that camera gone from the rig, it
starts through the first camera, with an issue.

### 7.11 Composed scene report (NGN11)

**What.** The Inspector reports a cooked scene composed: every node, owned and
instanced, with its values and the scene each value comes from.

**Why.** A cooked scene's composed values must be inspectable outside the
editor, from the tools and in tests. The editor itself reads an instance's
hierarchy from the mounted descriptors through the engine (section 8.2), never
by running the Inspector, so the report has tools and tests as its only
consumers. Before NGN11, the Inspector's `scenes` verb
(`Cooker/Tools/Inspector/SceneMetadata.cpp`) reported each scene alone: per node
its index, ID, name, parent and flags, and the perspective cameras; no
transform, no other component, no instance.

**How.**

- `scenes <cookedRoot> --virtual-path <vp>` composes that scene (NGN6). Without
  it, the verb composes every scene of the root: a scene without instances
  composes to itself, so the report has one shape.
- The report schema (`Schemas/oxygen.cooked-scenes.schema.json`) becomes
  `oxygen.cooked-scenes.v2`. Per node: its node path, node ID, name, parent,
  defining scene and components, with every changeable property's value in
  schema units (decoded from the records, Euler angles from the quaternion)
  and the scene that supplies it. Per instance root: its source. Then the
  unresolved changes and the issues. It stays installed with the SDK schemas
  (`Cooker/Tools/Inspector/CMakeLists.txt:113-123`).

**Done when.** The report of a nested scene matches composition for every node
and value and validates against the schema.

### 7.12 glTF scene selection (NGN12)

**What.** A glTF file can hold several scenes, alternative hierarchies over the
same nodes and meshes. The user chooses which one is the model's scene; the
import reports the scenes it does not use; and the editor learns the file's
scenes from the source analysis it already runs, without loading any buffer.

**Why.** A model is placed through its one model scene (SI-2). Today
`GltfAdapter.cpp:2676-2681` imports the default scene, or else the first, drops
the others without a word, and offers no way to choose
(`ReportDroppedSourceFeatures`, `Cooker/Import/Internal/DroppedSourceFeatures.cpp:194-221`,
reports every other dropped feature). A file whose useful scene is not the
default cannot be used. The editor already runs `analyze-sources` on every
model to discover its files (`SceneImportSourceDiscovery.cs:42`,
`CookSourceAnalyzer.cs:59`), which parses the glTF JSON without loading buffers
(`ImportSourceAnalysis.cpp:238`), but that report does not list scenes. For a
`.glb`, even that parse reads the whole file into memory
(`ReadCgltfFile`, `GltfAdapter.cpp:264-310`). The traversal has no visited set
and the importer never calls `cgltf_validate`, so a scene that lists one root
twice yields duplicate nodes and a cycle recurses without bound; both would also
break anchor uniqueness (NGN2). An FBX file has one root
(`ufbx_scene.root_node`, traversed at `FbxAdapter.cpp:3207-3210`): FBX does not
have this case.

**How.**

- **Option.** A model import gains the source scene: `--gltf-scene` on the
  ImportTool's `gltf` command, `gltf_scene` in the import manifest job (and so
  in retained recipes), and `SourceScene` in the editor's
  `ModelImportSettings`. It names a scene by name, so it survives the
  artist reordering scenes; an unnamed scene is named by index, `#2`. Without
  it the import keeps today's rule: the default scene, else the first.
- **Adapter.** The traversal starts at the chosen scene's root nodes. A chosen
  scene the file no longer has is the warning `import.gltf_scene_missing`, and
  the default rule applies, as for any optional input. Every scene not imported
  is reported by name with `import.source_feature_dropped`, a warning.
- **Products stay file-wide.** Geometry, materials and textures are cooked for
  every mesh and material of the file, as today (`GltfAdapter.cpp:996-1020`):
  they are assets a scene can reference on their own, so changing the source
  scene removes none of them. Only the model scene follows the choice.
- **Identity.** Anchors start at the chosen scene's roots (NGN2), so a node
  shared by two scenes has one ID in both. Changing the source scene keeps the
  changes on shared nodes; the others become kept changes, repaired by
  _Reassign_ (section 6.4).
- **Validation.** The adapter calls `cgltf_validate` after parsing, and its
  traversal refuses a node reached twice (`import.gltf_node_reached_twice`, an
  error), which also stops a cycle.
- **Scenes in the source analysis.** A glTF model job of `analyze-sources`
  reports the file's scenes: index, name, whether it is the default, and its
  root and node counts (`oxygen.source-analysis.schema.json`). The editor reads
  them from the analysis it already runs, with no extra process.
- **JSON chunk only.** The metadata parse of a `.glb` reads its 12-byte header
  and first chunk header, then only the JSON chunk, and parses that chunk as
  glTF JSON; buffer 0, which has no URI, is never loaded in metadata mode. A
  500 MB GLB costs a few hundred kilobytes to analyse.

**Done when.** A glTF file with two scenes imports the chosen one and reports
the other; a missing chosen scene warns and imports the default; a node reached
twice is an error; the source analysis lists the scenes; the metadata of a
`.glb` parsed from its JSON chunk alone equals the metadata parsed from the
whole file.

### 7.13 Retained source rebinding (NGN13)

**What.** When the ImportTool imports a model whose source file was moved,
re-exported elsewhere or copied, it finds the model's retained record and keeps
its source identity, so the node and surface IDs, and every change addressed by
them, survive. It decides from what the user states and from the source's
content, never from where the file sits, and it never requires deleting a
file.

**Why.** Node IDs (NGN2) and surface IDs are derived from the source identity
kept in the model import record (`<content_root>/imports/*.import.json`,
NGN14).
Called with `--content-root` and a source but no `--record`, the ImportTool
finds the record only through a recipe whose `source` is the same file
(`RetainedModelImport::RecordPath`, `Cooker/Import/RetainedModelImport.cpp:375-412`);
otherwise it creates a record named by a hash of the source's absolute path,
with a new identity (:446-456). A moved source therefore loses its identity
without a word, and leaves an orphan record that no diagnostic mentions: in
every scene that places the model, every change becomes a kept change. The
record stores no fingerprint of the source, so nothing could recognise the
move. Nor does anything stop two records from sharing a virtual path, although
the virtual path is the model's identity (`--virtual-path`, required,
`SceneImportRequestBuilder.cpp:125-130`) and names its products' asset keys:
both publish, with the same keys.

**How.**

- **Invariants.** One record per virtual path, as there is already one per
  source identity: `RequireUniqueRecord` (:137-156) also refuses two records
  with one `virtual_path`, naming both. A source file may back several records,
  one per virtual path: two models from one file.
- **Fingerprint.** At publication the record gains
  `source_fingerprint { size, sha256 }` of the primary source file, computed
  from the bytes the import read once `ImportSourceSnapshot::Verify()` has
  confirmed they did not change. It is absent until a record's first
  publication, as `published_generation` is null until then.
- **Lookup.** `RecordPath` becomes
  `LocateRecord(content_root, source, virtual_path)`. The first step that
  matches decides:

  | Step | Finds                                                                     | Means                                                                                                                       | Result                                                                                                                                                                                                       |
  | ---- | ------------------------------------------------------------------------- | --------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ |
  | 1    | A record with this virtual path                                           | The user names this model: its source moved, was re-exported elsewhere, or a copy becomes its source; the original may stay | That record. If its source is another file, the recipe's `source` is rewritten and `import.source_rebound` (info) names the old and new files                                                                |
  | 2    | A record whose source is this file, under another virtual path            | Another model from the same file                                                                                            | A new record; `import.source_has_model` (info) names the existing record, for the user who meant to rename that model's virtual path, which `--record` does                                                  |
  | 3    | A record whose source is missing and whose fingerprint equals this file's | The same file moved, and its virtual path with it                                                                           | That record, its source and virtual path rewritten; `import.source_relocated` (info). If several records match, none is taken: `import.source_relocation_ambiguous` (warning) lists them, and step 4 applies |
  | 4    | Nothing                                                                   | A new model                                                                                                                 | A new record, named `<source_identity>.import.json`; see the hints below                                                                                                                                     |

- **Hints at step 4.** A record with the same fingerprint whose source still
  exists is the original of a copy: `import.source_copy` (info) names it and
  says that importing with that record's virtual path makes this file its
  source. A record whose source is missing and whose file name equals this
  file's may be this model, moved and changed: `import.source_relocation_candidate`
  (warning) names it and the `--record` command that rebinds it.
- **Explicit record.** `--record <record> <source>` keeps its meaning: that
  record, rebound to this source, under the invariants.
- **Missing source on replay** (`--record` alone): `import.source_missing`
  (error) names the recorded path, instead of an adapter read failure, and says
  that importing the new file with the record's virtual path rebinds it.
- The editor uses the same records and the same lookup (NGN14). Its own
  relocations rewrite `source`, so the first step finds them; a model moved
  outside the editor is recognised as for the ImportTool.

The virtual path decides first because it is the user's own statement of which
model a source is. The fingerprint stands in for that statement only when the
virtual path changed too, and then only on proof: identical bytes, and the
original gone. A wrong adoption would silently apply one model's changes to
another model's same-named nodes; a missed one shows every change as kept, and
importing with the model's virtual path undoes it.

**Done when.** ImportTool tests: a moved source keeps its identity, with the
same virtual path and with a new one and identical bytes; a copy imported with
the original's virtual path becomes its source while the original stays; a copy
under a new virtual path is a new model with the `import.source_copy` hint; two
records with one virtual path are refused; a missing source on replay is
`import.source_missing`.

### 7.14 One model import record (NGN14)

**What.** A model's import state lives in one file, its **model import
record**, with one schema owned by the engine. It holds what to import (the
source, the model's virtual path, the settings) and what earlier imports
established (the source identity, the surface provenance, the published
anchors, the node continuity, the source fingerprint). The editor and the
ImportTool read and write the same records, in the same place, through the same
import job.

**Why.** Today the same state lives in two stores with two schemas and two code
paths:

| Store                      | Where                                                                                                      | Holds                                                                                                              |
| -------------------------- | ---------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------ |
| Editor sidecar             | `<model>.import.json` beside the model (`NativeSceneImportSettings`, `native-scene-import.schema.json`)    | Name, bundle root and primary file, the primary file's hash, the bundle's files, four policies, surface provenance |
| ImportTool retained record | `<content_root>/imports/*.import.json` (`RetainedModelImport`, `oxygen.retained-model-import.schema.json`) | The content root, a one-job recipe, surface provenance, the published generation                                   |

Each also duplicates state elsewhere: the editor copies the four policies and
the surface identity into every `batch` job (`ContentImportJob.cs:13-110`),
with manifest-builder defaults that repeat the sidecar's
(`ContentImportManifestBuilder.cs:162-165`), and its publication provenance
repeats the hash and the file list (`ImportedSourceDependencyState.cs:15`). The
engine record mixes import state with publication state
(`published_generation`). Every capability above that adds import state (NGN2's
continuity and published anchors, NGN13's fingerprint, the Model Editor's
settings) would otherwise be written twice.

**How.**

- **Place.** `<content_root>/imports/<source_identity>.import.json`. The
  editor's content root is the project root, so its records live in
  `<ProjectRoot>/imports/`, outside every authoring mount: the Content Explorer
  never lists them. The ImportTool's is its `--content-root`. The source
  identity never changes, so a record is never renamed or moved: relocating a
  model rewrites the record's `source`.
- **Schema.** `oxygen.model-import.schema.json` replaces both schemas:

  | Field                | Holds                                                                                                                |
  | -------------------- | -------------------------------------------------------------------------------------------------------------------- |
  | `schema_version`     | 1                                                                                                                    |
  | `source`             | The primary source file, relative to the record; inside or outside the content root                                  |
  | `virtual_path`       | The model's identity, under which its products are named                                                             |
  | `settings`           | The model import settings ([model editor](model-editor.md) section 5.3), in the import manifest's vocabulary         |
  | `source_identity`    | A UUID, from which node IDs (NGN2) and surface IDs are derived                                                       |
  | `surface_provenance` | As today, without a second copy of the identity                                                                      |
  | `node_continuity`    | The continuity entries (NGN2)                                                                                        |
  | `published_nodes`    | The anchor and node ID of each node the last import emitted (NGN2)                                                   |
  | `new_nodes`          | The published nodes whose anchor the publication before the last did not emit: _Reassign_ lists them first           |
  | `vanished_nodes`     | The nodes earlier imports emitted whose ID the last did not, kept until emitted again: the anchors kept changes lost |
  | `source_fingerprint` | Size and SHA-256 of every file the last import read: NGN13's relocation proof and the editor's reimport check        |

- **Left out, each owned elsewhere.** The content root: the record's
  grandparent folder. The published generation: publication state, kept by
  each publisher (the ImportTool in
  `<content_root>/.cooked/imports/<source_identity>/head.json`, the editor in
  its publication head). The bundle's file list: the source analysis reports it
  on every import. The importer's name: the schema version states it.
- **One job.** An import manifest job of type `model` names a record:
  `{ "type": "model", "record": "imports/<source_identity>.import.json" }`. The
  importer takes the source, virtual path, settings and identity from it, picks
  the glTF or FBX adapter from the source, and returns the record's new content
  (surface provenance, published nodes, fingerprint) in its report. The
  ImportTool's `gltf` and `fbx` verbs with `--record` and the editor's `batch`
  manifests both run it; jobs no longer carry `surface_source_identity`,
  `surface_provenance` or settings of their own, and the ImportTool's
  `--surface-source-identity` and `--surface-provenance` options are removed.
- **Commit.** Each publisher writes the reported content atomically with its
  own publication: the ImportTool in `Publish`, with today's lock and baseline
  check (`RetainedModelImport.cpp:725-757`); the editor in its publication
  transaction (`CookPublicationTransaction.Sources.cs:93-101`), which accepts
  changes only to the fields an import produces.
- **Lookup.** NGN13's `LocateRecord` serves both tools. The editor also keeps
  an index from source path to record (ED11).
- **Code.** `RetainedModelImport` becomes `ModelImportRecord`
  (`Cooker/Import/`), the record's one C++ reader and writer. The editor's
  `ModelImportSettings` reads and writes the same file against the installed
  schema, as the editor reads every engine schema.

**Done when.** The editor and the ImportTool importing the same model with the
same settings produce the same record content; a relocation in the editor
rewrites `source` alone; a publisher commits the import's new provenance with
its publication, and a failed import leaves the record unchanged.

### 7.15 Cooked format and cooker: one change, one recook

**What.** Every change section 7 makes to the cooked format, to the cooker's
inputs and to what the cooker emits, and the importer changes the Model
Editor's settings need ([model editor](model-editor.md) M-NGN1 to M-NGN10),
lands in one SDK release, so that every
model is reimported once and every scene and PAK rebuilt once.

**Why.** Three facts make a format change expensive, and make two of them twice
as expensive:

- Readers accept only the current version, with no migration: the scene loader
  refuses any other `kSceneAssetVersion` and asks for a recook
  (`Content/Loaders/SceneLoader.h:287-290`, `Data/SceneAsset.cpp:246-249`). A
  model scene is a scene asset, so the refusal covers every imported model as
  well as every authored scene.
- The editor treats every product as out of date when any SDK artifact changes:
  the ImportTool, the runtime libraries, the engine pak or a schema
  (`Oxygen.Managed.Core/src/Compatibility/NativeArtifactInventory.cs:14-91`,
  `Oxygen.Editor.ContentPipeline/src/Incremental/CookIncrementalPlanner.cs:106-130`).
  It also refuses to cook when its copies of the engine schemas differ from the
  installed ones (`NativeCompatibilityVerifier.Cooking.cs:37-46`), so the
  editor's descriptor generator moves with the cooker's schemas.
- A PAK copies each asset descriptor's bytes from the loose cooked roots
  (`Cooker/Pak/PakWriter.cpp:688-720`), so every PAK holding a recooked asset
  is rebuilt.

Model node IDs add a constraint of their own: a change stores its node path as
model node IDs, so NGN2 must ship before, or with, the first scene that can
store a change. Shipped later, it would turn every change on every model node
into a kept change.

**How.** One release carries the following. One `kSceneAssetVersion`
increase, from 15 to 16, covers every scene-side record, and one
`kScriptAssetVersion` increase, from 4 to 5, the parameter record scripts share
with scenes.

| Artifact                      | Change                                                                                                                                                                                                                                                                                                                                                                                                                                                              | Capability                              |
| ----------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | --------------------------------------- |
| `NodeRecord.node_id`          | Becomes a `data::NodeId`, as `NodePathElement` is; an authored node's is the descriptor's `id`, a model node's is derived from anchor and source identity                                                                                                                                                                                                                                                                                                           | NGN1, NGN2, NGN5                        |
| `NodeRef`                     | New: `{ node_index, node_path }`, the one way a cooked scene names a node                                                                                                                                                                                                                                                                                                                                                                                           | NGN5                                    |
| `OverrideRecord`              | Its node becomes a `NodeRef` target (70 to 74 bytes); sorted by `(target, override_id, part_id)`; `kNoNodePath` only for asset-sourced properties                                                                                                                                                                                                                                                                                                                   | NGN5                                    |
| `OverrideValue`               | Gains `as_quat`; size unchanged                                                                                                                                                                                                                                                                                                                                                                                                                                     | NGN4, NGN5                              |
| `override_id` values          | Every wired schema property has one; 1 to 4 unchanged                                                                                                                                                                                                                                                                                                                                                                                                               | NGN4                                    |
| New scene tables              | `kNodePath` (`NodePathRecord`), `kNodePathElement` (`NodePathElement`), `kScriptParamOverride` (`ScriptParamOverrideRecord`), with new `ComponentType` FourCCs in `Core/Meta/Data/ComponentType.inc`                                                                                                                                                                                                                                                                | NGN5                                    |
| `ScriptSlotRecord`            | Gains `slot_id`                                                                                                                                                                                                                                                                                                                                                                                                                                                     | NGN3                                    |
| `ScriptParamRecord`           | A node value becomes a `NodeRef`, in a scene's slots and in changes; shared with script assets, hence `kScriptAssetVersion` 4 to 5                                                                                                                                                                                                                                                                                                                                  | NGN4, NGN5                              |
| `SceneAssetDesc`              | `starting_camera` becomes a `NodeRef` (103 to 107 bytes)                                                                                                                                                                                                                                                                                                                                                                                                            | NGN5, NGN10                             |
| Scene loader                  | Reads and validates the new tables (`SceneLoader.h:472-581`, `ComponentTraits` in `Data/SceneAsset.h:45-90`); refuses repeated node IDs and slot IDs                                                                                                                                                                                                                                                                                                                | NGN1, NGN3, NGN5                        |
| Scene descriptor schema       | Node `id` required; `scene_instance.overrides`; `starting_camera` node path (`oxygen.scene-descriptor.schema.json` and its editor variant)                                                                                                                                                                                                                                                                                                                          | NGN1, NGN9, NGN10                       |
| Scripting sidecar             | `slot_id` parsed as a UUID and cooked; instance rows for script parameter changes                                                                                                                                                                                                                                                                                                                                                                                   | NGN3, NGN9                              |
| Scene schema and generator    | `SceneComponents.yaml` and `SceneComponents.schema.json`: override IDs everywhere, `Rotation` as one property, `model` source removed, the `SceneInstance` component (no wire); `Generated.SceneComponents.h` regenerated                                                                                                                                                                                                                                           | NGN4, ED1                               |
| Import manifest schema        | Job type `model`, naming a model import record; jobs lose `surface_source_identity`, `surface_provenance` and their own settings, and the ImportTool its `--surface-source-identity` and `--surface-provenance` options; the import settings gain `gltf_scene`, `up_axis`, `normals_smoothing_angle_deg`, `generate_lods`, `material_remap`, texture `max_size`, `import_lights`, `import_cameras` and `light_intensity_scale`; `bake_transforms` defaults to false | NGN12, NGN14, M-NGN3 to M-NGN8, M-NGN10 |
| Source analysis report schema | A model job reports the file's scenes, its detected unit, axes and handedness, its counts, and the names of its materials and textures (`oxygen.source-analysis.schema.json`)                                                                                                                                                                                                                                                                                       | NGN12, M-NGN2                           |
| Model import record schema    | `oxygen.model-import.schema.json` replaces `oxygen.retained-model-import.schema.json` and the editor's `native-scene-import.schema.json`: source, virtual path, settings, source identity, surface provenance, node continuity, published nodes, source fingerprint                                                                                                                                                                                                 | NGN2, NGN13, NGN14                      |
| Cooker diagnostics            | Node and slot ID errors; unresolved changes, surfaces and starting cameras as warnings; `surface_invalid` a warning                                                                                                                                                                                                                                                                                                                                                 | NGN1, NGN3, NGN9, NGN10                 |
| Model import                  | Node IDs by anchor; per-geometry LOD policy and surface defaults from the import settings, in the geometry fields that already hold them; the chosen glTF scene, the others reported; `cgltf_validate` and nodes reached twice refused; vanished and new anchors reported; generated LODs; remapped materials not produced; per-texture overrides applied; meshopt, WebP and KTX2 glTF data decoded                                                                 | NGN2, NGN12, M-NGN1, M-NGN4 to M-NGN9   |

Unchanged, and needing no format work: the loose cooked index
(`kLooseCookedIndexVersion` 6), the PAK header and directory (`kPakVersion` 10,
`kPakCatalogVersion` 3), asset keys (still hashes of virtual paths), and every
asset kind other than scenes and scripts.

**All of it ships together.** Every capability of section 7 is in the
release: the cooked data and the code that reads it arrive together, so no
shipped runtime meets a change it cannot apply.

**The recook, in order.**

1. Install the SDK, and rebuild the editor against it.
2. Bring the editor's files to the new formats outside the product, by
   editing their JSON: scene files, whose starting camera and node-typed script
   parameters become node references (ED1); and each editor sidecar
   (`Car.glb.import.json`) and ImportTool retained record becomes a model
   import record at `<content_root>/imports/<source_identity>.import.json`
   (NGN14), keeping its source identity. Deleting one instead gives its model
   new node IDs, so only a model no scene has changes on may be imported again. The product reads only the current formats: no
   format in this release has migration code.
3. Reimport every model. The editor does it in its first cook; the ImportTool
   replays each record (`import_models.ps1` without a source list replays them
   all). Each reimport records the anchors that later imports compare against,
   so the first one reports no vanished or new nodes.
4. Recook every script asset, and every authored scene after the models it instances (ED9 orders
   them). The editor writes node IDs from its `NodeId`s; the example and test
   descriptors gain their `id`s in the same change.
5. Rebuild every PAK from the recooked loose roots, the engine pak included
   when it holds scenes.

**Done when.** On a project cooked before the release, one cook after it
reimports every model, recooks every scene, and leaves no product at scene
format 15; a second cook does nothing. The editor's cook of a model whose
generation predates the release reimports it rather than reusing it.

## 8. Editor capabilities

The editor owns the authored side: the `SceneInstance` component in the scene
model, the edits that change it, the views that show instanced nodes, Live
Preview's projection, and the descriptor the cook reads. It learns what an
instance contains only from the engine's composed scene report (NGN11), and
drives instances in the engine only through the live API (NGN8); it never
decodes cooked bytes or resolves nesting levels. This section is the editor's
work list, each capability as what, why and how. Editor paths are relative to
`projects/`.

**Already in place.** The design extends these; it adds no parallel mechanism:

- **Override store.** `IOverridableComponent` (`Oxygen.Editor.World/src/Components/IOverridableComponent.cs:13`)
  holds a component's `SourceOverrides` (`SourceOverrides.cs:26`), keyed by
  `PropertyId(ComponentKind, PartKind, JsonPointer)`
  (`Oxygen.Managed.Core/src/PropertyId.cs:28`). `SetOverrides` and
  `OverrideWrite` (`Oxygen.Editor.Scenes/src/Edits/Properties/`) write it, and
  unresolved parts are kept (`SourceOverrides.cs:17-18`).
- **Schema.** `SceneSchema`, with `SceneSchema.Overridable`, is generated by a
  Roslyn generator from the engine SDK's `SceneComponents.yaml`
  (`Oxygen.Editor.Schemas/Generators/`), so an override ID the engine adds
  reaches the editor without hand-written tables.
- **Edits and history.** Every change is an `ISceneEdit`
  (`Oxygen.Editor.Scenes/src/Authoring/ISceneEdit.cs:23`) run by
  `SceneAuthoring`; locked targets are refused in `SceneEditScope.CheckTarget`
  (`SceneEditScope.cs:159-185`) with `SceneEditCodes.TargetLocked`.
- **Views.** The Scene Explorer's `DynamicTree` with `SceneNodeAdapter` and the
  Eye and Lock slots (`SceneRowActions.xaml`); its context menu and foot
  buttons show a refused command disabled with its reason as tooltip
  (`SceneExplorerViewModel.ContextActions.cs:120-124`, :399). The inspector's
  `PropertyCard` carries the override bar and reset
  (`Oxygen.Editor.Controls/src/Inspector/PropertyCard.cs:66-143`), and the
  shared `AssetPicker` (`Oxygen.Editor.Pickers/src/`) picks assets.
- **Live Preview.** `ScenePreview`'s projector turns `SceneChange`s into
  engine commands (`Oxygen.Editor.LivePreview/src/Scenes/ScenePreview.Projector.cs:33`),
  one Interop command class per operation (`Oxygen.Editor.Interop/src/Commands/`),
  with the native `NodeRegistry` mapping engine nodes back to `NodeId`s.
- **Cook.** `SceneDescriptorGenerator` writes the descriptor
  (`Oxygen.Editor.ContentPipeline/src/SceneDescriptorGenerator.cs:24`),
  `ContentCookScope.InputDependencies` orders cooks (`ContentCookScope.cs:73`),
  and the editor reads Inspector reports through `ImportToolContentPipelineApi`
  with their schemas checked.

**Missing:** a component that brings a source in, a way to read the composed
structure, selection and edits that address an instanced node, and views,
cook and Live Preview that know instances.

| ID   | Capability                    | Needed by                  | Builds on                                     |
| ---- | ----------------------------- | -------------------------- | --------------------------------------------- |
| ED1  | Instance component            | Section 4, SI-10           | -                                             |
| ED2  | Structure reading             | S5, sections 6.2 and 6.3   | NGN11                                         |
| ED3  | Instance edits                | S1, S6, S8, S10, S11, S15  | ED1, ED2                                      |
| ED4  | Scene Explorer                | S5, section 6.2            | ED2, ED3, ED7                                 |
| ED5  | Inspector                     | S5, S6, section 6.2        | ED2, ED3, ED7                                 |
| ED6  | Placement and pickers         | S1, S2, section 6.1        | ED3                                           |
| ED7  | Selection of instanced nodes  | S5, SI-13, section 6.9     | ED2, ED8                                      |
| ED8  | Live Preview and Interop      | S3 to S9 in Live Preview   | NGN8, ED1                                     |
| ED9  | Cook                          | S12, SI-1                  | ED1, ED12, NGN1, NGN3, NGN5, NGN9, NGN10      |
| ED10 | Kept changes                  | S7, S15, SI-7, SI-8, SI-16 | ED3, ED5, NGN2, NGN9                          |
| ED11 | Model Editor and model import | S13, S14                   | ED2, ED7, ED8, NGN2, NGN12, M-NGN1 to M-NGN10 |
| ED12 | Instanced starting camera     | SI-15, section 6.10        | ED1, ED5, NGN10                               |

ED1 comes first; ED2 and ED8 next; ED3 to ED7 and ED9 to ED12 build on them.
All of them ship with the engine release of section 7.15 (section 8.13).

### 8.1 Instance component (ED1)

**What.** A node of an authored scene can carry a `SceneInstanceComponent`
naming its source and holding every change the scene makes below it, addressed
by node path. It saves, loads, copies, undoes and retargets like any other
component.

**Why.** Section 4: the changes belong to the node that brings the source in,
and a component reuses every path the scene model already has for
components (add, remove, `Hydrate`/`Dehydrate`, undo by value, asset
references). The changes cannot live on instanced nodes' own components,
because instanced nodes are not in the editor's scene model: they are rebuilt
from the source on every reimport, as a `GeometryComponent` owns the overrides
of its asset's surfaces rather than the asset.

**How.**

- `Oxygen.Editor.World/src/Components/SceneInstanceComponent.cs`:
  `SceneInstanceComponent : GameComponent`, at most one
  per node, beside any other component. It holds:
  - `Source`, an `AssetPath`: a model's source file
    (`asset:///Content/Vehicles/Car.glb`) or an authored scene
    (`asset:///Content/Prefabs/Watchtower.oscene`);
  - `Changes`, an immutable `InstanceChanges`: per `NodePath`, an
    `InstancedNodeChanges` with the node's `SourceOverrides`, its script
    parameter changes (`Parameters`: per slot ID, per parameter name, a
    `ScriptValue`; they cook as the sidecar's `instance_bindings`), its last known
    name path (`Names`), in the anchor notation of section 7.2 and refreshed
    from the structure whenever the change resolves, which a kept change shows
    and swap matches (section 6.5), and `CarriedFrom`, the source a swap
    carried a kept change over from (SI-16).
- `NodePath` (`Oxygen.Editor.World/src/NodePath.cs`) is an immutable list of
  `NodeId`, shallowest first, with value equality and a canonical string form
  for JSON keys.
- `NodeReference(NodeId Node, NodePath Path)` (`Oxygen.Editor.World/src/NodeReference.cs`)
  is the one way the scene model names a node, as `NodeRef` is in the cooked
  scene: `Node` is a node of this scene and `Path` is empty, or `Node` is an
  instance root and `Path` runs below it. The starting camera (ED12),
  node-typed script parameter values (`ScriptingComponent.cs:131`), selection
  items (ED7) and lock keys (ED4) all hold one, so each can name an instanced
  node and none needs a second field for the path.
- It does not implement `IOverridableComponent`, whose overrides belong to one
  component: its changes are per node path, each a `SourceOverrides`, the same
  store type, and `SetOverrides` routes a write that carries a node path to
  them (ED3).
- The engine's `SceneComponents.yaml` gains the `SceneInstance` component
  (kind `scene-instance`, the asset property `Source`, no wire: it is cooked as
  the descriptor's `instance`), so the generated schema knows its kind
  (section 7.15).
- Serialization follows the World recipe: `SceneInstanceComponentData` with its
  `[JsonDerivedType]`, the `SceneJsonContext` entry
  (`Serialization/SceneJsonContext.cs:62`), registration in `GameComponent`,
  `SceneNode.HydrateOptionalComponents` (`SceneNode.cs:202`),
  `ComponentFamilies` and `SceneComponents`. `SceneAssetReferences` and
  `SceneReferenceFields` reach `Source` and every asset-typed change value, so
  relocation and retargeting update them as they update a geometry reference.

```json
{
  "$type": "SceneInstance",
  "Source": "asset:///Content/Vehicles/Car.glb",
  "Changes": [
    {
      "Path": ["5d0c…"],
      "Names": "Body/Door_FL",
      "Components": {
        "transform": {
          "Values": { "/local_rotation_euler_degrees": [0, 70, 0] }
        }
      }
    },
    {
      "Path": ["91aa…"],
      "Names": "Body",
      "Components": {
        "geometry": {
          "Parts": [
            {
              "Kind": "surface",
              "Id": "…",
              "LayoutRevision": "…",
              "Values": { "/material": "asset:///Content/Paint/Red.omat" }
            }
          ]
        }
      }
    }
  ]
}
```

**Done when.** A scene with instances and changes round-trips through save and
load; duplicate, copy and paste and undo carry the changes; relocating
`Car.glb` rewrites `Source`.

### 8.2 Structure reading (ED2)

**What.** For any source, the editor reads what an instance of it contains:
every instanced node by node path, with its name and parent, and the scene it
instances when it is a nested instance root. Values are read per selected node,
not per source (section 8.5).

**Why.** The Scene Explorer, the inspector, selection, swap and kept changes
all need to know what an instance contains (sections 6.2 to 6.5). Structure is
the source's own: an instance changes values only (SI-5), so the hierarchy is
in the cooked scene descriptors and nothing has to be composed. A model can be
gigabytes of geometry; its hierarchy is a few records per node.

**How.**

- The engine reads it: `IAssetLoader::ReadMountedSceneHierarchy(key)`
  (`Oxygen.Engine/src/Oxygen/Content/IAssetLoader.h`), beside
  `EnumerateMountedScenes` in `SceneCatalogQueryService`, reads the scene's
  descriptor through the mounted source that provides its key, parse-only,
  then each scene its `SceneInstanceRecord`s name, once per key. It loads no
  geometry, material, texture or other dependency and caches nothing. An
  instanced scene that is not mounted, a cycle or an instance record naming no
  node is an issue of the hierarchy, which keeps every other node.
- Interop's `ReadSceneHierarchyCommand` runs it on the engine thread for a
  virtual path; `OxygenWorld.ReadSceneHierarchyAsync` and the runtime's
  `IRuntimeWorldCommands.ReadSceneHierarchyAsync` carry it to the editor for a
  scene activation, as node observations are.
- The session's `SceneStructureStore` (`ISceneStructures`, in
  `Oxygen.Editor.ContentPipeline/src/Inspection/`) holds the editor's one copy
  of each structure: the last read of each source, kept until a newer read
  replaces it. It depends on nothing, so documents, their edit checks and the
  lock rule can use it while Live Preview follows those documents.
  `Find(project, source)` looks one up synchronously from any thread; it finds
  none while the source was never read: no scene shows it, or it is not
  mounted yet.
- Live Preview's `ScenePreview` fills the store. It reads, through the
  activation that shows the active scene, the structure of every source that
  scene instances: when the scene becomes ready, when an instance of a source
  not read yet is placed, and whenever the engine mounts the project's content
  again, as after a cook or a reimport. A model source maps to its scene
  product (`Car.glb` to `Car.glb/Car.oscene`) through
  `AssetFileTypes.SceneProductOf`.
- The store's `Changed` event fires on the thread that read the structures;
  views rebuild from `Find` then.
- `SceneStructure` holds `StructureNode`s by `NodePath`: name, parent path,
  defining scene, the scene each nested instance root instances, the geometry
  it renders and whether it has a camera (perspective or orthographic, for the
  starting camera, section 8.12), and the hierarchy's issues.
- A structure the store does not hold yet is read on demand with
  `IInstancedNodeReader.ReadStructureAsync`, which `ScenePreview` implements as
  it reads the others and keeps the result in the store: a swap that keeps
  changes by name reads the new source first (section 8.3).
- While a newly placed source's first cook runs it is not mounted: the instance
  root shows no rows until the cook mounts it.

**Done when.** The structure of a two-level nested scene lists every node by
its path; remounting after a cook reads it again; a source that is not mounted
yields none; reading a model reads no geometry.

### 8.3 Instance edits (ED3)

**What.** Every operation of section 6 on an instance is one undoable
`ISceneEdit`: placing, changing a value of an instanced node, swapping,
unpacking, applying to source, removing and reassigning kept changes.
Structural edits never reach an instanced node.

**Why.** History, dirty state and Live Preview follow `SceneAuthoring`, so an
operation outside it would escape undo and the engine. Instanced nodes are not
`SceneNode`s, so no structural edit can name one; what remains is that every
command offered on an instanced node refuses structure with one code and one
message.

**How.**

| Edit                 | Effect                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                |
| -------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `InsertNodes`        | With `SceneNodeTemplates.Instance(source)` (`Oxygen.Editor.Scenes/src/Nodes/SceneNodeTemplates.cs:18`), creates the instance root named after its source                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                              |
| `SetOverrides`       | `OverrideWrite` gains an optional `NodePath`. With one, the write lands in the instance root's `Changes`; validation uses `SceneSchema.Overridable`, and a surface part's owner comes from one function, `PartOwners.Resolve(target)`: the component's `PartOwner` for an owned node, the instanced node's geometry in the structure (ED2) for an instanced one                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                       |
| `SetInstanceSource`  | Sets `Source`; refuses a source that instances this scene (`SceneEditCodes.InstanceCycle`), by `InstanceRules.WouldCycle` over the cook dependency graph, the rule the pickers use; with changes, keeps them by name path (`InstanceSwap.KeepByName`, through `InstanceNamePaths`, which records each change's `Names`) or discards them (section 6.5), marking unmatched ones `CarriedFrom` the old source. Keeping them while the new source's structure is not read is refused with `InstanceUnavailable`. Its revert (`InstanceSwap.Replace`) restores the source and the exact changes; it raises `InstanceSourceChanged`, which Live Preview and the Scene Explorer rebuild the instance from                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                   |
| `UnpackInstance`     | `One` or `Complete` (section 6.8): owned `SceneNode`s with new `NodeId`s replace the component, first among the root's children; nested instance roots stay one with their changes re-rooted, this scene's winning. `InstanceConversion` builds the edit before it runs: an authored scene source gives its file's stored nodes with the changes written over them, a model the values the mounted content holds (`IInstancedNodeReader`), each with the edit's `UnpackedNode` (path, copy); a source not readable now refuses with `InstanceUnavailable`, and the edit refuses when the instance changed since. References to an unpacked node are retargeted: the starting camera, locks and hidden nodes, node values of scripts and of changes. Changes of nodes the source lacks are dropped with a `NodePathUnresolved` warning. A rotation is written as the change's angles where a change set it, else decomposed from the composed quaternion (YXZ): the same rotation, possibly as other angles. Its revert, `RestoreInstance`, removes the copies and restores the component and the references. _Copy_ of instanced nodes uses the same conversion, unpacking completely the subtree of each copied node |
| `ApplyToSource`      | Moves changes into an authored scene on the node path (section 6.7). `SourceApplication` builds it before it runs: the authored scenes the path passes through, read from their files, and the chosen file's bytes before and after, a change of a node the scene owns written as its stored value and a deeper one merged into the changes of its instance root, the moved values winning; a selection (`ChangeSelection`) takes the whole instance, one node, one component or one row's values. The edit writes the file through `IAuthoredSceneFiles` and keeps its previous bytes; its revert, `RestoreSource`, writes them back and restores the changes. Either is refused with `SceneEditCodes.SourceChanged` when the file no longer holds the bytes it expects, with `InstanceUnavailable` when the instance's changes changed since it was built, and with `AssetReferenceInvalid` for a model; changes whose node the scene lacks stay, listed in a `NodePathUnresolved` warning                                                                                                                                                                                                                          |
| `ReassignKeptChange` | Repairs a kept change where it was lost (section 6.4), as `KeptChanges.RepairOf` decides. In the model: the record gains the entry that the chosen node's anchor continues the lost one (from `vanished_nodes`), its previous bytes recorded; the content pipeline, the one writer of model import records, writes it when the record still holds the bytes read and reimports the model, as one operation of its writer; the scene's changes of the chosen node under its own ID become kept changes, listed in a `NodePathUnresolved` warning; the revert, `RestoreModelRecord`, writes the previous bytes back and reimports, refused with `SourceChanged` when the record changed since. In the instance: the change moves to the chosen node, its values winning over the node's own; the revert is `RestoreInstanceChanges`. A node the source lacks is refused with `NodePathUnresolved`, a node that already continues another with `OverrideInvalid`                                                                                                                                                                                                                                                         |
| Kept change removal  | `RemoveKeptChanges` removes the listed kept changes, component values and script parameters alike, labelled _Remove kept changes_; its revert, `RestoreInstanceChanges`, gives the instance its exact changes back. Kept changes are told against the source's complete structure: while it is not read, both kept-change edits are refused with `InstanceUnavailable`                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                |

- `SceneEditCodes` (`Oxygen.Editor.Scenes/src/Authoring/SceneEditCodes.cs:11`)
  gains `InstancedStructure`, `InstanceCycle`, `NodePathUnresolved` and
  `SourceChanged`.
- Every command that changes structure (rename, delete, cut, duplicate, drag,
  reparent, reorder, paste into, new child, add or remove component) asks one
  rule, `InstanceRules.RefusesStructure(target)`, before it builds an edit; its
  message names the node and source and what to do: "'Door_FL' comes from
  Car.glb. Unpack Car3 to change its structure." `SceneEditScope` raises
  `InstancedStructure` for an edit that receives a node path anyway.
- A command over a selection holding an instanced node is refused whole, as a
  locked node refuses it today.

**Done when.** Each edit applies, undoes and redoes; Live Preview follows; each
structural command on an instanced node is refused with the message.

### 8.4 Scene Explorer (ED4)

**What.** The tree shows an instance root's instanced nodes under it, dimmed,
with the instance root's link badge, change marks and kept-change caution, and
offers only the commands that apply (section 6.2).

**Why.** Today a row is a `SceneNodeAdapter` over a `SceneNode`
(`Oxygen.Editor.SceneExplorer/src/SceneNodeAdapter.cs:17`), and no row draws a
dimmed state: `IsEffectivelyHiddenInEditor` is computed for a "dimmed row
presentation" (`SceneExplorerViewModel.cs:2170`) that nothing renders.

**How.**

- `InstancedNodeAdapter : LayoutItemAdapter`, built from the structure (ED2)
  under the instance root's adapter, children created when expanded. It holds
  the instance root's `NodeId` and the node's `NodePath`; Eye and Lock work
  through the same `SceneRowActions`, keyed by the node's `NodeReference`
  (settings LLD 5.1).
- The row template binds the name's and glyph's foreground to a row state:
  `TextFillColorSecondaryBrush` for an instanced row, which also gives the
  editor-hidden rows their missing dimming.
- The instance root's glyph carries the link badge, and the caution glyph when
  it has kept changes; a row with changes carries the override bar.
- `GetContextActions` adds _Open model_ (model instances), _Select instance_,
  _Unpack_, _Unpack completely_, _Apply to …_ (one per authored scene on the
  path) and _Reset changes_, and `GetContextDisabledReason` returns the
  `InstanceRules` message for structural commands, so menu items and foot
  buttons are disabled with it as tooltip.

**Done when.** `Car3` expands into the car's nodes, dimmed; a locked instanced
node shows both states; every structural command is disabled with the message.

### 8.5 Inspector (ED5)

**What.** The Properties pane shows an instanced node like an owned one: its
components with their effective values, editable, each change marked with the
override bar and reset; a header that names its source; and, on an instance
root, the Scene Instance section.

**Why.** Sections are hand-written per component and bound to World component
instances (`Oxygen.Editor.SceneInspector/src/Components/InspectorEditorFactory.cs`),
and an instanced node has none. Writing a second set of sections for instanced
nodes would double every section and let them drift.

**How.**

- Per selected instanced node, the engine reads its effective values from the
  mounted descriptors, as it reads the hierarchy (section 8.2, which carries
  structure only): `IAssetLoader::ReadMountedSceneNode(key, path)` composes the
  source with `ComposeInstance`, as an instance of it would be, and returns the
  node's name, its geometry and every property its records hold, with the
  geometry values it replaces, in the schema's units and assets by their
  mounted virtual paths. Interop's `ReadSceneNodeCommand` and
  `IRuntimeWorldCommands.ReadSceneNodeAsync` carry it to
  `IInstancedNodeReader.ReadNodeAsync`, implemented by `ScenePreview`, which types each value by
  `SceneSchema.Changeable`. The values materialize as **transient
  components**: a node in no scene's tree whose World component instances
  (`TransformComponent`, `GeometryComponent`, a camera, a light) hold the
  effective values, written through the component descriptors, with this
  scene's changes as their overrides. The existing sections bind to them
  unchanged. Content mounted again reads the selected nodes again.
- Sections name each edited node by its `NodeReference`: a node of the scene
  by its `NodeId`, an instanced node by its instance root and path. A
  transient node keeps its source's `NodeId`, which two instances of one
  source share, so no section keys an edit by `NodeId`. `NodeOverrides`
  (`Oxygen.Editor.Scenes`) turns the writes of an instanced node into
  `OverrideWrite`s with its path (ED3), and gestures begin with
  `BeginNodeGestureAsync`, so a section never knows which it edits.
- A field's override bar and reset (`InspectorFieldOverride`) count the
  edited nodes that change it: for an instanced node, this scene's change in
  its instance root's `Changes`, never the values its source sets. Geometry
  rows show the effective values and mark only those changes. Reset removes
  the change, so the source's value applies. The geometry asset, which the
  source decides, cannot be replaced, and changes kept for lost surfaces stay
  those of the scene's own nodes (kept changes of instanced nodes: ED10).
- `SceneNodeDetailsView`'s header shows the name read-only and the second line
  "in Car3 · Car.glb (model)" with a link to the Model Editor on that node and
  a button that selects the instance root; _Add component_ and _Remove
  component_ are disabled with the refusal.
- `SceneInstanceSectionView` and its view model: Source, an `AssetPicker` of
  models and authored scenes (section 6.1's filter, `InstanceSources` in
  `Oxygen.Editor.Pickers`, shared with _Add > Model…_ and _Add > Scene…_),
  which swaps the source: with changes, a dialog asks _Keep changes by name_,
  _Discard changes_ or _Cancel_; the change count; the
  caution box with each kept change's name path and property, _Reassign…_,
  _Remove_ and _Remove all_ (ED10); and, on a prefab instance, the notice that
  the scene's environment and start state apply only when it runs as a level.
- _Apply to source_ (section 6.7) comes from one service,
  `InspectorSourceApply`: `PropertyCard` asks its `OverrideActions` source for
  the commands when its context menu opens, so every row with an override bar
  offers them, and the component list, the header and the Scene Instance
  section use the same commands.
- The "Locked for editing" bar (`SceneInspectorView.xaml:178-186`) shows for a
  locked instanced node as for an owned one.

**Done when.** Rotating `Door_FL` in the inspector stores one change on `Car3`
and Reset removes it; every section shows the same rows for an owned and an
instanced node of the same components.

### 8.6 Placement and pickers (ED6)

**What.** The user places a model or a scene from the scene toolbar, the
Content Explorer and the Model Editor, picking a model by its source file
(SI-2).

**Why.** The toolbar's _Add_ menu offers empty nodes, shapes, lights, cameras
and fog volumes (`Oxygen.Editor.SceneEditor/src/SceneEditorViewModel.Toolbar.cs:49-71`),
and pickers list a model's products rather than the model
([#106](https://github.com/abdes/DroidNet/issues/106)).

**How.**

- `BuildQuickAddMenu` gains _Model…_ and _Scene…_, each opening an
  `AssetPicker` flyout. Models are listed through the model asset kind, one row
  per source file ([#106](https://github.com/abdes/DroidNet/issues/106), step
  5); scenes for which `InstanceRules.WouldCycle` holds are not listed.
- The new node is placed by `GetPlacementAsync` (`SceneEditorViewModel.Toolbar.cs:130`)
  and inserted with `InsertNodes` and `SceneNodeTemplates.Instance`, then
  selected.
- The Content Explorer's context menu on a model or a scene gains _Add to
  scene_, which does the same in the open scene, and the Model Editor's toolbar
  has it too (M6).

**Done when.** Each entry point places an instance at the placement point and
selects it; a scene that would make a cycle is not offered.

### 8.7 Selection of instanced nodes (ED7)

**What.** Selection holds owned nodes and instanced nodes alike, and the
viewport selects the outermost instance root on a click and the exact node on
a double-click (SI-13).

**Why.** `ISceneSelectionService.SetSelection` takes `SceneNode`s and stores
`NodeId`s (`Oxygen.Editor.Scenes/src/Selection/ISceneSelectionService.cs:34`,
`SceneSelectionContext.cs:41`); an instanced node has neither. The viewport
handles single clicks only (`ViewportViewModel.Selection.cs:45`).

**How.**

- `NodeReference` (ED1) replaces `SceneNode` in `SetSelection`, and
  `SceneSelectionContext` stores references. The Scene Explorer, the inspector, the
  viewport and `PropertyFocusRequested` exchange references.
- `RuntimePickHit` (`Oxygen.Editor.Runtime/src/Engine/RuntimePickHit.cs:16`)
  gains the node path of an instanced hit, from the `NodeRegistry` entry ED8
  registers.
- `PickAsync` maps a click on an instanced node to its outermost instance
  root. `Viewport` handles `DoubleTapped`: it picks at the point and selects the
  hit node with its path, however deep.
- Lock state (`Oxygen.Editor.Scenes/src/Interaction/SceneNodeInteraction.cs`)
  is keyed by `NodeReference`, as settings LLD 5.1 states. A key that no
  longer resolves after a swap, a reimport or kept-change removal is dropped
  when the structure is read; Unpack retargets its nodes' keys first.
- A lock protects the node and every node below it, instanced or not.
  `ISceneNodeInteraction.GetLock(scene, reference)` returns a `NodeLock`: the
  closest locked node among the node, its ancestors in its instance's
  structure (`StructureNode.Parent`, across nesting levels), the instance root
  and the root's ancestors. The structure comes from the store (section
  8.2). Until it is read, nothing tells whether an ancestor is locked, so the
  lock is the unread instance and edits are refused: "The contents of 'Car3'
  are not available yet, so its nodes cannot be edited."
  (`SceneEditCodes.InstanceUnavailable`). The Explorer, the inspector, the
  gizmo and `SceneEditScope` all ask this one rule.
- Deleting or moving a subtree is refused when a node an instance in it brings
  in is locked, as for a locked owned descendant.
- The transform gizmo moves the selection's roots. A target whose instance
  root or scene ancestor is selected is dropped by the editor; a target below
  another target in the same instance is dropped by the native gizmo, which
  reads the live tree.

**Done when.** A click on a door selects `Car3`, a double-click selects
`Door_FL`; the selection survives switching between the tree, viewport and
inspector.

### 8.8 Live Preview and Interop (ED8)

**What.** Live Preview builds every instance in the engine through the live API
and keeps it in step with the scene's edits and the sources' republication.

**Why.** S5 to S8 happen in Live Preview. The projector projects overrides of
owned components per part today
(`Oxygen.Editor.LivePreview/src/Scenes/ScenePreview.Helpers.cs:259-308`), and
Interop has no way to instance a scene under a node; building instanced nodes
in the editor would duplicate composition (section 10).

**How.**

- The projector handles a `SceneInstanceComponent` added, its `Source`
  changed, or its source republished: it waits until the source scene and its
  dependencies are mounted, as `AwaitingMount` does for geometry, then
  instances it.
- `OverridesChanged` on an instance root carries the changed node paths; the
  projector sends one `SetInstanceChange` per changed node and property, or
  `ClearInstanceChange` when a change is removed.
- Removing the component or the node sends `DetachInstance`.
- Interop adds `InstantiateSceneCommand`, `SetInstanceChangeCommand`,
  `ClearInstanceChangeCommand` and `DetachInstanceCommand`
  (`Oxygen.Editor.Interop/src/Commands/`), with `OxygenWorld` methods of the
  same names over NGN8; values are encoded by the engine's `EncodeChange`.
- `NodeRegistry` registers each instanced node under its instance root's
  `NodeId` and node path, so a pick maps back to a `NodeReference`.

**Done when.** Placing, changing, swapping and removing an instance show in the
viewport with no scene rebuild; a reimport of the source updates every
instance.

### 8.9 Cook (ED9)

**What.** The descriptor carries node IDs, instances with their changes, the
instanced starting camera and script slot IDs, and the editor cooks an
instanced scene before the scenes that instance it.

**Why.** Today `SceneDescriptorGenerator.AddNode` writes a node as its array
index, with no ID (`SceneDescriptorGenerator.cs:223-244`), which NGN1 replaces
with the node's ID, and the descriptor has no instance changes.

**How.**

- `AddNode` writes `id` from the node's `NodeId`; parents stay indices.
- An instance root writes `instance.scene_ref`, the source's scene (a model's
  scene product), and `overrides` per node path, each property by its schema
  key in schema units (section 7.9).
- The starting camera writes its node path when it names an instanced camera
  (ED12).
- The scripting sidecar (`SceneDescriptorGenerator.Scripting.cs:66`, :82)
  already writes slot IDs; it gains the instance rows for script parameter
  changes, and writes every node-typed value, a scene's own slots included, as
  a node index and node path.
- `ContentCookScope.InputDependencies` of an instancing scene includes its
  sources' scenes, so they are cooked first and the cooker checks cycles and
  unresolved changes against them.
- Kept changes are written as they are; the cooker reports and leaves them out
  (NGN9).

**Done when.** The descriptor of Harbor validates against the schema and cooks;
Harbor recooks after `Car.glb` is reimported.

### 8.10 Kept changes (ED10)

**What.** Kept changes are visible where they matter and repairable in place:
the instance root's caution box, the Problems pane, and the Model Editor's node
continuity.

**Why.** SI-7, SI-8 and SI-16. The cooker refuses a lost surface today
(section 7.9); the editor itself has no publication gate for it, and keeps
unresolved parts already (`SourceOverrides.cs:17-18`), so the editor work is
presentation and repair, not removal of a gate.

**How.**

- `KeptChanges` (`Oxygen.Editor.Scenes`) tells an instance's kept changes
  against its source's complete structure: none while the structure is not
  read or misses an unreadable scene's nodes. A change records its node's
  names whenever it is written on a node the structure holds, and a swap that
  keeps changes by name lists those it keeps in a `NodePathUnresolved`
  warning.
- The caution box of the Scene Instance section (ED5), a warning `InfoBar`,
  lists each kept change with its last known name path and properties, and
  the source it was carried from, with _Reassign…_ (section 6.4,
  `ReassignKeptChange`), _Remove_ and _Remove all_ (`RemoveKeptChanges`). It
  follows the structures as they are read again. The Scene Explorer marks the
  instance root's glyph with the caution glyph while it has kept changes.
- A change kept for a surface the geometry no longer declares, on an instanced
  node that still exists, is listed with that node's surfaces in its Geometry
  section, as for an owned node: _Remove_, _Remove all_ and _Apply to_ a
  current surface (`SurfaceEdits.Reset`, `SurfaceEdits.RetargetAsync`, by node
  reference).
- _Reassign…_ opens the node picker the Model Editor's continuity flyout
  ([model editor](model-editor.md) section 5.3) shares: the nodes new in the
  last import first (`new_nodes`), then every node, with Find; a node that
  already continues another is disabled with the reason, and choosing a node
  whose own changes become kept says how many. It says where the repair
  happens: in the model, for every scene, or in this instance only.
- The cook's warnings (`scene.descriptor.instance_override_unresolved`,
  `scene.descriptor.surface_invalid`, `script.sidecar.instance_override_unresolved`)
  name their node: `KeptChangeDiagnostics` maps a descriptor location to the
  instance root and the change's names, or, for a lost surface, the node whose
  Geometry section lists it, with Go To Property. `KeptChangeProblems` puts them in the Problems pane,
  each cook of a scene replacing that scene's; _Select node_ there runs the
  same Go To Property, which selects the instance root and focuses its caution
  box through `PropertyFocusRequested` (`KeptChangeDiagnostics.PropertyPath`).

**Done when.** A renamed node's change shows in the caution box and as a cook
warning; _Reassign…_ restores it in every scene.

### 8.11 Model Editor and model import (ED11)

**What.** Opening a model shows it in the Model Editor, and importing one goes
through the Import model dialog, both with the same import settings panel.

**Why.** S13 and S14. Specified by [model editor](model-editor.md): its
capabilities M-ED1 to M-ED9 and the engine's M-NGN1 to M-NGN10.

**How.** In summary:

- `Oxygen.Editor.Models`, a new family module, owns `ModelImportSettings`,
  the editor's reader and writer of the model import record (NGN14), which
  replaces `NativeSceneImportSettings` and its sidecar; `ModelImportRecords`,
  an index from source path to record built from `<ProjectRoot>/imports/` and
  kept current by file watching, which replaces the six sidecar-existence
  checks (`CookInputResolver.cs:95`, `CookSourceAnalyzer.cs:196-208`,
  `AssetFeed.cs:260`, `CookRootRepair.cs:110`, `AssetsViewModel.cs:374`,
  `AssetsViewModel.AssetActions.cs:183`); `ModelDocumentKind`, `ModelContent`,
  its problems and the `IModelImportPrompt` port.
- The content pipeline runs a model's import as a `model` job naming its
  record: `ContentImportJob` loses its copied policies and surface identity,
  and `ContentImportManifestBuilder` its policy defaults. The publication
  transaction commits the record's new content with the publication, and the
  record's source fingerprint replaces `ImportedSourceDependencyState` as the
  reimport check.
- Relocating a model rewrites its record's `source`
  (`Relocation/AssetRelocationPlanner.cs:92-104` no longer moves a sidecar),
  and a model is imported when a record names it ([asset identity](asset-identity.md)).
- `Oxygen.Editor.ModelEditor`, its UI project, holds the document's view, the
  `ModelImportSettingsPanel` and the Import model dialog that replaces
  `SceneImportDialog`.
- The preview is a scene with one model instance, so ED2, ED7 and ED8 do the
  work.

**Done when.** The exit gates MG1 to MG10 of [model editor](model-editor.md)
pass.

### 8.12 Instanced starting camera (ED12)

**What.** The scene's starting camera can be an instanced camera, chosen in
the Play section or with the camera section's toggle (SI-15).

**Why.** `Scene.StartingCamera` is a `NodeId?`
(`Oxygen.Editor.World/src/Scene.cs:53`), and `StartingCameraChoice` holds a
`NodeId?` (`Oxygen.Editor.SceneInspector/src/SceneSettings/StartingCameraChoice.cs:14`).

**How.**

- `Scene.StartingCamera` and `SceneData.StartingCamera` become a
  `NodeReference?` (ED1), and `StartingCameraChoice` holds one; the descriptor
  writes it as the cooked `NodeRef` (ED9).
- `PlaySectionViewModel` lists instanced cameras under their instance root,
  from the structure, with their names (`CameraRig › Rig › MainCam`). Without
  a starting camera, Automatic names the first camera in composed order: the
  scene's own cameras, then those its instances bring in. A lost one stays
  listed with the caution glyph and the message naming its instance root.
- `StartingCameraToggle` (`Components/Cameras/StartingCameraToggle.cs:19`)
  works on a transient camera component (ED5) through the instanced write
  target.
- A starting camera whose path no longer resolves shows with the caution
  glyph; `UnpackInstance` retargets it to the owned copy.

**Done when.** A `CameraRig.oscene` camera chosen in Harbor's Play section is
the camera Play starts through.

### 8.13 What ships with the engine release

The engine release of section 7.15 refuses scene descriptors without node IDs
and changes the schemas the editor embeds, and the editor refuses to cook when
its schema copies differ (`NativeCompatibilityVerifier.Cooking.cs:37-46`). So
the editor capabilities of this section ship in the same change as the
engine release: no editor build meets content or schemas it does not
understand.

## 9. Exit gates

Each capability's _Done when_ in sections 7 and 8, and the
[model editor](model-editor.md)'s gates, are exit gates too; the rows below are
the end-to-end checks.

| Gate | Check                                                                                                                                                                                                                                |
| ---- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| G1   | `Car.glb` placed five times with _Add > Model…_ shows each car in the viewport and the Scene Explorer, and Play shows the same; `BurgerPiz.glb`, a 685-node district, places and plays the same way                                  |
| G2   | Opening `Door_FL`, hiding the Glass surface and painting the body red survive save, reopen, cook and Play, and each Reset restores the model's value                                                                                 |
| G3   | Reimport with `Door_FL` renamed: the other changes apply, the door's change is a kept change and the scene cooks with a warning; _Reassign_ onto `Door_Front_L` restores the change on every car in every scene, and Undo reverts it |
| G4   | Swap a car to `Truck.glb` with _Keep changes by name_ keeps the changes whose node the truck has and lists the rest as kept; _Discard_ removes all; Undo restores either                                                             |
| G5   | Harbor, placing `Watchtower`, which places `Torch` twice, puts out one tower's north torch two levels down; the runtime applies it over Watchtower's own value of that flame                                                         |
| G6   | _Apply to source_ moves a change into `Torch.oscene`, every torch of every tower shows it, Undo restores both scenes                                                                                                                 |
| G7   | _Unpack_ and _Unpack completely_ produce owned nodes whose resolved values equal the instance's; Undo restores the instance                                                                                                          |
| G8   | A cycle is refused by the Source picker, by `SetInstanceSource` and by the cooker with a named diagnostic                                                                                                                            |
| G9   | A surface lost on geometry reimport no longer blocks publication or the scene cook: it is a kept change with a warning                                                                                                               |
| G10  | Engine tests: node IDs stable across reimport, move and rename of the model; resolution order across nesting levels; unresolved node paths as issues; the live API's set and clear                                                   |

## 10. Alternatives rejected

| Alternative                                                                | Why not                                                                                                                                                                                              |
| -------------------------------------------------------------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Copy the model's nodes into the scene at placement or cook                 | Reimport would not update scenes; the editor and runtime would hold diverging copies (SC-1)                                                                                                          |
| Address instanced nodes by name path                                       | Renames and duplicate names retarget changes silently; name paths are only the swap fallback                                                                                                         |
| Block the cook on kept changes until repaired                              | No engine surveyed does; one broken piece of a model would stop a whole scene                                                                                                                        |
| Editor builds instanced nodes in Live Preview node by node                 | Duplicates `SceneInstantiation`; the editor and runtime could resolve nesting levels differently                                                                                                     |
| Editor decodes cooked scenes to show instanced nodes                       | The engine owns the format; the Inspector reports it, as for geometry inventories                                                                                                                    |
| A separate override system for instances                                   | The scene model's one override store and reset already cover parts; a node path is one more step in the address                                                                                      |
| Propose a match for each vanished node by name, mesh or transform          | Names, shared meshes and empty nodes repeat in most models, so a match is rarely evidence, and a wrong one moves a change without a word; the import reports vanished and new nodes as facts instead |
| Stable node IDs authored in the model file (glTF `extras`, FBX properties) | Models come from authors whose tools and habits the project does not control                                                                                                                         |
