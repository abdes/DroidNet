# Scene composition: instancing model and authored scenes

Status: **design**. Extends
[scene instantiation](scene-instantiation-and-player.md) and the editor's
[content identity](../../../../design/editor/lld/content-explorer-asset-identity.md).
The editor workflows that use it are in
[scene instances](../../../../design/editor/lld/scene-instances.md); this
document owns the engine contract: node identity, the cooked format,
instancing with changes, the live instancing API and the scene report.

Summary: a glTF or FBX import keeps its scene, the hierarchy of nodes with
their transforms, meshes, materials, cameras and lights, as a read-only
**model scene**. An authored Oxygen scene composes model scenes by
**instancing** them: one node in the authored scene stands for the whole
model, and the model's nodes appear under it when the scene is instantiated.
An authored scene can be instanced the same way (a prefab), and the instance
root carries the instancing scene's changes of the instanced nodes.
Oxygen-specific data (scripts, the starting camera and input contexts,
environment, the Play section) lives only on authored scenes. Nothing is
patched into import results.

## 1. Problem

| Fact today                                                                                                                                                                                                                                                                                                     | Consequence                                                                                                                 |
| -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | --------------------------------------------------------------------------------------------------------------------------- |
| A model import emits its materials, geometry and scene under the model (`Ship.glb/Ship.oscene`) ([content identity](../../../../design/editor/lld/content-explorer-asset-identity.md)); an authored scene can instance it, but cannot change anything inside it, and its node IDs change with names and paths. | The editor builds scenes from geometry one mesh at a time; the model's hierarchy, transforms, cameras and lights are lost.  |
| Examples run import results directly and patch them with script sidecars (`rotating-gltf`).                                                                                                                                                                                                                    | An import result becomes a runnable scene through a side door; Oxygen data such as start state has no proper home on it.    |
| Removing input context `auto_activate` ([start state](scene-instantiation-and-player.md#scene-start-state)) needs a scene to name its starting contexts.                                                                                                                                                       | A scene that is an import result cannot say which contexts start without adding Oxygen fields to the import or the sidecar. |

The editor's own rules already settle ownership: scenes are authored content,
and source media "remains source media until an import or descriptor workflow
promotes it into an authored asset" (editor ARCHITECTURE §content roots). What
is missing is the composition step that lets an authored scene use an
imported one as a whole.

## 2. Model

- **Model scene.** The scene an importer emits for one model, cooked as a
  normal `SceneAsset`. It is an import output: owned and rewritten only by
  its import, read-only everywhere else, and never a Play target on its own: the Player refuses a scene flagged
  `kModelScene` (`player.model_scene`).
- **Authored scene.** A scene descriptor. The only runnable scene, and the
  only owner of scripts, start state, environment and Play settings.
- **Scene instance.** A node of an authored scene whose `instance` field names
  a scene asset by virtual path: a model scene or another authored scene. The
  node is the instance root: it has its own name, transform, flags and
  scripts; the instanced scene's root nodes become its children when the scene
  is instantiated.
- **Instanced node.** A node an instance brings in. It is addressed from the
  instancing scene by the instance root and a **node path**: its cooked node ID
  in each instanced scene on the way, outermost first.
- **Change.** An override record of the instancing scene that addresses a
  instanced node's property. A change whose address does not resolve has no
  effect and is reported; it is never an error.

```json
{
  "name": "Backpack",
  "transform": { "translation": [-1.5, 0, 0] },
  "instance": { "scene_ref": "/Content/Models/backpack.glb/backpack.oscene" }
}
```

## 3. Runtime instancing, not cook-time copying (SC-1)

The cooked authored scene records each instance as a node index and the
instanced scene's asset key, and lists the instanced scene among its asset
references. It is a cook dependency, cooked first so the cooker can check the
instance against it, but not a load dependency: scene instantiation loads each
instanced scene and creates its nodes under the instance root, recursively,
with the same record handling as any scene, so a missing model does not fail
the authored scene's load.

Chosen over copying the model's nodes into the authored scene at cook
time because:

- A reimport updates every scene that uses the model, without recooking those
  scenes, as a material change updates every scene that uses the material.
- The editor shows the same structure the runtime builds: an instance root
  with the model's tree under it, rather than copies that drift from the
  model.
- The authored scene's own node indices do not depend on the model's
  content, so its scripts, sidecars and start state keep pointing at the
  right nodes when the model changes.

Rules:

- An instanced scene may itself contain instances. A cycle is a cook error and
  an instantiation issue.
- A missing or failed instanced scene is an instantiation issue on the
  instance root; the rest of the scene loads.
- The instanced scene's environment and Oxygen start state are ignored: only
  the authored scene's apply.
- An instanced authored scene brings its nodes and their scripts. Model scenes carry no scripts: a model import emits no script slots.

## 4. Node identity and changes inside an instance (SC-2, SC-5)

An instanced node is targeted by its address, never by name or index:

| Scene kind     | Cooked `NodeRecord.node_id`                                                                                                                                                                                            |
| -------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Authored scene | The descriptor node's required `id` (the editor's node ID). The cooker refuses a nil or duplicate ID                                                                                                                   |
| Model scene    | Derived from the node's anchor (its name path in the model, same-named siblings numbered in file order) and the model's source identity, the UUID its import settings keep. The model's virtual path is not part of it |

The same anchor gives the same ID on every import, as surface IDs are
derived under
[surface provenance](../../src/Oxygen/Cooker/Docs/Import/geometry_work_pipeline_v2.md#surface-provenance),
so no table of node IDs is stored. A renamed node is a new anchor; its old
ID's changes stop resolving until the model's import settings record that the
new anchor continues the old one; the import reports vanished and new anchors
and proposes no match. Script slots are addressed the same way, by the
slot ID the editor gives each slot.

A change can set any property of an instanced node's components: every scene
schema property, each with a stable override ID, and every script parameter.
It is applied by patching the instanced node's cooked record before the node
is built, with the code and validation an owned node is built with. A rotation
changes as a whole; position and scale change per axis. An instanced node's
value is the first present of the outermost scene's change, each deeper
nesting level's change, and the defining scene's value; asset defaults follow
for asset-sourced properties. One composition resolves this for the cooker,
instantiation, the live API and the Inspector, so they cannot disagree.

## 5. Format, cooker and API

Already in place: the descriptor node's `instance.scene_ref`, the cooked
`SceneInstanceRecord { node_index, scene_key }`, the cooker's cycle check, the
`kModelScene` flag, and recursive instantiation that brings an instanced
scene's nodes and scripts but not its environment or start state.

The capabilities this design adds, each with what, why and how, are specified
in [scene instances](../../../../design/editor/lld/scene-instances.md)
section 7. Its section 7.15 lists the format and cooker changes that ship in
one release, under one scene format version (16), so content is recooked
once:

| ID    | Capability                                                                                                                                                                                                                                |
| ----- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| NGN1  | The descriptor node's required `id` is the cooked `node_id`; repeated IDs fail the cook and the load                                                                                                                                      |
| NGN2  | Model node IDs derived from anchor and source identity; continuity entries in the import settings; vanished and new anchors reported                                                                                                      |
| NGN3  | Cooked script slots carry the editor's slot ID                                                                                                                                                                                            |
| NGN4  | Every wired schema property has an override ID; generated encoders and appliers over hand-written field writers                                                                                                                           |
| NGN5  | `NodeRef { node_index, node_path }`, the one way a cooked scene names a node, in overrides, the starting camera and script parameter values; node path tables; script parameter changes; scene format version 16, script format version 5 |
| NGN6  | `ComposeScene`, the one composition                                                                                                                                                                                                       |
| NGN7  | Instantiation builds the composed tree; `SceneInstanceComponent` on each instance root                                                                                                                                                    |
| NGN8  | `InstantiateUnder`, `SetChange`, `ClearChange` and `Detach` on a live scene                                                                                                                                                               |
| NGN9  | The descriptor's `instance.overrides`; unresolved changes and surfaces are warnings, left out of the cooked tables                                                                                                                        |
| NGN10 | The starting camera can name an instanced camera                                                                                                                                                                                          |
| NGN11 | The Inspector's `scenes` verb reports a composed scene (`oxygen.cooked-scenes.v2`)                                                                                                                                                        |
| NGN12 | The user chooses a glTF file's source scene; the others are reported; the source analysis lists the scenes from the JSON alone                                                                                                            |
| NGN13 | The ImportTool finds a moved, re-exported or copied source's retained record by virtual path, then by fingerprint; one record per virtual path                                                                                            |
| NGN14 | One model import record (`oxygen.model-import.schema.json`, `<content_root>/imports/<source_identity>.import.json`) shared by the editor and the ImportTool, run by a `model` import job                                                  |

## 6. Editor

Placement, the Scene Explorer and inspector presentation, swap, Unpack, Apply
to source and kept changes are specified by the editor's
[scene instances](../../../../design/editor/lld/scene-instances.md) LLD. The
editor builds instances in Live Preview through the live API (NGN8) and
never copies instanced nodes into its own scene model.

## 7. Examples migration

`rotating-gltf` becomes a scene descriptor with two instance roots, Backpack
and Chest, instancing the imported model scenes; the rotate script binds to
each instance root; the descriptor names its starting camera and starting
input context. Its script sidecars target the authored scene, as in
`multi-script`. No example runs an import result directly.

## 8. Decisions

Alternatives considered: patching import results (sidecar or import job
fields), which makes an import result runnable through a side door;
descriptor scenes placing meshes by hand, which loses every multi-node
model's hierarchy, cameras and lights; and cook-time copying (§3).

| ID   | Decision                                      | Choice and reason                                                                                                       |
| ---- | --------------------------------------------- | ----------------------------------------------------------------------------------------------------------------------- |
| SC-1 | Instances resolved at runtime or copied       | Runtime (§3): reimport updates every scene; the editor shows what the runtime builds                                    |
| SC-2 | Targeting nodes inside an instance            | By instance root and node path of source-owned node IDs (§4); names and indices change on reimport                      |
| SC-4 | Scripts and start state in an instanced scene | Nodes and scripts come along; environment and start state are ignored, since they belong to the scene that runs         |
| SC-5 | A change that no longer resolves              | A warning and no effect, in the cooker and at instantiation; the authored change is kept, since models come with errors |
| SC-6 | Which scene's change wins                     | The outermost: the scene being run or edited owns its changes of everything below it                                    |

## 9. Exit gates

| ID   | Check                                                                                                                                                |
| ---- | ---------------------------------------------------------------------------------------------------------------------------------------------------- |
| SG-1 | Cook: a descriptor instancing a model scene records the instance and its cook dependency; a cycle fails with a named diagnostic.                     |
| SG-2 | Instantiation: the model's nodes appear under the instance root with their transforms, renderables, cameras and lights; a missing model is an issue. |
| SG-3 | Reimport of the model changes what an unchanged authored scene shows.                                                                                |
| SG-4 | RenderScene runs `rotating-gltf` as an authored scene with instances, its scripts and its starting input context.                                    |
| SG-5 | Model node IDs survive reimport, moving and renaming the model; a renamed node gets a new ID.                                                        |
| SG-6 | A change two nesting levels deep applies over the nested scene's own change; an unresolved change is an issue and a cook warning.                    |
| SG-7 | The host API instances under a live node, reports node paths, and set then clear of a change restores the source value.                              |
| SG-8 | The `scenes` report of a nested scene lists every node with its node path and the scene that supplies each changeable value.                         |
