# ED-M09 - V0.1 Gaps And Blocking Issues

Status: `planned; blocking issue list pending owner review`

## 1. Purpose

Close what V0.1 still lacks before input authoring and release qualification:
the LLD behaviors that are designed but not built, and the abdes/DroidNet issues
that must be fixed before V0.1.

## 2. LLD Gaps

The design pack audit found three LLD behaviors that are not built.

| Gap                       | LLD                                                                                                          | Today                                                                                                                                                                                     | Work                                                                                                                                                                                                     |
| ------------------------- | ------------------------------------------------------------------------------------------------------------ | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Inherit source modes      | [Scene model](../lld/scene-authoring-model.md#visibility-participation-and-shadow-source-state), inspector 4 | World stores `IsVisible`, `CastsShadows` and `ReceivesShadows` as booleans, so the editor authors local values only; the Rendering section offers no Inherit and shows no resolved origin | Inherit / Shown / Hidden and Inherit / On / Off in World source and `SceneNodeData`, Scenes descriptors, Interop `NodePropertyApplier` and the cooked node records; migrate the booleans as local values |
| Light collection pruning  | [Scene model](../lld/scene-authoring-model.md#visibility-participation-and-shadow-source-state)              | `DirectionalLightResolver` and `SceneRenderer::BuildFrameLightSelection` traverse with `VisibleFilter`, so a locally Shown light under a Hidden parent contributes nothing                | Collect lights by each node's resolved visibility, as geometry extraction does; keep `VisibleFilter` for callers that prune subtrees deliberately                                                        |
| Material emission editing | [Material editor](../lld/material-editor.md)                                                                 | `MaterialSource` carries and writes `EmissiveColor` and `EmissiveIntensity`; `MaterialEdits` has no emission edit and the Material Editor shows emission only as a texture slot           | Emission colour and HDR intensity edits in `Oxygen.Editor.Materials` and their Material Editor controls, with history, Save, cook and preview                                                            |

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

The owner selects the abdes/DroidNet issues that block V0.1; they are listed
here with their order once selected.

## 4. Exit Checklist

- [ ] Inherit source modes, light collection pruning and material emission
      editing work as their LLDs state, through save, reopen and cook.
- [ ] The editor Hide mask removes hidden nodes from every editing pane and
      keeps them in game and capture views.
- [ ] Every selected blocking issue is closed.
