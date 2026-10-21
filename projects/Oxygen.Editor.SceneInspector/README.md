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
