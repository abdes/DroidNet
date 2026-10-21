# Oxygen.Editor.SceneEditor

The scene editor: the editor of a scene document, with its viewports, cameras,
transform gizmos and tools, its toolbar and the Play bar, and the user's
preview, viewport and transform tool settings.

The editor edits through the scene document's authoring and follows the
selection every scene part shares (Scenes' `ISceneSelectionService`). It
implements Scenes' `ISceneViews` (framing and cameras) and `ISceneAvailability`
(a scene that can no longer be shown), and contributes its commands to the
Shell's command service.

The viewports draw through Runtime's surfaces, views and input: the scene
editor is the one feature that references Runtime, never Interop. Applying
authoring state to the engine and Play belong to LivePreview.
