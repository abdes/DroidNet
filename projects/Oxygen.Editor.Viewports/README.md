# Oxygen.Editor.Viewports

The viewport control shared by the document editors that show a scene: the
scene editor and the Model Editor.

It holds the `Viewport` control and its view model (camera control, view modes,
selection, picking and framing), the transform tools and their gizmo feedback,
the viewport HUD, the viewport layouts, and the per-user transform tool
settings (tool, space and snapping), which are kept across projects.

A viewport has a profile: `Editing`, the scene editor's, with every overlay, and
`Viewing`, the Model Editor's, which hides the transform tools, layouts,
maximize and camera preview, picks the node hit rather than its instance, and
offers _Preview environment_ and an exposure compensation. `ViewportServices`
creates viewports and the preview settings panel for an editor that does not
reference the engine runtime; `SceneViews` registers the editor that shows each
document (`ISceneView`) for `ISceneViews`; `ViewportStateService` keeps each
scene's and each model's pane layout, cameras and view options per user.

The viewports draw through Runtime's surfaces, views and input, never Interop.
The host editor owns what the panes show: it creates the view models, arranges
them in a layout and keeps the per-document state.
