# Oxygen.Editor.SceneInspector

The Properties panel of a scene: the components of the selected nodes, the
scene's environment when the scene itself is selected, and the script slots,
each field edited through the scene document's authoring as one step of its
history.

The inspector follows the selection every scene part shares (Scenes'
`ISceneSelectionService`) and the property focus requests other parts send
through it. It shows assets through the Shell's `IAssetReveal`, a script's
problem through the Shell's `IProblemReveal`, and creates a script for a slot
through Scripting's `IScriptCreation`, so it references none of the panels that
implement them.

The shared property controls are Oxygen.Editor.Controls'; the asset pickers are
Oxygen.Editor.Pickers'.

## Layout

| Folder           | Holds                                                                                                                                                                                                                                                               |
| ---------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `src/`           | The module, the panel, the node header and what routes the selection to the editors.                                                                                                                                                                                |
| `Components/`    | The factory that creates the editor of each component kind, and one folder per component family: `Transform/`, `Cameras/`, `Lights/`, `Fog/`, `Rendering/` (the node's visibility and shadow flags), `Geometry/` and `Scripting/`, each with its editors and views. |
| `SceneSettings/` | What the panel shows when the scene itself is selected: its environment, play settings and references, one section each.                                                                                                                                            |
| `Editing/`       | Edit sessions and gestures over the scene's authoring, field values and diagnostics.                                                                                                                                                                                |
| `Presentation/`  | Field applicability, search, converters and the scene field catalog.                                                                                                                                                                                                |

The views merge `SceneInspectorStyles.xaml`, which holds only the inspector's
own resources. The property editors take their look from their default styles in
`Oxygen.Editor.Controls`; the application merges that library's
`Themes/EditorStyles.xaml` for the keyed styles of the inspector's own elements.

## Testing

The UI tests drive the shared controls through their public API where it
exists: `NumberBox` starts and cancels a text edit with `BeginTextEdit` and
`CancelEdit`. Tests of an inspector's drag and wheel sessions call its
`IInspectorEditSessionOwner` (`BeginNumberSession` and `CompleteNumberSession`
in `testsupport/UI/InspectorControls.cs`). Tests of how a view wires its
controls (late callbacks from recycled controls, vector boxes, filter changes)
raise the control's session events by reflection (`RaiseNumberEvent`), because
`NumberBox` has no public way to drive a drag.
Probes of the property editors' parts (`CardOf`, `SwatchOf`, `EnterTextAsync`)
come from the Controls test kit, `Oxygen.Editor.Controls.Testing`.
