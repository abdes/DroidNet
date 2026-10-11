# Scene composition: instancing imported model scenes

Status: **design; owner decisions SC-1 to SC-4 taken on 2026-10-11**. Extends
[scene instantiation](scene-instantiation-and-player.md) and the editor's
[content identity](../../../../design/editor/lld/content-browser-asset-identity.md).

Summary: a glTF or FBX import keeps its scene, the hierarchy of nodes with
their transforms, meshes, materials, cameras and lights, as a read-only
**model scene**. An authored Oxygen scene composes model scenes by
**instancing** them: one node in the authored scene stands for the whole
model, and the model's nodes appear under it when the scene is instantiated.
Oxygen-specific data (scripts, the starting camera and input contexts,
environment, the Play section) lives only on authored scenes. Nothing is
patched into import results.

## 1. Problem

| Fact today                                                                                                                                                                                          | Consequence                                                                                                                 |
| --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | --------------------------------------------------------------------------------------------------------------------------- |
| A model import emits `Materials/<group>`, `Geometry/<group>` and `Scenes/<group>` ([content identity](../../../../design/editor/lld/content-browser-asset-identity.md)); nothing can use the scene. | The editor builds scenes from geometry one mesh at a time; the model's hierarchy, transforms, cameras and lights are lost.  |
| Examples run import results directly and patch them with script sidecars (`rotating-gltf`).                                                                                                         | An import result becomes a runnable scene through a side door; Oxygen data such as start state has no proper home on it.    |
| Removing input context `auto_activate` ([start state](scene-instantiation-and-player.md#scene-start-state)) needs a scene to name its starting contexts.                                            | A scene that is an import result cannot say which contexts start without adding Oxygen fields to the import or the sidecar. |

The editor's own rules already settle ownership: scenes are authored content,
and source media "remains source media until an import or descriptor workflow
promotes it into an authored asset" (editor ARCHITECTURE §content roots). What
is missing is the composition step that lets an authored scene use an
imported one as a whole.

## 2. Model

- **Model scene.** The scene an importer emits for one model, cooked as a
  normal `SceneAsset`. It is an import output: owned and rewritten only by
  its import, read-only everywhere else, and never a Play target on its own.
- **Authored scene.** A scene descriptor. The only runnable scene, and the
  only owner of scripts, start state, environment and Play settings.
- **Scene instance.** A node of an authored scene whose `instance` field names
  a scene asset by virtual path. The node is the instance root: it has its
  own name, transform, flags and scripts; the instanced scene's root nodes
  become its children when the scene is instantiated.

```json
{
  "name": "Backpack",
  "transform": { "translation": [-1.5, 0, 0] },
  "instance": { "scene_ref": "/Content/Scenes/backpack/backpack.oscene" }
}
```

## 3. Runtime instancing, not cook-time copying (SC-1)

The cooked authored scene records each instance as a node index and the
instanced scene's asset key, and lists the instanced scene among its asset
references. It is not a load dependency: scene instantiation loads each
instanced scene and creates its nodes under the instance root, recursively,
with the same record handling as any scene, so a missing model does not fail
the authored scene's load.

Chosen over copying the model's nodes into the authored scene at cook
time because:

- A reimport updates every scene that uses the model, without recooking those
  scenes, as a material change updates every scene that uses the material.
- The editor shows the same structure the runtime builds: an instance node
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
- Model scenes carry no scripts. Instantiation ignores scripting records in
  an instanced scene (SC-4).

## 4. Targeting nodes inside an instance (SC-2)

An instance's children are not nodes of the authored scene. V0.1 authoring
data targets authored nodes only:

| Authored data             | Target                                                                     |
| ------------------------- | -------------------------------------------------------------------------- |
| Scripts, transform, flags | The instance root (scripts reach children at runtime through `self.node`). |
| Starting camera           | An authored camera node.                                                   |
| Material overrides        | Not on instance children in V0.1.                                          |

Overriding something inside an instance (a material on one mesh, a script on
one child, a model camera as the starting camera) needs an address of the
form instance root plus model node, and a rule for when the model's nodes
change on reimport. That is the prefab override problem; the PRD keeps prefab
editors out of V0.1, so it is proposed as a later extension rather than half
built now.

## 5. Format and cooker

- `SceneAssetDesc` gains a descriptor-local `instances` table of
  `SceneInstanceRecord { SceneNodeIndexT node_index; AssetKey scene_key; }`.
  It shares the scene version increase with the start-state fields.
- The scene descriptor schema gains the node field `instance.scene_ref`, a
  canonical scene virtual path, resolved by the same lookup as other
  virtual-path references and listed in the scene's asset references.
- A model import's scene output is marked as a model scene (an asset flag)
  so tools and the player can refuse it as a Play target.

## 6. Editor

- **Placing a model.** Dragging a model source or its scene output from the
  Content Browser into a scene, or Add > Model Instance, creates an instance
  node (ED-M08.12 drag and drop).
- **Scene Explorer** shows the instance root as an authored node and the
  model's tree under it read-only, distinguished like other derived content.
  Selecting a child selects it for inspection and framing; edits apply to the
  instance root.
- **Reimport** refreshes every open scene's instances after publication; no
  scene document changes.
- **Unpack** (copying the model's nodes into the scene as authored nodes) is
  not V0.1; it is the escape hatch when per-child edits are needed before
  overrides exist.

## 7. Examples migration

`rotating-gltf` becomes a scene descriptor with two instance nodes, Backpack
and Chest, instancing the imported model scenes; the rotate script binds to
each instance root; the descriptor names its starting camera and starting
input context. Its script sidecars target the authored scene, as in
`multi-script`. No example runs an import result directly.

## 8. Decisions

Alternatives considered: patching import results (sidecar or import job
fields), which makes an import result runnable through a side door;
descriptor scenes placing meshes by hand, which loses every multi-node
model's hierarchy, cameras and lights; and cook-time copying (§3).

| ID   | Decision                                             | Decided 2026-10-11 (owner)                                                                       |
| ---- | ---------------------------------------------------- | ------------------------------------------------------------------------------------------------ |
| SC-1 | Instances resolved at runtime or copied at cook time | Runtime (§3)                                                                                     |
| SC-2 | Targeting nodes inside an instance in V0.1           | Instance roots and authored nodes only; overrides later (§4)                                     |
| SC-3 | Scheduling                                           | Engine (format, cooker, instantiation) in ED-M08.10l; editor placement and explorer in ED-M08.12 |
| SC-4 | Scripts and start state inside an instanced scene    | Ignored; model scenes carry none                                                                 |

## 9. Exit gates

| ID   | Check                                                                                                                                                |
| ---- | ---------------------------------------------------------------------------------------------------------------------------------------------------- |
| SC-1 | Cook: a descriptor instancing a model scene records the instance and the dependency; a cycle fails with a named diagnostic.                          |
| SC-2 | Instantiation: the model's nodes appear under the instance root with their transforms, renderables, cameras and lights; a missing model is an issue. |
| SC-3 | Reimport of the model changes what an unchanged authored scene shows.                                                                                |
| SC-4 | RenderScene runs `rotating-gltf` as an authored scene with instances, its scripts and its starting input context.                                    |
